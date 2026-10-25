#include "photos.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QHash>
#include <QImageReader>
#include <QLoggingCategory>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QThreadPool>
#include <QUrl>

#include <algorithm>

Q_LOGGING_CATEGORY(lcPhotos, "godwit.photos")

namespace {

QSet<QString> imageFormats()
{
    QSet<QString> formats;
    for (const QByteArray &f : QImageReader::supportedImageFormats())
        formats.insert(QString::fromLatin1(f));
    return formats;
}

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
};

// The drives' photos that are not in the library yet, and the name each gets
// there: its own, or with -2, -3 ... when a different photo has that name.
// Hidden files and folders are left out, which skips the ._ files and
// .Trashes a Mac leaves on a drive.
QList<Copy> plan(const QString &usbDir, const QString &dir)
{
    const QSet<QString> formats = imageFormats();
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
            const QString suffix = f.suffix().toLower();
            if (!formats.contains(suffix))
                continue;
            const QString key = f.fileName().toLower();
            if (have.value(key).contains(f.size()))
                continue;
            QString name = f.fileName();
            for (int i = 2; taken.contains(name.toLower()); ++i)
                name = f.completeBaseName() + QLatin1Char('-') + QString::number(i) + QLatin1Char('.') + f.suffix();
            have[key].append(f.size());
            taken.insert(name.toLower());
            out.append({ f.absoluteFilePath(), name });
        }
    }
    return out;
}

} // namespace

Photos::Photos(QObject *parent) : QObject(parent)
{
    m_dir = qEnvironmentVariable("GODWIT_PHOTOS_DIR", QStringLiteral("/data/media/photos"));
    m_usbDir = qEnvironmentVariable("GODWIT_USB_DIR", QStringLiteral("/run/godwit-usb"));

    // A copy fires a change per file; list the folder once it goes quiet
    m_settle.setSingleShot(true);
    m_settle.setInterval(500);
    connect(&m_settle, &QTimer::timeout, this, &Photos::rescan);
    m_usbSettle.setSingleShot(true);
    m_usbSettle.setInterval(1000);
    connect(&m_usbSettle, &QTimer::timeout, this, &Photos::scanUsb);
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

    rescan();
    scanUsb();
}

void Photos::rescan()
{
    // The parent too, so a photos folder made later is still noticed; the
    // USB folder for drives added by hand in the simulator
    for (const QString &d : { m_dir, QFileInfo(m_dir).absolutePath(), m_usbDir })
        if (QFileInfo::exists(d) && !m_watcher.directories().contains(d))
            m_watcher.addPath(d);

    const QSet<QString> formats = imageFormats();
    QFileInfoList files = QDir(m_dir).entryInfoList(QDir::Files | QDir::Readable);
    QCollator byName;
    byName.setNumericMode(true);
    byName.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(), files.end(), [&byName](const QFileInfo &a, const QFileInfo &b) {
        return byName.compare(a.fileName(), b.fileName()) < 0;
    });

    QVariantList photos;
    int skipped = 0;
    for (const QFileInfo &f : std::as_const(files)) {
        if (!formats.contains(f.suffix().toLower())) {
            ++skipped;
            continue;
        }
        photos.append(QVariantMap{ { QStringLiteral("url"), QUrl::fromLocalFile(f.absoluteFilePath()).toString() },
                                   { QStringLiteral("n"), f.completeBaseName() } });
    }
    if (photos == m_photos && skipped == m_skipped)
        return;
    m_photos = photos;
    m_skipped = skipped;
    qCInfo(lcPhotos) << m_photos.size() << "photos in" << m_dir << "," << m_skipped << "in formats that cannot be shown";
    emit photosChanged();
    // What counts as new on a drive changed with the library
    if (m_usb && !m_importing)
        scanUsb();
}

// Off the GUI thread: a drive can hold thousands of files
void Photos::scanUsb()
{
    if (m_importing)
        return;  // finished() scans again
    if (QFileInfo::exists(m_usbDir) && !m_watcher.directories().contains(m_usbDir))
        m_watcher.addPath(m_usbDir);
    const int scan = ++m_usbScan;
    QThreadPool::globalInstance()->start([self = QPointer(this), usbDir = m_usbDir, dir = m_dir, scan] {
        const bool usb = !drives(usbDir).isEmpty();
        const int fresh = usb ? int(plan(usbDir, dir).size()) : 0;
        QMetaObject::invokeMethod(self, [self, usb, fresh, scan] {
            if (self && scan == self->m_usbScan)
                self->setUsb(usb, fresh);
        }, Qt::QueuedConnection);
    });
}

void Photos::setUsb(bool usb, int usbNew)
{
    if (usb == m_usb && usbNew == m_usbNew)
        return;
    if (usb && !m_usb)
        qCInfo(lcPhotos).nospace() << "USB drive in, " << usbNew << " new photos";
    else if (!usb && m_usb)
        qCInfo(lcPhotos) << "USB drive out";
    m_usb = usb;
    m_usbNew = usbNew;
    emit usbChanged();
}

void Photos::importUsb()
{
    if (m_importing || !m_usb)
        return;
    m_importing = true;
    progress(0, m_usbNew);
    QThreadPool::globalInstance()->start([self = QPointer(this), usbDir = m_usbDir, dir = m_dir] {
        const QList<Copy> copies = plan(usbDir, dir);
        const int total = int(copies.size());
        int count = 0, failed = 0;
        QMetaObject::invokeMethod(self, [self, total] { if (self) self->progress(0, total); }, Qt::QueuedConnection);
        for (const Copy &c : copies) {
            // Copied under a hidden name, so the library never lists half a photo
            const QString part = dir + QStringLiteral("/.import-") + c.to;
            QFile::remove(part);
            if (QFile::copy(c.from, part)
                && QFile::setPermissions(part, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup)
                && QFile::rename(part, dir + QLatin1Char('/') + c.to)) {
                ++count;
            } else {
                qCWarning(lcPhotos) << "could not import" << c.from;
                QFile::remove(part);
                ++failed;
            }
            QMetaObject::invokeMethod(self, [self, done = count + failed, total] { if (self) self->progress(done, total); }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(self, [self, count, failed] { if (self) self->finished(count, failed); }, Qt::QueuedConnection);
    });
}

void Photos::progress(int done, int total)
{
    m_importDone = done;
    m_importTotal = total;
    emit importChanged();
}

void Photos::finished(int count, int failed)
{
    qCInfo(lcPhotos) << "imported" << count << "photos from USB," << failed << "failed";
    m_importing = false;
    emit importChanged();
    emit imported(count, failed);
    rescan();
    scanUsb();
}
