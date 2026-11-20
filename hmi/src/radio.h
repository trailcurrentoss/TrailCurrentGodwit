#pragma once

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

class QProcess;

// The FM tuner, on an RTL-SDR dongle, which the Aircraft app also uses.
// godwit-radio ([../radio]) demodulates it and pw-play carries the sound to
// PipeWire's multimedia role, as music takes.
// Both run only while the radio is what is heard, so the tuner is idle and the
// dongle free the rest of the time.
//
// The first time, with no presets yet, the band is scanned and the strongest
// stations become the presets. Presets, the last frequency and the names RDS
// gave each station are kept in the state directory.
//
// Used on the head unit (eglfs) only, or with GODWIT_RADIO=1. Elsewhere the
// Radio app shows the simulator's demo stations. GODWIT_RADIO_BIN overrides
// where godwit-radio is.
class Radio : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)
    // Whether to run the tuner. Set by what is being heard.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // In MHz. Also moves by itself when seeking or after a scan: see tunedTo.
    Q_PROPERTY(double frequency READ frequency WRITE setFrequency NOTIFY tunedTo)
    // It has the dongle, or is still letting go of it. There is only the one:
    // the Aircraft app must wait for this to go false, as the radio must for
    // Aircraft.tunerHeld.
    Q_PROPERTY(bool tunerHeld READ tunerHeld NOTIFY tunerHeldChanged)
    // False while the dongle is missing
    Q_PROPERTY(bool tunerFound READ tunerFound NOTIFY stateChanged)
    Q_PROPERTY(bool seeking READ seeking NOTIFY stateChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY stateChanged)
    // There is a station on this frequency
    Q_PROPERTY(bool receiving READ receiving NOTIFY stateChanged)
    // 0..1
    Q_PROPERTY(double signal READ signal NOTIFY stateChanged)
    Q_PROPERTY(bool stereo READ stereo NOTIFY stateChanged)
    // The station as RDS has it: its call sign or name, the kind of programme
    // and what is on. Empty until sent, and many stations send only some.
    Q_PROPERTY(QString name READ name NOTIFY stateChanged)
    Q_PROPERTY(QString genre READ genre NOTIFY stateChanged)
    Q_PROPERTY(QString text READ text NOTIFY stateChanged)
    // Six of { b: "FM", f: MHz }, f being 0 when not set
    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    // Stations by name, for the voice assistant: { f: MHz, n: name }
    Q_PROPERTY(QVariantList known READ known NOTIFY knownChanged)

public:
    explicit Radio(QObject *parent = nullptr);
    ~Radio() override;

    bool available() const { return m_available; }
    bool active() const { return m_active; }
    void setActive(bool active);
    double frequency() const { return m_hz / 1e6; }
    void setFrequency(double mhz);
    bool tunerHeld() const { return m_held; }
    bool tunerFound() const { return m_tunerFound; }
    bool seeking() const { return m_seeking; }
    bool scanning() const { return m_scanning; }
    bool receiving() const { return m_receiving; }
    double signal() const { return m_signal; }
    bool stereo() const { return m_stereo; }
    QString name() const { return m_name; }
    QString genre() const { return m_genre; }
    QString text() const { return m_text; }
    QVariantList presets() const { return m_presets; }
    QVariantList known() const;

    Q_INVOKABLE void seek(int direction);
    Q_INVOKABLE void savePresets(const QVariantList &presets);
    // The name a frequency is known by, or empty
    Q_INVOKABLE QString nameAt(double mhz) const;
    // The signal last measured on a frequency, 0..1, or 0 if it has not been:
    // from the band scan, and from listening. The dial's tick while tuning
    // comes from this, since signal itself is only measured some time after
    // a retune.
    Q_INVOKABLE double signalAt(double mhz) const;

signals:
    void activeChanged();
    void tunerHeldChanged();
    void tunedTo(double mhz);
    void stateChanged();
    void presetsChanged();
    void knownChanged();

private:
    void start();
    void stop();
    void send(const QByteArray &command);
    void readHelper();
    void handleLine(const QString &line);
    void clearStation();
    void updateHeld();
    void applyScan();
    void learnName();
    void save();
    bool presetsEmpty() const;

    bool m_available = false;
    bool m_active = false;
    bool m_held = false;
    QString m_bin;
    QProcess *m_helper = nullptr;
    QProcess *m_player = nullptr;
    // A helper still letting go of the dongle: the next must wait for it
    QProcess *m_exiting = nullptr;
    QByteArray m_buffer;

    int m_hz = 87900000;
    bool m_tunerFound = true;
    bool m_seeking = false;
    bool m_scanning = false;
    bool m_receiving = false;
    double m_signal = 0;
    bool m_stereo = false;
    QString m_call, m_ps, m_name, m_genre, m_text;
    QVariantList m_presets;
    QHash<int, QString> m_names;      // Hz -> name
    QList<QPair<int, double>> m_found; // scan results: Hz, signal in dB
    // Hz -> signal 0..1, as measured this session. Not saved: what can be
    // heard depends on where the vehicle is.
    QHash<int, double> m_levels;
    int m_helperHz = 0;   // where godwit-radio is tuned, as it last said

    QTimer m_tuneDelay;   // a knob spun fast tunes once, where it stopped
    QTimer m_retry;       // looks for the dongle again
    QTimer m_saveDelay;
};
