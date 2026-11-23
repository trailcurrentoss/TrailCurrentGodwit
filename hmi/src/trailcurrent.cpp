#include "trailcurrent.h"
#include "canbus.h"
#include "music.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSocketNotifier>

#include <linux/can.h>
#include <linux/can/error.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstring>

Q_DECLARE_LOGGING_CATEGORY(lcCan)

namespace {
const QString DefaultDbc = QStringLiteral("/usr/share/dbc/TrailCurrent.dbc");
constexpr int PollMs = 1000;
constexpr int FlushMs = 50;
// Most modules send every 33 ms to 1 s; Plateau's status every 2 s
constexpr qint64 StaleMs = 3000;
constexpr qint64 SlowStaleMs = 6000;
// No more than one command a channel in this time; turns of the knob between
// them are joined into the last
constexpr qint64 SendGapMs = 40;
// A relay's status is checked this long after its toggle
constexpr qint64 RelayCheckMs = 300;
// A light, or the thermostat, that has not answered by then has not taken it
constexpr qint64 GiveUpMs = 1000;
constexpr qint64 ThermaGiveUpMs = 2500;
// Therma refuses setpoints outside this
constexpr double ThermaMinC = 5.0, ThermaMaxC = 35.0;

qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }

QString readLine(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readLine()).trimmed() : QString();
}

// "t0.3" -> ('t', 0, 3)
bool splitKey(const QString &key, QChar *kind, int *inst, int *ch)
{
    const int dot = key.indexOf(QLatin1Char('.'));
    if (key.size() < 4 || dot < 2)
        return false;
    bool ok1 = false, ok2 = false;
    *kind = key.at(0);
    *inst = key.mid(1, dot - 1).toInt(&ok1);
    *ch = key.mid(dot + 1).toInt(&ok2);
    return ok1 && ok2;
}

double round1(double v) { return std::round(v * 10) / 10; }

const char *chargeName(int cs)
{
    switch (cs) {
    case 0: return "Off";
    case 2: return "Fault";
    case 3: return "Bulk";
    case 4: return "Absorption";
    case 5: return "Float";
    default: return "";
    }
}

QString moduleName(const QString &id)
{
    static const QList<QPair<QString, QString>> kinds = {
        { "torrent", "Torrent" }, { "switchback", "Switchback" }, { "picket", "Picket" },
        { "solstice", "Solstice" }, { "borealis", "Borealis" }, { "plateau", "Plateau" },
        { "reservoir", "Reservoir" }, { "therma", "Therma" },
    };
    for (const auto &k : kinds) {
        if (!id.startsWith(k.first))
            continue;
        const QString n = id.mid(k.first.size());
        return n.isEmpty() ? k.second : k.second + QLatin1Char(' ') + QString::number(n.toInt() + 1);
    }
    return id;
}
}

TrailCurrent::TrailCurrent(CanBus *can, QObject *parent)
    : QObject(parent)
    , m_can(can)
{
    for (auto &t : m_pwm)
        t.fill(-1);
    m_relays.fill(-1);
    m_doors.fill(-1);

    m_overrideIface = qEnvironmentVariable("GODWIT_TRAILCURRENT_IFACE");
    m_live = QGuiApplication::platformName() == QLatin1String("eglfs") || !m_overrideIface.isEmpty();

    loadDbc();
    loadConfig();
    if (m_can)
        connect(m_can, &CanBus::portsChanged, this, &TrailCurrent::poll);

    m_pollTimer.setInterval(PollMs);
    connect(&m_pollTimer, &QTimer::timeout, this, &TrailCurrent::poll);
    m_flushTimer.setInterval(FlushMs);
    connect(&m_flushTimer, &QTimer::timeout, this, [this] {
        flushPending();
        publish();
    });
    m_dirty = Lights | Inputs | Energy | Water | Level | Camper | Air | Modules;
    publish();
    if (ready()) {
        m_pollTimer.start();
        m_flushTimer.start();
        poll();
    }
}

TrailCurrent::~TrailCurrent()
{
    closeSocket();
}

TrailCurrent *TrailCurrent::create(QQmlEngine *qml, QJSEngine *)
{
    return new TrailCurrent(qml->singletonInstance<CanBus *>("Godwit", "CanBus"));
}

// ----------------------------------------------------------------- the DBC

void TrailCurrent::loadDbc()
{
    const QString path = qEnvironmentVariable("GODWIT_TRAILCURRENT_DBC", DefaultDbc);
    QString error;
    if (!m_dbc.load(path, &error)) {
        if (m_live)
            qCWarning(lcCan) << "TrailCurrent bus off: cannot read" << path << error;
        else
            qCInfo(lcCan) << "TrailCurrent bus off: no DBC at" << path << "(set GODWIT_TRAILCURRENT_DBC)";
        return;
    }

    const QStringList outputs = { "Output1Value", "Output2Value", "Output3Value", "Output4Value",
                                  "Output5Value", "Output6Value", "Output7Value", "Output8Value" };
    for (int i = 0; i < 3; ++i) {
        addHandler(QStringLiteral("TorrentStatus%1").arg(i), outputs, [this, i, outputs](const auto &v) {
            for (int ch = 0; ch < 8; ++ch) {
                const int lvl = int(v.value(outputs.at(ch)));
                m_pwm[i][ch] = lvl;
                if (lvl > 0)
                    m_lastOn.insert(QStringLiteral("t%1.%2").arg(i).arg(ch), lvl);
            }
            seen(QStringLiteral("torrent%1").arg(i));
            m_dirty |= Lights;
        });
        addHandler(QStringLiteral("SwitchbackStatus%1").arg(i), { "RelayStateBitmask" }, [this, i](const auto &v) {
            m_relays[i] = int(v.value("RelayStateBitmask"));
            seen(QStringLiteral("switchback%1").arg(i));
            m_dirty |= Lights;
        });
    }
    // Picket 0-7, then the inputs of Switchback 0-2 in the same format
    for (int i = 0; i < 11; ++i) {
        addHandler(QStringLiteral("PicketStatus%1").arg(i), { "DoorStatus1to8", "DoorStatus9to12" }, [this, i](const auto &v) {
            const int mask = int(v.value("DoorStatus1to8")) | (int(v.value("DoorStatus9to12")) & 0x0F) << 8;
            m_doors[i] = mask;
            const int n = i < 8 ? 12 : 8;
            bool changed = false;
            // An input that reads closed has a switch and magnet fitted. One
            // with nothing connected reads open for ever and is left out.
            for (int b = 0; b < n; ++b) {
                if (mask & (1 << b))
                    continue;
                const QString key = i < 8 ? QStringLiteral("p%1.%2").arg(i).arg(b) : QStringLiteral("d%1.%2").arg(i - 8).arg(b);
                QVariantMap c = m_inputsConf.value(key).toMap();
                if (!c.value("used").toBool()) {
                    c["used"] = true;
                    // Picket's are doors and hatches; a Switchback input could be anything
                    if (!c.contains("warn"))
                        c["warn"] = i < 8;
                    m_inputsConf.insert(key, c);
                    changed = true;
                }
            }
            if (changed)
                saveConfig();
            seen(i < 8 ? QStringLiteral("picket%1").arg(i) : QStringLiteral("switchback%1").arg(i - 8));
            m_dirty |= Inputs;
        });
    }
    addHandler("ShuntBasicData1", { "BattVoltageWhole", "BattVoltageDecimal", "IsCurrentNegative", "ShuntCurrentWhole",
                                    "ShuntCurrentDecimal", "BattSocWhole", "BattSocDecimal" }, [this](const auto &v) {
        // Whole and hundredths sent apart, the sign in a byte of its own
        m_energy["volts"] = v.value("BattVoltageWhole") + v.value("BattVoltageDecimal") / 100.0;
        const double a = v.value("ShuntCurrentWhole") + v.value("ShuntCurrentDecimal") / 100.0;
        m_energy["amps"] = round1(v.value("IsCurrentNegative") != 0 ? -a : a);
        m_energy["soc"] = std::clamp(v.value("BattSocWhole") + v.value("BattSocDecimal") / 100.0, 0.0, 100.0);
        seen("solstice");
        m_dirty |= Energy;
    });
    addHandler("ShuntBasicData2", { "IsWattageNegative", "Wattage", "TimeToGo" }, [this](const auto &v) {
        // 0xFF marks discharging here, not 1
        m_energy["watts"] = v.value("IsWattageNegative") != 0 ? -v.value("Wattage") : v.value("Wattage");
        m_energy["ttg"] = v.value("TimeToGo") >= 65535 ? -1 : int(v.value("TimeToGo"));
        seen("solstice");
        m_dirty |= Energy;
    });
    addHandler("SolarMpptData1", { "PanelVoltageWhole", "PanelVoltageDecimal", "SolarWattage", "SolarChargeStatus" }, [this](const auto &v) {
        m_energy["solar"] = true;
        m_energy["panelV"] = round1(v.value("PanelVoltageWhole") + v.value("PanelVoltageDecimal") / 100.0);
        m_energy["solarW"] = int(v.value("SolarWattage"));
        m_energy["charge"] = QString::fromLatin1(chargeName(int(v.value("SolarChargeStatus"))));
        seen("solstice");
        m_dirty |= Energy;
    });
    addHandler("SolarMpptData2", { "IsPanelCurrentNegative", "PanelCurrentWhole", "PanelCurrentDecimal" }, [this](const auto &v) {
        const double a = v.value("PanelCurrentWhole") + v.value("PanelCurrentDecimal") / 100.0;
        m_energy["panelA"] = round1(v.value("IsPanelCurrentNegative") != 0 ? -a : a);
        seen("solstice");
        m_dirty |= Energy;
    });
    addHandler("WaterTankLevels", { "FreshWaterLevel", "GreyWaterLevel", "BlackWaterLevel" }, [this](const auto &v) {
        auto pct = [](double x) { return int(std::clamp(x, 0.0, 100.0)); };
        m_water["levels"] = QVariantList { pct(v.value("FreshWaterLevel")), pct(v.value("GreyWaterLevel")), pct(v.value("BlackWaterLevel")) };
        seen("reservoir");
        m_dirty |= Water;
    });
    addHandler("TiltData", { "PitchScaled", "RollScaled" }, [this](const auto &v) {
        m_level["pitch"] = round1(v.value("PitchScaled"));
        m_level["roll"] = round1(v.value("RollScaled"));
        seen("plateau");
        m_dirty |= Level;
    });
    addHandler("ThermaDesiredTemperature", { "SetpointDeciC", "ThresholdDeciC" }, [this](const auto &v) {
        m_camper["set"] = round1(v.value("SetpointDeciC"));
        m_camper["threshold"] = round1(v.value("ThresholdDeciC"));
        seen("therma");
        m_dirty |= Camper;
    });
    addHandler("ThermaStatus", { "ThermaMode", "CurrentDeciC", "FaultBitmask" }, [this](const auto &v) {
        const int mode = int(v.value("ThermaMode"));
        m_camper["mode"] = mode == 1 ? "heat" : mode == 2 ? "cool" : "idle";
        // 0 until Therma has its first temperature
        const double cur = round1(v.value("CurrentDeciC"));
        m_camper["cur"] = cur == 0 ? QVariant() : QVariant(cur);
        m_camper["fault"] = int(v.value("FaultBitmask"));
        seen("therma");
        m_dirty |= Camper;
    });
    addHandler("EnvironmentSensorData", { "TemperatureCelsius", "HumidityScaled", "CO2Ppm", "VOCIndex" }, [this](const auto &v) {
        // The DBC has this byte unsigned, but Borealis sends an int8
        // (TrailCurrentBorealis main.c), so below freezing it reads 216-255
        int t = int(v.value("TemperatureCelsius"));
        if (t > 127)
            t -= 256;
        m_air["tempC"] = t;
        m_air["rh"] = int(std::round(v.value("HumidityScaled")));
        m_air["co2"] = int(v.value("CO2Ppm"));
        m_air["voc"] = int(v.value("VOCIndex"));
        seen("borealis");
        m_dirty |= Air;
    });
    addHandler("BorealisSafetyData", { "CarbonMonoxidePpm", "LpgRatioScaled", "AlarmFlags" }, [this](const auto &v) {
        // Borealis decides the alarms itself; the readings are for showing only
        const int f = int(v.value("AlarmFlags"));
        m_air["co"] = int(v.value("CarbonMonoxidePpm"));
        m_air["lpg"] = v.value("LpgRatioScaled");
        m_air["coWarn"] = bool(f & 0x01);
        m_air["coAlarm"] = bool(f & 0x02);
        m_air["lpgWarn"] = bool(f & 0x04);
        m_air["lpgAlarm"] = bool(f & 0x08);
        m_air["co2Warn"] = bool(f & 0x10);
        m_air["co2Alarm"] = bool(f & 0x20);
        m_air["vocAlarm"] = bool(f & 0x40);
        m_air["safety"] = true;
        seen("borealis");
        m_dirty |= Air;
    });

    // What is sent
    for (int i = 0; i < 3; ++i) {
        if (!m_dbc.has(QStringLiteral("TorrentBrightness%1").arg(i), { "ChannelIndex", "BrightnessLevel" }))
            qCWarning(lcCan) << "TrailCurrent.dbc has no TorrentBrightness" << i << "- its lights cannot be set";
        if (!m_dbc.has(QStringLiteral("SwitchbackToggle%1").arg(i), { "RelayOrCommand", "CommandValue" }))
            qCWarning(lcCan) << "TrailCurrent.dbc has no SwitchbackToggle" << i << "- its relays cannot be set";
    }
    if (!m_dbc.has("ThermaSetDesiredRequest", { "RequestedSetpointDeciC" }))
        qCWarning(lcCan) << "TrailCurrent.dbc has no ThermaSetDesiredRequest - the thermostat cannot be set";

    qCInfo(lcCan) << "TrailCurrent.dbc:" << m_handlers.size() << "messages decoded, from" << path;
}

void TrailCurrent::addHandler(const QString &message, const QStringList &signalNames, Handler h)
{
    const Dbc::Message *m = m_dbc.message(message);
    if (!m || !m_dbc.has(message, signalNames) || m->extended) {
        qCWarning(lcCan) << "TrailCurrent.dbc has no" << message << "with" << signalNames << "- not decoded";
        return;
    }
    m_handlers.insert(m->id, { m, std::move(h) });
}

// --------------------------------------------------------------- settings

QString TrailCurrent::configPath() const
{
    return Music::stateDir() + QStringLiteral("/trailcurrent.json");
}

void TrailCurrent::loadConfig()
{
    QFile f(configPath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (const auto &m : o.value("modules").toArray())
        m_known.insert(m.toString());
    m_lightsConf = o.value("lights").toObject().toVariantMap();
    m_inputsConf = o.value("inputs").toObject().toVariantMap();
}

void TrailCurrent::saveConfig()
{
    QJsonObject o;
    QStringList known(m_known.cbegin(), m_known.cend());
    known.sort();
    o["modules"] = QJsonArray::fromStringList(known);
    o["lights"] = QJsonObject::fromVariantMap(m_lightsConf);
    o["inputs"] = QJsonObject::fromVariantMap(m_inputsConf);
    QDir().mkpath(QFileInfo(configPath()).absolutePath());
    QSaveFile f(configPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson());
        if (f.commit())
            return;
    }
    qCWarning(lcCan) << "could not save" << configPath();
}

void TrailCurrent::rename(const QString &key, const QString &name)
{
    const QString n = name.trimmed().left(24);
    QVariantMap &conf = key.startsWith(QLatin1Char('p')) || key.startsWith(QLatin1Char('d')) ? m_inputsConf : m_lightsConf;
    QVariantMap c = conf.value(key).toMap();
    if (n.isEmpty())
        c.remove("name");
    else
        c["name"] = n;
    conf.insert(key, c);
    saveConfig();
    m_dirty |= Lights | Inputs;
    publish();
}

void TrailCurrent::setHidden(const QString &key, bool hidden)
{
    QVariantMap c = m_lightsConf.value(key).toMap();
    c["hidden"] = hidden;
    m_lightsConf.insert(key, c);
    saveConfig();
    m_dirty |= Lights;
    publish();
}

void TrailCurrent::setDimmable(const QString &key, bool dim)
{
    QVariantMap c = m_lightsConf.value(key).toMap();
    c["dim"] = dim;
    m_lightsConf.insert(key, c);
    saveConfig();
    m_dirty |= Lights;
    publish();
}

void TrailCurrent::setWarn(const QString &key, bool warn)
{
    QVariantMap c = m_inputsConf.value(key).toMap();
    c["warn"] = warn;
    m_inputsConf.insert(key, c);
    saveConfig();
    m_dirty |= Inputs;
    publish();
}

void TrailCurrent::forgetMissing()
{
    const QSet<QString> heard(m_seen.keyBegin(), m_seen.keyEnd());
    m_known = heard;
    // Names of the channels of modules no longer known go with them
    auto prune = [this](QVariantMap &conf) {
        for (auto it = conf.begin(); it != conf.end();) {
            QChar kind; int inst = 0, ch = 0;
            QString module;
            if (splitKey(it.key(), &kind, &inst, &ch))
                module = kind == QLatin1Char('t') ? QStringLiteral("torrent%1").arg(inst)
                       : kind == QLatin1Char('p') ? QStringLiteral("picket%1").arg(inst)
                       : QStringLiteral("switchback%1").arg(inst);
            if (!m_known.contains(module))
                it = conf.erase(it);
            else
                ++it;
        }
    };
    prune(m_lightsConf);
    prune(m_inputsConf);
    saveConfig();
    m_dirty |= Lights | Inputs | Modules;
    publish();
}

// ------------------------------------------------------------------ socket

void TrailCurrent::poll()
{
    if (!ready())
        return;
    // Which interface, and may it transmit
    QString want;
    bool listen = false;
    if (!m_overrideIface.isEmpty()) {
        want = m_overrideIface;
    } else if (m_can) {
        const QVariantList ports = m_can->ports();
        const QStringList ifaces = CanBus::interfaces();
        for (int i = 0; i < ports.size(); ++i) {
            const QVariantMap p = ports.at(i).toMap();
            if (p.value("role").toString() != QLatin1String("TrailCurrent"))
                continue;
            if (i < ifaces.size())
                want = ifaces.at(i);
            listen = p.value("listen").toBool();
            break;
        }
    }
    bool changed = listen != m_listenOnly;
    m_listenOnly = listen;
    if (want != m_iface || (m_fd < 0 && !want.isEmpty())) {
        closeSocket();
        m_iface = want;
        if (!want.isEmpty())
            openSocket(want);
        changed = true;
    }
    // IFF_UP, from the interface's flags: vcan's operstate stays "unknown"
    const bool up = m_fd >= 0 && (readLine(QStringLiteral("/sys/class/net/") + m_iface + QStringLiteral("/flags")).toUInt(nullptr, 16) & IFF_UP);
    if (up != m_up) {
        m_up = up;
        changed = true;
    }
    if (changed)
        emit linkChanged();

    // Modules going quiet, or back
    QSet<QString> freshNow;
    for (auto it = m_seen.cbegin(); it != m_seen.cend(); ++it)
        if (fresh(it.key()))
            freshNow.insert(it.key());
    if (freshNow != m_freshNow) {
        m_freshNow = freshNow;
        m_dirty |= Lights | Inputs | Energy | Water | Level | Camper | Air | Modules;
    }
}

void TrailCurrent::openSocket(const QString &iface)
{
    const unsigned index = if_nametoindex(iface.toLocal8Bit().constData());
    if (!index)
        return;
    const int fd = ::socket(PF_CAN, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, CAN_RAW);
    if (fd < 0) {
        qCWarning(lcCan) << "TrailCurrent: no CAN socket:" << strerror(errno);
        return;
    }
    // Only the messages decoded here, and the controller's state
    QList<can_filter> filters;
    for (auto it = m_handlers.cbegin(); it != m_handlers.cend(); ++it)
        filters.append({ canid_t(it.key()), canid_t(CAN_EFF_FLAG | CAN_RTR_FLAG | CAN_SFF_MASK) });
    setsockopt(fd, SOL_CAN_RAW, CAN_RAW_FILTER, filters.constData(), socklen_t(filters.size() * sizeof(can_filter)));
    const can_err_mask_t errors = CAN_ERR_BUSOFF | CAN_ERR_RESTARTED;
    setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &errors, sizeof errors);

    sockaddr_can addr {};
    addr.can_family = AF_CAN;
    addr.can_ifindex = int(index);
    if (::bind(fd, reinterpret_cast<sockaddr *>(&addr), sizeof addr) < 0) {
        qCWarning(lcCan) << "TrailCurrent: cannot listen on" << iface << strerror(errno);
        ::close(fd);
        return;
    }
    m_fd = fd;
    m_busOff = false;
    m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &TrailCurrent::readFrames);
    qCInfo(lcCan) << "TrailCurrent bus on" << iface << (m_listenOnly ? "(listen only)" : "");
}

void TrailCurrent::closeSocket()
{
    delete m_notifier;
    m_notifier = nullptr;
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
}

void TrailCurrent::readFrames()
{
    for (int n = 0; n < 256; ++n) {
        can_frame f {};
        const ssize_t got = ::read(m_fd, &f, sizeof f);
        if (got < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ENETDOWN)
                return;
            // The interface went away; poll() finds it again if it comes back
            qCWarning(lcCan) << "TrailCurrent:" << m_iface << strerror(errno);
            closeSocket();
            emit linkChanged();
            return;
        }
        if (got == ssize_t(sizeof f))
            handleFrame(f);
    }
}

void TrailCurrent::handleFrame(const can_frame &f)
{
    if (f.can_id & CAN_ERR_FLAG) {
        const bool off = f.can_id & CAN_ERR_BUSOFF;
        if (off != m_busOff || (f.can_id & CAN_ERR_RESTARTED)) {
            m_busOff = off;
            qCWarning(lcCan) << "TrailCurrent bus" << (off ? "off" : "restarted");
            emit linkChanged();
        }
        return;
    }
    if (m_busOff) {
        m_busOff = false;
        emit linkChanged();
    }
    if (f.can_id & (CAN_EFF_FLAG | CAN_RTR_FLAG))
        return;
    const auto it = m_handlers.constFind(f.can_id & CAN_SFF_MASK);
    if (it != m_handlers.cend())
        it->second(Dbc::decode(*it->first, f.data, std::min<int>(f.len, CAN_MAX_DLEN)));
}

QString TrailCurrent::blocked() const
{
    if (!ready())
        return QStringLiteral("TrailCurrent messages are not installed");
    if (m_iface.isEmpty())
        return QStringLiteral("No CAN port is set to TrailCurrent");
    if (m_fd < 0)
        return QStringLiteral("The TrailCurrent CAN port is not found");
    if (!m_up)
        return QStringLiteral("The TrailCurrent bus is down");
    if (m_listenOnly)
        return QStringLiteral("The TrailCurrent bus is set to listen only");
    if (m_busOff)
        return QStringLiteral("The TrailCurrent bus is off. Check its wiring.");
    return QString();
}

bool TrailCurrent::refuse(const QString &reason)
{
    emit message(reason);
    return false;
}

bool TrailCurrent::sendFrame(const QString &message, const QHash<QString, double> &values)
{
    const Dbc::Message *m = m_dbc.message(message);
    if (!m || m->extended || m->dlc > CAN_MAX_DLEN || m_fd < 0)
        return false;
    can_frame f {};
    f.can_id = m->id;
    f.len = uint8_t(m->dlc);
    const QByteArray data = Dbc::encode(*m, values);
    std::memcpy(f.data, data.constData(), size_t(data.size()));
    if (::write(m_fd, &f, sizeof f) == ssize_t(sizeof f))
        return true;
    // ENOBUFS: the queue is full, as when nothing on the bus acknowledges
    const int err = errno;
    qCWarning(lcCan) << "TrailCurrent: could not send" << message << strerror(err);
    if (now() - m_lastTxError > 5000) {
        m_lastTxError = now();
        emit this->message(err == ENOBUFS ? QStringLiteral("Nothing on the TrailCurrent bus is answering")
                                          : QStringLiteral("Could not send on the TrailCurrent bus"));
    }
    return false;
}

// ---------------------------------------------------------------- commands

int TrailCurrent::lightStatus(const QString &key) const
{
    QChar kind; int inst = 0, ch = 0;
    if (!splitKey(key, &kind, &inst, &ch) || inst < 0 || inst > 2 || ch < 0 || ch > 7)
        return -1;
    if (kind == QLatin1Char('t'))
        return m_pwm[inst][ch];
    if (kind == QLatin1Char('r'))
        return m_relays[inst] < 0 ? -1 : (m_relays[inst] >> ch) & 1;
    return -1;
}

bool TrailCurrent::setLight(const QString &key, bool on)
{
    QChar kind; int inst = 0, ch = 0;
    if (!splitKey(key, &kind, &inst, &ch) || inst < 0 || inst > 2 || ch < 0 || ch > 7)
        return false;
    const QString b = blocked();
    if (!b.isEmpty())
        return refuse(b);
    const QString module = (kind == QLatin1Char('t') ? QStringLiteral("torrent%1") : QStringLiteral("switchback%1")).arg(inst);
    if (!fresh(module))
        return refuse(lightName(key) + QStringLiteral(" is not responding"));
    Pending &p = m_pending[key];
    if (kind == QLatin1Char('r')) {
        // A toggle in flight: another now would undo it
        if (p.sentAt >= 0 && now() - p.sentAt < RelayCheckMs)
            return true;
        p.target = on ? 1 : 0;
    } else {
        p.target = on ? m_lastOn.value(key, 255) : 0;
    }
    p.since = now();
    p.tries = 0;
    p.dirty = true;
    m_dirty |= Lights;
    flushPending();
    publish();
    return true;
}

bool TrailCurrent::setBrightness(const QString &key, int percent)
{
    if (!key.startsWith(QLatin1Char('t')))
        return setLight(key, percent > 0);
    QChar kind; int inst = 0, ch = 0;
    if (!splitKey(key, &kind, &inst, &ch) || inst < 0 || inst > 2 || ch < 0 || ch > 7)
        return false;
    const QString b = blocked();
    if (!b.isEmpty())
        return refuse(b);
    if (!fresh(QStringLiteral("torrent%1").arg(inst)))
        return refuse(lightName(key) + QStringLiteral(" is not responding"));
    const int level = int(std::lround(std::clamp(percent, 0, 100) * 255 / 100.0));
    Pending &p = m_pending[key];
    p.target = level;
    p.since = now();
    p.dirty = true;
    if (level > 0)
        m_lastOn.insert(key, level);
    m_dirty |= Lights;
    flushPending();
    publish();
    return true;
}

bool TrailCurrent::allLightsOff()
{
    const QString b = blocked();
    if (!b.isEmpty())
        return refuse(b);
    // A copy: each setLight() publishes a new list
    const QVariantList lights = m_lightsOut;
    for (const QVariant &v : lights) {
        const QVariantMap l = v.toMap();
        if (!l.value("hidden").toBool() && l.value("fresh").toBool() && l.value("on").toBool())
            setLight(l.value("key").toString(), false);
    }
    return true;
}

bool TrailCurrent::setCamperSetpoint(double celsius)
{
    const QString b = blocked();
    if (!b.isEmpty())
        return refuse(b);
    if (!fresh("therma"))
        return refuse(QStringLiteral("The camper thermostat is not responding"));
    Pending &p = m_pending[QStringLiteral("therma")];
    p.target = int(std::lround(std::clamp(celsius, ThermaMinC, ThermaMaxC) * 10));
    p.since = now();
    p.dirty = true;
    m_dirty |= Camper;
    flushPending();
    publish();
    return true;
}

// Sends what is waiting, checks what was sent against the status, and gives
// up on what the bus has not taken
void TrailCurrent::flushPending()
{
    const qint64 t = now();
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        const QString &key = it.key();
        Pending &p = it.value();
        bool done = false;
        if (key == QLatin1String("therma")) {
            const QVariant set = m_camper.value("set");
            if (!p.dirty && p.sentAt >= 0 && set.isValid() && std::lround(set.toDouble() * 10) == p.target) {
                done = true;
            } else if (t - p.since > ThermaGiveUpMs) {
                emit message(QStringLiteral("The camper thermostat did not take that setpoint"));
                done = true;
            } else if (p.dirty && t - m_lastSent.value(key) >= SendGapMs) {
                if (sendFrame("ThermaSetDesiredRequest", { { "RequestedSetpointDeciC", p.target / 10.0 } })) {
                    p.sentAt = t;
                    m_lastSent.insert(key, t);
                }
                p.dirty = false;
            }
        } else if (key.startsWith(QLatin1Char('t'))) {
            QChar kind; int inst = 0, ch = 0;
            splitKey(key, &kind, &inst, &ch);
            const int status = lightStatus(key);
            if (!p.dirty && status == p.target) {
                done = true;
            } else if (t - p.since > GiveUpMs) {
                emit message(lightName(key) + QStringLiteral(" did not respond"));
                done = true;
            } else if (p.dirty && t - m_lastSent.value(key) >= SendGapMs) {
                if (sendFrame(QStringLiteral("TorrentBrightness%1").arg(inst), { { "ChannelIndex", ch }, { "BrightnessLevel", p.target } })) {
                    p.sentAt = t;
                    m_lastSent.insert(key, t);
                }
                p.dirty = false;
            }
        } else if (key.startsWith(QLatin1Char('r'))) {
            QChar kind; int inst = 0, ch = 0;
            splitKey(key, &kind, &inst, &ch);
            const int status = lightStatus(key);
            if (status == p.target) {
                done = true;
            } else if (p.sentAt < 0 || t - p.sentAt >= RelayCheckMs) {
                // Toggled once, and once more if the status still disagrees
                if (p.tries >= 2) {
                    emit message(lightName(key) + QStringLiteral(" did not switch"));
                    done = true;
                } else if (status >= 0) {
                    if (sendFrame(QStringLiteral("SwitchbackToggle%1").arg(inst), { { "RelayOrCommand", ch }, { "CommandValue", 0 } }))
                        p.sentAt = t;
                    ++p.tries;
                } else {
                    done = true;
                }
            }
        } else {
            done = true;
        }
        if (done) {
            it = m_pending.erase(it);
            m_dirty |= key == QLatin1String("therma") ? Camper : Lights;
        } else {
            ++it;
        }
    }
}

// ------------------------------------------------------------------ status

void TrailCurrent::seen(const QString &module)
{
    const bool wasFresh = fresh(module);
    m_seen.insert(module, now());
    if (!wasFresh) {
        m_freshNow.insert(module);
        m_dirty |= Lights | Inputs | Energy | Water | Level | Camper | Air | Modules;
    }
    if (!m_known.contains(module)) {
        m_known.insert(module);
        saveConfig();
    }
}

bool TrailCurrent::fresh(const QString &module) const
{
    const auto it = m_seen.constFind(module);
    return it != m_seen.cend() && now() - *it < (module == QLatin1String("plateau") ? SlowStaleMs : StaleMs);
}

QString TrailCurrent::lightName(const QString &key) const
{
    const QString n = m_lightsConf.value(key).toMap().value("name").toString();
    if (!n.isEmpty())
        return n;
    QChar kind; int inst = 0, ch = 0;
    splitKey(key, &kind, &inst, &ch);
    // Numbered as Headwaters numbers them: lights 1-24, relays 1-24 (its 101-124)
    return (kind == QLatin1Char('t') ? QStringLiteral("Light %1") : QStringLiteral("Relay %1")).arg(inst * 8 + ch + 1);
}

QString TrailCurrent::inputName(const QString &key) const
{
    const QString n = m_inputsConf.value(key).toMap().value("name").toString();
    if (!n.isEmpty())
        return n;
    QChar kind; int inst = 0, ch = 0;
    splitKey(key, &kind, &inst, &ch);
    return kind == QLatin1Char('p') ? QStringLiteral("Door %1").arg(inst * 12 + ch + 1) : QStringLiteral("Input %1").arg(inst * 8 + ch + 1);
}

void TrailCurrent::publish()
{
    // Values that change often (battery current, tilt) are published at a
    // fifth of the rate the lights are
    const bool slowTick = (++m_tick % 5) == 0;
    const int slow = Energy | Level | Air;
    int due = m_dirty & ~slow;
    if (slowTick)
        due |= m_dirty & slow;
    m_dirty &= ~due;

    if (due & Modules) {
        QStringList ids(m_known.cbegin(), m_known.cend());
        for (auto it = m_seen.cbegin(); it != m_seen.cend(); ++it)
            if (!ids.contains(it.key()))
                ids.append(it.key());
        ids.sort();
        QVariantList out;
        for (const QString &id : std::as_const(ids))
            out.append(QVariantMap { { "id", id }, { "n", moduleName(id) }, { "fresh", fresh(id) } });
        if (out != m_modulesOut) {
            m_modulesOut = out;
            emit modulesChanged();
        }
    }
    if (due & Lights) {
        QVariantList out;
        for (int i = 0; i < 3; ++i) {
            for (const QChar kind : { QLatin1Char('t'), QLatin1Char('r') }) {
                const QString module = (kind == QLatin1Char('t') ? QStringLiteral("torrent%1") : QStringLiteral("switchback%1")).arg(i);
                if (!m_known.contains(module) && !m_seen.contains(module))
                    continue;
                const bool fr = fresh(module);
                for (int ch = 0; ch < 8; ++ch) {
                    const QString key = QStringLiteral("%1%2.%3").arg(kind).arg(i).arg(ch);
                    const QVariantMap c = m_lightsConf.value(key).toMap();
                    const auto pend = m_pending.constFind(key);
                    // While a command is on its way, show what was asked for
                    int v = pend != m_pending.cend() ? pend->target : lightStatus(key);
                    if (!fr && pend == m_pending.cend())
                        v = -1;
                    const bool pwm = kind == QLatin1Char('t');
                    int b = pwm ? (v > 0 ? std::max(1, int(std::lround(v * 100 / 255.0))) : 0) : (v > 0 ? 100 : 0);
                    out.append(QVariantMap {
                        { "key", key }, { "n", lightName(key) }, { "kind", pwm ? "pwm" : "relay" },
                        { "on", v > 0 }, { "b", b },
                        { "dim", pwm && c.value("dim", true).toBool() },
                        { "hidden", c.value("hidden", false).toBool() },
                        { "fresh", fr && v >= 0 },
                        { "busy", pend != m_pending.cend() },
                    });
                }
            }
        }
        if (out != m_lightsOut) {
            m_lightsOut = out;
            emit lightsChanged();
        }
    }
    if (due & Inputs) {
        QVariantList out;
        for (int i = 0; i < 11; ++i) {
            const QString module = i < 8 ? QStringLiteral("picket%1").arg(i) : QStringLiteral("switchback%1").arg(i - 8);
            const int n = i < 8 ? 12 : 8;
            const bool fr = fresh(module) && m_doors[i] >= 0;
            for (int b = 0; b < n; ++b) {
                const QString key = i < 8 ? QStringLiteral("p%1.%2").arg(i).arg(b) : QStringLiteral("d%1.%2").arg(i - 8).arg(b);
                const QVariantMap c = m_inputsConf.value(key).toMap();
                if (!c.value("used").toBool())
                    continue;
                out.append(QVariantMap {
                    { "key", key }, { "n", inputName(key) },
                    { "open", fr && (m_doors[i] >> b) & 1 },
                    { "warn", c.value("warn", i < 8).toBool() },
                    { "fresh", fr },
                });
            }
        }
        if (out != m_inputsOut) {
            m_inputsOut = out;
            emit inputsChanged();
        }
    }
    auto group = [this, due](int flag, const QString &module, const QVariantMap &values, QVariantMap &outMap, void (TrailCurrent::*sig)()) {
        if (!(due & flag))
            return;
        QVariantMap o = values;
        o["fresh"] = fresh(module);
        if (o != outMap) {
            outMap = o;
            emit (this->*sig)();
        }
    };
    group(Energy, "solstice", m_energy, m_energyOut, &TrailCurrent::energyChanged);
    group(Water, "reservoir", m_water, m_waterOut, &TrailCurrent::waterChanged);
    group(Level, "plateau", m_level, m_levelOut, &TrailCurrent::levelChanged);
    if (due & Camper) {
        QVariantMap o = m_camper;
        o["fresh"] = fresh("therma");
        const auto pend = m_pending.constFind(QStringLiteral("therma"));
        o["pending"] = pend != m_pending.cend() ? QVariant(pend->target / 10.0) : QVariant();
        if (o != m_camperOut) {
            m_camperOut = o;
            emit camperChanged();
        }
    }
    if (due & Air) {
        QVariantMap o = m_air;
        o["fresh"] = fresh("borealis");
        // Heard this run, and silent now: its alarms cannot be trusted
        o["lost"] = m_seen.contains("borealis") && !fresh("borealis");
        if (o != m_airOut) {
            m_airOut = o;
            emit airChanged();
        }
    }
}
