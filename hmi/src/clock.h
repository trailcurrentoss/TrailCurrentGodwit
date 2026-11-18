#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QQmlEngine>

// The system clock and time zone, through systemd-timedated over D-Bus.
// timedated also writes the time to the board's battery-backed RTC, so a
// time set here survives a restart. The godwit user is allowed to change
// them by a polkit rule shipped with the HMI.
//
// The zone does not survive one: /etc is held in memory over the read-only
// root, so /etc/localtime is back to the image's at every boot. The HMI keeps
// the chosen zone in its own settings and sets it again on start.
//
// Without polkit, timedated refuses the godwit user; the requests then go
// to godwit-clock (a root service, os/meta-godwit/recipes-godwit/godwit-clock)
// through $XDG_RUNTIME_DIR/clock-request.
//
// Network time is ConnMan's: with TimeUpdates "auto" it syncs by NTP whenever
// Wi-Fi or Ethernet is up. setNetworkTime(false) switches that off so a time
// set by hand stays. ConnMan keeps the setting under /var, which is also in
// memory, so it too is set again on start.
//
// Used on the head unit (eglfs) only, so the simulator leaves a PC's clock
// alone. GODWIT_TIMEDATE_BUS=system or =session overrides.
class Clock : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool available READ available CONSTANT)
    // IANA name, e.g. "America/Denver"
    Q_PROPERTY(QString timezone READ timezone NOTIFY timezoneChanged)

public:
    explicit Clock(QObject *parent = nullptr);

    bool available() const { return m_available; }
    QString timezone() const { return m_timezone; }

    // Moves the clock by seconds (negative for back)
    Q_INVOKABLE void adjust(qint64 seconds);
    Q_INVOKABLE void setTimezone(const QString &zone);
    // ConnMan's NTP on or off ("Set Time Automatically")
    Q_INVOKABLE void setNetworkTime(bool on);

signals:
    void timezoneChanged();
    // After the clock or zone has changed, so the screen can redraw
    void changed();

private:
    void readTimezone();
    void useZone(const QString &zone);
    void request(const QString &key, const QString &value);

    QDBusConnection m_bus;
    bool m_available = false;
    QString m_timezone;
    int m_networkTime = -1;        // last set: -1 never, 0 off, 1 on
    int m_networkTries = 0;
};
