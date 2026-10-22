#include "ofono.h"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(lcCall, "godwit.calls")

namespace {

const QString Service = QStringLiteral("org.ofono");
const QString ManagerIface = QStringLiteral("org.ofono.Manager");
const QString ModemIface = QStringLiteral("org.ofono.Modem");
const QString CallManagerIface = QStringLiteral("org.ofono.VoiceCallManager");
const QString CallIface = QStringLiteral("org.ofono.VoiceCall");
const QString VolumeIface = QStringLiteral("org.ofono.CallVolume");
const QString BusName = QStringLiteral("godwit-ofono");

// A private connection, retried until the system bus is up (see connman.cpp)
QDBusConnection openBus()
{
    QDBusConnection::disconnectFromBus(BusName);
    return QDBusConnection::connectToBus(
        qEnvironmentVariable("GODWIT_OFONO_BUS") == QLatin1String("session")
            ? QDBusConnection::SessionBus : QDBusConnection::SystemBus,
        BusName);
}

// a(oa{sv}), the shape of GetModems and GetCalls
QList<QPair<QString, QVariantMap>> readObjects(const QVariant &v)
{
    QList<QPair<QString, QVariantMap>> out;
    const QDBusArgument arg = v.value<QDBusArgument>();
    arg.beginArray();
    while (!arg.atEnd()) {
        QDBusObjectPath path;
        QVariantMap props;
        arg.beginStructure();
        arg >> path >> props;
        arg.endStructure();
        out.append({ path.path(), props });
    }
    arg.endArray();
    return out;
}

QVariantMap readMap(const QVariant &v)
{
    QVariantMap out;
    v.value<QDBusArgument>() >> out;
    return out;
}

void logErrors(const QDBusPendingCall &pending, QObject *ctx, const QString &what)
{
    auto *w = new QDBusPendingCallWatcher(pending, ctx);
    QObject::connect(w, &QDBusPendingCallWatcher::finished, ctx, [w, what] {
        w->deleteLater();
        if (w->isError())
            qCWarning(lcCall) << what << "failed:" << w->error().name() << w->error().message();
    });
}

} // namespace

Ofono::Ofono(QObject *parent)
    : QObject(parent)
    , m_bus(openBus())
{
    // Only the head unit drives real calls. A PC running the simulator keeps
    // the demo calls unless GODWIT_OFONO_BUS says which bus to use.
    if (qEnvironmentVariableIsEmpty("GODWIT_OFONO_BUS") && QGuiApplication::platformName() != QLatin1String("eglfs"))
        return;
    start();
}

void Ofono::start()
{
    if (!m_bus.isConnected()) {
        if (++m_busTries < 120)
            QTimer::singleShot(500, this, [this] { m_bus = openBus(); start(); });
        return;
    }
    qCInfo(lcCall) << "connected to the bus";

    auto *watcher = new QDBusServiceWatcher(Service, m_bus,
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] { refreshModems(); });
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        m_modems.clear();
        pickModem();
    });

    m_bus.connect(Service, QStringLiteral("/"), ManagerIface, QStringLiteral("ModemAdded"), this, SLOT(onModemAdded(QDBusMessage)));
    m_bus.connect(Service, QStringLiteral("/"), ManagerIface, QStringLiteral("ModemRemoved"), this, SLOT(onModemRemoved(QDBusMessage)));
    m_bus.connect(Service, QString(), ModemIface, QStringLiteral("PropertyChanged"), this, SLOT(onModemChanged(QDBusMessage)));
    m_bus.connect(Service, QString(), CallManagerIface, QStringLiteral("CallAdded"), this, SLOT(onCallAdded(QDBusMessage)));
    m_bus.connect(Service, QString(), CallManagerIface, QStringLiteral("CallRemoved"), this, SLOT(onCallRemoved(QDBusMessage)));
    m_bus.connect(Service, QString(), CallIface, QStringLiteral("PropertyChanged"), this, SLOT(onCallChanged(QDBusMessage)));
    m_bus.connect(Service, QString(), VolumeIface, QStringLiteral("PropertyChanged"), this, SLOT(onVolumeChanged(QDBusMessage)));

    if (m_bus.interface()->isServiceRegistered(Service))
        refreshModems();
}

void Ofono::refreshModems()
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, QStringLiteral("/"), ManagerIface, QStringLiteral("GetModems"));
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            qCWarning(lcCall) << "GetModems failed:" << reply.errorName() << reply.errorMessage();
            return;
        }
        m_modems.clear();
        for (const auto &[path, props] : readObjects(reply.arguments().constFirst()))
            m_modems.insert(path, props);
        pickModem();
    });
}

// The first hands-free phone that is online and can make calls
void Ofono::pickModem()
{
    QString found;
    for (auto it = m_modems.cbegin(); it != m_modems.cend(); ++it) {
        const QVariantMap &p = it.value();
        if (p.value(QStringLiteral("Type")).toString() == QLatin1String("hfp")
            && p.value(QStringLiteral("Online")).toBool()
            && p.value(QStringLiteral("Interfaces")).toStringList().contains(CallManagerIface)) {
            found = it.key();
            break;
        }
    }
    if (found == m_modem)
        return;
    m_modem = found;
    qCInfo(lcCall) << (found.isEmpty() ? QStringLiteral("no phone for calls") : QStringLiteral("phone for calls: ") + phoneName());
    m_calls.clear();
    rebuild();
    emit availableChanged();
    if (!found.isEmpty())
        refreshCalls();
}

void Ofono::refreshCalls()
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, m_modem, CallManagerIface, QStringLiteral("GetCalls"));
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return;
        m_calls.clear();
        for (const auto &[path, props] : readObjects(reply.arguments().constFirst()))
            m_calls.insert(path, props);
        rebuild();
    });
    QDBusMessage vol = QDBusMessage::createMethodCall(Service, m_modem, VolumeIface, QStringLiteral("GetProperties"));
    auto *v = new QDBusPendingCallWatcher(m_bus.asyncCall(vol), this);
    connect(v, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *v) {
        v->deleteLater();
        const QDBusMessage reply = v->reply();
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            m_muted = readMap(reply.arguments().constFirst()).value(QStringLiteral("Muted")).toBool();
            emit mutedChanged();
        }
    });
}

void Ofono::onModemAdded(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (args.size() < 2)
        return;
    m_modems.insert(args.at(0).value<QDBusObjectPath>().path(), readMap(args.at(1)));
    pickModem();
}

void Ofono::onModemRemoved(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (args.isEmpty())
        return;
    m_modems.remove(args.at(0).value<QDBusObjectPath>().path());
    pickModem();
}

void Ofono::onModemChanged(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (args.size() < 2 || !m_modems.contains(msg.path()))
        return;
    m_modems[msg.path()].insert(args.at(0).toString(), args.at(1).value<QDBusVariant>().variant());
    pickModem();
}

void Ofono::onCallAdded(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (msg.path() != m_modem || args.size() < 2)
        return;
    const QString path = args.at(0).value<QDBusObjectPath>().path();
    m_calls.insert(path, readMap(args.at(1)));
    qCInfo(lcCall) << "call" << m_calls.value(path).value(QStringLiteral("State")).toString();
    rebuild();
}

void Ofono::onCallRemoved(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (msg.path() != m_modem || args.isEmpty())
        return;
    if (m_calls.remove(args.at(0).value<QDBusObjectPath>().path())) {
        qCInfo(lcCall) << "call ended";
        rebuild();
    }
}

void Ofono::onCallChanged(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (args.size() < 2 || !m_calls.contains(msg.path()))
        return;
    const QString name = args.at(0).toString();
    const QVariant value = args.at(1).value<QDBusVariant>().variant();
    m_calls[msg.path()].insert(name, value);
    if (name == QLatin1String("State"))
        qCInfo(lcCall) << "call" << value.toString();
    rebuild();
}

void Ofono::onVolumeChanged(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (msg.path() != m_modem || args.size() < 2 || args.at(0).toString() != QLatin1String("Muted"))
        return;
    m_muted = args.at(1).value<QDBusVariant>().variant().toBool();
    emit mutedChanged();
}

void Ofono::rebuild()
{
    QVariantList list;
    for (auto it = m_calls.cbegin(); it != m_calls.cend(); ++it) {
        const QVariantMap &p = it.value();
        const QString state = p.value(QStringLiteral("State")).toString();
        if (state == QLatin1String("disconnected"))
            continue;
        list.append(QVariantMap {
            { QStringLiteral("path"), it.key() },
            { QStringLiteral("number"), p.value(QStringLiteral("LineIdentification")).toString() },
            { QStringLiteral("name"), p.value(QStringLiteral("Name")).toString() },
            { QStringLiteral("state"), state },
        });
    }
    if (list == m_callList)
        return;
    m_callList = list;
    emit callsChanged();
}

QString Ofono::phoneName() const
{
    return m_modems.value(m_modem).value(QStringLiteral("Name")).toString();
}

QString Ofono::address() const
{
    return m_modems.value(m_modem).value(QStringLiteral("Serial")).toString();
}

QString Ofono::callIn(const QStringList &states) const
{
    for (auto it = m_calls.cbegin(); it != m_calls.cend(); ++it)
        if (states.contains(it.value().value(QStringLiteral("State")).toString()))
            return it.key();
    return {};
}

void Ofono::dial(const QString &number)
{
    if (m_modem.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(Service, m_modem, CallManagerIface, QStringLiteral("Dial"));
    call << number << QStringLiteral("default");
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 30000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, number](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError()) {
            qCWarning(lcCall) << "dial failed:" << w->error().name() << w->error().message();
            emit dialFailed(number, w->error().message());
        }
    });
}

void Ofono::answer()
{
    const QString path = callIn({ QStringLiteral("incoming"), QStringLiteral("waiting") });
    if (!path.isEmpty())
        logErrors(m_bus.asyncCall(QDBusMessage::createMethodCall(Service, path, CallIface, QStringLiteral("Answer"))),
                  this, QStringLiteral("Answer"));
}

void Ofono::hangup()
{
    if (m_modem.isEmpty())
        return;
    // Releases every call, and declines one that is still ringing
    logErrors(m_bus.asyncCall(QDBusMessage::createMethodCall(Service, m_modem, CallManagerIface, QStringLiteral("HangupAll"))),
              this, QStringLiteral("HangupAll"));
}

void Ofono::setMuted(bool on)
{
    if (m_modem.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(Service, m_modem, VolumeIface, QStringLiteral("SetProperty"));
    call << QStringLiteral("Muted") << QVariant::fromValue(QDBusVariant(on));
    logErrors(m_bus.asyncCall(call), this, QStringLiteral("Mute"));
}
