#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QTimer>

class QProcess;

// The wake word, offline: pw-record takes 16 kHz mono from PipeWire's default
// microphone and godwit-wake (openWakeWord's models on ONNX Runtime) listens
// for "Hey Peregrine" in it. Listening runs while enabled; the HMI decides
// whether a detection is acted on.
//
// Used on the head unit (eglfs) only, or with GODWIT_WAKE=1. GODWIT_MIC names
// the PipeWire source, as for dictation; GODWIT_WAKE_BIN and GODWIT_WAKE_DIR
// override where godwit-wake and its models are.
class Wake : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    // The detector is running
    Q_PROPERTY(bool listening READ listening NOTIFY listeningChanged)

public:
    explicit Wake(QObject *parent = nullptr);
    ~Wake() override;

    bool available() const { return m_available; }
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool listening() const { return m_detector != nullptr; }

signals:
    void enabledChanged();
    void listeningChanged();
    void detected(double score);

private:
    void start();
    void stop();
    void readDetector();

    bool m_available = false;
    bool m_enabled = false;
    QString m_bin;
    QString m_dir;
    QProcess *m_recorder = nullptr;
    QProcess *m_detector = nullptr;
    // Starts again after the microphone or the detector stops by itself
    QTimer m_retry;
    int m_failures = 0;
};
