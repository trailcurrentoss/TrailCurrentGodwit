#include "dictation.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QProcess>
#include <QRegularExpression>
#include <QtEndian>

#include <cmath>

Q_LOGGING_CATEGORY(lcDict, "godwit.dictation")

namespace {

constexpr int Rate = 16000;
constexpr int FrameSamples = Rate / 50;          // 20 ms
constexpr int FrameBytes = FrameSamples * 2;
// Speech begins after this many loud frames in a row, and ends after this
// many quiet ones
constexpr int StartFrames = 3;
constexpr int EndFrames = 60;                    // 1.2 s
// Give up when nothing is said, and stop however long it goes on
constexpr int NoSpeechFrames = 7 * 50;
constexpr int MaxFrames = 20 * 50;
// Loud is this far over the background, and never below MinLoud (RMS of
// 16-bit samples, about -44 dBFS)
constexpr double LoudRatio = 2.5;
constexpr double MinLoud = 200;

QByteArray wavHeader(int dataBytes)
{
    QByteArray h(44, '\0');
    auto put32 = [&h](int at, quint32 v) { qToLittleEndian(v, h.data() + at); };
    auto put16 = [&h](int at, quint16 v) { qToLittleEndian(v, h.data() + at); };
    memcpy(h.data(), "RIFF", 4);
    put32(4, 36 + dataBytes);
    memcpy(h.data() + 8, "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);                // PCM
    put16(22, 1);                // mono
    put32(24, Rate);
    put32(28, Rate * 2);
    put16(32, 2);
    put16(34, 16);
    memcpy(h.data() + 36, "data", 4);
    put32(40, dataBytes);
    return h;
}

QProcessEnvironment pipewireEnv()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // The system-wide PipeWire's socket, as the mixer uses
    env.insert(QStringLiteral("PIPEWIRE_RUNTIME_DIR"), QStringLiteral("/run/pipewire"));
    return env;
}

} // namespace

Dictation::Dictation(QObject *parent)
    : QObject(parent)
    , m_whisper(qEnvironmentVariable("GODWIT_WHISPER", QStringLiteral("/usr/bin/whisper-cli")))
    , m_model(qEnvironmentVariable("GODWIT_WHISPER_MODEL", QStringLiteral("/usr/share/whisper/ggml-base.en.bin")))
{
    const bool wanted = qEnvironmentVariableIntValue("GODWIT_DICTATION") == 1
        || QGuiApplication::platformName() == QLatin1String("eglfs");
    m_available = wanted && QFileInfo::exists(m_whisper) && QFileInfo::exists(m_model);
    m_wav = qEnvironmentVariable("XDG_RUNTIME_DIR", QDir::tempPath()) + QStringLiteral("/dictation.wav");
}

Dictation::~Dictation()
{
    for (QProcess *p : { m_recorder, m_whisperProc }) {
        if (p && p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(500);
        }
    }
}

void Dictation::setState(const QString &state)
{
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged();
}

void Dictation::listen()
{
    if (!m_available)
        return;
    cancel();
    m_pcm.clear();
    m_partial.clear();
    m_floor = 0;
    m_frames = m_loud = m_quiet = 0;
    m_speech = false;

    m_recorder = new QProcess(this);
    m_recorder->setProcessEnvironment(pipewireEnv());
    m_recorder->setStandardErrorFile(QProcess::nullDevice());
    QProcess *rec = m_recorder;
    connect(rec, &QProcess::readyReadStandardOutput, this, [this, rec] {
        // Output still arriving from a recorder already stopped
        if (rec == m_recorder)
            readAudio();
    });
    connect(rec, &QProcess::finished, this, [this, rec] {
        rec->deleteLater();
        if (rec != m_recorder)
            return;
        m_recorder = nullptr;
        // Stopped by itself: the microphone went away
        if (m_state == QLatin1String("listening")) {
            qCWarning(lcDict) << "recording stopped";
            setState(QStringLiteral("idle"));
            emit failed(QStringLiteral("error"));
        }
    });
    QStringList args { QStringLiteral("--raw"), QStringLiteral("--rate"), QString::number(Rate),
                       QStringLiteral("--channels"), QStringLiteral("1"), QStringLiteral("--format"), QStringLiteral("s16"),
                       QStringLiteral("--media-role"), QStringLiteral("Communication") };
    const QString mic = qEnvironmentVariable("GODWIT_MIC");
    if (!mic.isEmpty())
        args << QStringLiteral("--target") << mic;
    args << QStringLiteral("-");
    rec->start(QStringLiteral("pw-record"), args);
    if (!rec->waitForStarted(2000)) {
        qCWarning(lcDict) << "pw-record did not start:" << rec->errorString();
        m_recorder = nullptr;
        rec->deleteLater();
        emit failed(QStringLiteral("error"));
        return;
    }
    qCInfo(lcDict) << "listening";
    setState(QStringLiteral("listening"));
}

void Dictation::readAudio()
{
    m_partial += m_recorder->readAllStandardOutput();
    while (m_partial.size() >= FrameBytes && m_state == QLatin1String("listening")) {
        const QByteArray frame = m_partial.left(FrameBytes);
        m_partial.remove(0, FrameBytes);
        m_pcm += frame;
        ++m_frames;

        double sum = 0;
        const auto *s = reinterpret_cast<const qint16 *>(frame.constData());
        for (int i = 0; i < FrameSamples; ++i)
            sum += double(qFromLittleEndian(s[i])) * qFromLittleEndian(s[i]);
        const double rms = std::sqrt(sum / FrameSamples);
        const double level = std::clamp((20 * std::log10(std::max(rms, 1.0) / 32768) + 60) / 50, 0.0, 1.0);
        if (std::abs(level - m_level) > 0.02) {
            m_level = level;
            emit levelChanged();
        }

        // The background follows the room until speech begins
        if (m_frames == 1)
            m_floor = rms;
        const bool loud = rms > std::max(m_floor * LoudRatio, MinLoud);
        if (!m_speech) {
            m_floor = m_floor * 0.95 + rms * 0.05;
            m_loud = loud ? m_loud + 1 : 0;
            if (m_loud >= StartFrames) {
                m_speech = true;
                qCDebug(lcDict) << "speech began, background" << m_floor;
            } else if (m_frames >= NoSpeechFrames) {
                qCInfo(lcDict) << "nothing said";
                cancel();
                emit failed(QStringLiteral("silence"));
                return;
            }
        } else {
            m_quiet = loud ? 0 : m_quiet + 1;
        }
        if ((m_speech && m_quiet >= EndFrames) || m_frames >= MaxFrames) {
            finish();
            return;
        }
    }
}

void Dictation::stopRecorder()
{
    if (!m_recorder)
        return;
    QProcess *rec = m_recorder;
    m_recorder = nullptr;
    rec->kill();
}

void Dictation::finish()
{
    if (m_state != QLatin1String("listening"))
        return;
    stopRecorder();
    m_level = 0;
    emit levelChanged();
    if (!m_speech) {
        setState(QStringLiteral("idle"));
        emit failed(QStringLiteral("silence"));
        return;
    }
    transcribe();
}

void Dictation::transcribe()
{
    QFile f(m_wav);
    if (!f.open(QIODevice::WriteOnly) || f.write(wavHeader(m_pcm.size()) + m_pcm) < 0) {
        qCWarning(lcDict) << "cannot write" << m_wav;
        setState(QStringLiteral("idle"));
        emit failed(QStringLiteral("error"));
        return;
    }
    f.close();
    qCInfo(lcDict) << "transcribing" << m_pcm.size() / 2 / double(Rate) << "s";
    m_pcm.clear();
    setState(QStringLiteral("transcribing"));

    m_whisperProc = new QProcess(this);
    m_whisperProc->setStandardErrorFile(QProcess::nullDevice());
    QProcess *w = m_whisperProc;
    connect(w, &QProcess::finished, this, [this, w](int code, QProcess::ExitStatus status) {
        w->deleteLater();
        QFile::remove(m_wav);
        if (w != m_whisperProc)
            return;
        m_whisperProc = nullptr;
        setState(QStringLiteral("idle"));
        if (status != QProcess::NormalExit || code != 0) {
            qCWarning(lcDict) << "whisper-cli failed" << code;
            emit failed(QStringLiteral("error"));
            return;
        }
        // Drop Whisper's sound labels: [BLANK_AUDIO], (wind blowing)
        QString text = QString::fromUtf8(w->readAllStandardOutput());
        static const QRegularExpression labels(QStringLiteral("\\[[^\\]]*\\]|\\([^)]*\\)"));
        text = text.remove(labels).simplified();
        qCInfo(lcDict) << "heard" << text;
        if (text.isEmpty())
            emit failed(QStringLiteral("silence"));
        else
            emit heard(text);
    });
    w->start(m_whisper, { QStringLiteral("-m"), m_model, QStringLiteral("-f"), m_wav,
                          QStringLiteral("-l"), QStringLiteral("en"), QStringLiteral("-t"), QStringLiteral("4"),
                          QStringLiteral("-nt"), QStringLiteral("-np") });
}

void Dictation::cancel()
{
    stopRecorder();
    if (m_whisperProc) {
        QProcess *w = m_whisperProc;
        m_whisperProc = nullptr;
        w->kill();
    }
    m_pcm.clear();
    if (m_level != 0) {
        m_level = 0;
        emit levelChanged();
    }
    setState(QStringLiteral("idle"));
}
