#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

// Timing for the Track app: laps on a road course or oval, 0-60 mph, and the
// quarter mile.
//
// Positions come from QML (setFix, from Gnss) about once a second, so every
// event is interpolated between fixes: a lap ends where the line from one fix
// to the next crosses the start/finish line, at the matching fraction of the
// time between them; a speed or a distance is reached where the speed or the
// distance run, assuming steady acceleration between fixes, gets there. Laps
// are timed from one crossing to the next, so they are right wherever the
// line is.
//
// setImu takes the head unit IMU's lateral and longitudinal g (BNO055). Until
// it reports, g-force, peak g and reaction time are unknown, and a launch is
// found from GNSS speed alone.
class Timing : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // "laps", "zero60" or "quarter"
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    // ready: waiting for a press. armed (0-60): waiting for the launch.
    // staged (quarter): the tree is running or green. running: a run or a lap
    // session is under way. done: a run finished; a press arms again.
    Q_PROPERTY(QString state READ state NOTIFY changed)
    // Seconds into the current lap or run, live
    Q_PROPERTY(double elapsed READ elapsed NOTIFY tick)
    // mph, from GNSS
    Q_PROPERTY(double speed READ speed NOTIFY tick)
    // The quarter-mile tree: 0 dark, 1-3 that amber lit, 4 green, 5 red (left
    // before the green)
    Q_PROPERTY(int tree READ tree NOTIFY changed)

    // Laps this session, newest first: {kind, n, ms, delta (ms against the
    // best before it, 0 if none), sectors: [ms, ms, ms] or [], best: bool}.
    // kind "out" runs from Start to the first start/finish crossing (leaving
    // the pits, the car joins the track past the line), "lap" from crossing to
    // crossing (n counts these), "in" from the last crossing to Stop (the pit
    // entry is before the line). Out and in laps are never a best.
    Q_PROPERTY(QVariantList laps READ laps NOTIFY changed)
    // Best lap at this track, ms (0 if none), kept across sessions
    Q_PROPERTY(double bestLap READ bestLap NOTIFY changed)
    // This lap's sector times so far, ms
    Q_PROPERTY(QVariantList sectors READ sectors NOTIFY changed)
    // Each sector of this lap as it closes: is the lap on course to be
    // faster, the same or slower than the best lap? The time so far at that
    // line against the best lap's time at the same line: "green" ahead,
    // "yellow" within LevelMs either way, "red" behind, "" without a best
    // lap. Colours only: the driver has no time to read sector times. At the
    // line the last sector fills in, so the whole lap shows; SectorHoldMs
    // later they clear for the new lap.
    Q_PROPERTY(QVariantList sectorColors READ sectorColors NOTIFY changed)
    // How many sectors a lap has at this track (its sector lines plus one), 0 without a track
    Q_PROPERTY(int sectorCount READ sectorCount NOTIFY changed)
    // How far round the lap, 0-1, by time against the best lap
    Q_PROPERTY(double progress READ progress NOTIFY tick)
    // Against the best lap at the last line crossed: 1 ahead, -1 behind, 0 level or unknown
    Q_PROPERTY(int ahead READ ahead NOTIFY changed)
    // Just over the start/finish line: the finished lap's time and sectors
    // stay up for a few seconds before the new lap takes over
    Q_PROPERTY(bool holding READ holding NOTIFY changed)

    // Runs in this mode, newest first. 0-60: {ms, s30, peak, trace}. Quarter:
    // {et, trap, rt, splits: {60, 330, 660, 1000, 1320} in ms, trace, red}
    // (red: left before the green).
    // trace is the speed through the run, [[seconds, mph], ...] from the
    // launch at [0, 0]. Unknown values are 0.
    Q_PROPERTY(QVariantList runs READ runs NOTIFY changed)
    // Best run in this mode at this track (0-60 ms or quarter ET ms), 0 if none
    Q_PROPERTY(double bestRun READ bestRun NOTIFY changed)
    // The best run's trace, to draw the run under way against
    Q_PROPERTY(QVariantList bestTrace READ bestTrace NOTIFY changed)
    // The run under way: the same fields as a run, filled in as they happen
    Q_PROPERTY(QVariantMap live READ live NOTIFY changed)

    Q_PROPERTY(bool imu READ imu NOTIFY tick)
    Q_PROPERTY(double gLat READ gLat NOTIFY tick)
    Q_PROPERTY(double gAcc READ gAcc NOTIFY tick)
    Q_PROPERTY(double gPeak READ gPeak NOTIFY tick)

public:
    explicit Timing(QObject *parent = nullptr);

    QString mode() const { return m_mode; }
    void setMode(const QString &m);
    QString state() const { return m_state; }
    double elapsed() const;
    double speed() const { return m_mph; }
    int tree() const { return m_tree; }
    QVariantList laps() const { return m_laps; }
    double bestLap() const;
    QVariantList sectors() const;
    double progress() const;
    int ahead() const { return m_ahead; }
    bool holding() const { return m_sectorHold.isActive(); }
    QVariantList sectorColors() const { return m_sectorColors; }
    int sectorCount() const { return m_gate.isEmpty() ? 0 : int(m_sectorGates.size()) + 1; }
    QVariantList runs() const;
    double bestRun() const;
    QVariantList bestTrace() const;
    QVariantMap live() const { return m_live; }
    bool imu() const { return m_imuMs.isValid() && m_imuMs.elapsed() < 2000; }
    double gLat() const { return imu() ? m_gLat : 0; }
    double gAcc() const { return imu() ? m_gAcc : 0; }
    double gPeak() const { return m_gPeak; }

    // The track: its id (for saving bests), start/finish line and sector lines,
    // each [[lat, lon], [lat, lon]]. Empty for none.
    Q_INVOKABLE void setTrack(const QString &id, const QVariantList &gate, const QVariantList &sectorGates);
    // A GNSS fix: position, speed in mph, and its time (ms since the epoch, UTC)
    Q_INVOKABLE void setFix(double lat, double lon, double mph, bool fix, double utcMs);
    // The IMU: lateral and longitudinal acceleration, g
    Q_INVOKABLE void setImu(double lateralG, double accelG);
    // The knob press: start a lap session, arm 0-60, or stage the tree; again
    // to stop a session or cancel a run
    Q_INVOKABLE void press();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void clearLaps();

signals:
    void changed();
    void tick();
    // A lap or run just finished; for a toast
    void finished(const QString &what);

private:
    struct Fix { double lat = 0, lon = 0, mph = 0, t = 0; bool ok = false; };
    void setState(const QString &s);
    void addLap(const QString &kind, double ms, const QVariantList &sectors);
    void judgeSector(int i, double ms);
    void lapFix(const Fix &a, const Fix &b);
    void zero60Fix(const Fix &a, const Fix &b);
    void quarterFix(const Fix &a, const Fix &b);
    double launchTime(const Fix &a, const Fix &b) const;
    void addTrace(const Fix &b);
    void finishRun(const QVariantMap &run, double score);
    void load();
    void save();
    QString key() const;

    QString m_mode = QStringLiteral("laps");
    QString m_state = QStringLiteral("ready");
    QString m_track;
    QVariantList m_gate, m_sectorGates;
    Fix m_prev;
    double m_mph = 0;
    double m_monoMinusUtc = 0;     // monotonic ms minus GNSS UTC ms, at the last fix
    QElapsedTimer m_mono;

    // Laps
    double m_sessionStart = 0;     // UTC ms the session started (the out lap), 0 before a fix
    double m_lapStart = 0;         // UTC ms of the last start/finish crossing, 0 before the first
    double m_lastLine = 0;         // UTC ms of the last line crossed, against repeats
    QVariantList m_lapSectors;     // ms since the lap started, at each sector line
    QVariantList m_laps;
    int m_ahead = 0;
    QVariantList m_sectorColors;   // this lap's sectors so far, or the last lap's
    double m_pendingLapMs = 0;     // the lap just finishing, while its last sector is judged

    // Runs
    double m_launch = 0;           // UTC ms of the launch, 0 before it
    double m_green = 0;            // monotonic ms the tree went green
    double m_dist = 0;             // metres run since the launch
    int m_tree = 0;
    QTimer m_treeTimer;
    QVariantMap m_live;

    // Saved: best lap and best runs per track, recent runs per mode
    QVariantMap m_best;            // track -> {laps, zero60, quarter}
    QVariantMap m_recent;          // mode -> [run, ...]

    double m_gLat = 0, m_gAcc = 0, m_gPeak = 0;
    QElapsedTimer m_imuMs;
    QTimer m_tick;
    QTimer m_sectorHold;           // clears the finished lap's sectors
};
