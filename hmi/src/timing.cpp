#include "timing.h"
#include "music.h"

#include <QLoggingCategory>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcTiming, "godwit.timing")

namespace {
constexpr double MsPerMph = 0.44704;          // metres a second in one mph
constexpr double LaunchMph = 2.0;             // moving faster than this is a launch
constexpr double AbortMph = 5.0;              // a 0-60 run that slows below this was abandoned
constexpr double ImuLaunchG = 0.15;           // the IMU sees a launch above this
constexpr double MinLineGapMs = 5000;         // the same line crossed again within this is noise
constexpr int SectorHoldMs = 4000;            // a finished lap's sectors stay up this long
constexpr double LevelMs = 200;               // a sector within this of the best lap is "level" (yellow)
constexpr int TreeStepMs = 500;               // a sportsman tree: ambers half a second apart
constexpr int MaxRecent = 10;
// The quarter-mile splits, feet and metres
const double SplitFt[] = { 60, 330, 660, 1000, 1320 };
constexpr double FtToM = 0.3048;

struct XY { double x, y; };

XY local(double lat, double lon, double lat0, double lon0)
{
    return { (lon - lon0) * 111320.0 * std::cos(lat0 * M_PI / 180), (lat - lat0) * 110540.0 };
}

double metres(double lat1, double lon1, double lat2, double lon2)
{
    const XY d = local(lat2, lon2, lat1, lon1);
    return std::hypot(d.x, d.y);
}

// Where the move from A to B crosses LINE ([[lat, lon], [lat, lon]]), as a
// fraction of the move, or -1 if it does not
double crossing(double alat, double alon, double blat, double blon, const QVariantList &line)
{
    if (line.size() != 2)
        return -1;
    const QVariantList e1 = line.at(0).toList(), e2 = line.at(1).toList();
    const double lat0 = e1.value(0).toDouble(), lon0 = e1.value(1).toDouble();
    const XY a = local(alat, alon, lat0, lon0), b = local(blat, blon, lat0, lon0);
    const XY g1 { 0, 0 }, g2 = local(e2.value(0).toDouble(), e2.value(1).toDouble(), lat0, lon0);
    const double rx = b.x - a.x, ry = b.y - a.y, sx = g2.x - g1.x, sy = g2.y - g1.y;
    const double den = rx * sy - ry * sx;
    if (std::fabs(den) < 1e-9)
        return -1;
    const double t = ((g1.x - a.x) * sy - (g1.y - a.y) * sx) / den;
    const double u = ((g1.x - a.x) * ry - (g1.y - a.y) * rx) / den;
    return (t >= 0 && t <= 1 && u >= 0 && u <= 1) ? t : -1;
}

// Seconds after the start of a move of DIST metres in SECS, from speed VA to
// VB (m/s) at steady acceleration, at which it has gone WANT metres. The
// speeds shape the curve; the measured distance scales it.
double timeAtDistance(double va, double vb, double secs, double dist, double want)
{
    const double acc = (vb - va) / secs;
    const double model = va * secs + 0.5 * acc * secs * secs;
    if (model <= 0 || dist <= 0)
        return secs * want / std::max(dist, 1e-6);
    const double k = dist / model;      // measured over modelled
    const double A = 0.5 * acc * k, B = va * k, C = -want;
    if (std::fabs(A) < 1e-9)
        return std::clamp(-C / B, 0.0, secs);
    const double disc = std::max(0.0, B * B - 4 * A * C);
    return std::clamp((-B + std::sqrt(disc)) / (2 * A), 0.0, secs);
}
}

Timing::Timing(QObject *parent)
    : QObject(parent)
{
    m_mono.start();
    m_tick.setInterval(50);
    connect(&m_tick, &QTimer::timeout, this, &Timing::tick);
    m_sectorHold.setSingleShot(true);
    m_sectorHold.setInterval(SectorHoldMs);
    connect(&m_sectorHold, &QTimer::timeout, this, [this] {
        // The new lap starts unjudged until its first sector closes
        m_sectorColors.clear();
        m_ahead = 0;
        emit changed();
    });
    m_treeTimer.setInterval(TreeStepMs);
    connect(&m_treeTimer, &QTimer::timeout, this, [this] {
        if (++m_tree >= 4) {
            m_tree = 4;
            m_green = m_mono.elapsed();
            m_treeTimer.stop();
        }
        emit changed();
    });
    load();
}

QString Timing::key() const
{
    return m_track.isEmpty() ? QStringLiteral("anywhere") : m_track;
}

void Timing::load()
{
    const QVariantMap s = loadSettings(QStringLiteral("timing"));
    m_best = s.value("best").toMap();
    m_recent = s.value("recent").toMap();
}

void Timing::save()
{
    saveSettings(QStringLiteral("timing"), { { "best", m_best }, { "recent", m_recent } });
}

void Timing::setMode(const QString &m)
{
    if (m == m_mode || (m != "laps" && m != "zero60" && m != "quarter"))
        return;
    stop();
    m_mode = m;
    emit changed();
}

void Timing::setState(const QString &s)
{
    m_state = s;
    if (s == "ready" || s == "done")
        m_tick.stop();
    else
        m_tick.start();
    emit changed();
}

void Timing::setTrack(const QString &id, const QVariantList &gate, const QVariantList &sectorGates)
{
    if (id != m_track) {
        stop();
        m_laps.clear();
    }
    m_track = id;
    m_gate = gate;
    m_sectorGates = sectorGates;
    emit changed();
}

void Timing::press()
{
    if (m_state != "ready" && m_state != "done") {
        stop();
        return;
    }
    m_live.clear();
    m_launch = 0;
    m_dist = 0;
    m_gPeak = 0;
    m_lapStart = 0;
    m_lapSectors.clear();
    m_ahead = 0;
    m_sectorColors.clear();
    m_sectorHold.stop();
    if (m_mode == "laps") {
        if (m_gate.isEmpty()) {
            emit finished(QStringLiteral("Choose a road course or oval to time laps"));
            return;
        }
        // The out lap runs from now to the first crossing
        m_sessionStart = m_prev.ok ? m_mono.elapsed() - m_monoMinusUtc : 0;
        setState("running");
    } else if (m_mode == "zero60") {
        setState("armed");
    } else {
        m_tree = 0;
        m_green = 0;
        m_treeTimer.start();
        setState("staged");
    }
}

void Timing::stop()
{
    // Stopping a lap session ends the in lap: the pit entry comes before the line
    if (m_mode == "laps" && m_state == "running" && m_lapStart > 0)
        addLap(QStringLiteral("in"), m_mono.elapsed() - (m_lapStart + m_monoMinusUtc), {});
    m_sessionStart = 0;
    m_sectorHold.stop();
    m_sectorColors.clear();
    m_ahead = 0;
    m_treeTimer.stop();
    m_tree = 0;
    m_lapStart = 0;
    m_launch = 0;
    setState("ready");
}

void Timing::addLap(const QString &kind, double ms, const QVariantList &secs)
{
    const bool timed = kind == QLatin1String("lap");
    const double best = bestLap();
    const bool isBest = timed && (best <= 0 || ms < best);
    int n = 0;
    for (const QVariant &l : std::as_const(m_laps))
        n += l.toMap().value("kind") == QLatin1String("lap");
    if (isBest) {
        for (QVariant &l : m_laps) {
            QVariantMap m = l.toMap();
            m["best"] = false;
            l = m;
        }
        QVariantMap tb = m_best.value(key()).toMap();
        tb["laps"] = QVariantMap { { "ms", ms }, { "sectors", secs } };
        m_best[key()] = tb;
        save();
    }
    m_laps.prepend(QVariantMap { { "kind", kind }, { "n", timed ? n + 1 : 0 }, { "ms", ms },
                                 { "delta", timed && best > 0 ? ms - best : 0.0 }, { "sectors", secs }, { "best", isBest } });
    if (timed)
        m_ahead = best <= 0 ? 0 : ms - best < -LevelMs ? 1 : ms - best > LevelMs ? -1 : 0;
    qCInfo(lcTiming) << kind << (timed ? n + 1 : 0) << ms << "ms" << (isBest ? "best" : "");
    emit finished(isBest ? QStringLiteral("Best lap") : kind == QLatin1String("out") ? QStringLiteral("Out lap")
                  : kind == QLatin1String("in") ? QStringLiteral("In lap") : QStringLiteral("Lap"));
    emit changed();
}

void Timing::judgeSector(int i, double ms)
{
    Q_UNUSED(ms)
    // The time so far at this line, against the best lap's at the same line
    const QVariantList best = m_best.value(key()).toMap().value("laps").toMap().value("sectors").toList();
    QString colour;
    if (best.size() > i) {
        double bestCum = 0, cum = 0;
        for (int k = 0; k <= i; ++k)
            bestCum += best.at(k).toDouble();
        // The first two lines are in m_lapSectors; the last is the lap itself
        cum = i < m_lapSectors.size() ? m_lapSectors.at(i).toDouble() : m_pendingLapMs;
        const double delta = cum - bestCum;
        colour = delta < -LevelMs ? QStringLiteral("green") : delta > LevelMs ? QStringLiteral("red") : QStringLiteral("yellow");
        m_ahead = delta < -LevelMs ? 1 : delta > LevelMs ? -1 : 0;
    }
    while (m_sectorColors.size() < i)
        m_sectorColors << QString();
    if (m_sectorColors.size() == i)
        m_sectorColors << colour;
    else
        m_sectorColors[i] = colour;
    emit changed();
}

void Timing::clearLaps()
{
    m_laps.clear();
    emit changed();
}

double Timing::elapsed() const
{
    const double start = m_mode == "laps" ? m_lapStart : m_launch;
    if (m_state != "running" || start <= 0)
        return 0;
    return std::max(0.0, (m_mono.elapsed() - (start + m_monoMinusUtc)) / 1000.0);
}

double Timing::bestLap() const
{
    return m_best.value(key()).toMap().value("laps").toMap().value("ms").toDouble();
}

QVariantList Timing::sectors() const
{
    QVariantList out;
    double prev = 0;
    for (const QVariant &s : m_lapSectors) {
        out << s.toDouble() - prev;
        prev = s.toDouble();
    }
    return out;
}

double Timing::progress() const
{
    const double best = bestLap();
    if (m_mode != "laps" || best <= 0)
        return 0;
    return std::clamp(elapsed() * 1000 / best, 0.0, 1.0);
}

QVariantList Timing::runs() const
{
    return m_recent.value(m_mode).toList();
}

double Timing::bestRun() const
{
    return m_best.value(key()).toMap().value(m_mode).toMap().value("score").toDouble();
}

QVariantList Timing::bestTrace() const
{
    return m_best.value(key()).toMap().value(m_mode).toMap().value("trace").toList();
}

// The speed through a run, from the launch at [0, 0]
void Timing::addTrace(const Fix &b)
{
    QVariantList trace = m_live.value("trace").toList();
    if (trace.isEmpty())
        trace << QVariant(QVariantList { 0.0, 0.0 });
    const double s = (b.t - m_launch) / 1000;
    if (s > 0)
        trace << QVariant(QVariantList { std::round(s * 1000) / 1000, std::round(b.mph * 10) / 10 });
    m_live["trace"] = trace;
}

void Timing::setImu(double lateralG, double accelG)
{
    m_gLat = lateralG;
    m_gAcc = accelG;
    m_imuMs.start();
    if (m_state == "running" || m_state == "armed" || m_state == "staged")
        m_gPeak = std::max(m_gPeak, std::hypot(lateralG, accelG));
    // The moment of launch, far closer than GNSS can tell
    if ((m_state == "armed" || (m_state == "staged" && m_tree == 4)) && accelG > ImuLaunchG && m_launch <= 0
        && !m_live.contains("imuLaunch"))
        m_live["imuLaunch"] = double(m_mono.elapsed());
}

void Timing::setFix(double lat, double lon, double mph, bool fix, double utcMs)
{
    m_mph = fix ? mph : 0;
    if (!fix) {
        m_prev.ok = false;
        emit tick();
        return;
    }
    m_monoMinusUtc = m_mono.elapsed() - utcMs;
    const Fix b { lat, lon, mph, utcMs, true };
    if (m_mode == "laps" && m_state == "running" && m_sessionStart <= 0)
        m_sessionStart = b.t;
    if (m_prev.ok && b.t > m_prev.t) {
        if (m_mode == "laps" && m_state == "running")
            lapFix(m_prev, b);
        else if (m_mode == "zero60" && (m_state == "armed" || m_state == "running"))
            zero60Fix(m_prev, b);
        else if (m_mode == "quarter" && (m_state == "staged" || m_state == "running"))
            quarterFix(m_prev, b);
    }
    m_prev = b;
    emit tick();
}

// ------------------------------------------------------------------ laps

void Timing::lapFix(const Fix &a, const Fix &b)
{
    // Sector lines first: a move that crosses one and then the finish line
    // records both, in order
    for (int i = 0; i < m_sectorGates.size(); ++i) {
        const double f = crossing(a.lat, a.lon, b.lat, b.lon, m_sectorGates.at(i).toList());
        if (f < 0 || m_lapStart <= 0 || m_lapSectors.size() != i)
            continue;
        const double t = a.t + f * (b.t - a.t);
        if (t - m_lastLine < MinLineGapMs)
            continue;
        m_lastLine = t;
        const double before = m_lapSectors.isEmpty() ? 0 : m_lapSectors.last().toDouble();
        m_lapSectors << t - m_lapStart;
        if (i == 0) {
            m_sectorHold.stop();
            m_sectorColors.clear();
        }
        judgeSector(i, t - m_lapStart - before);
        emit changed();
    }
    const double f = crossing(a.lat, a.lon, b.lat, b.lon, m_gate);
    if (f < 0)
        return;
    const double t = a.t + f * (b.t - a.t);
    if (t - m_lastLine < MinLineGapMs && m_lapStart > 0)
        return;
    m_lastLine = t;
    if (m_lapStart <= 0) {
        // The first crossing ends the out lap
        if (m_sessionStart > 0)
            addLap(QStringLiteral("out"), t - m_sessionStart, {});
    } else {
        const double ms = t - m_lapStart;
        QVariantList secs;
        if (m_lapSectors.size() == m_sectorGates.size() && !m_sectorGates.isEmpty()) {
            double prev = 0;
            for (const QVariant &s : std::as_const(m_lapSectors)) {
                secs << s.toDouble() - prev;
                prev = s.toDouble();
            }
            secs << ms - prev;
            m_pendingLapMs = ms;
            judgeSector(int(secs.size()) - 1, ms - prev);
        }
        addLap(QStringLiteral("lap"), ms, secs);
        m_sectorHold.start();
    }
    m_lapStart = t;
    m_lapSectors.clear();
    emit changed();
}

// ------------------------------------------------------------------ runs

// When the car left from rest, for the move from A (slow) to B (moving)
double Timing::launchTime(const Fix &a, const Fix &b) const
{
    // The IMU saw it
    if (m_live.contains("imuLaunch"))
        return m_live.value("imuLaunch").toDouble() - m_monoMinusUtc;
    const double va = a.mph * MsPerMph, vb = b.mph * MsPerMph;
    if (va > LaunchMph * MsPerMph) {
        // Already rolling at A: back along the same acceleration to zero
        const double acc = (vb - va) / ((b.t - a.t) / 1000);
        return acc > 0 ? std::max(a.t - 3000, a.t - va / acc * 1000) : a.t;
    }
    // From rest with steady acceleration, d = v t / 2, so it set off 2d/v ago
    const double d = metres(a.lat, a.lon, b.lat, b.lon);
    const double secs = vb > 0 ? 2 * d / vb : 0;
    return std::clamp(b.t - secs * 1000, a.t, b.t);
}

void Timing::zero60Fix(const Fix &a, const Fix &b)
{
    if (m_state == "armed") {
        if (b.mph <= LaunchMph)
            return;
        m_launch = launchTime(a, b);
        setState("running");
    }
    if (b.mph < AbortMph && a.mph >= AbortMph) {
        emit finished(QStringLiteral("Run abandoned"));
        stop();
        return;
    }
    // From the launch the speed rose from zero
    const double ta = std::max(a.t, m_launch), va = a.t < m_launch ? 0 : a.mph;
    for (const auto &[keyName, mph] : { std::pair { "s30", 30.0 }, std::pair { "ms", 60.0 } }) {
        if (m_live.contains(keyName) || va >= mph || b.mph < mph)
            continue;
        const double t = ta + (mph - va) / (b.mph - va) * (b.t - ta);
        m_live[keyName] = t - m_launch;
    }
    m_live["peak"] = m_gPeak;
    addTrace(b);
    emit changed();
    if (m_live.contains("ms"))
        finishRun(QVariantMap { { "ms", m_live.value("ms") }, { "s30", m_live.value("s30") }, { "peak", imu() ? m_gPeak : 0.0 },
                                { "trace", m_live.value("trace") } },
                  m_live.value("ms").toDouble());
}

void Timing::quarterFix(const Fix &a, const Fix &b)
{
    if (m_state == "staged") {
        if (b.mph <= LaunchMph)
            return;
        m_launch = launchTime(a, b);
        if (m_tree == 4 && m_live.contains("imuLaunch"))
            m_live["rt"] = (m_live.value("imuLaunch").toDouble() - m_green) / 1000.0;
        m_treeTimer.stop();
        // Gone before the green: a red light, as at the strip. The run is
        // still timed.
        if (m_tree < 4)
            m_live["red"] = true;
        m_tree = m_tree < 4 ? 5 : 4;
        setState("running");
    }
    const double ta = std::max(a.t, m_launch), va = (a.t < m_launch ? 0 : a.mph) * MsPerMph;
    const double vb = b.mph * MsPerMph, secs = (b.t - ta) / 1000;
    const double d = metres(a.lat, a.lon, b.lat, b.lon);
    QVariantMap splits = m_live.value("splits").toMap();
    for (double ft : SplitFt) {
        const QString k = QString::number(int(ft));
        const double want = ft * FtToM;
        if (splits.contains(k) || m_dist + d < want)
            continue;
        const double dt = timeAtDistance(va, vb, secs, d, want - m_dist);
        splits[k] = ta + dt * 1000 - m_launch;
        if (ft == 1320)
            m_live["trap"] = (va + (vb - va) * dt / secs) / MsPerMph;
    }
    m_dist += d;
    m_live["splits"] = splits;
    addTrace(b);
    emit changed();
    if (splits.contains("1320")) {
        const double et = splits.value("1320").toDouble();
        finishRun(QVariantMap { { "et", et }, { "trap", m_live.value("trap") },
                                { "rt", m_live.value("rt", 0.0) }, { "splits", splits }, { "trace", m_live.value("trace") },
                                { "red", m_live.value("red", false) } }, et);
    }
}

void Timing::finishRun(const QVariantMap &run, double score)
{
    QVariantList recent = m_recent.value(m_mode).toList();
    recent.prepend(run);
    while (recent.size() > MaxRecent)
        recent.removeLast();
    m_recent[m_mode] = recent;
    const bool isBest = bestRun() <= 0 || score < bestRun();
    if (isBest) {
        QVariantMap tb = m_best.value(key()).toMap();
        tb[m_mode] = QVariantMap { { "score", score }, { "trace", run.value("trace") } };
        m_best[key()] = tb;
    }
    save();
    qCInfo(lcTiming) << m_mode << "run" << score << "ms" << (isBest ? "best" : "");
    m_launch = 0;
    m_treeTimer.stop();
    setState("done");
    emit finished(isBest ? QStringLiteral("Best run") : QStringLiteral("Run"));
}
