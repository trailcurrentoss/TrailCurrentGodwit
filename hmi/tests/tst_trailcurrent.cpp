// TrailCurrent without a CAN interface: frames go straight into it, and what
// it sends comes out of the other end of a socket pair standing in for the
// bus. Needs GODWIT_TRAILCURRENT_DBC=<path to TrailCurrent.dbc>.
#include "trailcurrent.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <linux/can.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>

class TestTrailCurrent : public QObject
{
    Q_OBJECT

    QTemporaryDir state;
    TrailCurrent *tc = nullptr;
    int bus = -1;   // the far end: reads what TrailCurrent sends

    static can_frame frame(quint32 id, std::initializer_list<uint8_t> bytes)
    {
        can_frame f {};
        f.can_id = id;
        f.len = uint8_t(bytes.size());
        int i = 0;
        for (uint8_t b : bytes)
            f.data[i++] = b;
        return f;
    }
    void feed(quint32 id, std::initializer_list<uint8_t> bytes) { tc->handleFrame(frame(id, bytes)); }
    // Everything sent since the last call, as "id#hex"
    QStringList sent()
    {
        QStringList out;
        can_frame f {};
        while (::recv(bus, &f, sizeof f, MSG_DONTWAIT) == ssize_t(sizeof f))
            out.append(QStringLiteral("%1#%2").arg(f.can_id, 3, 16, QLatin1Char('0'))
                           .arg(QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(f.data), f.len).toHex())));
        return out;
    }
    void tick() { tc->flushPending(); tc->m_dirty = 0xFF; tc->m_tick = 4; tc->publish(); }
    QVariantMap light(const QString &key)
    {
        for (const QVariant &v : tc->lights())
            if (v.toMap().value("key") == key)
                return v.toMap();
        return {};
    }

private slots:
    void init()
    {
        if (qEnvironmentVariableIsEmpty("GODWIT_TRAILCURRENT_DBC"))
            QSKIP("set GODWIT_TRAILCURRENT_DBC to TrailCurrent.dbc");
        QVERIFY(state.isValid());
        qputenv("STATE_DIRECTORY", state.path().toLocal8Bit());
        QFile::remove(state.filePath("trailcurrent.json"));
        tc = new TrailCurrent(nullptr);
        QVERIFY(tc->ready());
        // No interface to look for: the socket pair is the bus
        tc->m_pollTimer.stop();
        tc->m_flushTimer.stop();
        int fds[2];
        QCOMPARE(::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK, 0, fds), 0);
        tc->m_fd = fds[0];
        bus = fds[1];
        tc->m_iface = QStringLiteral("test");
        tc->m_up = true;
    }
    void cleanup()
    {
        delete tc;   // closes its end
        tc = nullptr;
        if (bus >= 0)
            ::close(bus);
        bus = -1;
    }

    void lightsFromStatus()
    {
        feed(0x1B, { 255, 0, 128, 0, 0, 0, 0, 3 });
        tick();
        QCOMPARE(tc->lights().size(), 8);
        QCOMPARE(light("t0.0").value("b").toInt(), 100);
        QCOMPARE(light("t0.0").value("n").toString(), QStringLiteral("Light 1"));
        QCOMPARE(light("t0.2").value("b").toInt(), 50);
        QCOMPARE(light("t0.1").value("on").toBool(), false);
        // A glimmer is on, never 0%
        QCOMPARE(light("t0.7").value("b").toInt(), 1);
        QVERIFY(light("t0.0").value("fresh").toBool());
        QCOMPARE(tc->modules().size(), 1);
    }

    void brightnessIsSetNeverToggled()
    {
        feed(0x1C, { 0, 0, 0, 0, 0, 0, 0, 0 });   // Torrent 1
        tick();
        QVERIFY(tc->setBrightness("t1.4", 20));
        QCOMPARE(sent(), QStringList { "016#0433" });          // channel 4, 51 of 255
        QCOMPARE(light("t1.4").value("b").toInt(), 20);        // shown while on its way
        QVERIFY(light("t1.4").value("busy").toBool());
        // Turning off and on again goes back to the level it had
        feed(0x1C, { 0, 0, 0, 0, 51, 0, 0, 0 });
        tick();
        QVERIFY(!light("t1.4").value("busy").toBool());
        QTest::qWait(50);   // commands to one channel are at least 40 ms apart
        QVERIFY(tc->setLight("t1.4", false));
        QCOMPARE(sent(), QStringList { "016#0400" });
        feed(0x1C, { 0, 0, 0, 0, 0, 0, 0, 0 });
        tick();
        QTest::qWait(50);
        QVERIFY(tc->setLight("t1.4", true));
        QCOMPARE(sent(), QStringList { "016#0433" });
    }

    void turnsInQuickSuccessionAreJoined()
    {
        feed(0x1B, { 0, 0, 0, 0, 0, 0, 0, 0 });
        tick();
        tc->setBrightness("t0.0", 10);
        tc->setBrightness("t0.0", 15);
        tc->setBrightness("t0.0", 20);
        QCOMPARE(sent(), QStringList { "015#001a" });           // the first at once
        QTest::qWait(60);
        tick();
        QCOMPARE(sent(), QStringList { "015#0033" });           // then only the last
    }

    void lightThatDoesNotAnswer()
    {
        feed(0x1B, { 0, 0, 0, 0, 0, 0, 0, 0 });
        tick();
        QSignalSpy said(tc, &TrailCurrent::message);
        tc->setLight("t0.5", true);
        QCOMPARE(sent().size(), 1);
        QTest::qWait(1100);
        tc->m_seen["torrent0"] = QDateTime::currentMSecsSinceEpoch();
        tick();
        QCOMPARE(said.size(), 1);
        QCOMPARE(said.at(0).at(0).toString(), QStringLiteral("Light 6 did not respond"));
        QVERIFY(!light("t0.5").value("busy").toBool());
        QCOMPARE(light("t0.5").value("on").toBool(), false);
    }

    void relayTogglesOnlyWhenItDiffers()
    {
        feed(0x28, { 0x08 });                        // relay 3 on
        tick();
        QVERIFY(tc->setLight("r0.3", true));         // already on: nothing sent
        QCOMPARE(sent(), QStringList {});
        QVERIFY(tc->setLight("r0.2", true));
        QCOMPARE(sent(), QStringList { "025#0200" });
        feed(0x28, { 0x0C });
        tick();
        QVERIFY(!light("r0.2").value("busy").toBool());
        QCOMPARE(light("r0.2").value("on").toBool(), true);
    }

    void relayRetriedOnceThenGivenUp()
    {
        feed(0x29, { 0x00 });
        tick();
        QSignalSpy said(tc, &TrailCurrent::message);
        tc->setLight("r1.0", true);
        QCOMPARE(sent(), QStringList { "026#0000" });
        // A second tap while the toggle is on its way would undo it
        tc->setLight("r1.0", false);
        QCOMPARE(sent(), QStringList {});
        tc->m_pending["r1.0"].target = 1;
        QTest::qWait(320);
        feed(0x29, { 0x00 });
        tick();
        QCOMPARE(sent(), QStringList { "026#0000" });   // once more
        QTest::qWait(320);
        feed(0x29, { 0x00 });
        tick();
        QCOMPARE(sent(), QStringList {});
        QCOMPARE(said.size(), 1);
        QCOMPARE(said.at(0).at(0).toString(), QStringLiteral("Relay 9 did not switch"));
    }

    void thermostatShowsItsOwnSetpoint()
    {
        feed(0x3F, { 0x00, 0xC8, 0x05 });          // 20.0 °C, 0.5 band
        feed(0x40, { 1, 0x00, 0xBE, 1, 0, 0 });    // heating, 19.0 °C
        tick();
        QCOMPARE(tc->camper().value("set").toDouble(), 20.0);
        QCOMPARE(tc->camper().value("cur").toDouble(), 19.0);
        QCOMPARE(tc->camper().value("mode").toString(), QStringLiteral("heat"));
        QVERIFY(tc->setCamperSetpoint(21.5));
        QCOMPARE(sent(), QStringList { "041#00d7" });
        tick();
        QCOMPARE(tc->camper().value("set").toDouble(), 20.0);    // not what was asked
        QCOMPARE(tc->camper().value("pending").toDouble(), 21.5);
        feed(0x3F, { 0x00, 0xD7, 0x05 });
        tick();
        QCOMPARE(tc->camper().value("set").toDouble(), 21.5);
        QVERIFY(!tc->camper().value("pending").isValid());
        // Therma's own limits
        QTest::qWait(50);
        tc->setCamperSetpoint(60);
        QCOMPARE(sent(), QStringList { "041#015e" });             // 35.0
    }

    void doorsListedOnceSeenClosed()
    {
        feed(0x0A, { 0x05, 0x0F });                // 1 and 3 open, 2 and 4-8 closed; 9-12 open
        tick();
        QStringList keys;
        for (const QVariant &v : tc->inputs())
            keys.append(v.toMap().value("key").toString());
        QCOMPARE(keys, (QStringList { "p0.1", "p0.3", "p0.4", "p0.5", "p0.6", "p0.7" }));
        feed(0x0A, { 0x02, 0x0F });                // 2 opens, 1 and 3 close
        tick();
        QCOMPARE(tc->inputs().size(), 8);
        QCOMPARE(tc->inputs().at(1).toMap().value("n").toString(), QStringLiteral("Door 2"));
        QVERIFY(tc->inputs().at(1).toMap().value("open").toBool());
        QVERIFY(tc->inputs().at(1).toMap().value("warn").toBool());
        // Remembered: a restart lists them before they are heard
        delete tc;
        tc = new TrailCurrent(nullptr);
        tc->m_pollTimer.stop();
        tc->m_flushTimer.stop();
        tick();
        QCOMPARE(tc->inputs().size(), 8);
        QVERIFY(!tc->inputs().at(0).toMap().value("fresh").toBool());
    }

    void airAndAlarms()
    {
        feed(0x1F, { 0xF6, 14, 0x11, 0x94, 0x03, 0x20, 0x00, 0x64 });   // -10 °C, 45 %
        feed(0x20, { 0x00, 0xD2, 0x01, 0xF4, 0x02, 0, 0, 0 });           // 210 ppm, CO alarm
        tick();
        QCOMPARE(tc->air().value("tempC").toInt(), -10);
        QCOMPARE(tc->air().value("rh").toInt(), 45);
        QCOMPARE(tc->air().value("co").toInt(), 210);
        QVERIFY(tc->air().value("coAlarm").toBool());
        QVERIFY(!tc->air().value("coWarn").toBool());
        QVERIFY(!tc->air().value("lost").toBool());
        // Silent for too long: its alarms can no longer be trusted
        tc->m_seen["borealis"] = QDateTime::currentMSecsSinceEpoch() - 10000;
        tick();
        QVERIFY(tc->air().value("lost").toBool());
        QVERIFY(!tc->air().value("fresh").toBool());
    }

    void energyAndTanks()
    {
        feed(0x23, { 13, 25, 1, 4, 50, 84, 50 });   // 13.25 V, -4.5 A, 84.5 %
        feed(0x24, { 0xFF, 0x00, 0x3C, 0xFF, 0xFF }); // -60 W, no time-to-go
        feed(0x3E, { 75, 25, 100 });
        tick();
        QCOMPARE(tc->energy().value("volts").toDouble(), 13.25);
        QCOMPARE(tc->energy().value("amps").toDouble(), -4.5);
        QCOMPARE(tc->energy().value("soc").toDouble(), 84.5);
        QCOMPARE(tc->energy().value("watts").toDouble(), -60.0);
        QCOMPARE(tc->energy().value("ttg").toInt(), -1);
        QCOMPARE(tc->water().value("levels").toList(), (QVariantList { 75, 25, 100 }));
        QVERIFY(tc->water().value("fresh").toBool());
    }

    void refusedWhenListenOnly()
    {
        feed(0x1B, { 0, 0, 0, 0, 0, 0, 0, 0 });
        tick();
        tc->m_listenOnly = true;
        QSignalSpy said(tc, &TrailCurrent::message);
        QVERIFY(!tc->setLight("t0.0", true));
        QCOMPARE(sent(), QStringList {});
        QCOMPARE(said.size(), 1);
        QCOMPARE(said.at(0).at(0).toString(), QStringLiteral("The TrailCurrent bus is set to listen only"));
    }

    void refusedWhenModuleSilent()
    {
        QSignalSpy said(tc, &TrailCurrent::message);
        QVERIFY(!tc->setLight("t2.0", true));
        QCOMPARE(sent(), QStringList {});
        QCOMPARE(said.at(0).at(0).toString(), QStringLiteral("Light 17 is not responding"));
    }

    void namesAndHidingAreKept()
    {
        feed(0x1B, { 0, 0, 0, 0, 0, 0, 0, 0 });
        tick();
        tc->rename("t0.0", "  Porch  ");
        tc->setHidden("t0.1", true);
        tc->setDimmable("t0.2", false);
        delete tc;
        tc = new TrailCurrent(nullptr);
        tc->m_pollTimer.stop();
        tc->m_flushTimer.stop();
        tick();
        QCOMPARE(light("t0.0").value("n").toString(), QStringLiteral("Porch"));
        QVERIFY(light("t0.1").value("hidden").toBool());
        QVERIFY(!light("t0.2").value("dim").toBool());
        QVERIFY(!light("t0.0").value("fresh").toBool());   // known, not heard since start
    }
};

QTEST_GUILESS_MAIN(TestTrailCurrent)
#include "tst_trailcurrent.moc"
