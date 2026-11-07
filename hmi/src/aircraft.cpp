#include "aircraft.h"

#include <QDateTime>
#include <QFile>
#include <QLoggingCategory>

#include <cstring>

Q_LOGGING_CATEGORY(lcAircraft, "godwit.aircraft")

AircraftTable::~AircraftTable()
{
    delete m_file;
}

bool AircraftTable::open(const QString &path)
{
    auto *file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly) || file->size() == 0) {
        delete file;
        return false;
    }
    m_data = reinterpret_cast<const char *>(file->map(0, file->size()));
    if (!m_data) {
        delete file;
        return false;
    }
    m_size = file->size();
    m_file = file;
    return true;
}

QByteArray AircraftTable::find(const QByteArray &key) const
{
    if (!m_data || key.isEmpty())
        return {};
    // Over the bytes: land anywhere, find that line's start, compare its key
    qint64 lo = 0, hi = m_size;
    while (lo < hi) {
        const qint64 mid = lo + (hi - lo) / 2;
        qint64 start = mid;
        while (start > 0 && m_data[start - 1] != '\n')
            --start;
        const char *tab = static_cast<const char *>(std::memchr(m_data + start, '\t', size_t(m_size - start)));
        const char *nl = static_cast<const char *>(std::memchr(m_data + start, '\n', size_t(m_size - start)));
        if (!tab || !nl || tab > nl)
            return {};
        const int cmp = QByteArrayView(m_data + start, tab - (m_data + start)).compare(QByteArrayView(key));
        if (cmp == 0)
            return QByteArray(tab + 1, nl - tab - 1);
        if (cmp < 0)
            lo = (nl - m_data) + 1;
        else
            hi = start;
    }
    return {};
}

Aircraft::Aircraft(QObject *parent)
    : TunerReceiver("aircraft", "GODWIT_ADSB_BIN", QStringLiteral("/usr/bin/godwit-adsb"), parent)
{
    if (!available())
        return;
    const QString dir = qEnvironmentVariable("GODWIT_AIRCRAFT_DATA", QStringLiteral("/usr/share/godwit/aircraft"));
    // Without them the app still works: type and route just go unsaid
    if (!m_types.open(dir + QStringLiteral("/aircraft.tsv")) || !m_routes.open(dir + QStringLiteral("/routes.tsv")))
        qCInfo(lcAircraft) << "no aircraft tables in" << dir;
}

// What the tables say about an aircraft, looked up once when it is first heard
// and again when its callsign arrives
QVariantMap Aircraft::describe(const QByteArray &hex, const QByteArray &call)
{
    QVariantMap a;
    a.insert(QStringLiteral("hex"), QString::fromLatin1(hex));
    const QList<QByteArray> kind = m_types.find(hex).split('\t');
    if (kind.size() == 2) {
        if (!kind[0].isEmpty())
            a.insert(QStringLiteral("type"), QString::fromUtf8(kind[0]));
        if (!kind[1].isEmpty())
            a.insert(QStringLiteral("reg"), QString::fromUtf8(kind[1]));
    }
    if (!call.isEmpty()) {
        a.insert(QStringLiteral("call"), QString::fromLatin1(call));
        const QByteArray route = m_routes.find(call);
        if (!route.isEmpty()) {
            QStringList stops;
            for (const QByteArray &stop : route.split(' '))
                stops.append(QString::fromUtf8(stop));
            a.insert(QStringLiteral("route"), stops);
        }
    }
    return a;
}

bool Aircraft::handleLine(const QByteArray &line)
{
    const QList<QByteArray> p = line.split(' ');
    if (p.size() != 9 || p[0] != "ac")
        return false;
    const auto given = [](const QByteArray &v) { return v != "-"; };

    const QByteArray &hex = p[1];
    QVariantMap a = m_items.value(hex);
    const QByteArray call = given(p[2]) ? p[2] : QByteArray();
    if (a.isEmpty() || (!call.isEmpty() && a.value(QStringLiteral("call")).toString().toLatin1() != call)) {
        const QVariantMap known = describe(hex, call);
        for (auto it = known.begin(); it != known.end(); ++it)
            a.insert(it.key(), it.value());
    }
    if (given(p[3]))
        a.insert(QStringLiteral("category"), QString::fromLatin1(p[3]));
    if (given(p[4]) && given(p[5])) {
        a.insert(QStringLiteral("lat"), p[4].toDouble());
        a.insert(QStringLiteral("lon"), p[5].toDouble());
        // When, as the app's clock has it, so it can tell one going stale
        a.insert(QStringLiteral("at"), QDateTime::currentMSecsSinceEpoch() - p[8].toLongLong() * 1000);
    }
    if (given(p[6]))
        a.insert(QStringLiteral("alt"), p[6].toInt());
    if (given(p[7]))
        a.insert(QStringLiteral("speed"), p[7].toInt());
    m_items.insert(hex, a);
    return true;
}
