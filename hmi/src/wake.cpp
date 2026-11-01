#include "wake.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QProcess>

#include <algorithm>

Q_LOGGING_CATEGORY(lcWake, "godwit.wake")

namespace {

QProcessEnvironment pipewireEnv()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // The system-wide PipeWire's socket, as the mixer uses
    env.insert(QStringLiteral("PIPEWIRE_RUNTIME_DIR"), QStringLiteral("/run/pipewire"));
    return env;
}

} // namespace

Wake::Wake(QObject *parent)
    : QObject(parent)
    , m_bin(qEnvironmentVariable("GODWIT_WAKE_BIN", QStringLiteral("/usr/bin/godwit-wake")))
    , m_dir(qEnvironmentVariable("GODWIT_WAKE_DIR", QStringLiteral("/usr/share/godwit/wake")))
{
    const bool wanted = qEnvironmentVariableIntValue("GODWIT_WAKE") == 1
        || QGuiApplication::platformName() == QLatin1String("eglfs");
    m_available = wanted && QFileInfo::exists(m_bin)
        && QFileInfo::exists(m_dir + QStringLiteral("/hey_peregrine.onnx"));
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &Wake::start);
}

Wake::~Wake()
{
    stop();
}

void Wake::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
    m_failures = 0;
    if (enabled)
        start();
    else
        stop();
}

void Wake::start()
{
    if (!m_available || !m_enabled || m_detector)
        return;
    m_recorder = new QProcess(this);
    m_detector = new QProcess(this);
    m_recorder->setProcessEnvironment(pipewireEnv());
    m_recorder->setStandardErrorFile(QProcess::nullDevice());
    m_recorder->setStandardOutputProcess(m_detector);

    QProcess *det = m_detector;
    connect(det, &QProcess::readyReadStandardOutput, this, [this, det] {
        if (det == m_detector)
            readDetector();
    });
    connect(det, &QProcess::readyReadStandardError, this, [det] {
        const QStringList lines = QString::fromUtf8(det->readAllStandardError()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines)
            qCInfo(lcWake).noquote() << line;
    });
    connect(det, &QProcess::finished, this, [this, det](int code) {
        det->deleteLater();
        if (det != m_detector)
            return;
        // Stopped by itself: the microphone went away or the models failed
        qCWarning(lcWake) << "stopped listening" << code;
        stop();
        if (m_enabled) {
            m_retry.start(std::min(60000, 2000 << std::min(m_failures, 5)));
            ++m_failures;
        }
    });

    QStringList rec { QStringLiteral("--raw"), QStringLiteral("--rate"), QStringLiteral("16000"),
                      QStringLiteral("--channels"), QStringLiteral("1"), QStringLiteral("--format"), QStringLiteral("s16"),
                      QStringLiteral("--media-role"), QStringLiteral("Communication") };
    const QString mic = qEnvironmentVariable("GODWIT_MIC");
    if (!mic.isEmpty())
        rec << QStringLiteral("--target") << mic;
    rec << QStringLiteral("-");
    m_detector->start(m_bin, { QStringLiteral("--dir"), m_dir });
    m_recorder->start(QStringLiteral("pw-record"), rec);
    qCInfo(lcWake) << "listening for Hey Peregrine";
    emit listeningChanged();
}

void Wake::stop()
{
    m_retry.stop();
    if (!m_detector)
        return;
    QProcess *rec = m_recorder, *det = m_detector;
    m_recorder = nullptr;
    m_detector = nullptr;
    for (QProcess *p : { rec, det }) {
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(300);
        }
        p->deleteLater();
    }
    emit listeningChanged();
}

// godwit-wake prints "wake <score>" for each detection
void Wake::readDetector()
{
    while (m_detector->canReadLine()) {
        const QString line = QString::fromUtf8(m_detector->readLine()).trimmed();
        if (!line.startsWith(QLatin1String("wake ")))
            continue;
        const double score = line.mid(5).toDouble();
        m_failures = 0;
        qCInfo(lcWake) << "heard Hey Peregrine" << score;
        emit detected(score);
    }
}
