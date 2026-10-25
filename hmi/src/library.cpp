#include "library.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QImage>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThreadPool>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

#include <algorithm>
#include <functional>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcLibrary, "godwit.library")

namespace {

// Left free on the disk after an import, for the system and the map bundle's
// own updates
constexpr qint64 ReserveBytes = qint64(512) << 20;

// The mounted drives: the folders under the USB folder that are not empty
QStringList drives(const QString &usbDir)
{
    QStringList out;
    for (const QFileInfo &d : QDir(usbDir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
        if (!QDir(d.absoluteFilePath()).isEmpty())
            out << d.absoluteFilePath();
    return out;
}

struct Copy {
    QString from;
    QString to;   // file name in the library
    qint64 size;
};

// The drives' files that are not in the library yet, and the name each gets
// there: its own, or with -2, -3 ... when a different file has that name.
// Hidden files and folders are left out, which skips the ._ files and
// .Trashes a Mac leaves on a drive.
QList<Copy> plan(const QString &usbDir, const QString &dir, const QSet<QString> &suffixes)
{
    QHash<QString, QList<qint64>> have;   // lower-case name -> sizes
    QSet<QString> taken;
    static const QRegularExpression renamed(QStringLiteral("^(.*)-\\d+$"));
    for (const QFileInfo &f : QDir(dir).entryInfoList(QDir::Files)) {
        have[f.fileName().toLower()].append(f.size());
        taken.insert(f.fileName().toLower());
        // An earlier import's IMG_0002-2.JPG is still the drive's IMG_0002.JPG
        const QRegularExpressionMatch m = renamed.match(f.completeBaseName());
        if (m.hasMatch())
            have[(m.captured(1) + QLatin1Char('.') + f.suffix()).toLower()].append(f.size());
    }

    QList<Copy> out;
    for (const QString &drive : drives(usbDir)) {
        QDirIterator it(drive, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QFileInfo f = it.nextFileInfo();
            if (!suffixes.contains(f.suffix().toLower()))
                continue;
            const QString key = f.fileName().toLower();
            if (have.value(key).contains(f.size()))
                continue;
            QString name = f.fileName();
            for (int i = 2; taken.contains(name.toLower()); ++i)
                name = f.completeBaseName() + QLatin1Char('-') + QString::number(i) + QLatin1Char('.') + f.suffix();
            have[key].append(f.size());
            taken.insert(name.toLower());
            out.append({ f.absoluteFilePath(), name, f.size() });
        }
    }
    return out;
}

// Copies in large blocks, reporting each, and syncs the copy to disk: the
// head unit can lose power at any moment
bool copyFile(const QString &from, const QString &to, const std::function<void(qint64)> &copied)
{
    QFile in(from), out(to);
    if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    QByteArray buf(4 << 20, Qt::Uninitialized);
    for (;;) {
        const qint64 n = in.read(buf.data(), buf.size());
        if (n < 0)
            return false;
        if (n == 0)
            break;
        if (out.write(buf.constData(), n) != n)
            return false;
        copied(n);
    }
    return out.flush() && ::fsync(out.handle()) == 0;
}

} // namespace

MediaFolder::MediaFolder(const char *dirEnv, const QString &defaultDir, const QSet<QString> &suffixes, QObject *parent)
    : QObject(parent), m_suffixes(suffixes)
{
    m_dir = qEnvironmentVariable(dirEnv, defaultDir);
    m_usbDir = qEnvironmentVariable("GODWIT_USB_DIR", QStringLiteral("/run/godwit-usb"));

    // A copy fires a change per file; list the folder once it goes quiet
    m_settle.setSingleShot(true);
    m_settle.setInterval(500);
    connect(&m_settle, &QTimer::timeout, this, &MediaFolder::rescan);
    m_usbSettle.setSingleShot(true);
    m_usbSettle.setInterval(1000);
    connect(&m_usbSettle, &QTimer::timeout, this, &MediaFolder::scanUsb);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &path) {
        if (path == m_usbDir)
            m_usbSettle.start();
        else
            m_settle.start();
    });

    // A drive mounted or unmounted. The mount table cannot be watched like a
    // file; it reports changes as an exceptional condition instead.
    m_mounts.setFileName(QStringLiteral("/proc/self/mounts"));
    if (m_mounts.open(QIODevice::ReadOnly)) {
        m_mountsChanged = new QSocketNotifier(m_mounts.handle(), QSocketNotifier::Exception, this);
        connect(m_mountsChanged, &QSocketNotifier::activated, &m_usbSettle, qOverload<>(&QTimer::start));
    }
}

void MediaFolder::start()
{
    rescan();
    scanUsb();
}

void MediaFolder::rescan()
{
    // The parent too, so a folder made later is still noticed; the USB folder
    // for drives added by hand in the simulator
    for (const QString &d : { m_dir, QFileInfo(m_dir).absolutePath(), m_usbDir })
        if (QFileInfo::exists(d) && !m_watcher.directories().contains(d))
            m_watcher.addPath(d);

    QFileInfoList listing = QDir(m_dir).entryInfoList(QDir::Files | QDir::Readable);
    QCollator byName;
    byName.setNumericMode(true);
    byName.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(listing.begin(), listing.end(), [&byName](const QFileInfo &a, const QFileInfo &b) {
        return byName.compare(a.fileName(), b.fileName()) < 0;
    });

    QStringList files;
    int skipped = 0;
    for (const QFileInfo &f : std::as_const(listing)) {
        if (m_suffixes.contains(f.suffix().toLower()))
            files << f.absoluteFilePath();
        else
            ++skipped;
    }
    if (files == m_files && skipped == m_skipped)
        return;
    m_files = files;
    m_skipped = skipped;
    qCInfo(lcLibrary) << m_files.size() << "files in" << m_dir << "," << m_skipped << "in formats that cannot be shown";
    refreshItems();
    // What counts as new on a drive changed with the library
    if (m_usb && !m_importing)
        scanUsb();
}

void MediaFolder::refreshItems()
{
    QVariantList items;
    for (const QString &path : std::as_const(m_files)) {
        const QFileInfo f(path);
        QVariantMap row{ { QStringLiteral("url"), QUrl::fromLocalFile(path).toString() },
                         { QStringLiteral("n"), f.completeBaseName() } };
        decorate(row, f);
        items.append(row);
    }
    m_items = items;
    emit itemsChanged();
}

// Off the GUI thread: a drive can hold thousands of files
void MediaFolder::scanUsb()
{
    if (m_importing)
        return;  // finished() scans again
    if (QFileInfo::exists(m_usbDir) && !m_watcher.directories().contains(m_usbDir))
        m_watcher.addPath(m_usbDir);
    const int scan = ++m_usbScan;
    QThreadPool::globalInstance()->start([self = QPointer(this), usbDir = m_usbDir, dir = m_dir, suffixes = m_suffixes, scan] {
        const bool usb = !drives(usbDir).isEmpty();
        const int fresh = usb ? int(plan(usbDir, dir, suffixes).size()) : 0;
        QMetaObject::invokeMethod(self, [self, usb, fresh, scan] {
            if (self && scan == self->m_usbScan)
                self->setUsb(usb, fresh);
        }, Qt::QueuedConnection);
    });
}

void MediaFolder::setUsb(bool usb, int usbNew)
{
    if (usb == m_usb && usbNew == m_usbNew)
        return;
    if (usb && !m_usb)
        qCInfo(lcLibrary).nospace() << "USB drive in, " << usbNew << " new for " << m_dir;
    else if (!usb && m_usb)
        qCInfo(lcLibrary) << "USB drive out";
    m_usb = usb;
    m_usbNew = usbNew;
    emit usbChanged();
}

void MediaFolder::importUsb()
{
    if (m_importing || !m_usb)
        return;
    m_importing = true;
    progress(0, m_usbNew, 0);
    QThreadPool::globalInstance()->start([self = QPointer(this), usbDir = m_usbDir, dir = m_dir, suffixes = m_suffixes] {
        const QList<Copy> copies = plan(usbDir, dir, suffixes);
        const int total = int(copies.size());
        qint64 bytes = 0, copied = 0;
        for (const Copy &c : copies)
            bytes += c.size;
        int count = 0, failed = 0;
        QElapsedTimer since;
        since.start();
        auto report = [&](bool now) {
            if (!now && since.elapsed() < 150)
                return;
            since.restart();
            const double share = bytes ? double(copied) / double(bytes) : 1.0;
            QMetaObject::invokeMethod(self, [self, done = count + failed, total, share] {
                if (self)
                    self->progress(done, total, share);
            }, Qt::QueuedConnection);
        };
        report(true);
        for (const Copy &c : copies) {
            const qint64 before = copied;
            // Copied under a hidden name, so the library never lists half a file
            const QString part = dir + QStringLiteral("/.import-") + c.to;
            QFile::remove(part);
            const bool room = QStorageInfo(dir).bytesAvailable() > c.size + ReserveBytes;
            if (room
                && copyFile(c.from, part, [&](qint64 n) { copied += n; report(false); })
                && QFile::setPermissions(part, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup)
                && QFile::rename(part, dir + QLatin1Char('/') + c.to)) {
                ++count;
            } else {
                qCWarning(lcLibrary) << "could not import" << c.from << (room ? "" : "(no room left)");
                QFile::remove(part);
                ++failed;
            }
            copied = before + c.size;
            report(true);
        }
        QMetaObject::invokeMethod(self, [self, count, failed] { if (self) self->finished(count, failed); }, Qt::QueuedConnection);
    });
}

void MediaFolder::progress(int done, int total, double share)
{
    m_importDone = done;
    m_importTotal = total;
    m_importProgress = share;
    emit importChanged();
}

void MediaFolder::finished(int count, int failed)
{
    qCInfo(lcLibrary) << "imported" << count << "files from USB into" << m_dir << "," << failed << "failed";
    m_importing = false;
    emit importChanged();
    emit imported(count, failed);
    rescan();
    scanUsb();
}

// ---------------------------------------------------------------- Photos

namespace {
QSet<QString> imageFormats()
{
    QSet<QString> formats;
    for (const QByteArray &f : QImageReader::supportedImageFormats())
        formats.insert(QString::fromLatin1(f));
    return formats;
}
} // namespace

Photos::Photos(QObject *parent)
    : MediaFolder("GODWIT_PHOTOS_DIR", QStringLiteral("/data/media/photos"), imageFormats(), parent)
{
    start();
}

// ---------------------------------------------------------------- Videos

Videos::Videos(QObject *parent)
    : MediaFolder("GODWIT_VIDEOS_DIR", QStringLiteral("/data/media/videos"),
                  { QStringLiteral("mp4"), QStringLiteral("m4v"), QStringLiteral("mov"), QStringLiteral("mkv"),
                    QStringLiteral("webm"), QStringLiteral("avi"), QStringLiteral("mts"), QStringLiteral("m2ts"),
                    QStringLiteral("3gp") },
                  parent)
{
    m_cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/godwit-videos");
    QDir().mkpath(m_cache);
    loadIndex();

    m_player = new QMediaPlayer(this);
    m_sink = new QVideoSink(this);
    m_player->setVideoSink(m_sink);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        if (m_busy.isEmpty())
            return;
        if (s == QMediaPlayer::LoadedMedia)
            loaded();
        else if (s == QMediaPlayer::InvalidMedia)
            done(false);
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this, [this] { if (!m_busy.isEmpty()) done(false); });
    connect(m_sink, &QVideoSink::videoFrameChanged, this, &Videos::frame);
    m_giveUp.setSingleShot(true);
    m_giveUp.setInterval(20000);
    connect(&m_giveUp, &QTimer::timeout, this, [this] {
        qCWarning(lcLibrary) << "no still from" << m_player->source().toLocalFile();
        done(false);
    });
    connect(this, &MediaFolder::itemsChanged, this, [this] {
        if (m_busy.isEmpty())
            QTimer::singleShot(0, this, &Videos::next);
    });

    // PipeWire starts after the HMI, so the role sink can turn up later
    m_devices = new QMediaDevices(this);
    connect(m_devices, &QMediaDevices::audioOutputsChanged, this, &Videos::findAudioOutput);
    findAudioOutput();

    start();
}

// Changes when the file does, so a replaced video gets a new still
QString Videos::keyOf(const QFileInfo &file)
{
    const QByteArray id = file.absoluteFilePath().toUtf8() + '|' + QByteArray::number(file.size()) + '|'
        + QByteArray::number(file.lastModified().toMSecsSinceEpoch());
    return QString::fromLatin1(QCryptographicHash::hash(id, QCryptographicHash::Sha1).toHex());
}

void Videos::decorate(QVariantMap &row, const QFileInfo &file)
{
    const QString key = keyOf(file);
    const Info info = m_index.value(key);
    row.insert(QStringLiteral("thumb"), info.thumb ? QUrl::fromLocalFile(m_cache + QLatin1Char('/') + key + QStringLiteral(".jpg")).toString() : QString());
    row.insert(QStringLiteral("dur"), info.dur);
}

void Videos::loadIndex()
{
    QFile f(m_cache + QStringLiteral("/index.json"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it) {
        const QJsonObject v = it.value().toObject();
        m_index.insert(it.key(), { v.value(QLatin1String("dur")).toInt(), v.value(QLatin1String("thumb")).toBool() });
    }
}

void Videos::saveIndex()
{
    QJsonObject o;
    for (auto it = m_index.cbegin(); it != m_index.cend(); ++it)
        o.insert(it.key(), QJsonObject{ { QStringLiteral("dur"), it->dur }, { QStringLiteral("thumb"), it->thumb } });
    QSaveFile f(m_cache + QStringLiteral("/index.json"));
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

// The first video not looked at yet. Once all have been, the stills of
// videos no longer in the library are deleted.
void Videos::next()
{
    if (!m_busy.isEmpty())
        return;
    QSet<QString> keys;
    for (const QString &path : files()) {
        const QString key = keyOf(QFileInfo(path));
        keys.insert(key);
        if (m_index.contains(key))
            continue;
        m_busy = key;
        m_target = 0;
        m_giveUp.start();
        m_player->setSource(QUrl::fromLocalFile(path));
        return;
    }
    bool pruned = false;
    for (auto it = m_index.begin(); it != m_index.end();) {
        if (keys.contains(it.key())) {
            ++it;
            continue;
        }
        QFile::remove(m_cache + QLatin1Char('/') + it.key() + QStringLiteral(".jpg"));
        it = m_index.erase(it);
        pruned = true;
    }
    if (pruned)
        saveIndex();
}

void Videos::loaded()
{
    const qint64 dur = m_player->duration();
    m_index[m_busy].dur = int((dur + 500) / 1000);
    // A second in, past a fade from black, unless the video is only a moment
    m_target = dur >= 4000 ? 1000 : dur / 4;
    m_player->setPosition(m_target);
    m_player->play();
}

void Videos::frame()
{
    if (m_busy.isEmpty() || m_player->mediaStatus() == QMediaPlayer::LoadingMedia)
        return;
    const QVideoFrame f = m_sink->videoFrame();
    if (!f.isValid() || (f.startTime() >= 0 && f.startTime() / 1000 < m_target - 500))
        return;
    QImage still = f.toImage();
    if (still.isNull())
        return;
    still = still.scaled(480, 480, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    done(still.save(m_cache + QLatin1Char('/') + m_busy + QStringLiteral(".jpg"), "JPG", 85));
}

void Videos::done(bool thumb)
{
    m_giveUp.stop();
    m_player->stop();
    m_player->setSource(QUrl());
    m_index[m_busy].thumb = thumb;
    m_busy.clear();
    saveIndex();
    refreshItems();
}

void Videos::findAudioOutput()
{
    QAudioDevice found = QMediaDevices::defaultAudioOutput();
    for (const QAudioDevice &d : QMediaDevices::audioOutputs())
        if (d.id().contains("loopback.sink.role.multimedia"))
            found = d;
    if (found == m_audioOutput)
        return;
    qCInfo(lcLibrary) << "video sound to" << found.id() << found.description();
    m_audioOutput = found;
    emit audioOutputChanged();
}
