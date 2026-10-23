#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QQmlEngine>

// The system clock and time zone, through systemd-timedated over D-Bus.
// timedated also writes the time to the board's battery-backed RTC, so a
// time set here survives a restart. The godwit user is allowed to change
// them by a polkit rule shipped with the HMI.
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

signals:
    void timezoneChanged();
    // After the clock or zone has changed, so the screen can redraw
    void changed();

private:
    void readTimezone();

    QDBusConnection m_bus;
    bool m_available = false;
    QString m_timezone;
};
