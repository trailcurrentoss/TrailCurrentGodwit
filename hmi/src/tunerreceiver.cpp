#include "tunerreceiver.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QProcess>

Q_LOGGING_CATEGORY(lcTuner, "godwit.tuner")

TunerReceiver::TunerReceiver(const char *name, const char *binVariable, const QString &defaultBin, QObject *parent)
    : QObject(parent)
    , m_name(name)
    , m_bin(qEnvironmentVariable(binVariable, defaultBin))
{
    const bool wanted = qEnvironmentVariableIntValue("GODWIT_RADIO") == 1
        || QGuiApplication::platformName() == QLatin1String("eglfs");
    m_available = wanted && QFileInfo::exists(m_bin);

    m_retry.setSingleShot(true);
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, &TunerReceiver::start);
}

TunerReceiver::~TunerReceiver()
{
    // So that its ending does not start another
    m_active = false;
    stop();
    // Give it the chance to hand the dongle back before the process goes
    if (m_exiting && !m_exiting->waitForFinished(500))
        m_exiting->kill();
}

void TunerReceiver::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    emit activeChanged();
    if (active)
        start();
    else
        stop();
}

QVariantList TunerReceiver::list() const
{
    QVariantList out;
    out.reserve(m_items.size());
    for (auto it = m_items.begin(); it != m_items.end(); ++it)
        out.append(it.value());
    return out;
}

void TunerReceiver::updateHeld()
{
    const bool held = m_decoder || m_exiting;
    if (held == m_held)
        return;
    m_held = held;
    emit tunerHeldChanged();
}

void TunerReceiver::forget()
{
    if (m_items.isEmpty())
        return;
    m_items.clear();
    emit listChanged();
}

void TunerReceiver::start()
{
    if (!m_available || !m_active || m_decoder || m_exiting)
        return;

    m_decoder = new QProcess(this);
    m_buffer.clear();
    m_rateLines = 0;
    m_frames = 0;
    m_decoder->setStandardErrorFile(QProcess::nullDevice());

    QProcess *decoder = m_decoder;
    connect(decoder, &QProcess::readyReadStandardOutput, this, [this, decoder] {
        if (decoder == m_decoder)
            readDecoder();
    });
    connect(decoder, &QProcess::errorOccurred, this, [this, decoder](QProcess::ProcessError e) {
        qCWarning(lcTuner) << m_name << e << decoder->errorString();
    });
    connect(decoder, &QProcess::finished, this, [this, decoder](int code) {
        decoder->deleteLater();
        if (decoder == m_exiting) {
            m_exiting = nullptr;
            updateHeld();
            start();
            return;
        }
        if (decoder != m_decoder)
            return;
        // By itself: the dongle was pulled out, or was never there
        qCInfo(lcTuner) << m_name << "stopped" << code;
        m_decoder = nullptr;
        m_listening = false;
        if (code == 2)
            m_tunerFound = false;
        forget();
        updateHeld();
        emit stateChanged();
        if (m_active)
            m_retry.start();
    });
    m_decoder->start(m_bin, {});
    updateHeld();
    qCInfo(lcTuner) << m_name << "starting";
}

void TunerReceiver::stop()
{
    m_retry.stop();
    if (m_decoder) {
        // Closing its stdin ends it, which lets it hand the dongle back cleanly.
        // Not waited for here: this is the UI's thread.
        QProcess *decoder = m_decoder;
        m_decoder = nullptr;
        m_exiting = decoder;
        decoder->closeWriteChannel();
        QTimer::singleShot(1500, decoder, [decoder] { decoder->kill(); });
    }
    m_listening = false;
    forget();
    updateHeld();
    emit stateChanged();
}

// A line now and then says what is coming in, so an empty screen can be told
// apart from the log: no frames is the antenna or the sky, and frames but nothing
// listed is the decoding
void TunerReceiver::noteRate(int frames)
{
    m_frames += frames;
    if (++m_rateLines < 30)
        return;
    qCInfo(lcTuner) << m_name << m_frames << "frames in 30 s," << m_items.size() << "listed";
    m_rateLines = 0;
    m_frames = 0;
}

void TunerReceiver::readDecoder()
{
    m_buffer += m_decoder->readAllStandardOutput();
    int nl;
    bool changed = false;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        if (line == "ready") {
            m_listening = true;
            m_tunerFound = true;
            qCInfo(lcTuner) << m_name << "listening";
            emit stateChanged();
        } else if (line.startsWith("error")) {
            qCInfo(lcTuner) << m_name << "no tuner";
        } else if (line.startsWith("gone ")) {
            changed = m_items.remove(line.mid(5)) > 0 || changed;
        } else if (line.startsWith("rate ")) {
            noteRate(line.mid(5).toInt());
        } else {
            changed = handleLine(line) || changed;
        }
    }
    // A burst of lines each second, and one redraw for it
    if (changed)
        emit listChanged();
}
