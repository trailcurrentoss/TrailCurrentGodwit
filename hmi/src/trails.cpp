#include "trails.h"

#include <QDir>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QThreadPool>
#include <QTimeZone>
#include <QXmlStreamReader>

#include <algorithm>
#include <cmath>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcTrails, "godwit.trails")

namespace {

const QString SettingsName = QStringLiteral(".trails.json");
const QString RecordingName = QStringLiteral(".recording.gpx");

constexpr double EarthMeters = 6371008.8;
constexpr double MetersPerMile = 1609.344;
constexpr double DegToRad = M_PI / 180.0;

// GPS noise: a line kept to within this is as good as the line it came from
constexpr double ThinMeters = 5.0;
// Thinned harder past this, so one enormous file cannot slow the map down
constexpr int MaxPointsPerFile = 60000;
// Elevation is trusted only when nearly every point has one; a climb is counted
// only once it passes this, so the receiver's jitter does not add up
constexpr double ClimbStepM = 3.0;
constexpr double ElevationShare = 0.9;
// Recording: a fix nearer the last one than this is noise or a standstill
constexpr double RecordStepM = 5.0;
// The on-screen line takes a point at most this often, however long the drive
constexpr double RecordLineStepM = 25.0;
// No fix for this long starts a new part, rather than a straight line across
// wherever the receiver lost the sky
constexpr qint64 RecordGapMs = 60000;
constexpr qint64 RecordSyncMs = 10000;
// Faster than any vehicle: a bad fix
constexpr double MaxJumpMetersPerSecond = 120.0;

double metersBetween(const Trails::Point &a, const Trails::Point &b)
{
    const double la1 = a.lat * DegToRad, la2 = b.lat * DegToRad;
    const double dLa = la2 - la1, dLo = (b.lon - a.lon) * DegToRad;
    const double h = std::sin(dLa / 2) * std::sin(dLa / 2) + std::cos(la1) * std::cos(la2) * std::sin(dLo / 2) * std::sin(dLo / 2);
    return 2 * EarthMeters * std::asin(std::min(1.0, std::sqrt(h)));
}

// Douglas-Peucker, on a flat projection around the line: good enough at the
// scale of a tolerance of metres. Iterative, as tracks run to many thousands
// of points.
QList<Trails::Point> thin(const QList<Trails::Point> &in, double toleranceM)
{
    const int n = int(in.size());
    if (n < 3)
        return in;
    const double xScale = std::cos(in[n / 2].lat * DegToRad) * DegToRad * EarthMeters;
    const double yScale = DegToRad * EarthMeters;
    QList<bool> keep(n, false);
    keep[0] = keep[n - 1] = true;
    QList<std::pair<int, int>> todo{ { 0, n - 1 } };
    while (!todo.isEmpty()) {
        const auto [first, last] = todo.takeLast();
        if (last - first < 2)
            continue;
        const double ax = in[first].lon * xScale, ay = in[first].lat * yScale;
        const double dx = in[last].lon * xScale - ax, dy = in[last].lat * yScale - ay;
        const double len2 = dx * dx + dy * dy;
        double worst = 0;
        int at = -1;
        for (int i = first + 1; i < last; ++i) {
            const double px = in[i].lon * xScale - ax, py = in[i].lat * yScale - ay;
            const double t = len2 > 0 ? std::clamp((px * dx + py * dy) / len2, 0.0, 1.0) : 0;
            const double ex = px - t * dx, ey = py - t * dy;
            const double d = ex * ex + ey * ey;
            if (d > worst) {
                worst = d;
                at = i;
            }
        }
        if (at >= 0 && worst > toleranceM * toleranceM) {
            keep[at] = true;
            todo.append({ first, at });
            todo.append({ at, last });
        }
    }
    QList<Trails::Point> out;
    for (int i = 0; i < n; ++i)
        if (keep[i])
            out.append(in[i]);
    return out;
}

int countPoints(const QList<QList<Trails::Point>> &parts)
{
    int n = 0;
    for (const auto &p : parts)
        n += int(p.size());
    return n;
}

// A file name read as a name
QString nameFromFile(const QString &path)
{
    QString s = QFileInfo(path).completeBaseName();
    s.replace(QLatin1Char('_'), QLatin1Char(' '));
    return s.simplified();
}

QByteArray pointJson(const Trails::Point &p)
{
    // Six places is about a tenth of a metre
    return '[' + QByteArray::number(p.lon, 'f', 6) + ',' + QByteArray::number(p.lat, 'f', 6) + ']';
}

QByteArray lineJson(const QList<Trails::Point> &line)
{
    QByteArray out;
    out.reserve(line.size() * 24 + 2);
    out += '[';
    for (qsizetype i = 0; i < line.size(); ++i) {
        if (i)
            out += ',';
        out += pointJson(line[i]);
    }
    out += ']';
    return out;
}

QByteArray collectionJson(const QByteArray &features)
{
    return "{\"type\":\"FeatureCollection\",\"features\":[" + features + "]}";
}

} // namespace

Trails::Parsed Trails::parse(const QString &path)
{
    Parsed out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return out;

    QList<QList<Point>> tracks, routes;
    QStringList stack;
    QString metaName;
    QStringList topNames;   // of each <trk> and <rte>
    bool inTrkpt = false;
    int withEle = 0, total = 0;
    double climb = 0;
    bool haveEle = false;
    double lowEle = 0;

    QXmlStreamReader xml(&f);
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QStringView tag = xml.name();
            const QString parent = stack.isEmpty() ? QString() : stack.last();
            if (tag == QLatin1String("trkseg")) {
                tracks.append(QList<Point>{});
                // The next section can start anywhere: no climb between them
                haveEle = false;
            } else if (tag == QLatin1String("rte")) {
                routes.append(QList<Point>{});
            } else if ((tag == QLatin1String("trkpt") && !tracks.isEmpty()) || (tag == QLatin1String("rtept") && parent == QLatin1String("rte"))) {
                bool okLat = false, okLon = false;
                const auto attrs = xml.attributes();
                const double lat = attrs.value(QLatin1String("lat")).toDouble(&okLat);
                const double lon = attrs.value(QLatin1String("lon")).toDouble(&okLon);
                if (okLat && okLon && std::abs(lat) <= 90 && std::abs(lon) <= 180) {
                    const bool track = tag == QLatin1String("trkpt");
                    (track ? tracks : routes).last().append({ lat, lon });
                    inTrkpt = true;
                    if (track)
                        ++total;
                }
            } else if (tag == QLatin1String("ele") && inTrkpt && parent == QLatin1String("trkpt")) {
                bool ok = false;
                const double e = xml.readElementText().toDouble(&ok);
                if (ok) {
                    ++withEle;
                    if (!haveEle) {
                        haveEle = true;
                        lowEle = e;
                    } else if (e < lowEle) {
                        lowEle = e;
                    } else if (e - lowEle >= ClimbStepM) {
                        climb += e - lowEle;
                        lowEle = e;
                    }
                }
                continue;   // readElementText consumed the end tag
            } else if (tag == QLatin1String("name")) {
                if (parent == QLatin1String("metadata")) {
                    metaName = xml.readElementText().simplified();
                    continue;
                }
                if (parent == QLatin1String("trk") || parent == QLatin1String("rte")) {
                    topNames << xml.readElementText().simplified();
                    continue;
                }
            }
            stack.append(tag.toString());
        } else if (token == QXmlStreamReader::EndElement) {
            if (!stack.isEmpty())
                stack.removeLast();
            if (xml.name() == QLatin1String("trkpt") || xml.name() == QLatin1String("rtept"))
                inTrkpt = false;
        }
    }
    if (xml.hasError())
        qCWarning(lcTrails) << path << "line" << xml.lineNumber() << xml.errorString() << ": keeping what came before it";

    // A single point draws nothing
    const auto lines = [](const QList<QList<Point>> &in) {
        QList<QList<Point>> kept;
        for (const auto &p : in)
            if (p.size() >= 2)
                kept.append(p);
        return kept;
    };
    const QList<QList<Point>> raw = countPoints(lines(tracks)) ? lines(tracks) : lines(routes);
    if (raw.isEmpty())
        return out;

    out.points = countPoints(raw);
    out.gainM = total > 0 && double(withEle) / total >= ElevationShare ? climb : 0;
    out.west = out.south = 1000;
    out.east = out.north = -1000;
    for (const auto &part : raw) {
        for (qsizetype i = 0; i < part.size(); ++i) {
            const Point &p = part[i];
            out.west = std::min(out.west, p.lon);
            out.east = std::max(out.east, p.lon);
            out.south = std::min(out.south, p.lat);
            out.north = std::max(out.north, p.lat);
            if (i > 0)
                out.miles += metersBetween(part[i - 1], p) / MetersPerMile;
        }
    }

    for (double tol = ThinMeters;; tol *= 2) {
        out.parts.clear();
        for (const auto &part : raw)
            out.parts.append(thin(part, tol));
        if (countPoints(out.parts) <= MaxPointsPerFile)
            break;
    }

    // The file's own name for itself, or the only track's, or the file name:
    // when there are many tracks, theirs name the sections
    if (!metaName.isEmpty())
        out.name = metaName;
    else if (topNames.size() == 1 && !topNames.first().isEmpty())
        out.name = topNames.first();
    else
        out.name = nameFromFile(path);
    out.name.replace(QLatin1Char('_'), QLatin1Char(' '));
    return out;
}

Trails::Trails(QObject *parent)
    : MediaFolder("GODWIT_TRAILS_DIR", QStringLiteral("/data/media/trails"), { QStringLiteral("gpx") }, parent)
{
    // Where someone has driven is their own business, as with the other media
    // folders. Made here only if the image did not make it.
    if (!QFileInfo::exists(dir()) && QDir().mkpath(dir()))
        QFile::setPermissions(dir(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup);
    loadSettings();
    resume();
    rebuildOverlay();
    connect(this, &MediaFolder::itemsChanged, this, [this] {
        if (m_busy.isEmpty())
            QTimer::singleShot(0, this, &Trails::next);
    });
    start();
}

QStringList Trails::colors() const
{
    return { QStringLiteral("#E53935"), QStringLiteral("#EF6C00"), QStringLiteral("#C79100"), QStringLiteral("#0097A7"),
             QStringLiteral("#1E88E5"), QStringLiteral("#AB47BC"), QStringLiteral("#E91E8C"), QStringLiteral("#607D8B") };
}

void Trails::decorate(QVariantMap &row, const QFileInfo &file)
{
    const QString key = file.fileName();
    const QJsonObject s = m_settings.value(key).toObject();
    const auto it = m_parsed.constFind(key);
    const bool ready = it != m_parsed.constEnd();
    const bool bad = ready && it->parts.isEmpty();

    QString name = s.value(QLatin1String("name")).toString();
    if (name.isEmpty())
        name = ready && !it->name.isEmpty() ? it->name : nameFromFile(file.absoluteFilePath());
    row.insert(QStringLiteral("file"), key);
    row.insert(QStringLiteral("n"), name);
    row.insert(QStringLiteral("color"), s.value(QLatin1String("color")).toString(colors().first()));
    row.insert(QStringLiteral("on"), s.value(QLatin1String("on")).toBool(false));
    row.insert(QStringLiteral("rec"), s.value(QLatin1String("rec")).toBool(false));
    row.insert(QStringLiteral("ready"), ready);
    row.insert(QStringLiteral("bad"), bad);
    if (ready && !bad) {
        row.insert(QStringLiteral("mi"), it->miles);
        row.insert(QStringLiteral("gainM"), it->gainM);
        row.insert(QStringLiteral("parts"), int(it->parts.size()));
        row.insert(QStringLiteral("pts"), it->points);
        row.insert(QStringLiteral("west"), it->west);
        row.insert(QStringLiteral("south"), it->south);
        row.insert(QStringLiteral("east"), it->east);
        row.insert(QStringLiteral("north"), it->north);
    }
}

// The next file not read yet, one at a time
void Trails::next()
{
    if (!m_busy.isEmpty())
        return;
    for (const QString &path : files()) {
        const QString key = QFileInfo(path).fileName();
        if (m_parsed.contains(key))
            continue;
        m_busy = key;
        QThreadPool::globalInstance()->start([self = QPointer(this), key, path] {
            Parsed result = parse(path);
            QMetaObject::invokeMethod(self, [self, key, result] {
                if (!self)
                    return;
                self->m_parsed.insert(key, result);
                self->m_busy.clear();
                qCInfo(lcTrails).nospace() << key << ": " << result.parts.size() << " parts, " << result.points << " points, "
                                           << countPoints(result.parts) << " kept, " << result.miles << " mi";
                if (!result.parts.isEmpty())
                    self->adopt(key);
                self->refreshItems();   // and that looks for the next file
                self->rebuildOverlay();
            }, Qt::QueuedConnection);
        });
        return;
    }
}

// A file seen for the first time goes on the map, in a color of its own
void Trails::adopt(const QString &file)
{
    QJsonObject s = m_settings.value(file).toObject();
    if (s.contains(QLatin1String("color")) && s.contains(QLatin1String("on")))
        return;
    if (!s.contains(QLatin1String("color")))
        s.insert(QStringLiteral("color"), nextColor());
    if (!s.contains(QLatin1String("on")))
        s.insert(QStringLiteral("on"), true);
    m_settings.insert(file, s);
    saveSettings();
}

// The color used least so far, the earliest in the list on a tie
QString Trails::nextColor() const
{
    const QStringList all = colors();
    QHash<QString, int> used;
    for (auto it = m_settings.constBegin(); it != m_settings.constEnd(); ++it)
        ++used[it.value().toObject().value(QLatin1String("color")).toString()];
    QString best = all.first();
    for (const QString &c : all)
        if (used.value(c) < used.value(best))
            best = c;
    return best;
}

void Trails::rebuildOverlay()
{
    QByteArray features;
    int count = 0;
    for (auto it = m_parsed.constBegin(); it != m_parsed.constEnd(); ++it) {
        const QJsonObject s = m_settings.value(it.key()).toObject();
        if (it->parts.isEmpty() || !s.value(QLatin1String("on")).toBool(false))
            continue;
        if (count++)
            features += ',';
        features += "{\"type\":\"Feature\",\"properties\":{\"color\":\"" + s.value(QLatin1String("color")).toString(colors().first()).toLatin1()
            + "\"},\"geometry\":{\"type\":\"MultiLineString\",\"coordinates\":[";
        for (qsizetype i = 0; i < it->parts.size(); ++i) {
            if (i)
                features += ',';
            features += lineJson(it->parts[i]);
        }
        features += "]}}";
    }
    m_onCount = count;
    m_overlay = collectionJson(features);
    emit overlayChanged();
}

void Trails::setOn(const QString &file, bool on)
{
    QJsonObject s = m_settings.value(file).toObject();
    s.insert(QStringLiteral("on"), on);
    m_settings.insert(file, s);
    saveSettings();
    refreshItems();
    rebuildOverlay();
}

void Trails::setColor(const QString &file, const QString &color)
{
    static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    if (!hex.match(color).hasMatch())
        return;
    QJsonObject s = m_settings.value(file).toObject();
    s.insert(QStringLiteral("color"), color);
    m_settings.insert(file, s);
    saveSettings();
    refreshItems();
    rebuildOverlay();
}

void Trails::rename(const QString &file, const QString &name)
{
    const QString clean = name.simplified();
    if (clean.isEmpty())
        return;
    QJsonObject s = m_settings.value(file).toObject();
    s.insert(QStringLiteral("name"), clean);
    m_settings.insert(file, s);
    saveSettings();
    if (s.value(QLatin1String("rec")).toBool(false))
        writeNameInto(file, clean);
    refreshItems();
    rebuildOverlay();
}

void Trails::remove(const QString &file)
{
    // Only ever a file of this folder, whatever the name says
    if (file != QFileInfo(file).fileName() || !QFile::remove(dir() + QLatin1Char('/') + file)) {
        qCWarning(lcTrails) << "could not delete" << file;
        return;
    }
    m_parsed.remove(file);
    m_settings.remove(file);
    saveSettings();
    rebuildOverlay();
    // The folder's watcher takes the row out
}

QVariantMap Trails::shape(const QString &file) const
{
    const auto it = m_parsed.constFind(file);
    if (it == m_parsed.constEnd() || it->parts.isEmpty())
        return {};
    const double xScale = std::cos((it->south + it->north) / 2 * DegToRad);
    const double w = (it->east - it->west) * xScale, h = it->north - it->south;
    // A few hundred pixels across, so about a pixel of tolerance
    const Point sw{ it->south, it->west }, ne{ it->north, it->east };
    const double tol = metersBetween(sw, ne) / 400;
    QVariantList parts;
    for (const auto &part : it->parts) {
        QVariantList flat;
        for (const Point &p : thin(part, tol)) {
            flat.append((p.lon - it->west) * xScale);
            flat.append(it->north - p.lat);
        }
        parts.append(QVariant(flat));
    }
    return { { QStringLiteral("w"), w }, { QStringLiteral("h"), h }, { QStringLiteral("parts"), parts } };
}

QVariantMap Trails::nearest(const QString &file, double lat, double lon) const
{
    const auto it = m_parsed.constFind(file);
    if (it == m_parsed.constEnd() || it->parts.isEmpty())
        return {};
    const Point here{ lat, lon };
    Point best = it->parts.first().first();
    double bestM = metersBetween(here, best);
    for (const auto &part : it->parts) {
        for (const Point &p : part) {
            const double d = metersBetween(here, p);
            if (d < bestM) {
                bestM = d;
                best = p;
            }
        }
    }
    return { { QStringLiteral("lat"), best.lat }, { QStringLiteral("lon"), best.lon }, { QStringLiteral("mi"), bestM / MetersPerMile } };
}

// ------------------------------------------------------------- recording

QString Trails::recordingPath() const
{
    return dir() + QLatin1Char('/') + RecordingName;
}

QByteArray Trails::recordedLine() const
{
    if (m_recLine.size() < 2)
        return collectionJson({});
    return collectionJson("{\"type\":\"Feature\",\"properties\":{},\"geometry\":{\"type\":\"LineString\",\"coordinates\":" + lineJson(m_recLine) + "}}");
}

void Trails::startRecording()
{
    if (recording())
        return;
    m_recFile.setFileName(recordingPath());
    if (!m_recFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qCWarning(lcTrails) << "could not start a recording in" << dir();
        return;
    }
    m_recFile.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                    "<gpx version=\"1.1\" creator=\"TrailCurrent Godwit\" xmlns=\"http://www.topografix.com/GPX/1/1\">\n"
                    "<trk><name>Recorded track</name>\n<trkseg>\n");
    m_recFile.flush();
    m_sinceSync.start();
    m_recHasFix = false;
    m_recPoints = 0;
    m_recMiles = 0;
    m_recLine.clear();
    qCInfo(lcTrails) << "recording";
    emit recordingChanged();
    emit recordedChanged();
    emit recordedLineChanged();
}

void Trails::addFix(double lat, double lon, double altitude, const QDateTime &utc)
{
    if (!recording())
        return;
    const Point here{ lat, lon };
    QByteArray out;
    if (m_recHasFix) {
        const double moved = metersBetween(m_recLast, here);
        // None since a resume counts as a gap
        const qint64 ms = m_sinceFix.isValid() ? m_sinceFix.elapsed() : RecordGapMs + 1;
        if (moved < RecordStepM)
            return;
        if (ms > RecordGapMs) {
            out += "</trkseg>\n<trkseg>\n";
        } else {
            if (moved > MaxJumpMetersPerSecond * std::max<qint64>(ms, 1000) / 1000.0)
                return;
            m_recMiles += moved / MetersPerMile;
        }
    }
    out += "<trkpt lat=\"" + QByteArray::number(lat, 'f', 7) + "\" lon=\"" + QByteArray::number(lon, 'f', 7) + "\">";
    out += "<ele>" + QByteArray::number(altitude, 'f', 1) + "</ele>";
    if (utc.isValid())
        out += "<time>" + utc.toUTC().toString(Qt::ISODate).toLatin1() + "</time>";
    out += "</trkpt>\n";
    m_recFile.write(out);
    m_recFile.flush();
    if (m_sinceSync.elapsed() >= RecordSyncMs) {
        ::fsync(m_recFile.handle());
        m_sinceSync.restart();
    }
    m_recLast = here;
    m_sinceFix.start();
    ++m_recPoints;
    if (!m_recHasFix || metersBetween(m_recLineLast, here) >= RecordLineStepM) {
        m_recLine.append(here);
        m_recLineLast = here;
        emit recordedLineChanged();
    }
    m_recHasFix = true;
    emit recordedChanged();
}

QString Trails::stopRecording(const QString &name)
{
    if (!recording())
        return {};
    m_recFile.flush();
    m_recFile.close();
    const QString file = m_recPoints >= 2 ? saveRecording(name) : QString();
    if (file.isEmpty())
        QFile::remove(recordingPath());
    m_recPoints = 0;
    m_recMiles = 0;
    m_recLine.clear();
    qCInfo(lcTrails) << "recording stopped," << (file.isEmpty() ? QStringLiteral("nothing kept") : file);
    emit recordingChanged();
    emit recordedChanged();
    emit recordedLineChanged();
    return file;
}

// Closes the file off and moves it into the library. The last point of a file
// a power loss cut short may be half written, so everything after the last
// whole one goes.
QString Trails::saveRecording(const QString &name)
{
    QFile f(recordingPath());
    if (!f.open(QIODevice::ReadWrite))
        return {};
    const QByteArray end("</trkpt>\n");
    const QByteArray all = f.readAll();
    const qsizetype cut = all.lastIndexOf(end);
    if (cut < 0)
        return {};
    f.resize(cut + end.size());
    f.seek(cut + end.size());
    f.write("</trkseg>\n</trk>\n</gpx>\n");
    f.flush();
    ::fsync(f.handle());
    f.close();

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm"));
    QString file = QStringLiteral("Recording-%1.gpx").arg(stamp);
    for (int i = 2; QFileInfo::exists(dir() + QLatin1Char('/') + file); ++i)
        file = QStringLiteral("Recording-%1-%2.gpx").arg(stamp).arg(i);
    if (!QFile::rename(recordingPath(), dir() + QLatin1Char('/') + file)) {
        qCWarning(lcTrails) << "could not save the recording as" << file;
        return {};
    }
    QJsonObject s;
    s.insert(QStringLiteral("name"), name);
    s.insert(QStringLiteral("color"), nextColor());
    s.insert(QStringLiteral("on"), true);
    s.insert(QStringLiteral("rec"), true);
    m_settings.insert(file, s);
    saveSettings();
    writeNameInto(file, name);
    return file;
}

// Carries on with a recording the power cut off, so a stop for fuel with the
// engine off does not end it. The last point may be half written, so everything
// after the last whole one goes. A file already closed off was being saved when
// the power went: the record button had ended it, so it is saved, not resumed.
void Trails::resume()
{
    const QString path = recordingPath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray all = f.readAll();
    f.close();
    if (all.contains("</gpx>")) {
        const QString name = QStringLiteral("Track ") + QFileInfo(path).lastModified().toString(QStringLiteral("MMM d"));
        if (saveRecording(name).isEmpty())
            QFile::remove(path);
        return;
    }
    const QByteArray head("<trkseg>\n"), end("</trkpt>\n");
    qsizetype keep = all.lastIndexOf(end);
    if (keep >= 0) {
        keep += end.size();
    } else if ((keep = all.indexOf(head)) >= 0) {
        keep += head.size();
    } else {
        // Cut off before its header was written: nothing was recorded yet
        startRecording();
        m_resumed = recording();
        return;
    }

    // What was recorded so far, for the distance and the line on the map
    const QByteArray kept = all.left(keep);
    const auto attr = [](const QByteArray &tag, const char *name) {
        const QByteArray key = QByteArray(name) + "=\"";
        const qsizetype from = tag.indexOf(key);
        if (from < 0)
            return QByteArray();
        const qsizetype to = tag.indexOf('"', from + key.size());
        return to < 0 ? QByteArray() : tag.mid(from + key.size(), to - from - key.size());
    };
    qsizetype seg = kept.indexOf("<trkseg>");
    bool newPart = true;
    for (qsizetype at = kept.indexOf("<trkpt "); at >= 0; at = kept.indexOf("<trkpt ", at + 1)) {
        for (; seg >= 0 && seg < at; seg = kept.indexOf("<trkseg>", seg + 1))
            newPart = true;
        const QByteArray tag = kept.mid(at, kept.indexOf('>', at) - at);
        bool okLat = false, okLon = false;
        const Point p{ attr(tag, "lat").toDouble(&okLat), attr(tag, "lon").toDouble(&okLon) };
        if (!okLat || !okLon)
            continue;
        if (m_recPoints > 0 && !newPart)
            m_recMiles += metersBetween(m_recLast, p) / MetersPerMile;
        if (m_recLine.isEmpty() || metersBetween(m_recLineLast, p) >= RecordLineStepM) {
            m_recLine.append(p);
            m_recLineLast = p;
        }
        m_recLast = p;
        ++m_recPoints;
        newPart = false;
    }

    m_recFile.setFileName(path);
    if (!m_recFile.open(QIODevice::ReadWrite) || !m_recFile.resize(keep) || !m_recFile.seek(keep)) {
        // Left where it is for the next start to try again
        qCWarning(lcTrails) << "could not carry on the recording in" << dir();
        m_recFile.close();
        m_recPoints = 0;
        m_recMiles = 0;
        m_recLine.clear();
        return;
    }
    ::fsync(m_recFile.handle());
    m_sinceSync.start();
    // The next fix starts a new part: the vehicle may have moved while the
    // power was off, and no line should be drawn across that
    m_sinceFix.invalidate();
    m_recHasFix = m_recPoints > 0;
    m_resumed = true;
    qCInfo(lcTrails) << "carrying on a recording the power cut off," << m_recPoints << "points," << m_recMiles << "mi";
}

// The first <name> in a recorded file is the track's: its own header wrote it
void Trails::writeNameInto(const QString &file, const QString &name)
{
    const QString path = dir() + QLatin1Char('/') + file;
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly))
        return;
    QByteArray all = in.readAll();
    in.close();
    const qsizetype from = all.indexOf("<name>"), to = all.indexOf("</name>");
    if (from < 0 || to < from)
        return;
    all.replace(from + 6, to - from - 6, name.toHtmlEscaped().toUtf8());
    QSaveFile out(path);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(all);
        out.commit();
    }
}

// ---------------------------------------------------------------- settings

void Trails::loadSettings()
{
    QFile f(dir() + QLatin1Char('/') + SettingsName);
    if (f.open(QIODevice::ReadOnly))
        m_settings = QJsonDocument::fromJson(f.readAll()).object();
}

void Trails::saveSettings()
{
    QSaveFile f(dir() + QLatin1Char('/') + SettingsName);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(m_settings).toJson(QJsonDocument::Compact));
        f.commit();
    }
}
