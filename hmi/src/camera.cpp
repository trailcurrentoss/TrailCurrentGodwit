#include "camera.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutexLocker>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QTimer>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcCam, "godwit.camera")

namespace {

// 640x480 is what every UVC camera streams uncompressed at full frame rate,
// and plenty for a backup camera on this screen
constexpr int Width = 640;
constexpr int Height = 480;
constexpr int Buffers = 4;

int xioctl(int fd, unsigned long req, void *arg)
{
    int r;
    do {
        r = ioctl(fd, req, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

// The USB cameras, found through sysfs without opening anything (opening the
// SoC's codec nodes would wake the codec). A UVC camera's capture node has
// index 0; its second node carries only metadata. id is the USB interface
// ("1-1.2:1.0"): the port, and which camera when one device has several.
// With wanted set, opens that camera and returns only it.
struct Found { int fd = -1; QString id, name; };
QList<Found> findCameras(const QString &wanted = QString())
{
    QList<Found> out;
    const QDir sys(QStringLiteral("/sys/class/video4linux"));
    for (const QString &node : sys.entryList({ QStringLiteral("video*") }, QDir::Dirs | QDir::System, QDir::Name)) {
        const QString dir = sys.filePath(node);
        auto read = [&](const char *f) {
            QFile file(dir + QLatin1Char('/') + QLatin1String(f));
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
        };
        if (QFileInfo(dir + QStringLiteral("/device/driver")).symLinkTarget().section(QLatin1Char('/'), -1) != QLatin1String("uvcvideo")
            || read("index") != QLatin1String("0"))
            continue;
        Found f { -1, QFileInfo(QFileInfo(dir + QStringLiteral("/device")).canonicalFilePath()).fileName(), read("name") };
        if (wanted.isEmpty()) {
            out.append(f);
            continue;
        }
        if (f.id != wanted)
            continue;
        const QByteArray path = QByteArrayLiteral("/dev/") + node.toLatin1();
        f.fd = ::open(path.constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (f.fd < 0) {
            qCWarning(lcCam) << "cannot open" << path << strerror(errno);
            return {};
        }
        qCInfo(lcCam) << "using" << path << f.name << "on port" << f.id;
        return { f };
    }
    std::sort(out.begin(), out.end(), [](const Found &a, const Found &b) { return a.id < b.id; });
    return out;
}

// "1-1.2:1.0" -> "1.2", the port as counted from the board's own USB socket
QString portOf(const QString &id)
{
    return id.section(QLatin1Char(':'), 0, 0).section(QLatin1Char('-'), -1);
}

inline uchar clamp8(int v) { return uchar(v < 0 ? 0 : v > 255 ? 255 : v); }

// YUYV 4:2:2 (BT.601, limited range) to RGB32
QImage fromYuyv(const uchar *src, int w, int h, int stride)
{
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        const uchar *s = src + y * stride;
        auto *d = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < w; x += 2, s += 4) {
            const int u = s[1] - 128, v = s[3] - 128;
            const int ru = 409 * v, gu = -100 * u - 208 * v, bu = 516 * u;
            for (int k = 0; k < 2; ++k) {
                const int c = 298 * (s[k * 2] - 16) + 128;
                d[x + k] = qRgb(clamp8((c + ru) >> 8), clamp8((c + gu) >> 8), clamp8((c + bu) >> 8));
            }
        }
    }
    return img;
}

} // namespace

// ------------------------------------------------------------------ thread

void CameraThread::run()
{
    QString lastName = QStringLiteral("-");
    while (!m_stop) {
        const QList<Found> found = m_source.isEmpty() ? QList<Found>() : findCameras(m_source);
        const int fd = found.isEmpty() ? -1 : found.first().fd;
        const QString name = found.isEmpty() ? QString() : found.first().name;
        if (fd < 0) {
            if (!lastName.isEmpty())
                emit cameraChanged(QString());
            lastName.clear();
            // Look again in a second; a camera may be plugged in at any time
            for (int i = 0; i < 10 && !m_stop; ++i)
                msleep(100);
            continue;
        }
        emit cameraChanged(name);
        lastName = name;
        if (!stream(fd, name))
            msleep(500); // unplugged or failed; look again shortly
        ::close(fd);
    }
}

bool CameraThread::stream(int fd, const QString &name)
{
    v4l2_format fmt {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = Width;
    fmt.fmt.pix.height = Height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;
    if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0
        || (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_YUYV && fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG)) {
        // No uncompressed mode: most cameras that lack it offer MJPEG
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
        if (xioctl(fd, VIDIOC_S_FMT, &fmt) < 0 || fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
            qCWarning(lcCam) << name << "offers neither YUYV nor MJPEG";
            return false;
        }
    }
    const int w = int(fmt.fmt.pix.width), h = int(fmt.fmt.pix.height), stride = int(fmt.fmt.pix.bytesperline);
    const bool mjpeg = fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG;
    qCInfo(lcCam) << name << w << "x" << h << (mjpeg ? "MJPEG" : "YUYV");

    v4l2_requestbuffers req {};
    req.count = Buffers;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) {
        qCWarning(lcCam) << "REQBUFS failed:" << strerror(errno);
        return false;
    }
    struct Mapped { void *p = MAP_FAILED; size_t len = 0; };
    QList<Mapped> maps(req.count);
    bool ok = true;
    for (quint32 i = 0; i < req.count && ok; ++i) {
        v4l2_buffer b {};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        b.index = i;
        ok = xioctl(fd, VIDIOC_QUERYBUF, &b) == 0;
        if (ok) {
            maps[i].len = b.length;
            maps[i].p = mmap(nullptr, b.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, b.m.offset);
            ok = maps[i].p != MAP_FAILED && xioctl(fd, VIDIOC_QBUF, &b) == 0;
        }
    }
    v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ok)
        ok = xioctl(fd, VIDIOC_STREAMON, &type) == 0;
    if (!ok)
        qCWarning(lcCam) << "could not start" << name << ":" << strerror(errno);

    bool healthy = ok;
    while (ok && !m_stop) {
        pollfd p { fd, POLLIN, 0 };
        const int r = poll(&p, 1, 200);
        if (r == 0)
            continue;
        if (r < 0 || (p.revents & (POLLERR | POLLHUP))) {
            healthy = false;
            break;
        }
        v4l2_buffer b {};
        b.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        b.memory = V4L2_MEMORY_MMAP;
        if (xioctl(fd, VIDIOC_DQBUF, &b) < 0) {
            if (errno == EAGAIN)
                continue;
            qCInfo(lcCam) << name << "stopped:" << strerror(errno);
            healthy = false;
            break;
        }
        const auto *data = static_cast<const uchar *>(maps[b.index].p);
        QImage img = mjpeg ? QImage::fromData(data, int(b.bytesused), "JPEG").convertToFormat(QImage::Format_RGB32)
                           : fromYuyv(data, w, h, stride);
        xioctl(fd, VIDIOC_QBUF, &b);
        if (!img.isNull())
            emit frame(img);
    }

    xioctl(fd, VIDIOC_STREAMOFF, &type);
    for (const Mapped &m : maps)
        if (m.p != MAP_FAILED)
            munmap(m.p, m.len);
    return healthy;
}

// ------------------------------------------------------------------ list

CameraList::CameraList(QObject *parent)
    : QObject(parent)
{
    rescan();
    auto *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &CameraList::rescan);
    timer->start(2000);
}

void CameraList::rescan()
{
    QVariantList list;
    for (const Found &f : findCameras())
        list.append(QVariantMap { { QStringLiteral("id"), f.id }, { QStringLiteral("name"), f.name },
                                  { QStringLiteral("port"), portOf(f.id) } });
    if (list == m_devices)
        return;
    m_devices = list;
    emit devicesChanged();
}

// ------------------------------------------------------------------ item

CameraView::CameraView(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(ItemHasContents);
}

CameraView::~CameraView()
{
    stopThread();
}

void CameraView::setActive(bool on)
{
    if (m_active == on)
        return;
    m_active = on;
    if (on)
        startThread();
    else
        stopThread();
    emit activeChanged();
}

void CameraView::setSource(const QString &id)
{
    if (m_source == id)
        return;
    m_source = id;
    if (m_active) {
        stopThread();
        startThread();
    }
    emit sourceChanged();
}

void CameraView::setMirror(bool on)
{
    if (m_mirror == on)
        return;
    m_mirror = on;
    update();
    emit mirrorChanged();
}

void CameraView::startThread()
{
    if (m_thread)
        return;
    m_thread = new CameraThread(m_source, this);
    connect(m_thread, &CameraThread::frame, this, [this](const QImage &img) {
        {
            QMutexLocker lock(&m_lock);
            m_frame = img;
            m_fresh = true;
        }
        setStreaming(true);
        update();
    });
    connect(m_thread, &CameraThread::cameraChanged, this, [this](const QString &name) {
        setStreaming(false);
        if (m_camera != name) {
            m_camera = name;
            emit cameraChanged();
        }
    });
    m_thread->start();
}

void CameraView::stopThread()
{
    if (!m_thread)
        return;
    m_thread->stop();
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
    setStreaming(false);
}

void CameraView::setStreaming(bool on)
{
    if (m_streaming == on)
        return;
    m_streaming = on;
    if (!on) {
        QMutexLocker lock(&m_lock);
        m_frame = QImage();
        m_fresh = true;
        update();
    }
    emit streamingChanged();
}

QSGNode *CameraView::updatePaintNode(QSGNode *old, UpdatePaintNodeData *)
{
    auto *node = static_cast<QSGSimpleTextureNode *>(old);
    QMutexLocker lock(&m_lock);
    if (m_frame.isNull() || width() <= 0 || height() <= 0) {
        delete node;
        return nullptr;
    }
    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
    }
    if (m_fresh || !node->texture()) {
        node->setTexture(window()->createTextureFromImage(m_frame));
        m_fresh = false;
    }
    // Fill the item, cropping the frame's overflow evenly
    const QSizeF src = m_frame.size();
    const qreal scale = qMax(width() / src.width(), height() / src.height());
    const QSizeF crop(width() / scale, height() / scale);
    node->setSourceRect(QRectF(QPointF((src.width() - crop.width()) / 2, (src.height() - crop.height()) / 2), crop));
    node->setRect(boundingRect());
    node->setTextureCoordinatesTransform(m_mirror ? QSGSimpleTextureNode::MirrorHorizontally
                                                  : QSGSimpleTextureNode::NoTransform);
    return node;
}
