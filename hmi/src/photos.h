#pragma once

#include <QFile>
#include <QFileSystemWatcher>
#include <QObject>
#include <QQmlEngine>
#include <QSocketNotifier>
#include <QTimer>
#include <QVariantList>

// The photos in the media library, for the Pictures app: /data/media/photos
// on the head unit. Lists the files Qt can decode, in name order so a
// camera's numbering keeps them in the order they were taken, and follows the
// folder as photos are copied in or deleted.
//
// photos are rows {url, n}: the file's URL and its name without the extension.
// skipped counts the files left out because no image plugin on the head unit
// reads them (HEIC, for one).
//
// Also brings in photos from USB drives. udev mounts each one read-only under
// /run/godwit-usb (71-godwit-usb.rules); every folder in there that is not
// empty counts as a drive. usbNew is how many photos on the drives are not in
// the library yet: none with the same name and size. importUsb() copies them
// in on a worker thread, keeping both when two different photos share a name.
//
// GODWIT_PHOTOS_DIR and GODWIT_USB_DIR override the two folders, e.g. for the
// simulator.
class Photos : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString dir READ dir CONSTANT)
    Q_PROPERTY(QVariantList photos READ photos NOTIFY photosChanged)
    Q_PROPERTY(int skipped READ skipped NOTIFY photosChanged)
    Q_PROPERTY(bool usb READ usb NOTIFY usbChanged)
    Q_PROPERTY(int usbNew READ usbNew NOTIFY usbChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY importChanged)
    Q_PROPERTY(int importDone READ importDone NOTIFY importChanged)
    Q_PROPERTY(int importTotal READ importTotal NOTIFY importChanged)

public:
    explicit Photos(QObject *parent = nullptr);

    QString dir() const { return m_dir; }
    QVariantList photos() const { return m_photos; }
    int skipped() const { return m_skipped; }
    bool usb() const { return m_usb; }
    int usbNew() const { return m_usbNew; }
    bool importing() const { return m_importing; }
    int importDone() const { return m_importDone; }
    int importTotal() const { return m_importTotal; }

    Q_INVOKABLE void importUsb();

signals:
    void photosChanged();
    void usbChanged();
    void importChanged();
    // An import has finished: how many photos came in, and how many could
    // not be copied (the drive pulled out part way, or the disk full)
    void imported(int count, int failed);

private:
    void rescan();
    void scanUsb();
    void setUsb(bool usb, int usbNew);
    void progress(int done, int total);
    void finished(int count, int failed);

    QString m_dir;
    QString m_usbDir;
    QVariantList m_photos;
    int m_skipped = 0;
    QFileSystemWatcher m_watcher;
    QTimer m_settle;
    QTimer m_usbSettle;
    QFile m_mounts;                // the mount table, which signals when it changes
    QSocketNotifier *m_mountsChanged = nullptr;
    bool m_usb = false;
    int m_usbNew = 0;
    int m_usbScan = 0;   // drops the result of a scan overtaken by a newer one
    bool m_importing = false;
    int m_importDone = 0;
    int m_importTotal = 0;
};
