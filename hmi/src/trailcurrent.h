#pragma once

#include "dbc.h"

#include <QHash>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <array>
#include <functional>

class CanBus;
class QSocketNotifier;
struct can_frame;

// The TrailCurrent bus: the camper's lights, relays, battery, solar, tanks,
// leveling, thermostat, air and door sensors, read and commanded directly over
// SocketCAN on the port set to TrailCurrent in Settings › CAN Buses.
//
// Messages are decoded and encoded by name from TrailCurrent.dbc (installed by
// trailcurrent-dbc), so their layout lives only in that file. What they mean
// lives here:
//  - Lights are set with TorrentBrightness, [channel, level], never with a
//    single-channel toggle: a toggle sent twice undoes itself.
//  - Switchback relays only have a toggle. One is sent only when the relay's
//    status differs from what is wanted, then checked, and retried once.
//  - Therma's setpoint is requested; the value shown is always the one Therma
//    broadcasts back, never the one asked for.
// There are no acknowledgements on the bus: a command has worked when the
// module's status frame says so.
//
// The vehicle bus is not read here; it will go through AGL's KUKSA databroker.
//
// On the head unit (eglfs), or wherever GODWIT_TRAILCURRENT_IFACE names an
// interface (vcan0 on a desktop), `live` is true and the screens show only
// what the bus reports. Otherwise the simulator keeps its demo data.
class TrailCurrent : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // The screens show bus data (and nothing made up) rather than demo data
    Q_PROPERTY(bool live READ live CONSTANT)
    // TrailCurrent.dbc was read and has the messages used here
    Q_PROPERTY(bool ready READ ready NOTIFY linkChanged)
    // The interface listened on, "" if no port is set to TrailCurrent or it is missing
    Q_PROPERTY(QString iface READ iface NOTIFY linkChanged)
    // Why commands cannot be sent now, "" if they can
    Q_PROPERTY(QString blocked READ blocked NOTIFY linkChanged)
    // Modules heard since start or remembered from before:
    // {id ("torrent0"), n ("Torrent 1"), fresh}
    Q_PROPERTY(QVariantList modules READ modules NOTIFY modulesChanged)
    // Torrent outputs and Switchback relays of known modules:
    // {key, n, kind ("pwm" or "relay"), on, b (0-100), dim, hidden, fresh, busy}
    Q_PROPERTY(QVariantList lights READ lights NOTIFY lightsChanged)
    // Picket and Switchback inputs that have been seen closed, so have a switch
    // fitted: {key, n, open, warn (while driving), fresh}
    Q_PROPERTY(QVariantList inputs READ inputs NOTIFY inputsChanged)
    // Solstice: {fresh, volts, amps, soc, watts, ttg (minutes, -1 if charging),
    //  solar (bool), solarW, panelV, panelA, charge ("Off", "Bulk", ...)}
    Q_PROPERTY(QVariantMap energy READ energy NOTIFY energyChanged)
    // Reservoir: {fresh, levels: [fresh, grey, black] in %}
    Q_PROPERTY(QVariantMap water READ water NOTIFY waterChanged)
    // Plateau: {fresh, pitch, roll} in degrees
    Q_PROPERTY(QVariantMap level READ level NOTIFY levelChanged)
    // Therma: {fresh, set, threshold, cur (°C, null before its first sample),
    //  mode ("idle", "heat", "cool"), fault (bits), pending (°C asked for, or null)}
    Q_PROPERTY(QVariantMap camper READ camper NOTIFY camperChanged)
    // Borealis: {fresh, lost (heard, then silent), tempC, rh, co2, voc, co, lpg,
    //  coWarn, coAlarm, lpgWarn, lpgAlarm, co2Warn, co2Alarm, vocAlarm}
    Q_PROPERTY(QVariantMap air READ air NOTIFY airChanged)

public:
    explicit TrailCurrent(CanBus *can, QObject *parent = nullptr);
    ~TrailCurrent() override;
    static TrailCurrent *create(QQmlEngine *qml, QJSEngine *js);

    bool live() const { return m_live; }
    bool ready() const { return !m_handlers.isEmpty(); }
    QString iface() const { return m_fd >= 0 ? m_iface : QString(); }
    QString blocked() const;
    QVariantList modules() const { return m_modulesOut; }
    QVariantList lights() const { return m_lightsOut; }
    QVariantList inputs() const { return m_inputsOut; }
    QVariantMap energy() const { return m_energyOut; }
    QVariantMap water() const { return m_waterOut; }
    QVariantMap level() const { return m_levelOut; }
    QVariantMap camper() const { return m_camperOut; }
    QVariantMap air() const { return m_airOut; }

    // Commands. False, with a message, if they cannot be sent.
    Q_INVOKABLE bool setLight(const QString &key, bool on);
    // 0-100; 0 turns it off. Dimmable outputs only.
    Q_INVOKABLE bool setBrightness(const QString &key, int percent);
    // Every light shown in the Lights app
    Q_INVOKABLE bool allLightsOff();
    // Ask Therma for a setpoint, in °C
    Q_INVOKABLE bool setCamperSetpoint(double celsius);

    // Settings › TrailCurrent, saved in trailcurrent.json
    Q_INVOKABLE void rename(const QString &key, const QString &name);
    Q_INVOKABLE void setHidden(const QString &key, bool hidden);
    Q_INVOKABLE void setDimmable(const QString &key, bool dim);
    Q_INVOKABLE void setWarn(const QString &key, bool warn);
    // Forget modules not heard since start, and their names
    Q_INVOKABLE void forgetMissing();

signals:
    void linkChanged();
    void modulesChanged();
    void lightsChanged();
    void inputsChanged();
    void energyChanged();
    void waterChanged();
    void levelChanged();
    void camperChanged();
    void airChanged();
    // Something to tell the driver, as a toast
    void message(const QString &text);

private:
    // tests/tst_trailcurrent.cpp feeds it frames and reads what it sends
    friend class TestTrailCurrent;

    enum Group { Lights = 1, Inputs = 2, Energy = 4, Water = 8, Level = 16, Camper = 32, Air = 64, Modules = 128 };
    // A command waiting to be sent, or sent and waiting for the status to agree
    struct Pending {
        int target = 0;
        qint64 since = 0;    // when asked
        qint64 sentAt = -1;  // when last sent
        int tries = 0;
        bool dirty = true;   // the target changed since it was sent
    };
    using Handler = std::function<void(const QHash<QString, double> &)>;

    void loadDbc();
    void addHandler(const QString &message, const QStringList &signalNames, Handler h);
    void loadConfig();
    void saveConfig();
    QString configPath() const;

    void poll();
    void openSocket(const QString &iface);
    void closeSocket();
    void readFrames();
    void handleFrame(const can_frame &f);
    bool sendFrame(const QString &message, const QHash<QString, double> &values);
    void flushPending();
    void publish();

    void seen(const QString &module);
    bool fresh(const QString &module) const;
    QString lightName(const QString &key) const;
    QString inputName(const QString &key) const;
    // The level (Torrent, 0-255) or state (relay, 0/1) now, -1 if not known
    int lightStatus(const QString &key) const;
    bool refuse(const QString &reason);

    CanBus *m_can = nullptr;
    bool m_live = false;
    QString m_overrideIface;
    Dbc m_dbc;
    QHash<quint32, std::pair<const Dbc::Message *, Handler>> m_handlers;

    int m_fd = -1;
    QSocketNotifier *m_notifier = nullptr;
    QString m_iface;
    bool m_up = false;
    bool m_listenOnly = false;
    bool m_busOff = false;
    qint64 m_lastTxError = 0;

    // Last status heard; -1 where not heard
    std::array<std::array<int, 8>, 3> m_pwm;
    std::array<int, 3> m_relays;
    std::array<int, 11> m_doors;   // PicketStatus0-10: Picket 0-7, then Switchback 0-2 inputs
    // The last level each output was on at, to turn it back on at
    QHash<QString, int> m_lastOn;
    QVariantMap m_energy, m_water, m_level, m_camper, m_air;

    QHash<QString, qint64> m_seen;  // module id -> when last heard (ms)
    QSet<QString> m_freshNow;       // as of the last poll
    QSet<QString> m_known;          // saved: heard at some point
    QVariantMap m_lightsConf, m_inputsConf;

    QHash<QString, Pending> m_pending;   // "t0.3", "r1.5", "therma"
    QHash<QString, qint64> m_lastSent;   // per pending key

    int m_dirty = 0;
    int m_tick = 0;
    QTimer m_pollTimer;
    QTimer m_flushTimer;

    QVariantList m_modulesOut, m_lightsOut, m_inputsOut;
    QVariantMap m_energyOut, m_waterOut, m_levelOut, m_camperOut, m_airOut;
};
