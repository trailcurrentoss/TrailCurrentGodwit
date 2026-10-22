#include "router.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include <cmath>
#include <memory>

#ifdef GODWIT_HAVE_VALHALLA
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <valhalla/tyr/actor.h>
#endif

Q_LOGGING_CATEGORY(lcRoute, "godwit.route")

namespace {

QString configPath()
{
    return qEnvironmentVariable("GODWIT_VALHALLA_CONFIG", QStringLiteral("/etc/valhalla/valhalla.json"));
}

// Valhalla's DirectionsLeg.Maneuver.Type to a DemoData.DIRS key
QString dirFor(int type)
{
    switch (type) {
    case 1: case 2: case 3: return QStringLiteral("depart");
    case 4: case 5: case 6: return QStringLiteral("arrive");
    case 9: case 23: case 37: return QStringLiteral("slight");
    case 16: case 24: case 38: return QStringLiteral("slightL");
    case 10: return QStringLiteral("right");
    case 15: return QStringLiteral("left");
    case 11: return QStringLiteral("sharpR");
    case 14: return QStringLiteral("sharpL");
    case 12: case 13: return QStringLiteral("uturn");
    case 18: case 20: return QStringLiteral("exitR");
    case 19: case 21: return QStringLiteral("exitL");
    case 25: return QStringLiteral("merge");
    case 26: case 27: return QStringLiteral("round");
    case 28: case 29: return QStringLiteral("ferry");
    default: return QStringLiteral("straight");
    }
}

// Valhalla's shapes: Google's polyline encoding at 6 digits
QVariantList decodeShape(const QByteArray &s)
{
    QVariantList out;
    int lat = 0, lon = 0;
    qsizetype i = 0;
    auto next = [&](int &v) {
        int shift = 0, result = 0, b;
        do {
            if (i >= s.size())
                return false;
            b = s.at(i++) - 63;
            result |= (b & 0x1f) << shift;
            shift += 5;
        } while (b >= 0x20);
        v += (result & 1) ? ~(result >> 1) : (result >> 1);
        return true;
    };
    while (i < s.size()) {
        if (!next(lat) || !next(lon))
            break;
        out << QVariant(QVariantList{ lon / 1e6, lat / 1e6 });
    }
    return out;
}

QString roadOf(const QJsonObject &m)
{
    for (const char *k : { "street_names", "begin_street_names" }) {
        const QJsonArray names = m.value(QLatin1String(k)).toArray();
        if (!names.isEmpty())
            return names.first().toString();
    }
    return {};
}

// Valhalla's trip to the HMI's route
QVariantMap toRoute(const QJsonObject &trip)
{
    const QJsonObject leg = trip.value(QLatin1String("legs")).toArray().first().toObject();
    const QJsonArray man = leg.value(QLatin1String("maneuvers")).toArray();
    QVariantList steps;
    // Step k is maneuver k, reached after maneuver k-1's length
    for (qsizetype k = 1; k < man.size(); ++k) {
        const QJsonObject m = man.at(k).toObject(), prev = man.at(k - 1).toObject();
        const int type = m.value(QLatin1String("type")).toInt();
        QString st = roadOf(m);
        if (type == 26 && m.contains(QLatin1String("roundabout_exit_count")))
            st = QStringLiteral("Exit %1").arg(m.value(QLatin1String("roundabout_exit_count")).toInt())
                + (st.isEmpty() ? QString() : QStringLiteral(" · ") + st);
        steps << QVariantMap{
            { QStringLiteral("dir"), dirFor(type) },
            { QStringLiteral("st"), st },
            { QStringLiteral("d"), std::round(prev.value(QLatin1String("length")).toDouble() * 100) / 100 },
            { QStringLiteral("t"), prev.value(QLatin1String("time")).toDouble() / 60 },
            { QStringLiteral("i"), m.value(QLatin1String("instruction")).toString() },
        };
    }
    const QJsonObject sum = trip.value(QLatin1String("summary")).toObject();
    return {
        { QStringLiteral("mi"), sum.value(QLatin1String("length")).toDouble() },
        { QStringLiteral("mins"), sum.value(QLatin1String("time")).toDouble() / 60 },
        { QStringLiteral("steps"), steps },
        { QStringLiteral("shape"), decodeShape(leg.value(QLatin1String("shape")).toString().toLatin1()) },
    };
}

} // namespace

// Owns the Valhalla actor on the router thread. It is made on first use, so
// loading the tile index never delays the HMI's start.
class RouterWorker : public QObject
{
    Q_OBJECT
public:
    void run(int id, const QString &json)
    {
        QVariantMap route;
        QString error;
#ifdef GODWIT_HAVE_VALHALLA
        try {
            if (!m_actor) {
                boost::property_tree::ptree config;
                boost::property_tree::read_json(configPath().toStdString(), config);
                m_actor = std::make_unique<valhalla::tyr::actor_t>(config, true);
            }
            const std::string out = m_actor->route(json.toStdString());
            const QJsonObject trip = QJsonDocument::fromJson(QByteArray::fromStdString(out))
                                         .object().value(QLatin1String("trip")).toObject();
            route = toRoute(trip);
        } catch (const std::exception &e) {
            error = QString::fromUtf8(e.what());
        }
#else
        Q_UNUSED(json)
        error = QStringLiteral("Routing is not built in");
#endif
        emit done(id, route, error);
    }

signals:
    void done(int id, const QVariantMap &route, const QString &error);

private:
#ifdef GODWIT_HAVE_VALHALLA
    std::unique_ptr<valhalla::tyr::actor_t> m_actor;
#endif
};

Router::Router(QObject *parent)
    : QObject(parent)
{
#ifdef GODWIT_HAVE_VALHALLA
    m_available = QFileInfo::exists(configPath());
#endif
    m_worker = new RouterWorker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(this, &Router::request, m_worker, &RouterWorker::run);
    connect(m_worker, &RouterWorker::done, this, &Router::finished);
    m_thread.setObjectName(QStringLiteral("router"));
    m_thread.start(QThread::LowPriority);
}

Router::~Router()
{
    m_thread.quit();
    m_thread.wait();
}

void Router::plan(double fromLat, double fromLon, double toLat, double toLon)
{
    const QJsonObject req{
        { QStringLiteral("locations"), QJsonArray{
              QJsonObject{ { QStringLiteral("lat"), fromLat }, { QStringLiteral("lon"), fromLon } },
              QJsonObject{ { QStringLiteral("lat"), toLat }, { QStringLiteral("lon"), toLon } } } },
        { QStringLiteral("costing"), QStringLiteral("auto") },
        { QStringLiteral("units"), QStringLiteral("miles") },
        { QStringLiteral("directions_options"), QJsonObject{ { QStringLiteral("language"), QStringLiteral("en-US") } } },
    };
    m_route = { { QStringLiteral("lat"), toLat }, { QStringLiteral("lon"), toLon } };
    m_error.clear();
    emit routeChanged();
    m_busy = true;
    emit busyChanged();
    emit request(++m_id, QString::fromUtf8(QJsonDocument(req).toJson(QJsonDocument::Compact)));
}

void Router::clear()
{
    ++m_id;
    m_route.clear();
    m_error.clear();
    emit routeChanged();
    if (m_busy) {
        m_busy = false;
        emit busyChanged();
    }
}

void Router::finished(int id, const QVariantMap &route, const QString &error)
{
    if (id != m_id)
        return;
    if (!error.isEmpty())
        qCWarning(lcRoute) << "no route:" << error;
    const QVariant lat = m_route.value(QStringLiteral("lat")), lon = m_route.value(QStringLiteral("lon"));
    m_route = route;
    if (!route.isEmpty()) {
        m_route.insert(QStringLiteral("lat"), lat);
        m_route.insert(QStringLiteral("lon"), lon);
    }
    m_error = error;
    m_busy = false;
    emit routeChanged();
    emit busyChanged();
}

#include "router.moc"
