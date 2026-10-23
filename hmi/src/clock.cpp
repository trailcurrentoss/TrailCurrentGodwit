#include "clock.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QLoggingCategory>

#include <ctime>

Q_LOGGING_CATEGORY(lcClock, "godwit.clock")

namespace {

const QString Service = QStringLiteral("org.freedesktop.timedate1");
const QString Path = QStringLiteral("/org/freedesktop/timedate1");
const QString Iface = QStringLiteral("org.freedesktop.timedate1");

QDBusConnection openBus()
{
    return qEnvironmentVariable("GODWIT_TIMEDATE_BUS") == QLatin1String("session")
        ? QDBusConnection::sessionBus() : QDBusConnection::systemBus();
}

} // namespace

Clock::Clock(QObject *parent)
    : QObject(parent)
    , m_bus(openBus())
{
    m_available = (!qEnvironmentVariableIsEmpty("GODWIT_TIMEDATE_BUS")
                   || QGuiApplication::platformName() == QLatin1String("eglfs"))
        && m_bus.isConnected();
    if (m_available)
        readTimezone();
}

void Clock::readTimezone()
{
    QDBusMessage get = QDBusMessage::createMethodCall(Service, Path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                      QStringLiteral("Get"));
    get << Iface << QStringLiteral("Timezone");
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(get), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        w->deleteLater();
        const QDBusReply<QDBusVariant> reply = *w;
        if (!reply.isValid()) {
            qCWarning(lcClock) << "time zone unknown:" << reply.error().message();
            return;
        }
        const QString zone = reply.value().variant().toString();
        if (zone != m_timezone) {
            m_timezone = zone;
            emit timezoneChanged();
        }
    });
}

void Clock::adjust(qint64 seconds)
{
    if (!m_available || seconds == 0)
        return;
    // Relative: the usec are added to the current time
    QDBusMessage set = QDBusMessage::createMethodCall(Service, Path, Iface, QStringLiteral("SetTime"));
    set << qint64(seconds * 1000000) << true << false;
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(set), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w] {
        w->deleteLater();
        if (w->isError())
            qCWarning(lcClock) << "SetTime failed:" << w->error().message();
        else
            emit changed();
    });
}

void Clock::setTimezone(const QString &zone)
{
    if (!m_available || zone.isEmpty() || zone == m_timezone)
        return;
    QDBusMessage set = QDBusMessage::createMethodCall(Service, Path, Iface, QStringLiteral("SetTimezone"));
    set << zone << false;
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(set), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, zone] {
        w->deleteLater();
        if (w->isError()) {
            qCWarning(lcClock) << "SetTimezone failed:" << w->error().message();
            return;
        }
        // glibc does not notice /etc/localtime changing while TZ is unset;
        // name the zone so local time in this process follows it
        qputenv("TZ", ":" + zone.toLocal8Bit());
        tzset();
        m_timezone = zone;
        emit timezoneChanged();
        emit changed();
    });
}
