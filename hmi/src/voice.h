#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>

class QProcess;

// Spoken navigation prompts, offline: Piper (piper-tts) turns each into a WAV
// and pw-play plays it on PipeWire's navigation role, so it ducks music and
// follows the guidance volume. Piper stays running, so the voice is loaded
// once. A new prompt cuts off one still playing.
//
// Used on the head unit (eglfs) only, or with GODWIT_VOICE=1. GODWIT_PIPER and
// GODWIT_PIPER_VOICE override where Piper and its voice are.
class Voice : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)
    // A prompt is being prepared or played
    Q_PROPERTY(bool speaking READ speaking NOTIFY speakingChanged)

public:
    explicit Voice(QObject *parent = nullptr);
    ~Voice() override;

    bool available() const { return m_available; }
    bool speaking() const { return m_speaking; }

    Q_INVOKABLE void say(const QString &text);
    // Stops what is playing and anything waiting
    Q_INVOKABLE void stop();

signals:
    void speakingChanged();

private:
    void update();
    bool startPiper();
    void readPiper();
    void play(const QString &wav);

    bool m_available = false;
    bool m_speaking = false;
    QString m_piper;
    QString m_model;
    QString m_outDir;
    QProcess *m_tts = nullptr;
    QProcess *m_player = nullptr;
    // Prompts sent to Piper and not yet back; only the newest is played
    int m_pending = 0;
    // Prompts still coming back from Piper after stop(), to throw away
    int m_drop = 0;
};
