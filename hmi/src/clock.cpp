#include "clock.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QFile>
#include <QLoggingCategory>
#include <QMap>
#include <QSaveFile>
#include <QTimer>

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
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, seconds] {
        w->deleteLater();
        if (w->isError()) {
            qCInfo(lcClock) << "SetTime refused (" << w->error().message() << "), asking godwit-clock";
            request(QStringLiteral("ADJUST"), QString::number(seconds));
            QTimer::singleShot(1500, this, &Clock::changed);
        } else {
            emit changed();
        }
    });
}

void Clock::setTimezone(const QString &zone)
{
    if (!m_available || zone.isEmpty() || zone == m_timezone)
        return;
    // This screen follows the zone at once, whatever the system does
    useZone(zone);
    QDBusMessage set = QDBusMessage::createMethodCall(Service, Path, Iface, QStringLiteral("SetTimezone"));
    set << zone << false;
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(set), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, zone] {
        w->deleteLater();
        if (w->isError()) {
            qCInfo(lcClock) << "SetTimezone refused (" << w->error().message() << "), asking godwit-clock";
            request(QStringLiteral("ZONE"), zone);
        }
    });
}

// Local time in this process, in the zone. glibc does not notice
// /etc/localtime changing while TZ is unset, so the zone is named.
void Clock::useZone(const QString &zone)
{
    qputenv("TZ", ":" + zone.toLocal8Bit());
    tzset();
    m_timezone = zone;
    qCInfo(lcClock) << "time zone" << zone;
    emit timezoneChanged();
    emit changed();
}

// Ask godwit-clock (root) to do what timedated would not let this user do:
// KEY=value lines in $XDG_RUNTIME_DIR/clock-request. A request it has not
// taken yet is merged with, so nothing is lost: ADJUST adds up.
void Clock::request(const QString &key, const QString &value)
{
    const QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (dir.isEmpty())
        return;
    const QString path = dir + QStringLiteral("/clock-request");
    QMap<QString, QString> kv;
    QFile old(path);
    if (old.open(QIODevice::ReadOnly)) {
        for (const QByteArray &line : old.readAll().split('\n')) {
            const int eq = line.indexOf('=');
            if (eq > 0)
                kv.insert(QString::fromUtf8(line.left(eq)), QString::fromUtf8(line.mid(eq + 1)));
        }
    }
    if (key == QLatin1String("ADJUST") && kv.contains(key))
        kv[key] = QString::number(kv.value(key).toLongLong() + value.toLongLong());
    else
        kv[key] = value;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qCWarning(lcClock) << "cannot write" << path;
        return;
    }
    for (auto it = kv.cbegin(); it != kv.cend(); ++it)
        f.write((it.key() + QLatin1Char('=') + it.value() + QLatin1Char('\n')).toUtf8());
    if (!f.commit())
        qCWarning(lcClock) << "cannot write" << path;
}

void Clock::setNetworkTime(bool on)
{
    if (!m_available || m_networkTime == int(on))
        return;
    // A private connection: ConnMan may not be on the bus yet at start
    const QString name = QStringLiteral("godwit-clock-connman");
    QDBusConnection::disconnectFromBus(name);
    QDBusConnection bus = QDBusConnection::connectToBus(
        qEnvironmentVariable("GODWIT_TIMEDATE_BUS") == QLatin1String("session")
            ? QDBusConnection::SessionBus : QDBusConnection::SystemBus, name);
    QDBusMessage set = QDBusMessage::createMethodCall(QStringLiteral("net.connman"), QStringLiteral("/"),
                                                      QStringLiteral("net.connman.Clock"), QStringLiteral("SetProperty"));
    set << QStringLiteral("TimeUpdates")
        << QVariant::fromValue(QDBusVariant(on ? QStringLiteral("auto") : QStringLiteral("manual")));
    auto *w = new QDBusPendingCallWatcher(bus.asyncCall(set), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, w, on] {
        w->deleteLater();
        if (w->isError()) {
            // Not started yet: try again for a minute
            qCWarning(lcClock) << "network time" << (on ? "on" : "off") << "failed:" << w->error().message();
            if (++m_networkTries < 12)
                QTimer::singleShot(5000, this, [this, on] { if (m_networkTime != int(on)) setNetworkTime(on); });
            return;
        }
        qCInfo(lcClock) << "network time" << (on ? "on" : "off");
        m_networkTime = int(on);
        m_networkTries = 0;
    });
}
