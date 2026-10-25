#include "bluez.h"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QDateTime>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QTimer>

Q_LOGGING_CATEGORY(lcBt, "godwit.bt")

namespace {

const QString Service = QStringLiteral("org.bluez");
const QString AdapterIface = QStringLiteral("org.bluez.Adapter1");
const QString DeviceIface = QStringLiteral("org.bluez.Device1");
const QString PlayerIface = QStringLiteral("org.bluez.MediaPlayer1");
const QString TransportIface = QStringLiteral("org.bluez.MediaTransport1");
// The phone's ends of hands-free and music
const QString HfpAgUuid = QStringLiteral("0000111f-0000-1000-8000-00805f9b34fb");
const QString A2dpSourceUuid = QStringLiteral("0000110a-0000-1000-8000-00805f9b34fb");
// How long a connected phone gets to start music itself
constexpr int MediaWaitMs = 10000;
const QString AgentManagerIface = QStringLiteral("org.bluez.AgentManager1");
const QString PropsIface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString ObjectManagerIface = QStringLiteral("org.freedesktop.DBus.ObjectManager");
const QString AgentPath = QStringLiteral("/org/trailcurrent/godwit/btagent");
const QString BusName = QStringLiteral("godwit-bluez");
// What phones list the head unit as
const QString VisibleName = QStringLiteral("Godwit");

using Interfaces = QMap<QString, QVariantMap>;

// A private connection, retried until the system bus is up (see connman.cpp)
QDBusConnection openBus()
{
    QDBusConnection::disconnectFromBus(BusName);
    return QDBusConnection::connectToBus(
        qEnvironmentVariable("GODWIT_BLUEZ_BUS") == QLatin1String("session")
            ? QDBusConnection::SessionBus : QDBusConnection::SystemBus,
        BusName);
}

Interfaces readInterfaces(const QVariant &v)
{
    Interfaces out;
    v.value<QDBusArgument>() >> out;
    return out;
}

// "123456" -> "123 456", as phones show it
// Nested dictionaries (a player's Track) arrive still marshalled
QVariant plain(const QVariant &v)
{
    if (v.canConvert<QDBusArgument>()) {
        QVariantMap m;
        v.value<QDBusArgument>() >> m;
        return m;
    }
    return v;
}

QString formatCode(uint passkey)
{
    const QString s = QStringLiteral("%1").arg(passkey, 6, 10, QLatin1Char('0'));
    return s.left(3) + QLatin1Char(' ') + s.mid(3);
}

QString modelFor(const QVariantMap &p)
{
    const QString icon = p.value(QStringLiteral("Icon")).toString();
    if (icon == QLatin1String("phone"))
        return QStringLiteral("Phone");
    if (icon.startsWith(QLatin1String("audio")))
        return QStringLiteral("Audio device");
    if (icon == QLatin1String("computer"))
        return QStringLiteral("Computer");
    return QStringLiteral("Bluetooth device");
}

void logErrors(const QDBusPendingCall &pending, QObject *ctx, const QString &what)
{
    auto *w = new QDBusPendingCallWatcher(pending, ctx);
    QObject::connect(w, &QDBusPendingCallWatcher::finished, ctx, [w, what] {
        w->deleteLater();
        if (w->isError())
            qCWarning(lcBt) << what << "failed:" << w->error().name() << w->error().message();
    });
}

} // namespace

// ------------------------------------------------------------------ agent

BluezAgent::BluezAgent(Bluez *owner)
    : QObject(owner)
    , m_owner(owner)
{
}

QString BluezAgent::RequestPinCode(const QDBusObjectPath &device)
{
    // Older devices without Secure Simple Pairing
    if (!m_owner->pairingMode()) {
        sendErrorReply(QStringLiteral("org.bluez.Error.Rejected"), QStringLiteral("Not pairing"));
        return {};
    }
    const QString pin = QStringLiteral("%1").arg(QRandomGenerator::global()->bounded(10000), 4, 10, QLatin1Char('0'));
    m_owner->codeShown(device.path(), pin);
    return pin;
}

void BluezAgent::DisplayPinCode(const QDBusObjectPath &device, const QString &pincode)
{
    m_owner->codeShown(device.path(), pincode);
}

uint BluezAgent::RequestPasskey(const QDBusObjectPath &device)
{
    if (!m_owner->pairingMode()) {
        sendErrorReply(QStringLiteral("org.bluez.Error.Rejected"), QStringLiteral("Not pairing"));
        return 0;
    }
    const uint passkey = QRandomGenerator::global()->bounded(1000000);
    m_owner->codeShown(device.path(), formatCode(passkey));
    return passkey;
}

void BluezAgent::DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered)
{
    Q_UNUSED(entered);
    m_owner->codeShown(device.path(), formatCode(passkey));
}

void BluezAgent::RequestConfirmation(const QDBusObjectPath &device, uint passkey)
{
    // Answered later, when the driver accepts or leaves the pairing screen
    setDelayedReply(true);
    m_owner->confirmRequested(message(), device.path(), formatCode(passkey));
}

void BluezAgent::RequestAuthorization(const QDBusObjectPath &device)
{
    // Pairing with no code to compare; still the driver's decision
    setDelayedReply(true);
    m_owner->confirmRequested(message(), device.path(), QString());
}

void BluezAgent::AuthorizeService(const QDBusObjectPath &device, const QString &uuid)
{
    if (m_owner->isPaired(device.path()) || m_owner->pairingMode())
        return;
    qCInfo(lcBt) << "refused service" << uuid << "for unpaired" << device.path();
    sendErrorReply(QStringLiteral("org.bluez.Error.Rejected"), QStringLiteral("Not paired"));
}

void BluezAgent::Cancel()
{
    m_owner->agentCancelled();
}

// ------------------------------------------------------------------ Bluez

Bluez::Bluez(QObject *parent)
    : QObject(parent)
    , m_bus(openBus())
{
    // Only the head unit drives the real Bluetooth. A PC running the simulator
    // keeps the demo data unless GODWIT_BLUEZ_BUS says which bus to use.
    if (qEnvironmentVariableIsEmpty("GODWIT_BLUEZ_BUS") && QGuiApplication::platformName() != QLatin1String("eglfs"))
        return;
    m_real = true;
    start();
}

void Bluez::start()
{
    if (!m_bus.isConnected()) {
        if (++m_busTries < 120)
            QTimer::singleShot(500, this, [this] { m_bus = openBus(); start(); });
        return;
    }
    qCInfo(lcBt) << "connected to the bus";

    auto *watcher = new QDBusServiceWatcher(Service, m_bus,
        QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, [this] { attach(); });
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] { detach(); });

    // Reconnect to paired phones, as a car does, when one is in range again
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &Bluez::reconnect);
    timer->start(60000);

    m_agent = new BluezAgent(this);
    m_bus.registerObject(AgentPath, m_agent, QDBusConnection::ExportAllSlots);

    m_bus.connect(Service, QStringLiteral("/"), ObjectManagerIface, QStringLiteral("InterfacesAdded"),
                  this, SLOT(onInterfacesAdded(QDBusMessage)));
    m_bus.connect(Service, QStringLiteral("/"), ObjectManagerIface, QStringLiteral("InterfacesRemoved"),
                  this, SLOT(onInterfacesRemoved(QDBusMessage)));
    m_bus.connect(Service, QString(), PropsIface, QStringLiteral("PropertiesChanged"),
                  this, SLOT(onPropertiesChanged(QDBusMessage)));

    if (m_bus.interface()->isServiceRegistered(Service))
        attach();
}

void Bluez::attach()
{
    QDBusMessage reg = QDBusMessage::createMethodCall(Service, QStringLiteral("/org/bluez"), AgentManagerIface, QStringLiteral("RegisterAgent"));
    // A screen and a yes/no answer: phones show a code and ask to compare it
    reg << QVariant::fromValue(QDBusObjectPath(AgentPath)) << QStringLiteral("DisplayYesNo");
    logErrors(m_bus.asyncCall(reg), this, QStringLiteral("RegisterAgent"));
    QDBusMessage def = QDBusMessage::createMethodCall(Service, QStringLiteral("/org/bluez"), AgentManagerIface, QStringLiteral("RequestDefaultAgent"));
    def << QVariant::fromValue(QDBusObjectPath(AgentPath));
    logErrors(m_bus.asyncCall(def), this, QStringLiteral("RequestDefaultAgent"));

    refresh();
}

void Bluez::detach()
{
    m_pending = QDBusMessage();
    m_pairingMode = false;
    m_adapter.clear();
    m_adapterProps.clear();
    m_devices.clear();
    m_transports.clear();
    m_connecting.clear();
    setPairing({});
    setPlayer(QString(), {});
    rebuild();
    if (m_available) {
        m_available = false;
        emit availableChanged();
    }
    emit poweredChanged();
    emit nameChanged();
}

void Bluez::refresh()
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, QStringLiteral("/"), ObjectManagerIface, QStringLiteral("GetManagedObjects"));
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            qCWarning(lcBt) << "GetManagedObjects failed:" << reply.errorName() << reply.errorMessage();
            return;
        }
        m_devices.clear();
        m_transports.clear();
        const QDBusArgument arg = reply.arguments().constFirst().value<QDBusArgument>();
        arg.beginMap();
        while (!arg.atEnd()) {
            QDBusObjectPath path;
            Interfaces ifs;
            arg.beginMapEntry();
            arg >> path >> ifs;
            arg.endMapEntry();
            if (ifs.contains(AdapterIface) && m_adapter.isEmpty()) {
                m_adapter = path.path();
                m_adapterProps = ifs.value(AdapterIface);
            }
            if (ifs.contains(DeviceIface))
                m_devices.insert(path.path(), ifs.value(DeviceIface));
            if (ifs.contains(PlayerIface))
                setPlayer(path.path(), ifs.value(PlayerIface));
            if (ifs.contains(TransportIface))
                m_transports.insert(path.path());
        }
        arg.endMap();

        const bool had = m_available;
        m_available = !m_adapter.isEmpty();
        if (m_available) {
            qCInfo(lcBt) << "adapter" << m_adapter << "powered" << powered() << "devices" << m_devices.size();
            if (m_adapterProps.value(QStringLiteral("Alias")).toString() != VisibleName)
                setAdapterProperty(QStringLiteral("Alias"), VisibleName);
        } else {
            qCWarning(lcBt) << "no Bluetooth adapter";
        }
        rebuild();
        if (had != m_available)
            emit availableChanged();
        emit poweredChanged();
        emit nameChanged();
        reconnect();
    });
}

void Bluez::setPlayer(const QString &path, const QVariantMap &props)
{
    m_player = path;
    m_playerProps.clear();
    for (auto it = props.cbegin(); it != props.cend(); ++it)
        m_playerProps.insert(it.key(), plain(it.value()));
    if (!path.isEmpty())
        qCInfo(lcBt) << "media player" << m_playerProps.value(QStringLiteral("Name")).toString();
    rebuildPlayer();
}

void Bluez::rebuildPlayer()
{
    QVariantMap info;
    if (!m_player.isEmpty()) {
        const QVariantMap track = m_playerProps.value(QStringLiteral("Track")).toMap();
        info = {
            { QStringLiteral("name"), m_playerProps.value(QStringLiteral("Name")) },
            { QStringLiteral("status"), m_playerProps.value(QStringLiteral("Status")) },
            { QStringLiteral("title"), track.value(QStringLiteral("Title")) },
            { QStringLiteral("artist"), track.value(QStringLiteral("Artist")) },
            { QStringLiteral("album"), track.value(QStringLiteral("Album")) },
            { QStringLiteral("duration"), track.value(QStringLiteral("Duration")).toLongLong() },
            { QStringLiteral("position"), m_playerProps.value(QStringLiteral("Position")).toLongLong() },
            { QStringLiteral("at"), QDateTime::currentMSecsSinceEpoch() },
        };
    }
    m_playerInfo = info;
    emit playerChanged();
}

void Bluez::playerCall(const QString &method)
{
    if (m_player.isEmpty())
        return;
    logErrors(m_bus.asyncCall(QDBusMessage::createMethodCall(Service, m_player, PlayerIface, method)),
              this, QStringLiteral("Player ") + method);
}

void Bluez::reconnect()
{
    if (!powered() || m_pairingMode)
        return;
    for (auto it = m_devices.cbegin(); it != m_devices.cend(); ++it) {
        const QVariantMap &p = it.value();
        if (!p.value(QStringLiteral("Paired")).toBool() || p.value(QStringLiteral("Connected")).toBool())
            continue;
        if (m_connecting.contains(it.key()) || m_declined.contains(it.key()))
            continue;
        pagePhone(it.key());
    }
}

// Ask a phone back in range for hands-free only. Once connected the phone
// starts music itself, and opening A2DP from both ends at once breaks the
// stream; ensureMedia() covers a phone that does not.
void Bluez::pagePhone(const QString &path)
{
    m_connecting.insert(path);
    QDBusMessage call = QDBusMessage::createMethodCall(Service, path, DeviceIface, QStringLiteral("ConnectProfile"));
    call << HfpAgUuid;
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 30000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        m_connecting.remove(path);
        // Out of range just times out. A phone that answers and refuses was
        // disconnected on purpose: leave it until it connects again.
        if (w->isError() && w->error().message().contains(QLatin1String("refused"), Qt::CaseInsensitive)) {
            qCInfo(lcBt) << deviceName(path) << "declined; not asking again until it connects";
            m_declined.insert(path);
        }
    });
}

// Open music from our end only if the phone has not done it already
void Bluez::ensureMedia(const QString &path)
{
    if (!m_devices.value(path).value(QStringLiteral("Connected")).toBool())
        return;
    for (const QString &t : std::as_const(m_transports))
        if (t.startsWith(path + QLatin1Char('/')))
            return;
    qCInfo(lcBt) << "no music from" << deviceName(path) << "- opening it from here";
    QDBusMessage call = QDBusMessage::createMethodCall(Service, path, DeviceIface, QStringLiteral("ConnectProfile"));
    call << A2dpSourceUuid;
    logErrors(m_bus.asyncCall(call, 30000), this, QStringLiteral("ConnectProfile A2DP"));
}

void Bluez::onInterfacesAdded(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (args.size() < 2)
        return;
    const QString path = args.at(0).value<QDBusObjectPath>().path();
    const Interfaces ifs = readInterfaces(args.at(1));
    if (ifs.contains(AdapterIface) && m_adapter.isEmpty()) {
        refresh();
        return;
    }
    if (ifs.contains(DeviceIface)) {
        m_devices.insert(path, ifs.value(DeviceIface));
        rebuild();
    }
    if (ifs.contains(PlayerIface))
        setPlayer(path, ifs.value(PlayerIface));
    if (ifs.contains(TransportIface))
        m_transports.insert(path);
}

void Bluez::onInterfacesRemoved(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (args.size() < 2)
        return;
    const QString path = args.at(0).value<QDBusObjectPath>().path();
    const QStringList ifs = args.at(1).toStringList();
    if (ifs.contains(AdapterIface) && path == m_adapter) {
        detach();
        refresh(); // another adapter, if there is one
        return;
    }
    if (ifs.contains(DeviceIface) && m_devices.remove(path))
        rebuild();
    if (ifs.contains(PlayerIface) && path == m_player)
        setPlayer(QString(), {});
    if (ifs.contains(TransportIface))
        m_transports.remove(path);
}

void Bluez::onPropertiesChanged(const QDBusMessage &msg)
{
    const QList<QVariant> args = msg.arguments();
    if (args.size() < 2)
        return;
    const QString iface = args.at(0).toString();
    QVariantMap changed;
    args.at(1).value<QDBusArgument>() >> changed;
    const QString path = msg.path();

    if (iface == AdapterIface && path == m_adapter) {
        for (auto it = changed.cbegin(); it != changed.cend(); ++it)
            m_adapterProps.insert(it.key(), it.value());
        if (changed.contains(QStringLiteral("Powered"))) {
            emit poweredChanged();
            // Pairing asked before the radio was on: make it visible now
            if (powered() && m_pairingMode) {
                setAdapterProperty(QStringLiteral("Pairable"), true);
                setAdapterProperty(QStringLiteral("Discoverable"), true);
            }
            reconnect();
        }
        if (changed.contains(QStringLiteral("Alias")))
            emit nameChanged();
        return;
    }

    if (iface == PlayerIface && path == m_player) {
        for (auto it = changed.cbegin(); it != changed.cend(); ++it)
            m_playerProps.insert(it.key(), plain(it.value()));
        rebuildPlayer();
        return;
    }
    if (iface != DeviceIface || !m_devices.contains(path))
        return;
    QVariantMap &p = m_devices[path];
    const bool wasPaired = p.value(QStringLiteral("Paired")).toBool();
    const bool wasConnected = p.value(QStringLiteral("Connected")).toBool();
    for (auto it = changed.cbegin(); it != changed.cend(); ++it)
        p.insert(it.key(), it.value());
    const bool nowPaired = p.value(QStringLiteral("Paired")).toBool();

    if (!wasConnected && p.value(QStringLiteral("Connected")).toBool()) {
        m_declined.remove(path);
        QTimer::singleShot(MediaWaitMs, this, [this, path] { ensureMedia(path); });
    }

    if (!wasPaired && nowPaired) {
        const QString name = deviceName(path);
        qCInfo(lcBt) << "paired with" << name;
        // Trusted: the phone can reconnect later without asking again
        setDeviceProperty(path, QStringLiteral("Trusted"), true);
        if (m_pairingMode) {
            stopPairing();
            emit paired(name);
        }
    }
    rebuild();
}

void Bluez::rebuild()
{
    QVariantList list;
    for (auto it = m_devices.cbegin(); it != m_devices.cend(); ++it) {
        const QVariantMap &p = it.value();
        if (!p.value(QStringLiteral("Paired")).toBool() && !p.value(QStringLiteral("Bonded")).toBool())
            continue;
        list.append(QVariantMap {
            { QStringLiteral("name"), deviceName(it.key()) },
            { QStringLiteral("address"), p.value(QStringLiteral("Address")).toString() },
            { QStringLiteral("connected"), p.value(QStringLiteral("Connected")).toBool() },
            { QStringLiteral("model"), modelFor(p) },
        });
    }
    // Connected first, then by name
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap(), y = b.toMap();
        const bool cx = x.value(QStringLiteral("connected")).toBool(), cy = y.value(QStringLiteral("connected")).toBool();
        if (cx != cy)
            return cx;
        return x.value(QStringLiteral("name")).toString().localeAwareCompare(y.value(QStringLiteral("name")).toString()) < 0;
    });
    if (list == m_phones)
        return;
    m_phones = list;
    emit phonesChanged();
}

bool Bluez::powered() const
{
    return m_adapterProps.value(QStringLiteral("Powered")).toBool();
}

QString Bluez::name() const
{
    const QString alias = m_adapterProps.value(QStringLiteral("Alias")).toString();
    return alias.isEmpty() ? VisibleName : alias;
}

bool Bluez::isPaired(const QString &path) const
{
    return m_devices.value(path).value(QStringLiteral("Paired")).toBool();
}

QString Bluez::deviceName(const QString &path) const
{
    const QVariantMap p = m_devices.value(path);
    for (const char *key : { "Alias", "Name", "Address" }) {
        const QString v = p.value(QLatin1String(key)).toString();
        if (!v.isEmpty())
            return v;
    }
    // Not seen yet: the path ends in dev_AA_BB_CC_DD_EE_FF
    return path.section(QLatin1Char('/'), -1).mid(4).replace(QLatin1Char('_'), QLatin1Char(':'));
}

QString Bluez::pathFor(const QString &address) const
{
    for (auto it = m_devices.cbegin(); it != m_devices.cend(); ++it)
        if (it.value().value(QStringLiteral("Address")).toString() == address)
            return it.key();
    return {};
}

void Bluez::setAdapterProperty(const QString &name, const QVariant &value)
{
    if (m_adapter.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(Service, m_adapter, PropsIface, QStringLiteral("Set"));
    call << AdapterIface << name << QVariant::fromValue(QDBusVariant(value));
    logErrors(m_bus.asyncCall(call), this, QStringLiteral("Adapter ") + name);
}

void Bluez::setDeviceProperty(const QString &path, const QString &name, const QVariant &value)
{
    QDBusMessage call = QDBusMessage::createMethodCall(Service, path, PropsIface, QStringLiteral("Set"));
    call << DeviceIface << name << QVariant::fromValue(QDBusVariant(value));
    logErrors(m_bus.asyncCall(call), this, QStringLiteral("Device ") + name);
}

void Bluez::setPairing(const QVariantMap &p)
{
    if (p == m_pairing)
        return;
    m_pairing = p;
    emit pairingChanged();
}

void Bluez::setPowered(bool on)
{
    if (!m_available)
        return;
    // ConnMan owns the radio's rfkill switch, so ask it first; straight to
    // BlueZ when ConnMan is not managing Bluetooth
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("net.connman"), QStringLiteral("/net/connman/technology/bluetooth"),
                                                       QStringLiteral("net.connman.Technology"), QStringLiteral("SetProperty"));
    call << QStringLiteral("Powered") << QVariant::fromValue(QDBusVariant(on));
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, on](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (!w->isError())
            return;
        qCInfo(lcBt) << "ConnMan did not switch Bluetooth (" << w->error().name() << "), asking BlueZ";
        setAdapterProperty(QStringLiteral("Powered"), on);
    });
}

void Bluez::startPairing()
{
    if (!m_available)
        return;
    qCInfo(lcBt) << "pairing screen open: visible to phones";
    m_pairingMode = true;
    setPairing({ { QStringLiteral("stage"), QStringLiteral("search") } });
    if (!powered()) {
        setPowered(true); // made visible once powered
        return;
    }
    // Visible until the pairing screen closes
    setAdapterProperty(QStringLiteral("DiscoverableTimeout"), QVariant::fromValue(uint(0)));
    setAdapterProperty(QStringLiteral("Pairable"), true);
    setAdapterProperty(QStringLiteral("Discoverable"), true);
}

void Bluez::stopPairing()
{
    if (!m_pairingMode)
        return;
    qCInfo(lcBt) << "pairing screen closed";
    replyToPending(false);
    m_pairingMode = false;
    setAdapterProperty(QStringLiteral("Discoverable"), false);
    setAdapterProperty(QStringLiteral("Pairable"), false);
    setPairing({});
}

void Bluez::acceptPairing()
{
    if (m_pending.type() == QDBusMessage::InvalidMessage) {
        qCInfo(lcBt) << "accept ignored: no pairing request waiting";
        return;
    }
    qCInfo(lcBt) << "pairing accepted for" << deviceName(m_pendingDevice);
    const QString device = m_pendingDevice;
    QVariantMap p = m_pairing;
    p.insert(QStringLiteral("stage"), QStringLiteral("busy"));
    setPairing(p);
    replyToPending(true);
    // If the phone gives up after this, BlueZ may not say so; go back to waiting
    QTimer::singleShot(30000, this, [this, device] {
        if (m_pairingMode && m_pairing.value(QStringLiteral("stage")) == QLatin1String("busy") && !isPaired(device)) {
            setPairing({ { QStringLiteral("stage"), QStringLiteral("search") } });
            emit pairingFailed(QStringLiteral("timeout"));
        }
    });
}

void Bluez::replyToPending(bool accept)
{
    if (m_pending.type() == QDBusMessage::InvalidMessage)
        return;
    m_bus.send(accept ? m_pending.createReply()
                      : m_pending.createErrorReply(QStringLiteral("org.bluez.Error.Rejected"), QStringLiteral("Declined")));
    m_pending = QDBusMessage();
}

void Bluez::confirmRequested(const QDBusMessage &msg, const QString &device, const QString &code)
{
    if (!m_pairingMode) {
        // Only while the pairing screen is open
        qCInfo(lcBt) << "refused pairing from" << deviceName(device) << "outside pairing";
        m_bus.send(msg.createErrorReply(QStringLiteral("org.bluez.Error.Rejected"), QStringLiteral("Not pairing")));
        return;
    }
    qCInfo(lcBt) << "pairing request from" << deviceName(device) << (code.isEmpty() ? QStringLiteral("(no code)") : code);
    replyToPending(false); // a newer request replaces an older one
    m_pending = msg;
    m_pendingDevice = device;
    setPairing({
        { QStringLiteral("stage"), QStringLiteral("confirm") },
        { QStringLiteral("phone"), QVariantMap { { QStringLiteral("name"), deviceName(device) } } },
        { QStringLiteral("code"), code },
    });
}

void Bluez::codeShown(const QString &device, const QString &code)
{
    qCInfo(lcBt) << "showing code" << code << "for" << deviceName(device);
    // The phone types this code; nothing to accept here
    setPairing({
        { QStringLiteral("stage"), QStringLiteral("busy") },
        { QStringLiteral("phone"), QVariantMap { { QStringLiteral("name"), deviceName(device) } } },
        { QStringLiteral("code"), code },
    });
}

void Bluez::agentCancelled()
{
    qCInfo(lcBt) << "BlueZ cancelled the pairing request";
    m_pending = QDBusMessage();
    if (!m_pairingMode)
        return;
    setPairing({ { QStringLiteral("stage"), QStringLiteral("search") } });
    emit pairingFailed(QStringLiteral("cancelled"));
}

void Bluez::connectPhone(const QString &address)
{
    const QString path = pathFor(address);
    if (path.isEmpty())
        return;
    const QString name = deviceName(path);
    m_declined.remove(path); // asked for by hand
    QDBusMessage call = QDBusMessage::createMethodCall(Service, path, DeviceIface, QStringLiteral("Connect"));
    auto *w = new QDBusPendingCallWatcher(m_bus.asyncCall(call, 30000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this, name](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        if (w->isError()) {
            qCWarning(lcBt) << "connect" << name << "failed:" << w->error().name() << w->error().message();
            emit connectFailed(name, w->error().message());
        }
    });
}

void Bluez::disconnectPhone(const QString &address)
{
    const QString path = pathFor(address);
    if (!path.isEmpty())
        logErrors(m_bus.asyncCall(QDBusMessage::createMethodCall(Service, path, DeviceIface, QStringLiteral("Disconnect"))),
                  this, QStringLiteral("Disconnect"));
}

void Bluez::forget(const QString &address)
{
    const QString path = pathFor(address);
    if (path.isEmpty() || m_adapter.isEmpty())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(Service, m_adapter, AdapterIface, QStringLiteral("RemoveDevice"));
    call << QVariant::fromValue(QDBusObjectPath(path));
    logErrors(m_bus.asyncCall(call), this, QStringLiteral("RemoveDevice"));
}
