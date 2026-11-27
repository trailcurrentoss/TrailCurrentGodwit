#pragma once

#include <QDateTime>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>

// The sky over the camper and what the solar panels make of it, for the
// Energy screen. Everything is worked out here, with no network:
//  - The sun's and moon's places in the sky, sunrise, sunset and moonrise,
//    and the moon's phase, from the last known position and the clock.
//  - How cloudy it is: solar watts against what the array would make under a
//    clear sky at the sun's present height. Judged only while the charger is
//    in Bulk, because Absorption and Float hold the panels back on purpose.
//  - Solar Today: the watts the charger reports, added up since local midnight.
//  - Settings › Solar / House Battery: the array's rated size and the house
//    battery's capacity.
//
// Kept in solar.json in the HMI's state folder, which the factory reset
// erases: the settings, today's total, and the last position, rounded to
// about a kilometre.
class Solar : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // A position is known (from GPS now, or saved from before)
    Q_PROPERTY(bool located READ located NOTIFY skyChanged)
    // South of the equator, where the moon's lit side is mirrored
    Q_PROPERTY(bool southern READ southern NOTIFY skyChanged)
    // The moment the sky is worked out for: now, every 30 s. On a desktop,
    // GODWIT_SOLAR_AT=<ISO date-time> holds it there to preview any hour.
    Q_PROPERTY(QDateTime now READ now NOTIFY skyChanged)
    // Degrees above the horizon, and from north
    Q_PROPERTY(double sunElevation READ sunElevation NOTIFY skyChanged)
    Q_PROPERTY(double sunAzimuth READ sunAzimuth NOTIFY skyChanged)
    Q_PROPERTY(double moonElevation READ moonElevation NOTIFY skyChanged)
    Q_PROPERTY(double moonAzimuth READ moonAzimuth NOTIFY skyChanged)
    // Today's sunrise and sunset (invalid in polar day or night), and the next
    // sunrise and moonrise from now
    Q_PROPERTY(QDateTime sunrise READ sunrise NOTIFY skyChanged)
    Q_PROPERTY(QDateTime sunset READ sunset NOTIFY skyChanged)
    Q_PROPERTY(QDateTime nextSunrise READ nextSunrise NOTIFY skyChanged)
    Q_PROPERTY(QDateTime nextMoonrise READ nextMoonrise NOTIFY skyChanged)
    // 0-7: new, waxing crescent, first quarter, waxing gibbous, full, waning
    // gibbous, last quarter, waning crescent. moonPhaseName is its name.
    Q_PROPERTY(int moonPhase READ moonPhase NOTIFY skyChanged)
    Q_PROPERTY(QString moonPhaseName READ moonPhaseName NOTIFY skyChanged)

    // What a clear sky would give the array now, in watts; 0 if the array size
    // is not set or the sun is down
    Q_PROPERTY(double clearSkyWatts READ clearSkyWatts NOTIFY skyChanged)
    // 0 clear to 1 overcast, smoothed; -1 when it cannot be judged (no array
    // size, no position, the sun too low, or no solar reading yet)
    Q_PROPERTY(double cloud READ cloud NOTIFY solarChanged)
    // "sun", "partly", "cloudy" with hysteresis, or "" when not judged
    Q_PROPERTY(QString condition READ condition NOTIFY solarChanged)
    // The charger is holding the panels back (Absorption or Float)
    Q_PROPERTY(bool throttled READ throttled NOTIFY solarChanged)
    // Watt-hours from the panels since local midnight
    Q_PROPERTY(double todayWh READ todayWh NOTIFY solarChanged)

    // Settings, 0 = not set
    Q_PROPERTY(int arrayWatts READ arrayWatts WRITE setArrayWatts NOTIFY setupChanged)
    Q_PROPERTY(int batteryAh READ batteryAh WRITE setBatteryAh NOTIFY setupChanged)

public:
    explicit Solar(QObject *parent = nullptr);
    ~Solar() override;

    QDateTime now() const { return m_now; }
    bool located() const { return m_located; }
    bool southern() const { return m_lat < 0; }
    double sunElevation() const { return m_sunEl; }
    double sunAzimuth() const { return m_sunAz; }
    double moonElevation() const { return m_moonEl; }
    double moonAzimuth() const { return m_moonAz; }
    QDateTime sunrise() const { return m_sunrise; }
    QDateTime sunset() const { return m_sunset; }
    QDateTime nextSunrise() const { return m_nextSunrise; }
    QDateTime nextMoonrise() const { return m_nextMoonrise; }
    int moonPhase() const { return m_phase; }
    QString moonPhaseName() const;
    double clearSkyWatts() const { return clearSky(m_sunEl, m_arrayW); }
    double cloud() const { return m_cloud; }
    QString condition() const { return m_condition; }
    bool throttled() const { return m_throttled; }
    double todayWh() const { return m_todayWh; }
    int arrayWatts() const { return m_arrayW; }
    int batteryAh() const { return m_batteryAh; }
    void setArrayWatts(int w);
    void setBatteryAh(int ah);

    // Where the camper is. Called on each GPS fix; saved when it has moved.
    Q_INVOKABLE void setPosition(double lat, double lon);
    // The charger's latest reading: panel watts and its state ("Bulk",
    // "Absorption", "Float", "Off", "Fault", "")
    Q_INVOKABLE void feed(double watts, const QString &charge);
    // Clears today's total, for the factory reset and tests
    Q_INVOKABLE void resetToday();

    // Astronomy, in degrees, for any moment and place
    struct Position { double elevation; double azimuth; };
    static Position sunAt(const QDateTime &t, double lat, double lon);
    static Position moonAt(const QDateTime &t, double lat, double lon);
    // Sun-moon elongation, 0 at new moon, 180 at full
    static double moonAge(const QDateTime &t);
    // The first time after `from` and before `to` when the sun (or moon)
    // crosses `altitude` going up (rising) or down; invalid if it does not
    static QDateTime crossing(bool moon, const QDateTime &from, const QDateTime &to,
                              double lat, double lon, double altitude, bool rising);
    // A clear sky's watts on a flat array of `arrayW` at sun height `elev`
    static double clearSky(double elev, int arrayW);
    Q_INVOKABLE double clearSkyAt(double elev, int arrayW) const { return clearSky(elev, arrayW); }

signals:
    void skyChanged();
    void solarChanged();
    void setupChanged();

private:
    friend class TestSolar;

    void update();                         // the sky, now
    void updateAt(const QDateTime &now);
    void feedAt(double watts, const QString &charge, qint64 ms);
    void load();
    void save();

    QDateTime m_now, m_fixed;
    bool m_located = false;
    double m_lat = 0, m_lon = 0;
    double m_sunEl = -90, m_sunAz = 0, m_moonEl = -90, m_moonAz = 0;
    QDateTime m_sunrise, m_sunset, m_nextSunrise, m_nextMoonrise;
    int m_phase = 0;

    int m_arrayW = 0;
    int m_batteryAh = 0;

    double m_ratio = -1;          // smoothed solar / clear sky
    double m_cloud = -1;
    QString m_condition;
    bool m_throttled = false;
    qint64 m_lastFeed = 0;        // ms
    double m_todayWh = 0;
    QDate m_today;
    qint64 m_lastSave = 0;
    bool m_unsaved = false;

    QTimer m_timer;
};
