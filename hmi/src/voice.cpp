#include "voice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QProcess>

Q_LOGGING_CATEGORY(lcVoice, "godwit.voice")

namespace {

// Headroom for Piper's full-scale output; the navigation role's volume
// (the guidance volume) applies on top
constexpr double PlayVolume = 0.5;

} // namespace

Voice::Voice(QObject *parent)
    : QObject(parent)
    , m_piper(qEnvironmentVariable("GODWIT_PIPER", QStringLiteral("/usr/lib/piper/piper")))
    , m_model(qEnvironmentVariable("GODWIT_PIPER_VOICE",
                                   QStringLiteral("/usr/share/piper/voices/en_US-ljspeech-medium.onnx")))
{
    const bool wanted = qEnvironmentVariableIntValue("GODWIT_VOICE") == 1
        || QGuiApplication::platformName() == QLatin1String("eglfs");
    m_available = wanted && QFileInfo::exists(m_piper) && QFileInfo::exists(m_model);
    // Prompts are short-lived files in the HMI's runtime directory
    m_outDir = qEnvironmentVariable("XDG_RUNTIME_DIR", QDir::tempPath()) + QStringLiteral("/voice");
}

Voice::~Voice()
{
    for (QProcess *p : { m_tts, m_player }) {
        if (p && p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(500);
        }
    }
}

bool Voice::startPiper()
{
    if (m_tts && m_tts->state() == QProcess::Running)
        return true;
    QDir().mkpath(m_outDir);
    if (!m_tts) {
        m_tts = new QProcess(this);
        m_tts->setProcessChannelMode(QProcess::SeparateChannels);
        m_tts->setStandardErrorFile(QProcess::nullDevice());
        connect(m_tts, &QProcess::readyReadStandardOutput, this, &Voice::readPiper);
        connect(m_tts, &QProcess::finished, this, [this](int code) {
            qCWarning(lcVoice) << "piper exited" << code;
            m_pending = 0;
            m_drop = 0;
            update();
        });
    }
    const QString dir = QFileInfo(m_piper).absolutePath();
    m_tts->start(m_piper, { QStringLiteral("--quiet"), QStringLiteral("--model"), m_model,
                            QStringLiteral("--espeak_data"), dir + QStringLiteral("/espeak-ng-data"),
                            QStringLiteral("--output_dir"), m_outDir });
    if (!m_tts->waitForStarted(2000)) {
        qCWarning(lcVoice) << "piper did not start:" << m_tts->errorString();
        return false;
    }
    return true;
}

void Voice::say(const QString &text)
{
    if (!m_available)
        return;
    QString line = text.simplified();
    if (line.isEmpty() || !startPiper())
        return;
    qCInfo(lcVoice) << "say" << line;
    // Piper reads one prompt per line
    line.append(QLatin1Char('\n'));
    m_tts->write(line.toUtf8());
    ++m_pending;
    update();
}

void Voice::update()
{
    const bool on = m_pending > 0 || (m_player && m_player->state() != QProcess::NotRunning);
    if (on != m_speaking) {
        m_speaking = on;
        emit speakingChanged();
    }
}

// Piper prints each finished WAV's path
void Voice::readPiper()
{
    while (m_tts->canReadLine()) {
        const QString wav = QString::fromUtf8(m_tts->readLine()).trimmed();
        if (wav.isEmpty())
            continue;
        // Stopped since, or a newer prompt is on its way: skip this one
        if (m_drop > 0 || --m_pending > 0) {
            if (m_drop > 0)
                --m_drop;
            QFile::remove(wav);
            continue;
        }
        m_pending = 0;
        play(wav);
    }
    update();
}

void Voice::play(const QString &wav)
{
    if (m_player && m_player->state() != QProcess::NotRunning) {
        m_player->disconnect(this);
        m_player->kill();
        m_player->waitForFinished(300);
        m_player->deleteLater();
        m_player = nullptr;
    }
    m_player = new QProcess(this);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // The system-wide PipeWire's socket, as the mixer uses
    env.insert(QStringLiteral("PIPEWIRE_RUNTIME_DIR"), QStringLiteral("/run/pipewire"));
    m_player->setProcessEnvironment(env);
    QProcess *player = m_player;
    connect(player, &QProcess::finished, this, [this, player, wav] {
        QFile::remove(wav);
        if (player == m_player)
            m_player = nullptr;
        player->deleteLater();
        update();
    });
    qCDebug(lcVoice) << "play" << wav;
    player->start(QStringLiteral("pw-play"), { QStringLiteral("--media-role"), QStringLiteral("Navigation"),
                                               QStringLiteral("--volume"), QString::number(PlayVolume), wav });
}

void Voice::stop()
{
    m_drop += m_pending;
    m_pending = 0;
    if (m_player && m_player->state() != QProcess::NotRunning)
        m_player->kill();
    update();
}
