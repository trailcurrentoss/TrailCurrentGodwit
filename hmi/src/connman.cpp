#include "connman.h"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(lcNet, "godwit.net")

namespace {

const QString Service = QStringLiteral("net.connman");
const QString ManagerIface = QStringLiteral("net.connman.Manager");
const QString TechnologyIface = QStringLiteral("net.connman.Technology");
const QString ServiceIface = QStringLiteral("net.connman.Service");
const QString AgentPath = QStringLiteral("/org/trailcurrent/godwit/agent");

const QString BusName = QStringLiteral("godwit-connman");

// A private connection rather than QDBusConnection::systemBus(): that one is
// shared, and once it has failed it never connects again. The HMI starts
// before the system bus does, so the first attempts are expected to fail.
QDBusConnection openBus()
{
    QDBusConnection::disconnectFromBus(BusName);
    return QDBusConnection::connectToBus(
        qEnvironmentVariable("GODWIT_CONNMAN_BUS") == QLatin1String("session")
            ? QDBusConnection::SessionBus : QDBusConnection::SystemBus,
        BusName);
}

// a(oa{sv}), the shape of GetServices and GetTechnologies
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

QString securityLabel(const QStringList &sec)
{
    if (sec.contains(QLatin1String("sae")))
        return QStringLiteral("WPA3");
    if (sec.contains(QLatin1String("psk")))
        return QStringLiteral("WPA2");
    if (sec.contains(QLatin1String("ieee8021x")))
        return QStringLiteral("Enterprise");
    if (sec.contains(QLatin1String("wep")))
        return QStringLiteral("WEP");
    return QStringLiteral("Open");
}

// Fire-and-forget calls still report failures to the journal
void logErrors(const QDBusPendingCall &pending, QObject *ctx, const QString &what)
{
    auto *w = new QDBusPendingCallWatcher(pending, ctx);
    QObject::connect(w, &QDBusPendingCallWatcher::finished, ctx, [w, what] {
        w->deleteLater();
        if (w->isError())
            qCWarning(lcNet) << what << "failed:" << w->error().name() << w->error().message();
    });
}

bool isConnectedState(const QString &state)
{
    return state == QLatin1String("ready") || state == QLatin1String("online");
}

} // namespace

ConnmanAgent::ConnmanAgent(QObject *object, Connman *owner)
    : QDBusAbstractAdaptor(object)
    , m_owner(owner)
{
}

void ConnmanAgent::ReportError(const QDBusObjectPath &service, const QString &error)
{
    Q_UNUSED(service);
    m_owner->agentError(error);
}

QVariantMap ConnmanAgent::RequestInput(const QDBusObjectPath &service, const QVariantMap &fields)
{
    Q_UNUSED(service);
    QVariantMap answer;
    if (fields.contains(QStringLiteral("Passphrase"))) {
        const QString pass = m_owner->takePassphrase();
        if (!pass.isEmpty())
            answer.insert(QStringLiteral("Passphrase"), pass);
    }
    return answer;
}

Connman::Connman(QObject *parent)
    : QObject(parent)
    , m_bus(openBus())
{
    // Only the head unit drives the real Wi-Fi. A PC running the simulator
    // keeps the demo data unless GODWIT_CONNMAN_BUS says which bus to use.
    if (qEnvironmentVariableIsEmpty("GODWIT_CONNMAN_BUS") && QGuiApplication::platformName() != QLatin1String("eglfs"))
        return;
    start();
}

void Connman::start()
{
    if (!m_bus.isConnected()) {
        // No bus yet: keep trying for a minute, which covers boot. A desktop
        // without a system bus gives up and stays on the demo networks.
        if (++m_busTries < 120)
            QTimer::singleShot(500, this, [this] { m_bus = openBus(); start(); });
        return;
    }
    qCInfo(lcNet) << "connected to the bus";

    auto *watcher = new QDBusServiceWatcher(Service, m_bus,
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] { attach(); });
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] { detach(); });

    auto *agentObject = new QObject(this);
    m_agent = new ConnmanAgent(agentObject, this);
    m_bus.registerObject(AgentPath, agentObject);

    m_bus.connect(Service, QStringLiteral("/"), ManagerIface, QStringLiteral("ServicesChanged"),
                  this, SLOT(onServicesChanged(QDBusMessage)));
    m_bus.connect(Service, QString(), ServiceIface, QStringLiteral("PropertyChanged"),
                  this, SLOT(onServiceChanged(QDBusMessage)));

    if (m_bus.interface()->isServiceRegistered(Service))
        attach();
}

void Connman::attach()
{
    m_available = true;
    emit availableChanged();

    QDBusMessage reg = QDBusMessage::createMethodCall(Service, QStringLiteral("/"), ManagerIface, QStringLiteral("RegisterAgent"));
    reg << QVariant::fromValue(QDBusObjectPath(AgentPath));
    logErrors(m_bus.asyncCall(reg), this, QStringLiteral("RegisterAgent"));

    refreshTechnology();
    refreshServices();
}

void Connman::detach()
{
    m_available = false;
    m_powered = false;
    m_services.clear();
    m_techPath.clear();
    setConnecting(QString());
    rebuild();
    emit wifiChanged();
    emit availableChanged();
}

void Connman::refreshTechnology()
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, QStringLiteral("/"), ManagerIface, QStringLiteral("GetTechnologies"));
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            qCWarning(lcNet) << "GetTechnologies failed:" << reply.errorName() << reply.errorMessage();
            return;
        }
        for (const auto &[path, props] : readObjects(reply.arguments().constFirst())) {
            if (props.value(QStringLiteral("Type")).toString() != QLatin1String("wifi"))
                continue;
            if (path != m_techPath) {
                if (!m_techPath.isEmpty())
                    m_bus.disconnect(Service, m_techPath, TechnologyIface, QStringLiteral("PropertyChanged"),
                                     this, SLOT(onTechnologyChanged(QString, QDBusVariant)));
                m_techPath = path;
                m_bus.connect(Service, m_techPath, TechnologyIface, QStringLiteral("PropertyChanged"),
                              this, SLOT(onTechnologyChanged(QString, QDBusVariant)));
            }
            m_powered = props.value(QStringLiteral("Powered")).toBool();
            qCInfo(lcNet) << "Wi-Fi technology" << m_techPath << "powered" << m_powered;
            emit wifiChanged();
        }
    });
}

void Connman::refreshServices()
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, QStringLiteral("/"), ManagerIface, QStringLiteral("GetServices"));
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            qCWarning(lcNet) << "GetServices failed:" << reply.errorName() << reply.errorMessage();
            return;
        }
        m_services.clear();
        for (const auto &entry : readObjects(reply.arguments().constFirst()))
            if (entry.second.value(QStringLiteral("Type")).toString() == QLatin1String("wifi"))
                m_services.append(entry);
        rebuild();
    });
}

void Connman::onServicesChanged(const QDBusMessage &msg)
{
    Q_UNUSED(msg);
    // The signal carries only what changed; re-reading the list is simpler and cheap
    refreshServices();
}

void Connman::onTechnologyChanged(const QString &name, const QDBusVariant &value)
{
    if (name == QLatin1String("Powered")) {
        m_powered = value.variant().toBool();
        emit wifiChanged();
        if (m_powered)
            refreshServices();
    }
}

void Connman::onServiceChanged(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (args.size() < 2)
        return;
    const QString name = args.at(0).toString();
    const QVariant value = args.at(1).value<QDBusVariant>().variant();
    for (auto &entry : m_services) {
        if (entry.first != msg.path())
            continue;
        entry.second.insert(name, value);
        if (name == QLatin1String("State")) {
            const QString ssid = entry.second.value(QStringLiteral("Name")).toString();
            const QString state = value.toString();
            if (ssid == m_connectingSsid && isConnectedState(state)) {
                setConnecting(QString());
                emit connected(ssid);
            } else if (ssid == m_connectingSsid && state == QLatin1String("failure")) {
                setConnecting(QString());
                emit failed(ssid, entry.second.value(QStringLiteral("Error")).toString());
            }
        }
        rebuild();
        return;
    }
}

void Connman::rebuild()
{
    QVariantList list;
    for (const auto &[path, p] : std::as_const(m_services)) {
        const QString ssid = p.value(QStringLiteral("Name")).toString();
        if (ssid.isEmpty())
            continue; // hidden network
        const QString state = p.value(QStringLiteral("State")).toString();
        list.append(QVariantMap {
            { QStringLiteral("ssid"), ssid },
            { QStringLiteral("sig"), p.value(QStringLiteral("Strength")).toInt() / 100.0 },
            { QStringLiteral("sec"), securityLabel(p.value(QStringLiteral("Security")).toStringList()) },
            { QStringLiteral("saved"), p.value(QStringLiteral("Favorite")).toBool() },
            { QStringLiteral("connected"), isConnectedState(state) },
            { QStringLiteral("autoConnect"), p.value(QStringLiteral("AutoConnect")).toBool() },
            { QStringLiteral("state"), state },
            { QStringLiteral("path"), path },
        });
    }
    m_networks = list;
    emit networksChanged();
}

QString Connman::connectedSsid() const
{
    for (const QVariant &n : m_networks) {
        const QVariantMap m = n.toMap();
        if (m.value(QStringLiteral("connected")).toBool())
            return m.value(QStringLiteral("ssid")).toString();
    }
    return {};
}

QString Connman::pathFor(const QString &ssid) const
{
    for (const auto &[path, p] : m_services)
        if (p.value(QStringLiteral("Name")).toString() == ssid)
            return path;
    return {};
}

void Connman::setConnecting(const QString &ssid)
{
    if (m_connectingSsid == ssid)
        return;
    m_connectingSsid = ssid;
    emit connectingChanged();
}

void Connman::setWifiPowered(bool on)
{
    if (m_techPath.isEmpty()) {
        qCWarning(lcNet) << "Wi-Fi power change ignored: ConnMan has no Wi-Fi technology";
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(Service, m_techPath, TechnologyIface, QStringLiteral("SetProperty"));
    call << QStringLiteral("Powered") << QVariant::fromValue(QDBusVariant(on));
    logErrors(m_bus.asyncCall(call), this, QStringLiteral("Wi-Fi power"));
}

void Connman::scan()
{
    if (m_techPath.isEmpty() || m_scanning)
        return;
    m_scanning = true;
    emit scanningChanged();
    QDBusMessage call = QDBusMessage::createMethodCall(Service, m_techPath, TechnologyIface, QStringLiteral("Scan"));
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 30000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        m_scanning = false;
        emit scanningChanged();
        refreshServices();
    });
}

void Connman::connectTo(const QString &ssid, const QString &passphrase)
{
    const QString path = pathFor(ssid);
    if (path.isEmpty()) {
        emit failed(ssid, QStringLiteral("not-found"));
        return;
    }
    m_pendingPassphrase = passphrase;
    setConnecting(ssid);
    QDBusMessage call = QDBusMessage::createMethodCall(Service, path, ServiceIface, QStringLiteral("Connect"));
    // Connect returns once the connection is up or has failed
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 120000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, ssid](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        m_pendingPassphrase.clear();
        const QDBusMessage reply = w->reply();
        if (m_connectingSsid != ssid)
            return; // already settled by a state change or an agent error
        setConnecting(QString());
        if (reply.type() == QDBusMessage::ErrorMessage
            && reply.errorName() != QLatin1String("net.connman.Error.AlreadyConnected"))
            emit failed(ssid, reply.errorMessage());
        else
            emit connected(ssid);
    });
}

void Connman::disconnectFrom(const QString &ssid)
{
    const QString path = pathFor(ssid);
    if (!path.isEmpty())
        m_bus.asyncCall(QDBusMessage::createMethodCall(Service, path, ServiceIface, QStringLiteral("Disconnect")));
}

void Connman::forget(const QString &ssid)
{
    const QString path = pathFor(ssid);
    if (!path.isEmpty())
        m_bus.asyncCall(QDBusMessage::createMethodCall(Service, path, ServiceIface, QStringLiteral("Remove")));
}

void Connman::setAutoConnect(const QString &ssid, bool on)
{
    const QString path = pathFor(ssid);
    if (path.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(Service, path, ServiceIface, QStringLiteral("SetProperty"));
    call << QStringLiteral("AutoConnect") << QVariant::fromValue(QDBusVariant(on));
    m_bus.asyncCall(call);
}

QString Connman::takePassphrase()
{
    return std::exchange(m_pendingPassphrase, QString());
}

void Connman::agentError(const QString &error)
{
    const QString ssid = m_connectingSsid;
    setConnecting(QString());
    m_pendingPassphrase.clear();
    if (!ssid.isEmpty())
        emit failed(ssid, error);
}
