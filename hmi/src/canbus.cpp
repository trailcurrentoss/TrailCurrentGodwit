#include "canbus.h"
#include "music.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QSaveFile>

#include <algorithm>

Q_LOGGING_CATEGORY(lcCan, "godwit.can")

namespace {
constexpr int Ports = 2;
constexpr int PollMs = 1000;
// Rates are averaged over this many polls, so a bus that sends a frame every
// second or two does not flicker between receiving and silent
constexpr int Window = 3;
// A detection that has not reported by then is taken to have failed
constexpr qint64 DetectTimeoutMs = 120000;
// ARPHRD_CAN, from /sys/class/net/<iface>/type
constexpr int ArphrdCan = 280;

const QStringList Roles = { QStringLiteral("Vehicle"), QStringLiteral("TrailCurrent"), QStringLiteral("Off") };
// Kept in step with CAN_RATES and CAN_DATA_RATES in DemoData.js
const QList<int> Rates = { 125000, 250000, 500000, 1000000 };
const QList<int> DataRates = { 1000000, 2000000, 4000000, 5000000, 8000000 };

QString readLine(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromLatin1(f.readLine()).trimmed() : QString();
}
}

CanBus::CanBus(QObject *parent)
    : QObject(parent)
{
    const bool saved = QFileInfo::exists(confPath());
    m_ports = load();
    // First start: write the defaults, so godwit-can-apply has something to apply
    if (!saved) {
        write();
        apply();
    }
    for (int i = 0; i < Ports; ++i) {
        m_status.append(QVariantMap());
        m_history.append(QList<Sample>());
    }
    m_poll.setInterval(PollMs);
    connect(&m_poll, &QTimer::timeout, this, &CanBus::poll);
    m_poll.start();
    poll();
}

// CAN 1 on the TrailCurrent bus (its ESP32 modules are classic CAN only), CAN 2
// on the vehicle's CAN FD bus, receiving only until the driver chooses otherwise
QVariantList CanBus::defaults()
{
    return {
        QVariantMap { { "role", "TrailCurrent" }, { "fd", false }, { "rate", 500000 }, { "drate", 2000000 }, { "listen", false } },
        QVariantMap { { "role", "Vehicle" }, { "fd", true }, { "rate", 500000 }, { "drate", 2000000 }, { "listen", true } },
    };
}

bool CanBus::valid(const QVariantList &ports)
{
    if (ports.size() != Ports)
        return false;
    for (const QVariant &v : ports) {
        const QVariantMap p = v.toMap();
        if (!Roles.contains(p.value("role").toString())
            || !Rates.contains(p.value("rate").toInt())
            || !DataRates.contains(p.value("drate").toInt()))
            return false;
    }
    return true;
}

QString CanBus::confPath() const
{
    return Music::stateDir() + QStringLiteral("/can.conf");
}

// KEY=value lines, CAN1_ROLE=TrailCurrent and so on. Plain text rather than
// JSON so godwit-can-apply can read it without an interpreter.
QVariantList CanBus::load() const
{
    QFile f(confPath());
    if (!f.open(QIODevice::ReadOnly))
        return defaults();
    QVariantMap kv;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0)
            kv.insert(line.left(eq), line.mid(eq + 1));
    }
    QVariantList ports;
    for (int i = 0; i < Ports; ++i) {
        const QString k = QStringLiteral("CAN%1_").arg(i + 1);
        ports.append(QVariantMap {
            { "role", kv.value(k + "ROLE") },
            { "fd", kv.value(k + "FD").toString() == QLatin1String("1") },
            { "rate", kv.value(k + "RATE").toInt() },
            { "drate", kv.value(k + "DRATE").toInt() },
            { "listen", kv.value(k + "LISTEN").toString() == QLatin1String("1") },
        });
    }
    if (valid(ports))
        return ports;
    qCWarning(lcCan) << confPath() << "is not valid, using the defaults";
    return defaults();
}

void CanBus::write()
{
    QByteArray out = "# CAN ports, from Settings > CAN. Applied by godwit-can-apply.\n";
    for (int i = 0; i < Ports; ++i) {
        const QVariantMap p = m_ports.at(i).toMap();
        const QByteArray k = "CAN" + QByteArray::number(i + 1) + "_";
        out += k + "ROLE=" + p.value("role").toString().toLatin1() + '\n';
        out += k + "FD=" + (p.value("fd").toBool() ? "1" : "0") + '\n';
        out += k + "RATE=" + QByteArray::number(p.value("rate").toInt()) + '\n';
        out += k + "DRATE=" + QByteArray::number(p.value("drate").toInt()) + '\n';
        out += k + "LISTEN=" + (p.value("listen").toBool() ? "1" : "0") + '\n';
    }
    QDir().mkpath(QFileInfo(confPath()).absolutePath());
    QSaveFile f(confPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(out);
        if (f.commit())
            return;
    }
    qCWarning(lcCan) << "could not save" << confPath();
}

bool CanBus::save(const QVariantList &ports)
{
    if (!valid(ports)) {
        qCWarning(lcCan) << "refused invalid settings" << ports;
        return false;
    }
    m_ports = ports;
    emit portsChanged();
    write();
    apply();
    return true;
}

bool CanBus::detect(int port)
{
    if (port < 0 || port >= Ports || m_detection.value("state") == QLatin1String("running"))
        return false;
    if (QGuiApplication::platformName() != QLatin1String("eglfs")) {
        qCInfo(lcCan) << "not detecting: this is not the head unit";
        return false;
    }
    const QString dir = qEnvironmentVariable("XDG_RUNTIME_DIR");
    QFile::remove(dir + QStringLiteral("/can-detect.out"));
    QFile f(dir + QStringLiteral("/can-detect"));
    if (!f.open(QIODevice::WriteOnly) || f.write(QByteArray::number(port + 1) + '\n') < 0) {
        qCWarning(lcCan) << "could not ask to detect:" << f.errorString();
        return false;
    }
    m_detection = { { "port", port }, { "state", "running" }, { "step", 0 }, { "steps", 0 }, { "rate", 0 } };
    m_detectSince = QDateTime::currentMSecsSinceEpoch();
    emit detectionChanged();
    return true;
}

// KEY=value lines from godwit-can-apply, rewritten whole at each step
void CanBus::readDetection()
{
    if (m_detection.value("state") != QLatin1String("running"))
        return;
    QFile f(qEnvironmentVariable("XDG_RUNTIME_DIR") + QStringLiteral("/can-detect.out"));
    QVariantMap kv;
    if (f.open(QIODevice::ReadOnly)) {
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq > 0)
                kv.insert(line.left(eq).toLower(), line.mid(eq + 1));
        }
    }
    QVariantMap d = m_detection;
    if (kv.value("port").toInt() == d.value("port").toInt() + 1) {
        for (const char *key : { "state", "result", "data" })
            d[key] = kv.value(key).toString();
        for (const char *key : { "step", "steps", "rate", "drate", "frames" })
            d[key] = kv.value(key).toInt();
    }
    if (d.value("state") == QLatin1String("running")
        && QDateTime::currentMSecsSinceEpoch() - m_detectSince > DetectTimeoutMs) {
        qCWarning(lcCan) << "rate detection did not finish";
        d["state"] = QStringLiteral("failed");
    }
    if (d != m_detection) {
        m_detection = d;
        emit detectionChanged();
    }
}

void CanBus::apply()
{
    if (QGuiApplication::platformName() != QLatin1String("eglfs")) {
        qCInfo(lcCan) << "not applying: this is not the head unit";
        return;
    }
    const QString request = qEnvironmentVariable("XDG_RUNTIME_DIR") + QStringLiteral("/can-apply");
    QFile f(request);
    if (!f.open(QIODevice::WriteOnly))
        qCWarning(lcCan) << "could not ask to apply:" << f.errorString();
}

// Physical CAN interfaces (not vcan), in device path order
QStringList CanBus::interfaces()
{
    QList<QPair<QString, QString>> found;
    const QDir net(QStringLiteral("/sys/class/net"));
    for (const QString &name : net.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System)) {
        const QString dir = net.filePath(name);
        if (readLine(dir + QStringLiteral("/type")).toInt() != ArphrdCan)
            continue;
        const QString device = QFileInfo(dir + QStringLiteral("/device")).canonicalFilePath();
        if (!device.isEmpty())
            found.append({ device, name });
    }
    std::sort(found.begin(), found.end());
    QStringList names;
    for (const auto &f : std::as_const(found))
        names.append(f.second);
    return names;
}

void CanBus::poll()
{
    readDetection();
    const QStringList ifaces = interfaces();
    QVariantList status;
    for (int i = 0; i < Ports; ++i) {
        QVariantMap s { { "iface", QString() }, { "up", false }, { "fps", 0 }, { "eps", 0 } };
        QList<Sample> &h = m_history[i];
        if (i < ifaces.size()) {
            const QString stats = QStringLiteral("/sys/class/net/") + ifaces.at(i) + QStringLiteral("/statistics/");
            Sample now;
            now.frames = readLine(stats + QStringLiteral("rx_packets")).toULongLong()
                       + readLine(stats + QStringLiteral("tx_packets")).toULongLong();
            now.errors = readLine(stats + QStringLiteral("rx_errors")).toULongLong();
            const bool up = readLine(QStringLiteral("/sys/class/net/") + ifaces.at(i) + QStringLiteral("/operstate")) == QLatin1String("up");
            // Counters restart when the interface is set up again
            if (!up || (!h.isEmpty() && (now.frames < h.last().frames || now.errors < h.last().errors)))
                h.clear();
            h.append(now);
            while (h.size() > Window + 1)
                h.removeFirst();
            const int secs = (h.size() - 1) * PollMs / 1000;
            s["iface"] = ifaces.at(i);
            s["up"] = up;
            if (secs > 0) {
                s["fps"] = int((h.last().frames - h.first().frames + secs / 2) / secs);
                s["eps"] = int((h.last().errors - h.first().errors + secs / 2) / secs);
            }
        } else {
            h.clear();
        }
        status.append(s);
    }
    if (status != m_status) {
        m_status = status;
        emit statusChanged();
    }
}
