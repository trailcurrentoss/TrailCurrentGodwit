#include "weatherradio.h"
#include "music.h"

#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QProcess>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <iterator>

Q_LOGGING_CATEGORY(lcWeatherRadio, "godwit.weatherradio")

namespace {

constexpr int kChannels[] = { 162400000, 162425000, 162450000, 162475000, 162500000, 162525000, 162550000 };
constexpr int kCount = int(std::size(kChannels));
// How far above the noise a channel must stand to count as a station. An empty one
// reads under 1 dB in godwit-wxradio's tests, and a station just heard about 6.
constexpr double kStationDb = 6.0;
// The bars: from nothing at 3 dB to full at 30
constexpr double kLevelLowDb = 3.0;
constexpr double kLevelHighDb = 30.0;

bool isChannel(int hz)
{
    return std::find(std::begin(kChannels), std::end(kChannels), hz) != std::end(kChannels);
}

double milesBetween(double lat1, double lon1, double lat2, double lon2)
{
    const double k = M_PI / 180;
    const double a = std::pow(std::sin((lat2 - lat1) * k / 2), 2)
        + std::cos(lat1 * k) * std::cos(lat2 * k) * std::pow(std::sin((lon2 - lon1) * k / 2), 2);
    return 2 * std::asin(std::min(1.0, std::sqrt(a))) * 3958.8;
}

} // namespace

WeatherRadio::WeatherRadio(QObject *parent)
    : QObject(parent)
    , m_bin(qEnvironmentVariable("GODWIT_WXRADIO_BIN", QStringLiteral("/usr/bin/godwit-wxradio")))
{
    const bool wanted = qEnvironmentVariableIntValue("GODWIT_RADIO") == 1
        || QGuiApplication::platformName() == QLatin1String("eglfs");
    m_available = wanted && QFileInfo::exists(m_bin);

    clearLevels();
    const int hz = loadSettings(QStringLiteral("weatherradio")).value(QStringLiteral("hz")).toInt();
    if (isChannel(hz)) {
        m_hz = hz;
        m_chosen = true;
    }

    m_retry.setSingleShot(true);
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, &WeatherRadio::start);
    m_saveDelay.setSingleShot(true);
    m_saveDelay.setInterval(2000);
    connect(&m_saveDelay, &QTimer::timeout, this, [this] {
        saveSettings(QStringLiteral("weatherradio"), { { QStringLiteral("hz"), m_hz } });
    });

    if (m_available)
        loadTable(qEnvironmentVariable("GODWIT_WX_STATIONS", QStringLiteral("/usr/share/godwit/weather/nwr-stations.tsv")));
}

WeatherRadio::~WeatherRadio()
{
    if (m_saveDelay.isActive())
        saveSettings(QStringLiteral("weatherradio"), { { QStringLiteral("hz"), m_hz } });
    // So that its ending does not start another
    m_active = false;
    stop();
    // Give it the chance to hand the dongle back before the process goes
    if (m_exiting && !m_exiting->waitForFinished(500))
        m_exiting->kill();
}

void WeatherRadio::loadTable(const QString &path)
{
    QFile file(path);
    // Without it the app still works: the channels just go unnamed
    if (!file.open(QIODevice::ReadOnly)) {
        qCInfo(lcWeatherRadio) << "no station table at" << path;
        return;
    }
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const QList<QByteArray> p = line.split('\t');
        if (p.size() != 6)
            continue;
        bool hzOk, latOk, lonOk;
        const Station s { p[0].toInt(&hzOk), QString::fromUtf8(p[1]), QString::fromUtf8(p[4]), QString::fromUtf8(p[5]),
                          p[2].toDouble(&latOk), p[3].toDouble(&lonOk) };
        if (hzOk && latOk && lonOk && isChannel(s.hz))
            m_stations.append(s);
    }
    qCInfo(lcWeatherRadio) << m_stations.size() << "stations";
}

QVariantList WeatherRadio::channels(bool located, double lat, double lon) const
{
    QVariantList out;
    out.reserve(kCount);
    for (const int hz : kChannels) {
        QVariantMap row {
            { QStringLiteral("mhz"), hz / 1e6 },
            { QStringLiteral("f"), QString::number(hz / 1e6, 'f', 3) },
        };
        if (located) {
            const Station *nearest = nullptr;
            double best = 0;
            for (const Station &s : m_stations) {
                if (s.hz != hz)
                    continue;
                const double miles = milesBetween(lat, lon, s.lat, s.lon);
                if (!nearest || miles < best) {
                    nearest = &s;
                    best = miles;
                }
            }
            if (nearest) {
                row.insert(QStringLiteral("n"), nearest->site);
                row.insert(QStringLiteral("c"), nearest->call);
                row.insert(QStringLiteral("st"), nearest->state);
                row.insert(QStringLiteral("mi"), best);
            }
        }
        out.append(row);
    }
    return out;
}

void WeatherRadio::setActive(bool active)
{
    if (active == m_active)
        return;
    m_active = active;
    emit activeChanged();
    if (active)
        start();
    else
        stop();
}

void WeatherRadio::setFrequency(double mhz)
{
    const int hz = qRound(mhz * 1000) * 1000;
    if (!isChannel(hz))
        return;
    m_chosen = true;
    tune(hz);
}

void WeatherRadio::tune(int hz)
{
    if (hz == m_hz)
        return;
    m_hz = hz;
    if (m_receiving) {
        m_receiving = false;
        emit stateChanged();
    }
    emit frequencyChanged();
    m_saveDelay.start();
    if (m_helper)
        m_helper->write("tune " + QByteArray::number(hz) + '\n');
}

bool WeatherRadio::tuneStrongest()
{
    const auto top = std::max_element(m_levelsDb.begin(), m_levelsDb.end());
    if (top == m_levelsDb.end() || *top < kStationDb)
        return false;
    m_chosen = true;
    tune(kChannels[top - m_levelsDb.begin()]);
    return true;
}

void WeatherRadio::start()
{
    if (!m_available || !m_active || m_helper)
        return;
    // The dongle cannot be opened twice; finished() starts this again
    if (m_exiting)
        return;

    m_helper = new QProcess(this);
    m_player = new QProcess(this);
    m_buffer.clear();
    m_helper->setStandardOutputProcess(m_player);
    m_player->setStandardOutputFile(QProcess::nullDevice());
    m_player->setStandardErrorFile(QProcess::nullDevice());

    QProcess *helper = m_helper;
    connect(helper, &QProcess::readyReadStandardError, this, [this, helper] {
        if (helper == m_helper)
            readHelper();
    });
    connect(helper, &QProcess::errorOccurred, this, [helper](QProcess::ProcessError e) {
        qCWarning(lcWeatherRadio) << "godwit-wxradio" << e << helper->errorString();
    });
    connect(helper, &QProcess::finished, this, [this, helper](int code) {
        helper->deleteLater();
        if (helper == m_exiting) {
            m_exiting = nullptr;
            updateHeld();
            start();
            return;
        }
        if (helper != m_helper)
            return;
        // By itself: the dongle was pulled out, or was never there
        qCInfo(lcWeatherRadio) << "tuner stopped" << code;
        m_helper = nullptr;
        if (m_player) {
            m_player->kill();
            m_player->deleteLater();
            m_player = nullptr;
        }
        m_listening = false;
        m_receiving = false;
        // 2 is no dongle. Otherwise it was the sound that stopped: PipeWire starts
        // after the HMI, and restarts on a deploy.
        if (code == 2)
            m_tunerFound = false;
        clearLevels();
        updateHeld();
        emit stateChanged();
        if (m_active)
            m_retry.start();
    });

    // The sound first, so it is there for the tuner's first samples
    m_player->start(QStringLiteral("pw-play"),
                    { QStringLiteral("--raw"), QStringLiteral("--rate"), QStringLiteral("48000"),
                      QStringLiteral("--channels"), QStringLiteral("1"), QStringLiteral("--format"), QStringLiteral("s16"),
                      QStringLiteral("--media-role"), QStringLiteral("Multimedia"), QStringLiteral("-") });
    m_helper->start(m_bin, { QStringLiteral("--freq"), QString::number(m_hz) });
    updateHeld();
    qCInfo(lcWeatherRadio) << "listening";
    qCDebug(lcWeatherRadio) << "on" << frequency();
}

void WeatherRadio::updateHeld()
{
    const bool held = m_helper || m_exiting;
    if (held == m_held)
        return;
    m_held = held;
    emit tunerHeldChanged();
}

void WeatherRadio::clearLevels()
{
    m_levelsDb = QList<double>(kCount, 0.0);
    m_levels = QVariantList(kCount, 0.0);
    emit levelsChanged();
}

void WeatherRadio::stop()
{
    m_retry.stop();
    if (m_player) {
        m_player->kill();
        m_player->deleteLater();
        m_player = nullptr;
    }
    if (m_helper) {
        // Closing its stdin ends it, which lets it hand the dongle back cleanly.
        // Not waited for here: this is the UI's thread.
        QProcess *helper = m_helper;
        m_helper = nullptr;
        m_exiting = helper;
        helper->closeWriteChannel();
        QTimer::singleShot(1500, helper, [helper] { helper->kill(); });
    }
    m_listening = false;
    m_receiving = false;
    clearLevels();
    updateHeld();
    emit stateChanged();
}

void WeatherRadio::readHelper()
{
    m_buffer += m_helper->readAllStandardError();
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(nl);
        m_buffer.remove(0, nl + 1);
        handleLine(line);
    }
}

void WeatherRadio::handleLine(const QByteArray &line)
{
    const QList<QByteArray> p = line.split(' ');
    if (p[0] == "ready") {
        m_listening = true;
        m_tunerFound = true;
        emit stateChanged();
    } else if (p[0] == "error") {
        qCInfo(lcWeatherRadio) << "no tuner";
    } else if (p[0] == "status" && p.size() == 4) {
        const bool receiving = p[3] == "1";
        // For troubleshooting, in verbose logging: a line now and then says what the
        // tuner hears, so a quiet screen can be told apart. Two status lines a second.
        if (++m_statusLines >= 60) {
            m_statusLines = 0;
            qCDebug(lcWeatherRadio) << frequency() << "snr" << p[1] << "speech" << p[2] << "receiving" << receiving
                                   << "levels" << m_levelsDb;
        }
        if (receiving != m_receiving) {
            m_receiving = receiving;
            qCDebug(lcWeatherRadio) << frequency() << (receiving ? "station heard" : "no station") << "snr" << p[1];
            emit stateChanged();
        }
    } else if (p[0] == "levels" && p.size() == kCount + 1) {
        QVariantList levels;
        for (int i = 0; i < kCount; ++i) {
            const double db = p[i + 1].toDouble();
            m_levelsDb[i] = db;
            levels.append(std::clamp((db - kLevelLowDb) / (kLevelHighDb - kLevelLowDb), 0.0, 1.0));
        }
        // The first time it is opened: the strongest there is
        if (!m_chosen)
            tuneStrongest();
        if (levels != m_levels) {
            m_levels = levels;
            emit levelsChanged();
        }
    }
}
