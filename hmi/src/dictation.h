#pragma once

#include <QByteArray>
#include <QObject>
#include <QQmlEngine>

class QProcess;

// Speech to text for message replies, offline: pw-record takes 16 kHz mono
// from PipeWire's default microphone, listening stops after a pause in the
// speech, and whisper.cpp (whisper-cli, base.en) writes out what was said.
//
// Used on the head unit (eglfs) only, or with GODWIT_DICTATION=1. GODWIT_MIC
// names the PipeWire source to record from; GODWIT_WHISPER and
// GODWIT_WHISPER_MODEL override where whisper-cli and its model are.
class Dictation : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)
    // "idle", "listening" or "transcribing"
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    // How loud the microphone is while listening, 0 to 1
    Q_PROPERTY(double level READ level NOTIFY levelChanged)

public:
    explicit Dictation(QObject *parent = nullptr);
    ~Dictation() override;

    bool available() const { return m_available; }
    QString state() const { return m_state; }
    double level() const { return m_level; }

    // Start listening; it stops by itself after a pause, or with finish()
    Q_INVOKABLE void listen();
    // Stop listening now and write out what was said
    Q_INVOKABLE void finish();
    // Stop without a result
    Q_INVOKABLE void cancel();

signals:
    void stateChanged();
    void levelChanged();
    void heard(const QString &text);
    // reason: "silence" (nothing was said) or "error"
    void failed(const QString &reason);

private:
    void readAudio();
    void transcribe();
    void setState(const QString &state);
    void stopRecorder();

    bool m_available = false;
    QString m_whisper;
    QString m_model;
    QString m_wav;
    QString m_state = QStringLiteral("idle");
    double m_level = 0;
    QProcess *m_recorder = nullptr;
    QProcess *m_whisperProc = nullptr;
    QByteArray m_pcm;        // everything recorded, 16-bit mono
    QByteArray m_partial;    // a frame not yet complete
    double m_floor = 0;      // background level, from the first frames
    int m_frames = 0;
    int m_loud = 0;          // loud frames in a row
    int m_quiet = 0;         // quiet frames in a row since speech began
    bool m_speech = false;
};
