#pragma once

#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

class QProcess;

// NOAA Weather Radio, on the RTL-SDR dongle the Radio, Aircraft and Ships apps also
// use. godwit-wxradio ([../radio]) demodulates it and pw-play carries the sound to
// PipeWire's multimedia role, as music takes. Both run only while the Weather Radio
// app is open.
//
// The broadcast does not say which transmitter it is, so the app names the nearest on
// each channel from a table on the head unit. No internet is involved.
//
// Used on the head unit (eglfs) only, or with GODWIT_RADIO=1. Elsewhere available
// stays false and the app shows the simulator's demo stations. GODWIT_WXRADIO_BIN
// overrides where godwit-wxradio is, and GODWIT_WX_STATIONS where the table is.
class WeatherRadio : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)
    // Whether to run the tuner
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // In MHz: one of the seven channels
    Q_PROPERTY(double frequency READ frequency WRITE setFrequency NOTIFY frequencyChanged)
    // It has the dongle, or is still letting go of it. Nothing else may open the
    // dongle until this goes false.
    Q_PROPERTY(bool tunerHeld READ tunerHeld NOTIFY tunerHeldChanged)
    // False while the dongle is missing
    Q_PROPERTY(bool tunerFound READ tunerFound NOTIFY stateChanged)
    // It has opened the dongle
    Q_PROPERTY(bool listening READ listening NOTIFY stateChanged)
    // There is a station on this channel, and so something to hear
    Q_PROPERTY(bool receiving READ receiving NOTIFY stateChanged)
    // How strong each of the seven is, 0..1
    Q_PROPERTY(QVariantList levels READ levels NOTIFY levelsChanged)

public:
    explicit WeatherRadio(QObject *parent = nullptr);
    ~WeatherRadio() override;

    bool available() const { return m_available; }
    bool active() const { return m_active; }
    void setActive(bool active);
    double frequency() const { return m_hz / 1e6; }
    void setFrequency(double mhz);
    bool tunerHeld() const { return m_held; }
    bool tunerFound() const { return m_tunerFound; }
    bool listening() const { return m_listening; }
    bool receiving() const { return m_receiving; }
    QVariantList levels() const { return m_levels; }

    // The seven channels in order: { mhz, f: "162.550" } and, given a position and
    // a table, the nearest transmitter on it: { n: site, c: call sign, st: state,
    // mi: miles }
    Q_INVOKABLE QVariantList channels(bool located, double lat, double lon) const;
    // Goes to the strongest channel. False if nothing stands out from the noise.
    Q_INVOKABLE bool tuneStrongest();

signals:
    void activeChanged();
    void frequencyChanged();
    void tunerHeldChanged();
    void stateChanged();
    void levelsChanged();

private:
    struct Station
    {
        int hz;
        QString call, site, state;
        double lat, lon;
    };

    void loadTable(const QString &path);
    void start();
    void stop();
    void readHelper();
    void handleLine(const QByteArray &line);
    void tune(int hz);
    void updateHeld();
    void clearLevels();

    bool m_available = false;
    bool m_active = false;
    bool m_held = false;
    bool m_tunerFound = true;
    bool m_listening = false;
    bool m_receiving = false;
    int m_hz = 162550000;
    // Someone has chosen a channel, or the first open has found one; until then the
    // first levels choose it
    bool m_chosen = false;

    QString m_bin;
    QProcess *m_helper = nullptr;
    QProcess *m_player = nullptr;
    // One still letting go of the dongle: the next must wait for it
    QProcess *m_exiting = nullptr;
    QByteArray m_buffer;

    int m_statusLines = 0;
    QList<double> m_levelsDb;
    QVariantList m_levels;
    QList<Station> m_stations;

    QTimer m_retry;       // looks for the dongle again
    QTimer m_saveDelay;
};
