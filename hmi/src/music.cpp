#include "music.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMediaDevices>
#include <QPointer>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThreadPool>

#include <algorithm>

#ifdef GODWIT_HAVE_TAGLIB
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/taglib.h>
#include <taglib/tpropertymap.h>
#endif

Q_LOGGING_CATEGORY(lcMusic, "godwit.music")

// ------------------------------------------------------------- MusicSource

MusicSource::MusicSource(const QString &id, const QString &name, QObject *parent)
    : QObject(parent), m_id(id), m_name(name)
{
}

void MusicSource::setStatus(const QString &status, const QString &note)
{
    if (status == m_status && note == m_note)
        return;
    qCInfo(lcMusic).noquote() << m_id << status << note;
    m_status = status;
    m_note = note;
    emit statusChanged();
}

void MusicSource::setPlaying(bool playing)
{
    if (playing == m_playing)
        return;
    m_playing = playing;
    emit playingChanged();
}

void MusicSource::setTrack(const QVariantMap &track)
{
    if (track == m_track)
        return;
    m_track = track;
    emit trackChanged();
}

void MusicSource::setPosition(double seconds)
{
    m_position = seconds;
    m_positionAt = double(QDateTime::currentMSecsSinceEpoch());
    emit positionChanged();
}

void MusicSource::setShuffleFlag(bool on)
{
    if (on == m_shuffle)
        return;
    m_shuffle = on;
    emit shuffleChanged();
}

void MusicSource::setLists(const QVariantList &lists)
{
    if (lists == m_lists)
        return;
    m_lists = lists;
    emit listsChanged();
}

void MusicSource::setBusy(bool busy)
{
    if (busy == m_busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

// ------------------------------------------------------------- QueueSource

QueueSource::QueueSource(const QString &id, const QString &name, QObject *parent)
    : MusicSource(id, name, parent)
{
    m_player = new QMediaPlayer(this);
    m_output = new QAudioOutput(this);
    m_player->setAudioOutput(m_output);
    // PipeWire starts after the HMI, so the role sink can turn up later
    m_devices = new QMediaDevices(this);
    connect(m_devices, &QMediaDevices::audioOutputsChanged, this, &QueueSource::pickOutput);
    pickOutput();

    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
        setPlaying(s == QMediaPlayer::PlayingState);
        setPosition(m_player->position() / 1000.0);
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        if (s == QMediaPlayer::EndOfMedia) {
            m_failures = 0;
            load(m_index + 1, true);
        } else if (s == QMediaPlayer::LoadedMedia || s == QMediaPlayer::BufferedMedia) {
            m_failures = 0;
        }
    });
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 ms) {
        if (m_index < 0 || m_index >= m_tracks.size() || ms <= 0)
            return;
        Track &t = m_tracks[m_index];
        if (t.dur <= 0) {
            t.dur = int((ms + 500) / 1000);
            showTrack();
            emit queueChanged();
        }
    });
    // Seeking and stalls move the position other than by playing on
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 ms) {
        const double now = double(QDateTime::currentMSecsSinceEpoch());
        const double expected = position() + (playing() ? (now - positionAt()) / 1000.0 : 0);
        if (std::abs(expected - ms / 1000.0) > 1.5)
            setPosition(ms / 1000.0);
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &error) {
        qCWarning(lcMusic) << this->id() << "cannot play" << m_player->source() << error;
        // Skip on, but stop after a run of failures: the drive was pulled out,
        // or the network is down
        if (++m_failures >= 3 || m_tracks.size() < 2) {
            m_wantPlay = false;
            emit message(QStringLiteral("Cannot play ") + (m_index >= 0 && m_index < m_tracks.size() ? m_tracks[m_index].title : QString()));
            return;
        }
        load(m_index + 1, true);
    });
}

void QueueSource::pickOutput()
{
    const QAudioDevice d = multimediaOutput();
    if (d != m_output->device())
        m_output->setDevice(d);
}

QVariantList QueueSource::queue() const
{
    QVariantList out;
    out.reserve(m_tracks.size());
    for (const Track &t : m_tracks)
        out.append(QVariantMap{ { QStringLiteral("title"), t.title }, { QStringLiteral("artist"), t.artist },
                                { QStringLiteral("dur"), t.dur } });
    return out;
}

void QueueSource::setQueue(const QList<Track> &tracks, const QString &title, int start)
{
    m_unshuffled = tracks;
    m_tracks = tracks;
    m_queueTitle = title;
    m_index = -1;
    if (shuffle() && start >= 0 && start < m_tracks.size()) {
        // The chosen track first, the rest in a random order after it
        m_tracks.move(start, 0);
        std::shuffle(m_tracks.begin() + 1, m_tracks.end(), *QRandomGenerator::global());
        start = 0;
    }
    emit queueChanged();
    if (start >= 0 && start < m_tracks.size())
        load(start, true);
    else
        stop();
}

void QueueSource::refreshQueue(const QHash<QString, Track> &byKey)
{
    bool changed = false;
    for (QList<Track> *list : { &m_tracks, &m_unshuffled }) {
        for (Track &t : *list) {
            const auto it = byKey.constFind(t.key);
            if (it == byKey.cend())
                continue;
            const QUrl url = t.url;
            t = *it;
            t.url = url;
            changed = true;
        }
    }
    if (!changed)
        return;
    showTrack();
    emit queueChanged();
}

void QueueSource::load(int index, bool play)
{
    if (m_tracks.isEmpty()) {
        stop();
        return;
    }
    index = (index % m_tracks.size() + m_tracks.size()) % m_tracks.size();
    m_index = index;
    m_wantPlay = play;
    showTrack();
    emit queueChanged();
    m_player->stop();
    setPosition(0);
    const int loading = ++m_loading;
    const Track t = m_tracks[index];
    auto start = [this, loading, index](const QUrl &url) {
        if (loading != m_loading)
            return;
        if (url.isEmpty()) {
            m_player->setSource({});
            emit m_player->errorOccurred(QMediaPlayer::ResourceError, QStringLiteral("no stream"));
            return;
        }
        m_tracks[index].url = url;
        m_player->setSource(url);
        if (m_wantPlay)
            m_player->play();
    };
    if (!t.url.isEmpty()) {
        start(t.url);
        return;
    }
    setBusy(true);
    resolve(t, [this, start, loading](const QUrl &url) {
        if (loading == m_loading)
            setBusy(false);
        start(url);
    });
}

void QueueSource::showTrack()
{
    if (m_index < 0 || m_index >= m_tracks.size()) {
        setTrack({});
        return;
    }
    const Track &t = m_tracks[m_index];
    setTrack({ { QStringLiteral("title"), t.title }, { QStringLiteral("artist"), t.artist },
               { QStringLiteral("album"), t.album }, { QStringLiteral("art"), t.art },
               { QStringLiteral("dur"), t.dur } });
}

void QueueSource::stop()
{
    ++m_loading;
    m_wantPlay = false;
    m_player->stop();
    m_player->setSource({});
    m_index = -1;
    setBusy(false);
    showTrack();
    setPosition(0);
    emit queueChanged();
}

void QueueSource::play()
{
    if (m_tracks.isEmpty())
        return;
    if (m_index < 0 || m_player->source().isEmpty()) {
        load(std::max(0, m_index), true);
        return;
    }
    m_wantPlay = true;
    m_player->play();
}

void QueueSource::pause()
{
    m_wantPlay = false;
    m_player->pause();
}

void QueueSource::next()
{
    m_failures = 0;
    load(m_index + 1, true);
}

void QueueSource::previous()
{
    m_failures = 0;
    // A few seconds in, back to the start of this track, as players do
    if (m_player->position() > 4000) {
        m_player->setPosition(0);
        setPosition(0);
        play();
        return;
    }
    load(m_index - 1, true);
}

void QueueSource::playAt(int index)
{
    if (index < 0 || index >= m_tracks.size())
        return;
    m_failures = 0;
    load(index, true);
}

void QueueSource::setShuffle(bool on)
{
    if (on == shuffle())
        return;
    setShuffleFlag(on);
    if (m_tracks.size() < 2)
        return;
    const QString current = m_index >= 0 ? m_tracks[m_index].key : QString();
    if (on) {
        // What follows the playing track, in a random order
        const int from = std::max(0, m_index + 1);
        std::shuffle(m_tracks.begin() + from, m_tracks.end(), *QRandomGenerator::global());
    } else {
        QHash<QString, QUrl> urls;
        for (const Track &t : std::as_const(m_tracks))
            urls.insert(t.key, t.url);
        m_tracks = m_unshuffled;
        for (Track &t : m_tracks)
            t.url = urls.value(t.key, t.url);
        m_index = -1;
        for (int i = 0; i < m_tracks.size(); ++i)
            if (m_tracks[i].key == current)
                m_index = i;
    }
    emit queueChanged();
}

// ------------------------------------------------------------- MusicFolder

namespace {

QSet<QString> audioSuffixes()
{
    return { QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("aac"), QStringLiteral("flac"),
             QStringLiteral("ogg"), QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("wav"),
             QStringLiteral("aif"), QStringLiteral("aiff"), QStringLiteral("wma"), QStringLiteral("alac") };
}

// Cache key: changes when the file does
QString keyOf(const QFileInfo &f)
{
    const QByteArray id = f.absoluteFilePath().toUtf8() + '|' + QByteArray::number(f.size()) + '|'
        + QByteArray::number(f.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex());
}

// "03 - Artist - Title", "Artist - Title", "03. Title", "Title"
QueueSource::Track fromName(const QFileInfo &f)
{
    QueueSource::Track t;
    QString n = f.completeBaseName();
    static const QRegularExpression number(QStringLiteral("^\\d{1,3}[\\s._-]+"));
    n.remove(number);
    const int dash = n.indexOf(QStringLiteral(" - "));
    if (dash > 0) {
        t.artist = n.left(dash).trimmed();
        t.title = n.mid(dash + 3).trimmed();
    } else {
        t.title = n.trimmed();
    }
    if (t.title.isEmpty())
        t.title = f.completeBaseName();
    return t;
}

// A cover image beside the file, as rippers and stores leave them
QString folderCover(const QFileInfo &f)
{
    static const QStringList names = { QStringLiteral("cover"), QStringLiteral("folder"), QStringLiteral("front"),
                                       QStringLiteral("album"), QStringLiteral("albumart") };
    const QFileInfoList files = f.dir().entryInfoList({ QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"), QStringLiteral("*.png") },
                                                     QDir::Files | QDir::Readable);
    for (const QString &want : names)
        for (const QFileInfo &c : files)
            if (c.completeBaseName().compare(want, Qt::CaseInsensitive) == 0)
                return c.absoluteFilePath();
    return {};
}

struct Tags {
    QString title, artist, album, art;
    int dur = 0;
    int number = 0;
};

// On a worker thread. art is written into cacheDir as <key>.jpg when the
// file carries a cover.
Tags readFile(const QString &path, const QString &key, const QString &cacheDir)
{
    Tags out;
    const QFileInfo f(path);
    const QueueSource::Track named = fromName(f);
    out.title = named.title;
    out.artist = named.artist;
#ifdef GODWIT_HAVE_TAGLIB
    TagLib::FileRef ref(QFile::encodeName(path).constData(), true, TagLib::AudioProperties::Fast);
    if (!ref.isNull() && ref.tag()) {
        const TagLib::Tag *tag = ref.tag();
        const QString title = QString::fromStdString(tag->title().to8Bit(true)).trimmed();
        const QString artist = QString::fromStdString(tag->artist().to8Bit(true)).trimmed();
        if (!title.isEmpty())
            out.title = title;
        if (!artist.isEmpty())
            out.artist = artist;
        out.album = QString::fromStdString(tag->album().to8Bit(true)).trimmed();
        out.number = int(tag->track());
    }
    if (!ref.isNull() && ref.audioProperties())
        out.dur = ref.audioProperties()->lengthInSeconds();
#if TAGLIB_MAJOR_VERSION >= 2
    if (!ref.isNull()) {
        const auto pictures = ref.complexProperties("PICTURE");
        if (!pictures.isEmpty()) {
            const TagLib::ByteVector data = pictures.front()["data"].toByteVector();
            QImage img = QImage::fromData(QByteArrayView(data.data(), qsizetype(data.size())));
            if (!img.isNull()) {
                const QString file = cacheDir + QLatin1Char('/') + key + QStringLiteral(".jpg");
                img = img.scaled(400, 400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                if (img.save(file, "JPG", 85))
                    out.art = QUrl::fromLocalFile(file).toString();
            }
        }
    }
#endif
#else
    Q_UNUSED(key) Q_UNUSED(cacheDir)
#endif
    if (out.art.isEmpty()) {
        const QString cover = folderCover(f);
        if (!cover.isEmpty())
            out.art = QUrl::fromLocalFile(cover).toString();
    }
    return out;
}

} // namespace

MusicFolder::MusicFolder(QObject *parent)
    : MediaFolder("GODWIT_MUSIC_DIR", QStringLiteral("/data/media/music"), audioSuffixes(), parent)
{
    m_cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/godwit-music");
    QDir().mkpath(m_cache);
    loadIndex();
    connect(this, &MediaFolder::itemsChanged, this, [this] {
        readTags(files());
        emit libraryChanged();
    });
    start();
}

QueueSource::Track MusicFolder::trackFor(const QString &path) const
{
    const auto it = m_tags.constFind(path);
    QueueSource::Track t = it != m_tags.cend() ? *it : fromName(QFileInfo(path));
    t.key = path;
    t.url = QUrl::fromLocalFile(path);
    return t;
}

QList<QueueSource::Track> MusicFolder::libraryTracks() const
{
    QList<QueueSource::Track> out;
    for (const QString &p : files())
        out.append(trackFor(p));
    return out;
}

QList<QueueSource::Track> MusicFolder::driveTracks() const
{
    QList<QueueSource::Track> out;
    for (const QString &p : m_drive)
        out.append(trackFor(p));
    return out;
}

QHash<QString, QueueSource::Track> MusicFolder::known() const
{
    QHash<QString, QueueSource::Track> out;
    for (const QStringList &list : { files(), m_drive })
        for (const QString &p : list)
            out.insert(p, trackFor(p));
    return out;
}

void MusicFolder::driveFiles(const QStringList &files)
{
    // In folder order, so albums keep their running order
    QStringList sorted = files;
    QCollator byName;
    byName.setNumericMode(true);
    byName.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(sorted.begin(), sorted.end(), [&byName](const QString &a, const QString &b) { return byName.compare(a, b) < 0; });
    if (sorted == m_drive)
        return;
    m_drive = sorted;
    readTags(m_drive);
    emit driveChanged();
}

// Tags not read yet, a batch at a time on a worker thread
void MusicFolder::readTags(const QStringList &paths)
{
    for (const QString &p : paths)
        if (!m_tags.contains(p) && !m_pending.contains(p))
            m_pending.append(p);
    if (m_reading || m_pending.isEmpty())
        return;
    m_reading = true;
    const QStringList batch = m_pending.mid(0, 200);
    m_pending = m_pending.mid(batch.size());
    // The index is keyed by keyOf(), so a changed file is read again
    QHash<QString, QueueSource::Track> cached;
    for (const QString &p : batch) {
        const QString key = keyOf(QFileInfo(p));
        const auto it = m_tags.constFind(key);
        if (it != m_tags.cend())
            cached.insert(p, *it);
    }
    QThreadPool::globalInstance()->start([self = QPointer(this), batch, cached, cache = m_cache] {
        QHash<QString, QueueSource::Track> read;
        QHash<QString, QueueSource::Track> byKey;
        for (const QString &p : batch) {
            if (cached.contains(p)) {
                read.insert(p, cached.value(p));
                continue;
            }
            const QFileInfo f(p);
            const QString key = keyOf(f);
            const Tags tags = readFile(p, key, cache);
            QueueSource::Track t;
            t.title = tags.title;
            t.artist = tags.artist;
            t.album = tags.album;
            t.art = tags.art;
            t.dur = tags.dur;
            t.number = tags.number;
            read.insert(p, t);
            byKey.insert(key, t);
        }
        QMetaObject::invokeMethod(self, [self, read, byKey] {
            if (!self)
                return;
            for (auto it = read.cbegin(); it != read.cend(); ++it)
                self->m_tags.insert(it.key(), it.value());
            for (auto it = byKey.cbegin(); it != byKey.cend(); ++it)
                self->m_tags.insert(it.key(), it.value());
            if (!byKey.isEmpty())
                self->saveIndex();
            self->m_reading = false;
            emit self->tagsRead();
            self->readTags({});
        }, Qt::QueuedConnection);
    });
}

void MusicFolder::loadIndex()
{
    QFile f(m_cache + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QJsonObject v = it.value().toObject();
        QueueSource::Track t;
        t.title = v.value(QLatin1String("title")).toString();
        t.artist = v.value(QLatin1String("artist")).toString();
        t.album = v.value(QLatin1String("album")).toString();
        t.art = v.value(QLatin1String("art")).toString();
        t.dur = v.value(QLatin1String("dur")).toInt();
        t.number = v.value(QLatin1String("n")).toInt();
        m_tags.insert(it.key(), t);
    }
}

// Only the entries keyed by keyOf() (40 hex digits); paths are this run's
void MusicFolder::saveIndex()
{
    QJsonObject o;
    for (auto it = m_tags.cbegin(); it != m_tags.cend(); ++it) {
        if (it.key().size() != 40 || it.key().contains(QLatin1Char('/')))
            continue;
        o.insert(it.key(), QJsonObject{ { QStringLiteral("title"), it->title }, { QStringLiteral("artist"), it->artist },
                                        { QStringLiteral("album"), it->album }, { QStringLiteral("art"), it->art },
                                        { QStringLiteral("dur"), it->dur }, { QStringLiteral("n"), it->number } });
    }
    QSaveFile f(m_cache + QStringLiteral("/index.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

// ------------------------------------------------------------- LibrarySource

LibrarySource::LibrarySource(MusicFolder *folder, bool usb, QObject *parent)
    : QueueSource(usb ? QStringLiteral("usb") : QStringLiteral("local"), usb ? QStringLiteral("USB") : QStringLiteral("Local"), parent),
      m_folder(folder), m_usb(usb)
{
    connect(folder, usb ? &MusicFolder::driveChanged : &MusicFolder::libraryChanged, this, &LibrarySource::rebuild);
    connect(folder, &MusicFolder::tagsRead, this, [this] {
        refreshQueue(m_folder->known());
        rebuild();
    });
    rebuild();
}

QList<QueueSource::Track> LibrarySource::all() const
{
    return m_usb ? m_folder->driveTracks() : m_folder->libraryTracks();
}

// The lists to browse: All Songs, each album and each artist
void LibrarySource::rebuild()
{
    const QList<Track> songs = all();
    if (songs.isEmpty()) {
        setStatus(QStringLiteral("ready"), m_usb ? (m_folder->usb() ? QStringLiteral("No music on this USB drive")
                                                                    : QStringLiteral("Plug in a USB drive with music"))
                                                 : QStringLiteral("No music yet. Import it from a USB drive."));
    } else {
        setStatus(QStringLiteral("ready"), QString::number(songs.size()) + (songs.size() == 1 ? QStringLiteral(" song") : QStringLiteral(" songs")));
    }

    QMap<QString, int> albums, artists;
    for (const Track &t : songs) {
        if (!t.album.isEmpty())
            ++albums[t.album];
        if (!t.artist.isEmpty())
            ++artists[t.artist];
    }
    QVariantList lists;
    if (!songs.isEmpty())
        lists.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("all") }, { QStringLiteral("title"), QStringLiteral("All Songs") },
                                  { QStringLiteral("sub"), QString::number(songs.size()) + QStringLiteral(" songs") },
                                  { QStringLiteral("icon"), QStringLiteral("musical-notes") } });
    for (auto it = albums.cbegin(); it != albums.cend(); ++it)
        lists.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("album:") + it.key() }, { QStringLiteral("title"), it.key() },
                                  { QStringLiteral("sub"), QStringLiteral("Album · ") + QString::number(it.value()) + QStringLiteral(" songs") },
                                  { QStringLiteral("icon"), QStringLiteral("disc") } });
    for (auto it = artists.cbegin(); it != artists.cend(); ++it)
        lists.append(QVariantMap{ { QStringLiteral("id"), QStringLiteral("artist:") + it.key() }, { QStringLiteral("title"), it.key() },
                                  { QStringLiteral("sub"), QStringLiteral("Artist · ") + QString::number(it.value()) + QStringLiteral(" songs") },
                                  { QStringLiteral("icon"), QStringLiteral("person") } });
    setLists(lists);

    // The drive went away while it played: nothing left to play from
    if (m_usb && songs.isEmpty() && !tracks().isEmpty())
        setQueue({}, QString());
    // Ready to play: All Songs is queued until something else is chosen
    if (!songs.isEmpty() && (tracks().isEmpty() || (queueTitle() == QLatin1String("All Songs") && !playing() && queueIndex() < 0)))
        setQueue(songs, QStringLiteral("All Songs"));
}

void LibrarySource::play()
{
    if (tracks().isEmpty()) {
        openList(QStringLiteral("all"));
        return;
    }
    QueueSource::play();
}

// An artist or album by name plays all of it; otherwise songs whose titles
// match, from the first
void LibrarySource::playSearch(const QString &text)
{
    const QString q = text.trimmed();
    const QList<Track> songs = all();
    auto has = [&q](const QString &s) { return s.contains(q, Qt::CaseInsensitive); };
    for (const char *kind : { "artist:", "album:" }) {
        for (const Track &t : songs) {
            const QString name = qstrcmp(kind, "artist:") == 0 ? t.artist : t.album;
            if (!name.isEmpty() && name.compare(q, Qt::CaseInsensitive) == 0) {
                emit message(QStringLiteral("Playing ") + name);
                openList(QLatin1String(kind) + name);
                return;
            }
        }
    }
    QList<Track> hits;
    for (const Track &t : songs)
        if (has(t.title) || has(t.artist) || has(t.album))
            hits.append(t);
    if (hits.isEmpty()) {
        emit message(QStringLiteral("No ") + name().toLower() + QStringLiteral(" music matches ") + q);
        return;
    }
    emit message(QStringLiteral("Playing ") + hits.constFirst().title);
    setQueue(hits, q, 0);
}

void LibrarySource::openList(const QString &id)
{
    QList<Track> songs = all();
    QString title = QStringLiteral("All Songs");
    if (id.startsWith(QLatin1String("album:"))) {
        title = id.mid(6);
        songs.removeIf([&title](const Track &t) { return t.album != title; });
        // Album order, where the tags number the tracks
        std::stable_sort(songs.begin(), songs.end(), [](const Track &a, const Track &b) {
            return (a.number ? a.number : 9999) < (b.number ? b.number : 9999);
        });
    } else if (id.startsWith(QLatin1String("artist:"))) {
        title = id.mid(7);
        songs.removeIf([&title](const Track &t) { return t.artist != title; });
    }
    if (songs.isEmpty())
        return;
    setQueue(songs, title, shuffle() ? int(QRandomGenerator::global()->bounded(songs.size())) : 0);
}

// ---------------------------------------------------------------- Music

Music::Music(QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(stateDir());
    QFile::setPermissions(stateDir(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    m_folder = new MusicFolder(this);
    m_local = new LibrarySource(m_folder, false, this);
    m_usb = new LibrarySource(m_folder, true, this);
}

MusicSource *Music::local() const { return m_local; }
MusicSource *Music::usb() const { return m_usb; }

MusicSource *Music::source(const QString &id) const
{
    if (id == QLatin1String("local"))
        return m_local;
    if (id == QLatin1String("usb"))
        return m_usb;
    return nullptr;
}

// /var/lib/godwit on the head unit (systemd's StateDirectory=); the user's
// data folder elsewhere
QString Music::stateDir()
{
    const QString s = qEnvironmentVariable("STATE_DIRECTORY");
    return s.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) : s.section(QLatin1Char(':'), 0, 0);
}

// ---------------------------------------------------------------- settings

QVariantMap loadSettings(const QString &id)
{
    QFile f(Music::stateDir() + QLatin1Char('/') + id + QStringLiteral(".json"));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
}

void saveSettings(const QString &id, const QVariantMap &values)
{
    const QString path = Music::stateDir() + QLatin1Char('/') + id + QStringLiteral(".json");
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        f.write(QJsonDocument(QJsonObject::fromVariantMap(values)).toJson());
        if (f.commit())
            return;
    }
    qCWarning(lcMusic) << "could not save" << path;
}
