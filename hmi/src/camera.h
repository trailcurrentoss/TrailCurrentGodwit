#pragma once

#include <QImage>
#include <QMutex>
#include <QQuickItem>
#include <QThread>
#include <QVariantList>
#include <atomic>

// The USB cameras plugged in, rescanned every two seconds:
// [{id, name, port}], ordered by USB port. id is the USB interface
// ("1-1.2:1.0"), so a camera keeps its id across boots while it stays in the
// same port.
class CameraList : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)

public:
    explicit CameraList(QObject *parent = nullptr);
    QVariantList devices() const { return m_devices; }

signals:
    void devicesChanged();

private:
    void rescan();
    QVariantList m_devices;
};

// Reads one USB (UVC) camera through V4L2 and hands each frame over as an
// image. Waits and retries while that camera is not plugged in, and starts
// again if it is unplugged.
class CameraThread : public QThread
{
    Q_OBJECT

public:
    CameraThread(const QString &source, QObject *parent = nullptr) : QThread(parent), m_source(source) {}
    void stop() { m_stop = true; }

signals:
    void frame(const QImage &image);
    void cameraChanged(const QString &name); // empty when there is none

protected:
    void run() override;

private:
    bool stream(int fd, const QString &name);
    const QString m_source;
    std::atomic_bool m_stop = false;
};

// Live video from one USB camera, filling the item (cropped, not stretched).
// Streams only while active, so the camera is idle unless the camera screen
// is showing.
class CameraView : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    // Which camera: an id from CameraList. Empty shows nothing.
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    // Flip left to right, for a rear camera that is not mirrored itself
    Q_PROPERTY(bool mirror READ mirror WRITE setMirror NOTIFY mirrorChanged)
    // The camera's name; empty when none is connected
    Q_PROPERTY(QString camera READ camera NOTIFY cameraChanged)
    // True once frames are arriving
    Q_PROPERTY(bool streaming READ streaming NOTIFY streamingChanged)

public:
    explicit CameraView(QQuickItem *parent = nullptr);
    ~CameraView() override;

    bool active() const { return m_active; }
    void setActive(bool on);
    QString source() const { return m_source; }
    void setSource(const QString &id);
    bool mirror() const { return m_mirror; }
    void setMirror(bool on);
    QString camera() const { return m_camera; }
    bool streaming() const { return m_streaming; }

signals:
    void activeChanged();
    void sourceChanged();
    void mirrorChanged();
    void cameraChanged();
    void streamingChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;

private:
    void startThread();
    void stopThread();
    void setStreaming(bool on);

    CameraThread *m_thread = nullptr;
    bool m_active = false;
    QString m_source;
    bool m_mirror = false;
    bool m_streaming = false;
    QString m_camera;
    QMutex m_lock;
    QImage m_frame;
    bool m_fresh = false;
};
