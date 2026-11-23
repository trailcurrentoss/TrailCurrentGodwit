// Dbc against cantools: dbc_fixtures.json holds frames and values cantools
// decoded and encoded (make_dbc_fixtures.py). The TrailCurrent cases need the
// DBC they were made from: GODWIT_TRAILCURRENT_DBC=<path to TrailCurrent.dbc>.
#include "dbc.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include <cmath>

class TestDbc : public QObject
{
    Q_OBJECT

    QJsonObject fixtures;

    void check(const Dbc &dbc, const QJsonArray &cases)
    {
        QVERIFY(!cases.isEmpty());
        for (const auto &v : cases) {
            const QJsonObject c = v.toObject();
            const QString name = c.value("message").toString();
            const Dbc::Message *m = dbc.message(name);
            QVERIFY2(m, qPrintable(name));

            const QByteArray data = QByteArray::fromHex(c.value("data").toString().toLatin1());
            const auto got = Dbc::decode(*m, reinterpret_cast<const uint8_t *>(data.constData()), data.size());
            const QJsonObject want = c.value("decoded").toObject();
            QCOMPARE(got.size(), want.size());
            for (auto it = want.begin(); it != want.end(); ++it) {
                QVERIFY2(got.contains(it.key()), qPrintable(name + "." + it.key()));
                const double a = got.value(it.key()), b = it.value().toDouble();
                QVERIFY2(std::abs(a - b) <= 1e-6 * std::max(1.0, std::abs(b)),
                         qPrintable(QStringLiteral("%1.%2: %3, cantools %4").arg(name, it.key()).arg(a).arg(b)));
            }

            QHash<QString, double> values;
            const QJsonObject in = c.value("values").toObject();
            for (auto it = in.begin(); it != in.end(); ++it)
                values.insert(it.key(), it.value().toDouble());
            const QByteArray enc = Dbc::encode(*m, values);
            QCOMPARE(enc.toHex(), c.value("encoded").toString().toLatin1());
        }
    }

private slots:
    void initTestCase()
    {
        QFile f(QStringLiteral(GODWIT_TEST_DIR "/dbc_fixtures.json"));
        QVERIFY(f.open(QIODevice::ReadOnly));
        fixtures = QJsonDocument::fromJson(f.readAll()).object();
        QVERIFY(!fixtures.isEmpty());
    }

    void littleEndianSignedScaled()
    {
        Dbc dbc;
        QVERIFY(dbc.parse(fixtures.value("extraDbc").toString().toUtf8()));
        check(dbc, fixtures.value("extra").toArray());
    }

    void trailCurrent()
    {
        const QString path = qEnvironmentVariable("GODWIT_TRAILCURRENT_DBC");
        if (path.isEmpty())
            QSKIP("set GODWIT_TRAILCURRENT_DBC to the TrailCurrent.dbc the fixtures were made from");
        Dbc dbc;
        QString error;
        QVERIFY2(dbc.load(path, &error), qPrintable(error));
        check(dbc, fixtures.value("trailcurrent").toArray());

        // The commands, byte for byte as the modules read them
        const auto bright = Dbc::encode(*dbc.message("TorrentBrightness0"), { { "ChannelIndex", 2 }, { "BrightnessLevel", 128 } });
        QCOMPARE(bright.toHex(), QByteArray("0280"));
        const auto toggle = Dbc::encode(*dbc.message("SwitchbackToggle1"), { { "RelayOrCommand", 5 } });
        QCOMPARE(toggle.toHex(), QByteArray("0500"));
        const auto therma = Dbc::encode(*dbc.message("ThermaSetDesiredRequest"), { { "RequestedSetpointDeciC", -2.5 } });
        QCOMPARE(therma.toHex(), QByteArray("ffe7"));
    }

    void rejectsWhatItCannotRead()
    {
        Dbc dbc;
        QVERIFY(!dbc.parse("VERSION \"\"\n"));
        QVERIFY(dbc.isEmpty());
        // A signal running past its message is dropped, the message kept
        QVERIFY(dbc.parse("BO_ 5 Short: 1 X\n SG_ Wide : 7|16@0+ (1,0) [0|0] \"\" X\n"));
        QVERIFY(dbc.message("Short"));
        QVERIFY(!dbc.message("Short")->signal("Wide"));
        // Decoding a frame shorter than the message leaves out what it lacks
        QVERIFY(dbc.parse("BO_ 6 Two: 2 X\n SG_ A : 7|8@0+ (1,0) [0|0] \"\" X\n SG_ B : 15|8@0+ (1,0) [0|0] \"\" X\n"));
        const uint8_t one[] = { 9 };
        const auto got = Dbc::decode(*dbc.message("Two"), one, 1);
        QCOMPARE(got.value("A"), 9.0);
        QVERIFY(!got.contains("B"));
    }
};

QTEST_APPLESS_MAIN(TestDbc)
#include "tst_dbc.moc"
