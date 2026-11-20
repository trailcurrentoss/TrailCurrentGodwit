#include "radio.h"
#include "music.h"

#include <QFileInfo>
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QProcess>
#include <QVariantMap>

#include <algorithm>

Q_LOGGING_CATEGORY(lcRadio, "godwit.radio")

namespace {

constexpr int kPresets = 6;
// What godwit-radio counts as a station; see its main.cpp
constexpr double kStationDb = 9.0;
constexpr double kStationRms = 0.03;

int hzOf(double mhz)
{
    return qRound(mhz * 10) * 100000;
}

// A signal to noise ratio as 0..1: noise reads 3 dB, the strongest stations
// nearly 20
double signalOf(double snrDb)
{
    return qRound(std::clamp((snrDb - 3) / 16, 0.0, 1.0) * 20) / 20.0;
}

// Programme types as North America's RBDS has them
QString genreOf(int pty)
{
    static const char *const names[] = {
        "", "News", "Information", "Sports", "Talk", "Rock", "Classic Rock", "Adult Hits",
        "Soft Rock", "Top 40", "Country", "Oldies", "Soft", "Nostalgia", "Jazz", "Classical",
        "R&B", "Soft R&B", "Language", "Religious Music", "Religious Talk", "Personality",
        "Public", "College", "", "", "", "", "", "Weather", "", "",
    };
    return pty >= 0 && pty < 32 ? QString::fromLatin1(names[pty]) : QString();
}

} // namespace

Radio::Radio(QObject *parent)
    : QObject(parent)
    , m_bin(qEnvironmentVariable("GODWIT_RADIO_BIN", QStringLiteral("/usr/bin/godwit-radio")))
{
    const bool wanted = qEnvironmentVariableIntValue("GODWIT_RADIO") == 1
        || QGuiApplication::platformName() == QLatin1String("eglfs");
    m_available = wanted && QFileInfo::exists(m_bin);

    const QVariantMap saved = loadSettings(QStringLiteral("radio"));
    const int hz = saved.value(QStringLiteral("hz")).toInt();
    if (hz > 0)
        m_hz = hz;
    m_presets = saved.value(QStringLiteral("presets")).toList();
    while (m_presets.size() < kPresets)
        m_presets.append(QVariantMap{ { QStringLiteral("b"), QStringLiteral("FM") }, { QStringLiteral("f"), 0.0 } });
    const QVariantMap names = saved.value(QStringLiteral("names")).toMap();
    for (auto it = names.begin(); it != names.end(); ++it)
        m_names.insert(it.key().toInt(), it.value().toString());
    m_name = m_names.value(m_hz);

    m_tuneDelay.setSingleShot(true);
    m_tuneDelay.setInterval(60);
    connect(&m_tuneDelay, &QTimer::timeout, this, [this] {
        send("tune " + QByteArray::number(m_hz) + '\n');
    });
    m_retry.setSingleShot(true);
    m_retry.setInterval(2000);
    connect(&m_retry, &QTimer::timeout, this, &Radio::start);
    m_saveDelay.setSingleShot(true);
    m_saveDelay.setInterval(2000);
    connect(&m_saveDelay, &QTimer::timeout, this, &Radio::save);
}

Radio::~Radio()
{
    if (m_saveDelay.isActive())
        save();
    // So that its ending does not start another
    m_active = false;
    stop();
    // Give it the chance to hand the dongle back before the process goes
    if (m_exiting && !m_exiting->waitForFinished(500))
        m_exiting->kill();
}

void Radio::setActive(bool active)
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

void Radio::setFrequency(double mhz)
{
    const int hz = hzOf(mhz);
    if (!m_available || hz == m_hz)
        return;
    m_hz = hz;
    // Takes over from a seek or a scan in progress
    m_seeking = false;
    m_scanning = false;
    clearStation();
    emit tunedTo(frequency());
    m_saveDelay.start();
    if (m_helper)
        m_tuneDelay.start();
}

void Radio::seek(int direction)
{
    if (!m_helper)
        return;
    m_tuneDelay.stop();
    m_seeking = true;
    m_scanning = false;
    clearStation();
    send(direction < 0 ? "seek -1\n" : "seek 1\n");
}

void Radio::savePresets(const QVariantList &presets)
{
    if (presets == m_presets)
        return;
    m_presets = presets;
    emit presetsChanged();
    m_saveDelay.start();
}

QString Radio::nameAt(double mhz) const
{
    return m_names.value(hzOf(mhz));
}

double Radio::signalAt(double mhz) const
{
    return m_levels.value(hzOf(mhz), 0.0);
}

QVariantList Radio::known() const
{
    QVariantList out;
    out.reserve(m_names.size());
    for (auto it = m_names.begin(); it != m_names.end(); ++it)
        out.append(QVariantMap{ { QStringLiteral("f"), it.key() / 1e6 }, { QStringLiteral("n"), it.value() } });
    return out;
}

bool Radio::presetsEmpty() const
{
    return std::all_of(m_presets.begin(), m_presets.end(), [](const QVariant &p) {
        return p.toMap().value(QStringLiteral("f")).toDouble() <= 0;
    });
}

void Radio::start()
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
        qCWarning(lcRadio) << "godwit-radio" << e << helper->errorString();
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
        qCInfo(lcRadio) << "tuner stopped" << code;
        m_helper = nullptr;
        if (m_player) {
            m_player->kill();
            m_player->deleteLater();
            m_player = nullptr;
        }
        m_seeking = m_scanning = false;
        m_receiving = false;
        m_signal = 0;
        m_helperHz = 0;
        // 2 is no dongle. Otherwise it was the sound that stopped: PipeWire
        // starts after the HMI, and restarts on a deploy.
        if (code == 2)
            m_tunerFound = false;
        updateHeld();
        emit stateChanged();
        if (m_active)
            m_retry.start();
    });

    // The sound first, so it is there for the tuner's first samples
    m_player->start(QStringLiteral("pw-play"),
                    { QStringLiteral("--raw"), QStringLiteral("--rate"), QStringLiteral("57000"),
                      QStringLiteral("--channels"), QStringLiteral("2"), QStringLiteral("--format"), QStringLiteral("s16"),
                      QStringLiteral("--media-role"), QStringLiteral("Multimedia"), QStringLiteral("-") });
    m_helper->start(m_bin, { QStringLiteral("--freq"), QString::number(m_hz) });
    updateHeld();
    qCInfo(lcRadio) << "listening on" << frequency();
}

void Radio::updateHeld()
{
    const bool held = m_helper || m_exiting;
    if (held == m_held)
        return;
    m_held = held;
    emit tunerHeldChanged();
}

void Radio::stop()
{
    m_retry.stop();
    m_tuneDelay.stop();
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
    m_seeking = m_scanning = false;
    m_receiving = false;
    m_signal = 0;
    m_stereo = false;
    m_helperHz = 0;
    updateHeld();
    emit stateChanged();
}

void Radio::send(const QByteArray &command)
{
    if (m_helper)
        m_helper->write(command);
}

void Radio::readHelper()
{
    m_buffer += m_helper->readAllStandardError();
    int nl;
    while ((nl = m_buffer.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_buffer.left(nl));
        m_buffer.remove(0, nl + 1);
        handleLine(line);
    }
}

void Radio::handleLine(const QString &line)
{
    const int space = line.indexOf(QLatin1Char(' '));
    const QString key = space < 0 ? line : line.left(space);
    const QString value = space < 0 ? QString() : line.mid(space + 1);

    if (key == QLatin1String("status")) {
        const QStringList p = value.split(QLatin1Char(' '));
        if (p.size() < 4)
            return;
        const double snr = p[0].toDouble(), rms = p[1].toDouble();
        const bool receiving = snr >= kStationDb && rms >= kStationRms;
        const double signal = signalOf(snr);
        const bool stereo = p[3].toDouble() > 0.3;
        // Status comes only while listening, settled on m_helperHz
        if (m_helperHz)
            m_levels.insert(m_helperHz, receiving ? signal : 0.0);
        if (receiving == m_receiving && signal == m_signal && stereo == m_stereo)
            return;
        m_receiving = receiving;
        m_signal = signal;
        m_stereo = stereo;
        emit stateChanged();
    } else if (key == QLatin1String("tuned")) {
        const int hz = value.toInt();
        m_helperHz = hz;
        // A scan's sweep is not shown; the dial stays where it was
        if (m_scanning)
            return;
        // Only a seek moves the dial. A knob turned since is what counts.
        if (hz != m_hz && (m_seeking || !m_tuneDelay.isActive())) {
            m_hz = hz;
            clearStation();
            emit tunedTo(frequency());
        }
    } else if (key == QLatin1String("seekdone")) {
        m_seeking = false;
        m_saveDelay.start();
        emit stateChanged();
    } else if (key == QLatin1String("station")) {
        const QStringList p = value.split(QLatin1Char(' '));
        if (p.size() == 2) {
            m_found.append({ p[0].toInt(), p[1].toDouble() });
            m_levels.insert(p[0].toInt(), signalOf(p[1].toDouble()));
        }
    } else if (key == QLatin1String("scandone")) {
        m_scanning = false;
        applyScan();
    } else if (key == QLatin1String("call")) {
        m_call = value;
        learnName();
    } else if (key == QLatin1String("ps")) {
        m_ps = value;
        learnName();
    } else if (key == QLatin1String("pty")) {
        m_genre = genreOf(value.toInt());
        emit stateChanged();
    } else if (key == QLatin1String("rt")) {
        m_text = value;
        emit stateChanged();
    } else if (key == QLatin1String("ready")) {
        if (!m_tunerFound) {
            m_tunerFound = true;
            emit stateChanged();
        }
        if (presetsEmpty()) {
            qCInfo(lcRadio) << "no presets yet: scanning the band";
            m_found.clear();
            // The scan measures the whole band: what it does not find is not there
            m_levels.clear();
            m_scanning = true;
            clearStation();
            send("scan\n");
        }
    } else if (key == QLatin1String("error")) {
        qCInfo(lcRadio) << "no tuner";
    }
}

void Radio::clearStation()
{
    m_call.clear();
    m_ps.clear();
    m_genre.clear();
    m_text.clear();
    // What it was called last time, until it says so again
    m_name = m_names.value(m_hz);
    m_receiving = false;
    m_signal = 0;
    m_stereo = false;
    emit stateChanged();
}

// The strongest stations become the presets, in order up the band, and the
// strongest of all plays
void Radio::applyScan()
{
    qCInfo(lcRadio) << "found" << m_found.size() << "stations";
    auto found = m_found;
    m_found.clear();
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    if (found.isEmpty()) {
        emit stateChanged();
        return;
    }
    const int strongest = found.first().first;
    found = found.mid(0, kPresets);
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });

    QVariantList presets;
    for (const auto &f : std::as_const(found))
        presets.append(QVariantMap{ { QStringLiteral("b"), QStringLiteral("FM") }, { QStringLiteral("f"), f.first / 1e6 } });
    while (presets.size() < kPresets)
        presets.append(QVariantMap{ { QStringLiteral("b"), QStringLiteral("FM") }, { QStringLiteral("f"), 0.0 } });
    m_presets = presets;
    emit presetsChanged();

    setFrequency(strongest / 1e6);
    emit stateChanged();
}

// The call sign, which never changes, over the name the station sends: many
// scroll the song through that
void Radio::learnName()
{
    const QString name = !m_call.isEmpty() ? m_call : m_ps;
    if (name.isEmpty() || name == m_name)
        return;
    m_name = name;
    emit stateChanged();
    if (m_names.value(m_hz) != name) {
        m_names.insert(m_hz, name);
        emit knownChanged();
        m_saveDelay.start();
    }
}

void Radio::save()
{
    QVariantMap names;
    for (auto it = m_names.begin(); it != m_names.end(); ++it)
        names.insert(QString::number(it.key()), it.value());
    saveSettings(QStringLiteral("radio"), { { QStringLiteral("hz"), m_hz },
                                            { QStringLiteral("presets"), m_presets },
                                            { QStringLiteral("names"), names } });
}
