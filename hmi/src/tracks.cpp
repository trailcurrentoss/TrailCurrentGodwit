#include "tracks.h"
#include "music.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcTracks, "godwit.tracks")

namespace {
constexpr int MaxResults = 300;
constexpr double MetresPerMile = 1609.344;
constexpr double ResortMetres = 200;     // re-sort a nearest-first list after moving this far
constexpr double FenceSearchKm = 8;      // only test geofences of tracks this close
constexpr double CarMarginM = 400;       // show the car on the drawing within this of it
constexpr double SectorHalfM = 14;       // half a sector line's length, like the start/finish gate

double distanceM(double lat1, double lon1, double lat2, double lon2)
{
    const double r = 6371000, p = M_PI / 180;
    const double a = std::sin((lat2 - lat1) * p / 2), b = std::sin((lon2 - lon1) * p / 2);
    const double h = a * a + std::cos(lat1 * p) * std::cos(lat2 * p) * b * b;
    return 2 * r * std::asin(std::sqrt(std::min(1.0, h)));
}

// Lower case, accents gone: "Autódromo" is found by "autodromo"
QString fold(const QString &s)
{
    QString d = s.normalized(QString::NormalizationForm_D).toLower();
    d.removeIf([](QChar c) { return c.category() == QChar::Mark_NonSpacing; });
    return d;
}

QJsonObject readJson(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool insideRing(const QVariantList &ring, double lat, double lon)
{
    bool in = false;
    for (int i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const QVariantList a = ring.at(i).toList(), b = ring.at(j).toList();
        const double ay = a.value(0).toDouble(), ax = a.value(1).toDouble();
        const double by = b.value(0).toDouble(), bx = b.value(1).toDouble();
        if ((ay > lat) != (by > lat) && lon < (bx - ax) * (lat - ay) / (by - ay) + ax)
            in = !in;
    }
    return in;
}
}

Tracks::Tracks(QObject *parent)
    : QObject(parent)
{
    load();
    const QString saved = loadSettings(QStringLiteral("track")).value(QStringLiteral("id")).toString();
    if (!saved.isEmpty())
        select(saved);
    refilter();
}

QString Tracks::dir()
{
    return qEnvironmentVariable("GODWIT_TRACKS_DIR", QStringLiteral("/usr/share/godwit/tracks"));
}

void Tracks::load()
{
    const QJsonArray tracks = readJson(dir() + QStringLiteral("/index.json")).value(QLatin1String("tracks")).toArray();
    m_index.reserve(tracks.size());
    for (const QJsonValue &v : tracks) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.id = o.value(QLatin1String("id")).toString();
        e.name = o.value(QLatin1String("name")).toString();
        e.place = o.value(QLatin1String("place")).toString();
        e.state = o.value(QLatin1String("state")).toString();
        e.country = o.value(QLatin1String("country")).toString();
        e.type = o.value(QLatin1String("type")).toString();
        e.length = qRound(o.value(QLatin1String("length_m")).toDouble());
        const QJsonArray c = o.value(QLatin1String("center")).toArray();
        e.lat = c.at(0).toDouble();
        e.lon = c.at(1).toDouble();
        e.search = fold(e.name + QLatin1Char(' ') + e.place + QLatin1Char(' ') + e.state);
        m_index.append(e);
    }
    if (m_index.isEmpty())
        qCWarning(lcTracks) << "no tracks in" << dir();
    else
        qCInfo(lcTracks) << m_index.size() << "tracks from" << dir();
}

void Tracks::setQuery(const QString &q)
{
    if (q == m_query)
        return;
    m_query = q;
    refilter();
}

void Tracks::setType(const QString &t)
{
    if (t == m_type)
        return;
    m_type = t;
    refilter();
}

void Tracks::setSort(const QString &s)
{
    if (s == m_sort)
        return;
    m_sort = s;
    refilter();
}

void Tracks::refilter()
{
    const QStringList words = fold(m_query).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QVector<QPair<double, const Entry *>> hits;
    for (const Entry &e : std::as_const(m_index)) {
        if (!m_type.isEmpty() && e.type != m_type)
            continue;
        if (!std::all_of(words.begin(), words.end(), [&e](const QString &w) { return e.search.contains(w); }))
            continue;
        hits.append({ m_fix ? distanceM(m_lat, m_lon, e.lat, e.lon) : -1, &e });
    }
    const bool nearest = m_sort == QLatin1String("nearest") && m_fix;
    std::sort(hits.begin(), hits.end(), [&](const auto &a, const auto &b) {
        if (nearest)
            return a.first < b.first;
        if (m_sort == QLatin1String("length"))
            return a.second->length > b.second->length;
        return QString::localeAwareCompare(a.second->name, b.second->name) < 0;
    });
    QVariantList out;
    for (int i = 0; i < hits.size() && i < MaxResults; ++i) {
        const Entry &e = *hits.at(i).second;
        out.append(QVariantMap {
            { "id", e.id }, { "name", e.name }, { "place", e.place }, { "state", e.state },
            { "country", e.country }, { "type", e.type }, { "length_m", e.length },
            { "mi", hits.at(i).first < 0 ? -1.0 : hits.at(i).first / MetresPerMile },
        });
    }
    m_results = out;
    emit resultsChanged();
}

QVariantMap Tracks::detail(const QString &id) const
{
    if (id.isEmpty() || id.contains(QLatin1Char('/')))
        return {};
    return readJson(dir() + QStringLiteral("/tracks/") + id + QStringLiteral(".json")).toVariantMap();
}

void Tracks::select(const QString &id)
{
    m_detail = detail(id);
    if (!id.isEmpty() && m_detail.isEmpty())
        qCWarning(lcTracks) << "no track" << id;
    saveSettings(QStringLiteral("track"), { { QStringLiteral("id"), m_detail.isEmpty() ? QString() : id } });
    buildCurrent();
    setPosition(m_lat, m_lon, m_fix);
}

// The chosen track ready to draw: metres, north up, origin at the top left
void Tracks::buildCurrent()
{
    m_current.clear();
    if (m_detail.isEmpty()) {
        emit currentChanged();
        return;
    }
    const QVariantList c = m_detail.value("center").toList();
    const double lat0 = c.value(0).toDouble(), lon0 = c.value(1).toDouble();
    const double kx = 111320.0 * std::cos(lat0 * M_PI / 180), ky = 110540.0;
    double minx = 1e9, miny = 1e9, maxx = -1e9, maxy = -1e9;
    auto project = [&](const QVariantList &line) {
        QVector<double> xy;
        for (const QVariant &p : line) {
            const QVariantList ll = p.toList();
            const double x = (ll.value(1).toDouble() - lon0) * kx, y = -(ll.value(0).toDouble() - lat0) * ky;
            minx = std::min(minx, x); maxx = std::max(maxx, x);
            miny = std::min(miny, y); maxy = std::max(maxy, y);
            xy << x << y;
        }
        return xy;
    };
    QList<QVector<double>> lines, pits;
    for (const QVariant &l : m_detail.value("outline").toList())
        lines << project(l.toList());
    for (const QVariant &l : m_detail.value("pit").toList())
        pits << project(l.toList());
    QVector<double> loop = project(m_detail.value("loop").toList());

    auto shifted = [&](const QVector<double> &xy) {
        QVariantList out;
        for (int i = 0; i + 1 < xy.size(); i += 2)
            out << std::round((xy[i] - minx) * 10) / 10 << std::round((xy[i + 1] - miny) * 10) / 10;
        return out;
    };
    auto shiftedLatLon = [&](const QVariantList &ends) {
        QVector<double> xy;
        for (const QVariant &p : ends) {
            const QVariantList ll = p.toList();
            xy << (ll.value(1).toDouble() - lon0) * kx << -(ll.value(0).toDouble() - lat0) * ky;
        }
        return shifted(xy);
    };
    QVariantMap shape { { "w", maxx - minx }, { "h", maxy - miny } };
    QVariantList L, P;
    for (const auto &l : std::as_const(lines)) L << QVariant(shifted(l));
    for (const auto &l : std::as_const(pits)) P << QVariant(shifted(l));
    shape["lines"] = L;
    shape["pit"] = P;

    double loopM = 0;
    if (m_detail.contains("gate")) {
        const QVariantList ends = m_detail.value("gate").toMap().value("ends").toList();
        shape["gate"] = shiftedLatLon(ends);
        // Sector markers a third and two thirds of the way round from the gate
        if (loop.size() >= 6) {
            QVector<double> cum { 0 };
            for (int i = 2; i + 1 < loop.size(); i += 2)
                cum << cum.last() + std::hypot(loop[i] - loop[i - 2], loop[i + 1] - loop[i - 1]);
            loopM = cum.last();
            const QVariantList g = shape["gate"].toList();
            const double gx = (g.value(0).toDouble() + g.value(2).toDouble()) / 2 + minx;
            const double gy = (g.value(1).toDouble() + g.value(3).toDouble()) / 2 + miny;
            // How far round the loop the gate is: projected onto the nearest
            // segment, not the nearest stored point (a straight is one segment)
            const int n = int(cum.size());
            double startM = 0, best = 1e18;
            for (int s = 0; s + 1 < n; ++s) {
                const double ax = loop[2 * s], ay = loop[2 * s + 1], bx = loop[2 * s + 2], by = loop[2 * s + 3];
                const double vx = bx - ax, vy = by - ay, vv = vx * vx + vy * vy;
                const double u = vv > 0 ? std::clamp(((gx - ax) * vx + (gy - ay) * vy) / vv, 0.0, 1.0) : 0;
                const double d = std::hypot(ax + u * vx - gx, ay + u * vy - gy);
                if (d < best) { best = d; startM = cum[s] + u * std::sqrt(vv); }
            }
            QVariantList sectors, sectorGates;
            for (double frac : { 1.0 / 3, 2.0 / 3 }) {
                // The point that far round, between the stored points either side
                const double want = std::fmod(startM + frac * loopM, loopM);
                int s = int(std::upper_bound(cum.begin(), cum.end(), want) - cum.begin()) - 1;
                s = std::clamp(s, 0, n - 2);
                const double segM = cum[s + 1] - cum[s], u = segM > 0 ? (want - cum[s]) / segM : 0;
                const double x = loop[2 * s] + u * (loop[2 * s + 2] - loop[2 * s]);
                const double y = loop[2 * s + 1] + u * (loop[2 * s + 3] - loop[2 * s + 1]);
                sectors << QVariant(QVariantList { std::round((x - minx) * 10) / 10, std::round((y - miny) * 10) / 10 });
                // A timing line across that segment, like the start/finish line
                const double dx = loop[2 * s + 2] - loop[2 * s], dy = loop[2 * s + 3] - loop[2 * s + 1];
                const double len = std::hypot(dx, dy) > 0 ? std::hypot(dx, dy) : 1;
                const double px = -dy / len * SectorHalfM, py = dx / len * SectorHalfM;
                auto ll = [&](double gx, double gy) { return QVariant(QVariantList { lat0 - gy / ky, lon0 + gx / kx }); };
                sectorGates << QVariant(QVariantList { ll(x + px, y + py), ll(x - px, y - py) });
            }
            shape["sectors"] = sectors;
            m_detail["sector_gates"] = sectorGates;

            // The loop in three pieces from the start/finish line, to colour
            // each sector on the drawing as the lap goes
            auto pointAt = [&](double m, int &seg) {
                m = std::fmod(std::fmod(m, loopM) + loopM, loopM);
                seg = std::clamp(int(std::upper_bound(cum.begin(), cum.end(), m) - cum.begin()) - 1, 0, n - 2);
                const double segM = cum[seg + 1] - cum[seg], u = segM > 0 ? (m - cum[seg]) / segM : 0;
                return std::pair { loop[2 * seg] + u * (loop[2 * seg + 2] - loop[2 * seg]),
                                   loop[2 * seg + 1] + u * (loop[2 * seg + 3] - loop[2 * seg + 1]) };
            };
            QVariantList loopSectors;
            for (int part = 0; part < 3; ++part) {
                const double from = startM + part * loopM / 3, to = startM + (part + 1) * loopM / 3;
                QVariantList xy;
                int seg = 0;
                auto add = [&](double x, double y) { xy << std::round((x - minx) * 10) / 10 << std::round((y - miny) * 10) / 10; };
                auto [x0, y0] = pointAt(from, seg);
                add(x0, y0);
                // Every stored point strictly between, walking round the loop
                for (double m = from; ; ) {
                    const double into = std::fmod(m, loopM);
                    int s = std::clamp(int(std::upper_bound(cum.begin(), cum.end(), into) - cum.begin()), 1, n - 1);
                    double next = m + (cum[s] - into);
                    if (cum[s] - into <= 1e-6)
                        next = m + (s + 1 < n ? cum[s + 1] - cum[s] : 1e-3);
                    if (next >= to)
                        break;
                    add(loop[2 * s], loop[2 * s + 1]);
                    m = next;
                }
                auto [x1, y1] = pointAt(to, seg);
                add(x1, y1);
                loopSectors << QVariant(xy);
            }
            shape["loopSectors"] = loopSectors;
        }
    } else if (m_detail.contains("drag")) {
        QVariantList starts;
        for (const QVariant &s : m_detail.value("drag").toMap().value("starts").toList())
            starts << QVariant(shiftedLatLon(s.toMap().value("gate").toList()));
        shape["starts"] = starts;
    }

    m_current = m_detail;
    m_current.remove("outline");
    m_current.remove("pit");
    m_current.remove("loop");
    m_current.remove("geofence");
    m_current["shape"] = shape;
    m_current["loop_m"] = loopM;
    m_current["origin"] = QVariantList { lat0, lon0, minx, miny, kx, ky };
    emit currentChanged();
}

const QVariantList &Tracks::fence(const QString &id) const
{
    auto it = m_fences.find(id);
    if (it == m_fences.end())
        it = m_fences.insert(id, detail(id).value("geofence").toList());
    return *it;
}

QString Tracks::findHere() const
{
    if (!m_fix)
        return {};
    for (const Entry &e : m_index) {
        if (distanceM(m_lat, m_lon, e.lat, e.lon) > FenceSearchKm * 1000)
            continue;
        if (insideRing(fence(e.id), m_lat, m_lon))
            return e.id;
    }
    return {};
}

void Tracks::setPosition(double lat, double lon, bool fix)
{
    const bool resort = fix && m_sort == QLatin1String("nearest")
        && (!m_fix || distanceM(m_lat, m_lon, lat, lon) > ResortMetres);
    const bool wasFix = m_fix;
    m_fix = fix;
    if (fix) {
        m_lat = lat;
        m_lon = lon;
        m_seen = true;
    }

    // Without a fix the car stays where it was last seen; the drawing shows
    // it hollow so the driver can tell the position is stale
    QVariantList car;
    const QVariantList o = m_current.value("origin").toList();
    if (m_seen && o.size() == 6) {
        const QVariantMap shape = m_current.value("shape").toMap();
        const double x = (m_lon - o[1].toDouble()) * o[4].toDouble() - o[2].toDouble();
        const double y = -(m_lat - o[0].toDouble()) * o[5].toDouble() - o[3].toDouble();
        if (x > -CarMarginM && y > -CarMarginM && x < shape.value("w").toDouble() + CarMarginM
            && y < shape.value("h").toDouble() + CarMarginM)
            car = { std::round(x * 10) / 10, std::round(y * 10) / 10 };
    }
    if (car != m_car || wasFix != m_fix) {
        m_car = car;
        emit carChanged();
    }

    const QString here = findHere();
    if (here != m_here) {
        m_here = here;
        emit hereChanged();
        if (!here.isEmpty()) {
            const auto e = std::find_if(m_index.begin(), m_index.end(), [&](const Entry &x) { return x.id == here; });
            qCInfo(lcTracks) << "arrived at" << here;
            emit arrived(here, e != m_index.end() ? e->name : here);
        }
    }
    if (resort)
        refilter();
}
