#include "solar.h"

#include "music.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QTimeZone>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcSolar, "godwit.solar")

namespace {

constexpr double Deg = M_PI / 180.0;

// The smoothed ratio follows solar watts with this time constant, so a
// passing cloud shadow or a tree beside the road does not flip the sky
constexpr double SmoothSeconds = 120.0;
// Below this sun height the panels' angle and the horizon matter more than
// cloud, so cloud is not judged
constexpr double JudgeAbove = 8.0;
// Losses between the panels' label and the charger: heat, wiring, MPPT
constexpr double Derate = 0.8;
// Watts are added up over gaps no longer than this; a longer silence from
// the charger is not counted as harvest
constexpr qint64 MaxGapMs = 30000;
constexpr qint64 SaveEveryMs = 5 * 60 * 1000;
// Sun at sunrise and sunset: refraction and the sun's radius
constexpr double SunHorizon = -0.833;
// Moon: refraction, less the moon's radius
constexpr double MoonHorizon = -0.3;

double wrap360(double a)
{
    a = std::fmod(a, 360.0);
    return a < 0 ? a + 360.0 : a;
}

// Days since J2000.0
double daysJ2000(const QDateTime &t)
{
    return t.toMSecsSinceEpoch() / 86400000.0 + 2440587.5 - 2451545.0;
}

double obliquity(double n) { return 23.439 - 0.0000004 * n; }

// The sun's ecliptic longitude (degrees)
double sunLongitude(double n)
{
    const double L = wrap360(280.460 + 0.9856474 * n);
    const double g = wrap360(357.528 + 0.9856003 * n) * Deg;
    return wrap360(L + 1.915 * std::sin(g) + 0.020 * std::sin(2 * g));
}

// Right ascension and declination (degrees) of an ecliptic direction
void toEquatorial(double lonDeg, double latDeg, double n, double &ra, double &dec)
{
    const double e = obliquity(n) * Deg, l = lonDeg * Deg, b = latDeg * Deg;
    ra = wrap360(std::atan2(std::sin(l) * std::cos(e) - std::tan(b) * std::sin(e), std::cos(l)) / Deg);
    dec = std::asin(std::sin(b) * std::cos(e) + std::cos(b) * std::sin(e) * std::sin(l)) / Deg;
}

Solar::Position horizontal(double ra, double dec, double n, double lat, double lon)
{
    const double gmst = wrap360(280.46061837 + 360.98564736629 * n);
    const double ha = wrap360(gmst + lon - ra) * Deg;
    const double phi = lat * Deg, d = dec * Deg;
    const double el = std::asin(std::sin(phi) * std::sin(d) + std::cos(phi) * std::cos(d) * std::cos(ha));
    const double az = std::atan2(std::sin(ha), std::cos(ha) * std::sin(phi) - std::tan(d) * std::cos(phi));
    return { el / Deg, wrap360(az / Deg + 180.0) };
}

// The moon's geocentric ecliptic longitude and latitude (degrees) and
// distance (Earth radii): the mean orbit plus its largest perturbations,
// good to a fraction of a degree
void moonEcliptic(double n, double &lon, double &lat, double &dist)
{
    const double d = n + 1.5;   // days from 2000 Jan 0.0
    const double N = wrap360(125.1228 - 0.0529538083 * d) * Deg;
    const double i = 5.1454 * Deg;
    const double w = wrap360(318.0634 + 0.1643573223 * d) * Deg;
    const double a = 60.2666, e = 0.054900;
    const double M = wrap360(115.3654 + 13.0649929509 * d) * Deg;
    double E = M + e * std::sin(M) * (1.0 + e * std::cos(M));
    for (int k = 0; k < 5; ++k)
        E = E - (E - e * std::sin(E) - M) / (1 - e * std::cos(E));
    const double xv = a * (std::cos(E) - e), yv = a * std::sqrt(1 - e * e) * std::sin(E);
    const double v = std::atan2(yv, xv), r = std::hypot(xv, yv);
    const double xh = r * (std::cos(N) * std::cos(v + w) - std::sin(N) * std::sin(v + w) * std::cos(i));
    const double yh = r * (std::sin(N) * std::cos(v + w) + std::cos(N) * std::sin(v + w) * std::cos(i));
    const double zh = r * std::sin(v + w) * std::sin(i);
    lon = std::atan2(yh, xh) / Deg;
    lat = std::atan2(zh, std::hypot(xh, yh)) / Deg;
    dist = r;

    const double Ms = wrap360(356.0470 + 0.9856002585 * d) * Deg;
    const double ws = wrap360(282.9404 + 4.70935e-5 * d) * Deg;
    const double Ls = Ms + ws, Lm = M + w + N;
    const double D = Lm - Ls, F = Lm - N;
    lon += -1.274 * std::sin(M - 2 * D) + 0.658 * std::sin(2 * D) - 0.186 * std::sin(Ms)
         - 0.059 * std::sin(2 * M - 2 * D) - 0.057 * std::sin(M - 2 * D + Ms)
         + 0.053 * std::sin(M + 2 * D) + 0.046 * std::sin(2 * D - Ms) + 0.041 * std::sin(M - Ms)
         - 0.035 * std::sin(D) - 0.031 * std::sin(M + Ms);
    lat += -0.173 * std::sin(F - 2 * D) - 0.055 * std::sin(M - F - 2 * D) - 0.046 * std::sin(M + F - 2 * D);
    dist += -0.58 * std::cos(M - 2 * D) - 0.46 * std::cos(2 * D);
    lon = wrap360(lon);
}

QString statePath() { return Music::stateDir() + QStringLiteral("/solar.json"); }

} // namespace

// ----------------------------------------------------------------- astronomy

Solar::Position Solar::sunAt(const QDateTime &t, double lat, double lon)
{
    const double n = daysJ2000(t);
    double ra, dec;
    toEquatorial(sunLongitude(n), 0, n, ra, dec);
    return horizontal(ra, dec, n, lat, lon);
}

Solar::Position Solar::moonAt(const QDateTime &t, double lat, double lon)
{
    const double n = daysJ2000(t);
    double l, b, r, ra, dec;
    moonEcliptic(n, l, b, r);
    toEquatorial(l, b, n, ra, dec);
    Position p = horizontal(ra, dec, n, lat, lon);
    // Seen from the ground, not the Earth's centre: up to a degree lower
    p.elevation -= std::asin(1.0 / r) / Deg * std::cos(p.elevation * Deg);
    return p;
}

double Solar::moonAge(const QDateTime &t)
{
    const double n = daysJ2000(t);
    double l, b, r;
    moonEcliptic(n, l, b, r);
    return wrap360(l - sunLongitude(n));
}

QDateTime Solar::crossing(bool moon, const QDateTime &from, const QDateTime &to,
                          double lat, double lon, double altitude, bool rising)
{
    auto above = [&](const QDateTime &t) {
        return (moon ? moonAt(t, lat, lon) : sunAt(t, lat, lon)).elevation - altitude;
    };
    const qint64 step = 10 * 60;   // seconds; nothing crosses twice in ten minutes
    QDateTime a = from;
    double fa = above(a);
    while (a < to) {
        QDateTime b = a.addSecs(step);
        const double fb = above(b);
        if ((rising && fa < 0 && fb >= 0) || (!rising && fa >= 0 && fb < 0)) {
            for (int k = 0; k < 12; ++k) {   // to within a second or so
                const QDateTime m = a.addMSecs(a.msecsTo(b) / 2);
                const double fm = above(m);
                if ((fm < 0) == (fa < 0)) { a = m; fa = fm; } else { b = m; }
            }
            return b.addMSecs(-b.time().msec());
        }
        a = b;
        fa = fb;
    }
    return {};
}

double Solar::clearSky(double elev, int arrayW)
{
    if (arrayW <= 0 || elev <= 0)
        return 0;
    // Haurwitz's clear-sky irradiance on flat ground, W/m²; a roof array lies flat
    const double s = std::sin(elev * Deg);
    const double ghi = 1098.0 * s * std::exp(-0.057 / s);
    return arrayW * ghi / 1000.0 * Derate;
}

QString Solar::moonPhaseName() const
{
    static const char *const names[] = { "New Moon", "Waxing Crescent", "First Quarter", "Waxing Gibbous",
                                         "Full Moon", "Waning Gibbous", "Last Quarter", "Waning Crescent" };
    return QString::fromLatin1(names[std::clamp(m_phase, 0, 7)]);
}

// ----------------------------------------------------------------- the object

Solar::Solar(QObject *parent)
    : QObject(parent)
{
    if (qEnvironmentVariableIsSet("GODWIT_SOLAR_AT"))
        m_fixed = QDateTime::fromString(qEnvironmentVariable("GODWIT_SOLAR_AT"), Qt::ISODate);
    load();
    m_timer.setInterval(30000);
    connect(&m_timer, &QTimer::timeout, this, &Solar::update);
    m_timer.start();
    update();
}

Solar::~Solar()
{
    if (m_unsaved)
        save();
}

void Solar::update() { updateAt(m_fixed.isValid() ? m_fixed.toUTC() : QDateTime::currentDateTimeUtc()); }

void Solar::updateAt(const QDateTime &now)
{
    m_now = now;
    // Today's total starts again at local midnight
    const QDate today = now.toLocalTime().date();
    if (m_today.isValid() && today != m_today) {
        m_todayWh = 0;
        m_unsaved = true;
        emit solarChanged();
    }
    m_today = today;
    if (m_unsaved && now.toMSecsSinceEpoch() - m_lastSave > SaveEveryMs)
        save();

    m_phase = int(std::lround(moonAge(now) / 45.0)) % 8;
    if (m_located) {
        const Position s = sunAt(now, m_lat, m_lon), m = moonAt(now, m_lat, m_lon);
        m_sunEl = s.elevation;
        m_sunAz = s.azimuth;
        m_moonEl = m.elevation;
        m_moonAz = m.azimuth;
        const QDateTime midnight = QDateTime(today, QTime(0, 0)).toUTC();
        m_sunrise = crossing(false, midnight, midnight.addSecs(86400), m_lat, m_lon, SunHorizon, true);
        m_sunset = crossing(false, midnight, midnight.addSecs(86400), m_lat, m_lon, SunHorizon, false);
        m_nextSunrise = crossing(false, now, now.addSecs(2 * 86400), m_lat, m_lon, SunHorizon, true);
        m_nextMoonrise = m_moonEl > MoonHorizon ? QDateTime()
                       : crossing(true, now, now.addSecs(36 * 3600), m_lat, m_lon, MoonHorizon, true);
    }
    emit skyChanged();
}

void Solar::setPosition(double lat, double lon)
{
    if (!std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat) > 90 || std::abs(lon) > 180)
        return;
    // About a kilometre: plenty for the sky, and no more than that is kept
    lat = std::round(lat * 100) / 100;
    lon = std::round(lon * 100) / 100;
    if (m_located && std::abs(lat - m_lat) < 0.05 && std::abs(lon - m_lon) < 0.05)
        return;
    const bool first = !m_located;
    m_located = true;
    m_lat = lat;
    m_lon = lon;
    save();
    if (first)
        qCInfo(lcSolar) << "position known; the sky follows it";
    update();
}

void Solar::feed(double watts, const QString &charge) { feedAt(watts, charge, QDateTime::currentMSecsSinceEpoch()); }

void Solar::feedAt(double watts, const QString &charge, qint64 ms)
{
    watts = std::max(0.0, watts);
    // Solar Today
    if (m_lastFeed > 0 && ms > m_lastFeed && ms - m_lastFeed <= MaxGapMs) {
        m_todayWh += watts * (ms - m_lastFeed) / 3600000.0;
        m_unsaved = true;
    }
    const double dt = m_lastFeed > 0 ? std::clamp((ms - m_lastFeed) / 1000.0, 0.0, 60.0) : 0.0;
    m_lastFeed = ms;

    // Cloud: only from Bulk, with the sun high enough and the array size known
    const bool throttled = charge == QLatin1String("Absorption") || charge == QLatin1String("Float");
    const double clear = clearSky(m_sunEl, m_arrayW);
    if (charge == QLatin1String("Bulk") && m_located && m_sunEl >= JudgeAbove && clear > 0) {
        const double r = watts / clear;
        m_ratio = m_ratio < 0 ? r : m_ratio + (r - m_ratio) * (1.0 - std::exp(-dt / SmoothSeconds));
    } else if (!throttled && (m_sunEl < JudgeAbove || m_arrayW <= 0 || !m_located)) {
        m_ratio = -1;   // cannot be judged; Absorption and Float keep the last judgement
    }
    m_throttled = throttled;
    m_cloud = m_ratio < 0 ? -1 : std::clamp((0.75 - m_ratio) / 0.5, 0.0, 1.0);

    // Named with hysteresis so the label does not flicker at a boundary
    QString c = m_condition;
    if (m_cloud < 0)
        c.clear();
    else if (c.isEmpty())
        c = m_cloud < 0.25 ? QStringLiteral("sun") : m_cloud < 0.75 ? QStringLiteral("partly") : QStringLiteral("cloudy");
    else if (c == QLatin1String("sun") && m_cloud > 0.33)
        c = m_cloud < 0.83 ? QStringLiteral("partly") : QStringLiteral("cloudy");
    else if (c == QLatin1String("partly") && m_cloud < 0.17)
        c = QStringLiteral("sun");
    else if (c == QLatin1String("partly") && m_cloud > 0.83)
        c = QStringLiteral("cloudy");
    else if (c == QLatin1String("cloudy") && m_cloud < 0.67)
        c = m_cloud < 0.17 ? QStringLiteral("sun") : QStringLiteral("partly");
    m_condition = c;
    emit solarChanged();
}

void Solar::resetToday()
{
    m_todayWh = 0;
    m_unsaved = true;
    save();
    emit solarChanged();
}

void Solar::setArrayWatts(int w)
{
    w = std::clamp(w, 0, 10000);
    if (w == m_arrayW)
        return;
    m_arrayW = w;
    m_ratio = -1;
    save();
    emit setupChanged();
    emit skyChanged();
}

void Solar::setBatteryAh(int ah)
{
    ah = std::clamp(ah, 0, 5000);
    if (ah == m_batteryAh)
        return;
    m_batteryAh = ah;
    save();
    emit setupChanged();
}

void Solar::load()
{
    QFile f(statePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m_arrayW = o.value("arrayW").toInt();
    m_batteryAh = o.value("batteryAh").toInt();
    if (o.contains("lat") && o.contains("lon")) {
        m_located = true;
        m_lat = o.value("lat").toDouble();
        m_lon = o.value("lon").toDouble();
    }
    if (QDate::fromString(o.value("day").toString(), Qt::ISODate) == QDate::currentDate())
        m_todayWh = o.value("todayWh").toDouble();
    m_today = QDate::currentDate();
}

void Solar::save()
{
    QJsonObject o;
    o["arrayW"] = m_arrayW;
    o["batteryAh"] = m_batteryAh;
    if (m_located) {
        o["lat"] = m_lat;
        o["lon"] = m_lon;
    }
    o["day"] = (m_today.isValid() ? m_today : QDate::currentDate()).toString(Qt::ISODate);
    o["todayWh"] = std::round(m_todayWh * 10) / 10;
    if (Music::stateDir().isEmpty())
        return;
    QDir().mkpath(Music::stateDir());
    QSaveFile f(statePath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson());
        if (f.commit()) {
            m_lastSave = QDateTime::currentMSecsSinceEpoch();
            m_unsaved = false;
            return;
        }
    }
    qCWarning(lcSolar) << "could not save" << statePath();
}
