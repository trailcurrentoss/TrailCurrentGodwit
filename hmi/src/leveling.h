#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

// Leveling the parked vehicle: Settings › Leveling, and what to do about a
// tilt. The tilt itself comes from wherever it is measured (Plateau on the
// TrailCurrent bus today); everything here works from pitch and roll alone,
// so a sensor wired to the head unit can take Plateau's place.
//
// What the user levels with is one of:
//  - boards or blocks: stacked under the low wheels, `pieceMm` thick each,
//    at most `maxPieces` under one wheel;
//  - ramps that rise as the wheel is driven up them, `rampMm` at the top.
// A wheel that would need more than that is "too steep".
//
// Kept in level.json in the HMI's state folder (erased by the factory reset).
class Leveling : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // "boards", "blocks" or "ramps"
    Q_PROPERTY(QString leveler READ leveler WRITE setLeveler NOTIFY setupChanged)
    // Thickness of one board or block of the kind chosen (each kind keeps its own)
    Q_PROPERTY(int pieceMm READ pieceMm WRITE setPieceMm NOTIFY setupChanged)
    // The most boards or blocks the user will stack under one wheel
    Q_PROPERTY(int maxPieces READ maxPieces WRITE setMaxPieces NOTIFY setupChanged)
    // How far the ramps raise a wheel at their top
    Q_PROPERTY(int rampMm READ rampMm WRITE setRampMm NOTIFY setupChanged)
    // Between the axles, and between the wheels side to side; 0 = not set
    Q_PROPERTY(int wheelbaseMm READ wheelbaseMm WRITE setWheelbaseMm NOTIFY setupChanged)
    Q_PROPERTY(int trackMm READ trackMm WRITE setTrackMm NOTIFY setupChanged)
    // Where Plateau is fixed: 0 floor, 1 left wall, 2 right wall
    Q_PROPERTY(int mounting READ mounting WRITE setMounting NOTIFY setupChanged)
    // The highest a wheel can be raised with what is set up
    Q_PROPERTY(int maxRaiseMm READ maxRaiseMm NOTIFY setupChanged)
    // Wheelbase and track are both set, so raises can be worked out
    Q_PROPERTY(bool sized READ sized NOTIFY setupChanged)
    // Every wheel within this of the highest counts as level
    Q_PROPERTY(int levelMm READ levelMm CONSTANT)

public:
    explicit Leveling(QObject *parent = nullptr);

    static constexpr int LevelMm = 10;   // every wheel within this of the highest: level
    int levelMm() const { return LevelMm; }

    QString leveler() const { return m_leveler; }
    int pieceMm() const { return m_leveler == QLatin1String("blocks") ? m_blockMm : m_boardMm; }
    int maxPieces() const { return m_maxPieces; }
    int rampMm() const { return m_rampMm; }
    int wheelbaseMm() const { return m_wheelbaseMm; }
    int trackMm() const { return m_trackMm; }
    int mounting() const { return m_mounting; }
    int maxRaiseMm() const { return m_leveler == QLatin1String("ramps") ? m_rampMm : pieceMm() * m_maxPieces; }
    bool sized() const { return m_wheelbaseMm > 0 && m_trackMm > 0; }

    void setLeveler(const QString &l);
    void setPieceMm(int mm);
    void setMaxPieces(int n);
    void setRampMm(int mm);
    void setWheelbaseMm(int mm);
    void setTrackMm(int mm);
    void setMounting(int m);

    // What to do about a tilt, pitch + nose up and roll + leaning right, in
    // degrees:
    //  frontMm, rearMm, leftMm, rightMm: each axis on its own, how far its low
    //    end must rise (0 for the high end)
    //  raiseMm: [front left, front right, rear left, rear right], how far each
    //    wheel must rise to meet the highest; empty when not sized
    //  pieces: the same as whole boards or blocks (each at least 1 when a raise
    //    is needed); zeros for ramps
    //  level: every wheel within LevelMm; steep: one needs more than maxRaiseMm
    Q_INVOKABLE QVariantMap guide(double pitch, double roll) const;
    // Boards or blocks for a raise: the nearest whole number, at least one
    Q_INVOKABLE int piecesFor(double mm) const;
    // For ramps: how far along from the low end (0-1) a wheel is raised by mm
    Q_INVOKABLE double rampAt(double mm) const;

signals:
    void setupChanged();

private:
    friend class TestLeveling;
    void load();
    void save();

    QString m_leveler = QStringLiteral("boards");
    int m_boardMm = 38;      // a 2x board: 1.5 in
    int m_blockMm = 25;      // a common plastic block: 1 in
    int m_maxPieces = 3;
    int m_rampMm = 102;      // 4 in
    int m_wheelbaseMm = 0;
    int m_trackMm = 0;
    int m_mounting = 0;
};
