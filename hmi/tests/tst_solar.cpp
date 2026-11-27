// Solar: the sky worked out offline, the cloud judgement and Solar Today.
// Astronomy is checked against published times for known dates.
#include "solar.h"

#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <cmath>

class TestSolar : public QObject
{
    Q_OBJECT

    QTemporaryDir state;
    static constexpr double DenverLat = 39.74, DenverLon = -104.99;

    static QDateTime utc(int y, int mo, int d, int h, int mi)
    {
        return QDateTime(QDate(y, mo, d), QTime(h, mi), QTimeZone::UTC);
    }
    // Feeds a steady reading once a second for `seconds`
    static void steady(Solar &s, double watts, const QString &charge, qint64 &ms, int seconds)
    {
        for (int i = 0; i < seconds; ++i) {
            ms += 1000;
            s.feedAt(watts, charge, ms);
        }
    }

private slots:
    void initTestCase()
    {
        QVERIFY(state.isValid());
        qputenv("STATE_DIRECTORY", state.path().toUtf8());
    }

    void sunriseAndSunset()
    {
        // Denver, 21 June 2026: sunrise 5:32 AM and sunset 8:31 PM MDT
        const QDateTime day = utc(2026, 6, 21, 6, 0);   // local midnight, MDT
        const QDateTime rise = Solar::crossing(false, day, day.addSecs(86400), DenverLat, DenverLon, -0.833, true);
        const QDateTime set = Solar::crossing(false, day, day.addSecs(86400), DenverLat, DenverLon, -0.833, false);
        QVERIFY(rise.isValid() && set.isValid());
        QVERIFY2(std::abs(rise.secsTo(utc(2026, 6, 21, 11, 32))) < 180, qPrintable(rise.toString(Qt::ISODate)));
        QVERIFY2(std::abs(set.secsTo(utc(2026, 6, 22, 2, 31))) < 180, qPrintable(set.toString(Qt::ISODate)));
        // Solar noon is about 1:01 PM MDT, 90 - 39.74 + 23.44 = 73.7 degrees up, due south
        const Solar::Position noon = Solar::sunAt(utc(2026, 6, 21, 19, 1), DenverLat, DenverLon);
        QVERIFY2(std::abs(noon.elevation - 73.7) < 0.5, qPrintable(QString::number(noon.elevation)));
        QVERIFY2(std::abs(noon.azimuth - 180.0) < 3, qPrintable(QString::number(noon.azimuth)));
    }

    void moonPhases()
    {
        // Total lunar eclipse, 3 March 2026 at 11:33 UTC: full, opposite the sun
        const QDateTime eclipse = utc(2026, 3, 3, 11, 33);
        QVERIFY2(std::abs(Solar::moonAge(eclipse) - 180.0) < 3, qPrintable(QString::number(Solar::moonAge(eclipse))));
        const Solar::Position sun = Solar::sunAt(eclipse, DenverLat, DenverLon);
        const Solar::Position moon = Solar::moonAt(eclipse, DenverLat, DenverLon);
        QVERIFY2(std::abs(moon.elevation + sun.elevation) < 2.5,
                 qPrintable(QStringLiteral("moon %1 sun %2").arg(moon.elevation).arg(sun.elevation)));
        // Annular solar eclipse, 17 February 2026 at 12:12 UTC: new
        const double age = Solar::moonAge(utc(2026, 2, 17, 12, 12));
        QVERIFY2(std::min(age, 360.0 - age) < 3, qPrintable(QString::number(age)));
    }

    void phaseNames()
    {
        Solar s;
        s.updateAt(utc(2026, 3, 3, 11, 33));
        QCOMPARE(s.moonPhase(), 4);
        QCOMPARE(s.moonPhaseName(), QStringLiteral("Full Moon"));
        // First quarter, 25 March 2026
        s.updateAt(utc(2026, 3, 25, 19, 18));
        QCOMPARE(s.moonPhaseName(), QStringLiteral("First Quarter"));
    }

    void clearSky()
    {
        QCOMPARE(Solar::clearSky(30, 0), 0.0);
        QCOMPARE(Solar::clearSky(-2, 600), 0.0);
        // Overhead sun, 1 kW array: 1098 W/m² less the air and 20% losses
        QVERIFY(std::abs(Solar::clearSky(90, 1000) - 829.7) < 1);
        QVERIFY(Solar::clearSky(10, 600) < Solar::clearSky(40, 600));
    }

    void cloudJudgement()
    {
        Solar s;
        s.m_located = true;
        s.m_arrayW = 600;
        s.m_sunEl = 50;
        const double clear = Solar::clearSky(50, 600);
        qint64 ms = 1000;

        steady(s, clear * 0.95, "Bulk", ms, 60);
        QCOMPARE(s.condition(), QStringLiteral("sun"));
        QVERIFY(s.cloud() >= 0 && s.cloud() < 0.1);

        // A passing shadow of a few seconds changes nothing
        steady(s, clear * 0.1, "Bulk", ms, 5);
        QCOMPARE(s.condition(), QStringLiteral("sun"));

        // Ten minutes at half: partly cloudy
        steady(s, clear * 0.5, "Bulk", ms, 600);
        QCOMPARE(s.condition(), QStringLiteral("partly"));

        // Ten minutes at a tenth: cloudy or shaded
        steady(s, clear * 0.1, "Bulk", ms, 600);
        QCOMPARE(s.condition(), QStringLiteral("cloudy"));
        QCOMPARE(s.cloud(), 1.0);

        // Float holds the panels back: the last judgement stays
        steady(s, clear * 0.02, "Float", ms, 600);
        QVERIFY(s.throttled());
        QCOMPARE(s.condition(), QStringLiteral("cloudy"));

        // Back in Bulk in full sun
        steady(s, clear, "Bulk", ms, 900);
        QCOMPARE(s.condition(), QStringLiteral("sun"));
        QVERIFY(!s.throttled());

        // The sun too low to judge
        s.m_sunEl = 4;
        steady(s, 10, "Bulk", ms, 5);
        QCOMPARE(s.condition(), QString());
        QCOMPARE(s.cloud(), -1.0);
    }

    void noArraySizeNoJudgement()
    {
        Solar s;
        s.m_located = true;
        s.m_arrayW = 0;
        s.m_sunEl = 50;
        qint64 ms = 1000;
        steady(s, 300, "Bulk", ms, 30);
        QCOMPARE(s.condition(), QString());
        QCOMPARE(s.clearSkyWatts(), 0.0);
    }

    void solarToday()
    {
        Solar s;
        s.resetToday();
        qint64 ms = 1000;
        s.feedAt(600, "Bulk", ms);
        steady(s, 600, "Bulk", ms, 3600);
        QVERIFY2(std::abs(s.todayWh() - 600.0) < 1, qPrintable(QString::number(s.todayWh())));
        // A minute of silence from the charger is not harvest
        ms += 60000;
        s.feedAt(600, "Bulk", ms);
        QVERIFY(std::abs(s.todayWh() - 600.0) < 1);
        // Next local day: starts again
        s.updateAt(QDateTime::currentDateTimeUtc().addDays(1));
        QCOMPARE(s.todayWh(), 0.0);
    }

    void savedSetup()
    {
        {
            Solar s;
            s.setArrayWatts(640);
            s.setBatteryAh(280);
            s.setPosition(39.7412, -104.9876);
        }
        Solar again;
        QCOMPARE(again.arrayWatts(), 640);
        QCOMPARE(again.batteryAh(), 280);
        QVERIFY(again.located());
        // Kept to about a kilometre
        QCOMPARE(again.m_lat, 39.74);
        QCOMPARE(again.m_lon, -104.99);
        // The factory reset erases the state folder: everything goes
        QVERIFY(QFile::remove(state.path() + "/solar.json"));
        Solar fresh;
        QCOMPARE(fresh.arrayWatts(), 0);
        QVERIFY(!fresh.located());
    }
};

QTEST_GUILESS_MAIN(TestSolar)
#include "tst_solar.moc"
