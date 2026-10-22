#include "places.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrlQuery>

#include <cmath>

Q_LOGGING_CATEGORY(lcPlaces, "godwit.places")

namespace {

constexpr int Limit = 8;

double miles(double lat1, double lon1, double lat2, double lon2)
{
    constexpr double R = 3958.8, rad = M_PI / 180;
    const double dLat = (lat2 - lat1) * rad, dLon = (lon2 - lon1) * rad;
    const double h = std::sin(dLat / 2) * std::sin(dLat / 2)
        + std::cos(lat1 * rad) * std::cos(lat2 * rad) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2 * R * std::asin(std::sqrt(h));
}

// The list's icon for an OpenStreetMap key=value
QString iconFor(const QString &key, const QString &value)
{
    if (value == QLatin1String("camp_site") || value == QLatin1String("caravan_site"))
        return QStringLiteral("bonfire-outline");
    if (value == QLatin1String("national_park") || value == QLatin1String("park")
        || value == QLatin1String("nature_reserve") || value == QLatin1String("protected_area"))
        return QStringLiteral("leaf-outline");
    if (value == QLatin1String("cafe"))
        return QStringLiteral("cafe-outline");
    if (value == QLatin1String("restaurant") || value == QLatin1String("fast_food")
        || value == QLatin1String("pub") || value == QLatin1String("bar"))
        return QStringLiteral("restaurant-outline");
    if (value == QLatin1String("fuel") || value == QLatin1String("charging_station"))
        return QStringLiteral("car");
    if (value == QLatin1String("drinking_water") || value == QLatin1String("water_point"))
        return QStringLiteral("water");
    if (key == QLatin1String("shop"))
        return QStringLiteral("storefront-outline");
    if (key == QLatin1String("place") || key == QLatin1String("boundary"))
        return QStringLiteral("flag-outline");
    return QStringLiteral("location-outline");
}

// "Main St · Moab, UT": street, then the town and state, as space allows
QString addressOf(const QJsonObject &p)
{
    QStringList street;
    if (p.contains(QLatin1String("street")))
        street << (p.value(QLatin1String("housenumber")).toString() + QLatin1Char(' ')
                   + p.value(QLatin1String("street")).toString()).trimmed();
    QStringList area;
    for (const char *k : { "city", "county", "state" }) {
        const QString v = p.value(QLatin1String(k)).toString();
        if (!v.isEmpty() && v != p.value(QLatin1String("name")).toString() && !area.contains(v))
            area << v;
        if (area.size() == 2)
            break;
    }
    if (area.isEmpty())
        area << p.value(QLatin1String("country")).toString();
    street << area.join(QStringLiteral(", "));
    street.removeAll(QString());
    return street.join(QStringLiteral(" · "));
}

} // namespace

Places::Places(QObject *parent)
    : QObject(parent)
    , m_base(qEnvironmentVariable("GODWIT_PHOTON_URL", QStringLiteral("http://127.0.0.1:2322")))
    , m_net(new QNetworkAccessManager(this))
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, &Places::send);
}

void Places::search(const QString &query, double lat, double lon)
{
    m_query = query.trimmed();
    m_lat = lat;
    m_lon = lon;
    if (m_query.isEmpty()) {
        m_timer.stop();
        if (m_reply)
            m_reply->abort();
        if (!m_results.isEmpty()) {
            m_results.clear();
            emit resultsChanged();
        }
        return;
    }
    m_timer.start();
}

void Places::send()
{
    if (m_reply)
        m_reply->abort();
    QUrl url(m_base + QStringLiteral("/api"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), m_query);
    q.addQueryItem(QStringLiteral("limit"), QString::number(Limit));
    q.addQueryItem(QStringLiteral("lat"), QString::number(m_lat, 'f', 6));
    q.addQueryItem(QStringLiteral("lon"), QString::number(m_lon, 'f', 6));
    q.addQueryItem(QStringLiteral("lang"), QStringLiteral("en"));
    url.setQuery(q);
    QNetworkRequest req(url);
    req.setTransferTimeout(8000);
    m_reply = m_net->get(req);
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finished(reply); });
    emit busyChanged();
}

void Places::finished(QNetworkReply *reply)
{
    reply->deleteLater();
    const bool current = reply == m_reply;
    if (current)
        m_reply = nullptr;
    if (reply->error() == QNetworkReply::OperationCanceledError) {
        if (current)
            emit busyChanged();
        return;
    }
    if (!current)
        return;
    emit busyChanged();

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(lcPlaces) << "search failed:" << reply->errorString();
        setAvailable(false);
        m_results.clear();
        emit resultsChanged();
        return;
    }
    setAvailable(true);

    QVariantList rows;
    const QJsonArray features = QJsonDocument::fromJson(reply->readAll()).object().value(QLatin1String("features")).toArray();
    for (const QJsonValue &f : features) {
        const QJsonObject p = f.toObject().value(QLatin1String("properties")).toObject();
        const QJsonArray c = f.toObject().value(QLatin1String("geometry")).toObject().value(QLatin1String("coordinates")).toArray();
        if (c.size() != 2)
            continue;
        const double lon = c.at(0).toDouble(), lat = c.at(1).toDouble();
        QString name = p.value(QLatin1String("name")).toString();
        if (name.isEmpty())
            name = (p.value(QLatin1String("housenumber")).toString() + QLatin1Char(' ')
                    + p.value(QLatin1String("street")).toString()).trimmed();
        if (name.isEmpty())
            continue;
        rows << QVariantMap{
            { QStringLiteral("n"), name },
            { QStringLiteral("a"), addressOf(p) },
            { QStringLiteral("icon"), iconFor(p.value(QLatin1String("osm_key")).toString(),
                                              p.value(QLatin1String("osm_value")).toString()) },
            { QStringLiteral("mi"), std::round(miles(m_lat, m_lon, lat, lon) * 10) / 10 },
            { QStringLiteral("lat"), lat },
            { QStringLiteral("lon"), lon },
        };
    }
    m_results = rows;
    emit resultsChanged();
}

void Places::setAvailable(bool available)
{
    if (m_available == available)
        return;
    m_available = available;
    emit availableChanged();
}
