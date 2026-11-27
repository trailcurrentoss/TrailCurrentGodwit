// Leveling: which wheels to raise, by how much, and with how many boards,
// blocks or how far up a ramp. Checked against Plateau's own corner sums.
#include "leveling.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

class TestLeveling : public QObject
{
    Q_OBJECT

    QTemporaryDir state;

    static double at(const QVariantMap &g, const char *key, int i)
    {
        return g.value(QLatin1String(key)).toList().value(i).toDouble();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(state.isValid());
        qputenv("STATE_DIRECTORY", state.path().toUtf8());
    }

    void notSizedGivesNoRaises()
    {
        Leveling l;
        const QVariantMap g = l.guide(-1.4, 0.8);
        QVERIFY(!l.sized());
        QVERIFY(!g.contains("raiseMm"));
        QVERIFY(!g.value("level").toBool());
    }

    void cornersMatchPlateau()
    {
        Leveling l;
        l.m_wheelbaseMm = 3665;
        l.m_trackMm = 1750;
        // Nose down 1.4°, leaning right 0.8°: the front is low, the right is low
        const QVariantMap g = l.guide(-1.4, 0.8);
        const double fb = 3665 * std::tan(-1.4 * M_PI / 180), lr = 1750 * std::tan(0.8 * M_PI / 180);
        // Plateau: fl = fb/2 + lr/2 and so on; the highest is the rear left
        QVERIFY(std::abs(at(g, "raiseMm", 2)) < 0.01);                       // rear left: highest
        QVERIFY(std::abs(at(g, "raiseMm", 1) - (-fb + lr)) < 0.01);          // front right: lowest
        QVERIFY(std::abs(at(g, "raiseMm", 0) - (-fb)) < 0.01);               // front left
        QVERIFY(std::abs(at(g, "raiseMm", 3) - lr) < 0.01);                  // rear right
        QVERIFY(std::abs(g.value("frontMm").toDouble() - (-fb)) < 0.01);
        QCOMPARE(g.value("rearMm").toDouble(), 0.0);
        QVERIFY(std::abs(g.value("rightMm").toDouble() - lr) < 0.01);
        QCOMPARE(g.value("leftMm").toDouble(), 0.0);
        QVERIFY(!g.value("level").toBool());
        QVERIFY(!g.value("steep").toBool());
        // Boards of 38 mm: 89.5 -> 2, 114 -> 3, 24.4 -> 1, none for the highest
        QCOMPARE(g.value("pieces").toList(), (QVariantList { 2, 3, 0, 1 }));
    }

    void levelAndSteep()
    {
        Leveling l;
        l.m_wheelbaseMm = 3665;
        l.m_trackMm = 1750;
        QVERIFY(l.guide(0.1, -0.1).value("level").toBool());
        // 3 boards of 38 mm is 114 mm: 4.6° nose up needs about 295 mm
        QVERIFY(l.guide(4.6, 0).value("steep").toBool());
        l.m_maxPieces = 10;
        QVERIFY(!l.guide(4.6, 0).value("steep").toBool());
    }

    void blocksKeepTheirOwnThickness()
    {
        Leveling l;
        l.setLeveler("blocks");
        QCOMPARE(l.pieceMm(), 25);
        l.setPieceMm(30);
        l.setLeveler("boards");
        QCOMPARE(l.pieceMm(), 38);
        l.setLeveler("blocks");
        QCOMPARE(l.pieceMm(), 30);
        QCOMPARE(l.piecesFor(40), 1);
        QCOMPARE(l.piecesFor(50), 2);
        QCOMPARE(l.piecesFor(3), 1);   // any raise worth making is at least one
    }

    void ramps()
    {
        Leveling l;
        l.m_wheelbaseMm = 3665;
        l.m_trackMm = 1750;
        l.setLeveler("ramps");
        l.setRampMm(102);
        QCOMPARE(l.maxRaiseMm(), 102);
        const QVariantMap g = l.guide(0, -1.1);
        QCOMPARE(g.value("pieces").toList(), (QVariantList { 0, 0, 0, 0 }));
        QVERIFY(!g.value("steep").toBool());
        // Half height is a third of the way up a sine-shaped ramp; the top is the end
        QVERIFY(std::abs(l.rampAt(51) - 1.0 / 3) < 0.01);
        QCOMPARE(l.rampAt(102), 1.0);
        QCOMPARE(l.rampAt(500), 1.0);
    }

    void saved()
    {
        {
            Leveling l;
            l.setLeveler("ramps");
            l.setRampMm(90);
            l.setWheelbaseMm(3665);
            l.setTrackMm(1750);
            l.setMounting(1);
            l.setMaxPieces(4);
        }
        Leveling again;
        QCOMPARE(again.leveler(), QStringLiteral("ramps"));
        QCOMPARE(again.rampMm(), 90);
        QCOMPARE(again.wheelbaseMm(), 3665);
        QCOMPARE(again.trackMm(), 1750);
        QCOMPARE(again.mounting(), 1);
        QCOMPARE(again.maxPieces(), 4);
        QVERIFY(QFile::remove(state.path() + "/level.json"));
        Leveling fresh;
        QCOMPARE(fresh.leveler(), QStringLiteral("boards"));
        QVERIFY(!fresh.sized());
    }
};

QTEST_GUILESS_MAIN(TestLeveling)
#include "tst_leveling.moc"
