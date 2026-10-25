#pragma once

#include <QAudioDevice>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QObject>
#include <QQmlEngine>
#include <QSet>
#include <QSocketNotifier>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

class QVideoSink;

// One folder of the media library on the head unit (/data/media/<kind>):
// lists the files it can show, in name order so a camera's numbering keeps
// them in the order they were taken, and follows the folder as files are
// copied in or deleted.
//
// items are rows {url, n}: the file's URL and its name without the extension,
// plus whatever the kind adds. skipped counts the files left out because the
// head unit cannot show them (HEIC photos, for one).
//
// Also brings in files from USB drives. udev mounts each one read-only under
// /run/godwit-usb (71-godwit-usb.rules); every folder in there that is not
// empty counts as a drive. usbNew is how many of the drive's files are not in
// the library yet: none with the same name and size. importUsb() copies them
// in on a worker thread, keeping both when two different files share a name,
// and skips a file the disk has no room for.
//
// GODWIT_USB_DIR overrides the USB folder, e.g. for the simulator.
class MediaFolder : public QObject
{
    Q_OBJECT
    QML_ANONYMOUS

    Q_PROPERTY(QString dir READ dir CONSTANT)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(int skipped READ skipped NOTIFY itemsChanged)
    Q_PROPERTY(bool usb READ usb NOTIFY usbChanged)
    Q_PROPERTY(int usbNew READ usbNew NOTIFY usbChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY importChanged)
    // Files copied so far and in all, and the share of the bytes copied (0..1)
    Q_PROPERTY(int importDone READ importDone NOTIFY importChanged)
    Q_PROPERTY(int importTotal READ importTotal NOTIFY importChanged)
    Q_PROPERTY(double importProgress READ importProgress NOTIFY importChanged)

public:
    QString dir() const { return m_dir; }
    QVariantList items() const { return m_items; }
    int skipped() const { return m_skipped; }
    bool usb() const { return m_usb; }
    int usbNew() const { return m_usbNew; }
    bool importing() const { return m_importing; }
    int importDone() const { return m_importDone; }
    int importTotal() const { return m_importTotal; }
    double importProgress() const { return m_importProgress; }

    Q_INVOKABLE void importUsb();

signals:
    void itemsChanged();
    void usbChanged();
    void importChanged();
    // An import has finished: how many files came in, and how many could not
    // be copied (the drive pulled out part way, or no room left)
    void imported(int count, int failed);

protected:
    // dirEnv names the environment variable that overrides defaultDir.
    // suffixes are the lower-case file extensions this folder holds.
    MediaFolder(const char *dirEnv, const QString &defaultDir, const QSet<QString> &suffixes, QObject *parent);
    // Lists the folder and starts watching it and the drives; call last in
    // the subclass's constructor
    void start();
    // Adds the kind's own fields to a file's row
    virtual void decorate(QVariantMap &row, const QFileInfo &file) { Q_UNUSED(row) Q_UNUSED(file) }
    // Builds the rows again from the last listing, after decorate() would
    // give something new
    void refreshItems();
    QStringList files() const { return m_files; }

private:
    void rescan();
    void scanUsb();
    void setUsb(bool usb, int usbNew);
    void progress(int done, int total, double share);
    void finished(int count, int failed);

    QString m_dir;
    QString m_usbDir;
    QSet<QString> m_suffixes;
    QStringList m_files;   // absolute paths, in order
    QVariantList m_items;
    int m_skipped = 0;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QTimer m_usbSettle;
    QFile m_mounts;        // the mount table, which signals when it changes
    QSocketNotifier *m_mountsChanged = nullptr;
    bool m_usb = false;
    int m_usbNew = 0;
    int m_usbScan = 0;     // drops the result of a scan overtaken by a newer one
    bool m_importing = false;
    int m_importDone = 0;
    int m_importTotal = 0;
    double m_importProgress = 0;
};

// /data/media/photos, for the Pictures app: the files an image plugin on the
// head unit reads. GODWIT_PHOTOS_DIR overrides the folder.
class Photos : public MediaFolder
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Photos(QObject *parent = nullptr);
};

// /data/media/videos, for the Videos app. Rows add thumb, a still from the
// video ("" until it is made, or when it cannot be), and dur, its length in
// seconds (0 until known). Both are taken once per video, one video at a
// time, by a player with no output, and kept in the cache folder
// (/var/cache/godwit on the head unit), so they survive restarts.
// GODWIT_VIDEOS_DIR overrides the folder.
//
// audioOutput is where the player's sound goes: AGL's multimedia role sink,
// so the HMI's media volume and mute apply to it. Qt ties its stream to the
// device it is given, which takes it past WirePlumber's role routing, so the
// role sink has to be that device. Elsewhere it is the default output.
class Videos : public MediaFolder
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QAudioDevice audioOutput READ audioOutput NOTIFY audioOutputChanged)

public:
    explicit Videos(QObject *parent = nullptr);

    QAudioDevice audioOutput() const { return m_audioOutput; }

signals:
    void audioOutputChanged();

protected:
    void decorate(QVariantMap &row, const QFileInfo &file) override;

private:
    struct Info {
        int dur = 0;      // seconds
        bool thumb = false;
    };
    static QString keyOf(const QFileInfo &file);
    void loadIndex();
    void saveIndex();
    void next();
    void loaded();
    void frame();
    void done(bool thumb);
    void findAudioOutput();

    QString m_cache;
    QHash<QString, Info> m_index;   // keyOf() -> what is known
    QMediaPlayer *m_player = nullptr;
    QVideoSink *m_sink = nullptr;
    QTimer m_giveUp;
    QString m_busy;                 // the key being worked on
    qint64 m_target = 0;            // ms into the video the still is taken at
    QMediaDevices *m_devices = nullptr;
    QAudioDevice m_audioOutput;
};
