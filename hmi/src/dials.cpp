#include "dials.h"

#include "dialprotocol.h"
#include "iconprovider.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QSocketNotifier>

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>

#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

// What the dial firmware puts in its USB product string; Linux names the
// HID device "<manufacturer> <product>".
constexpr QByteArrayView HidName = "HID_NAME=TrailCurrent Godwit Dial";

using Report = std::array<uint8_t, DialProto::ReportLen + 1>;

Report report(uint8_t id)
{
    Report r{};
    r[0] = id;
    return r;
}

// UTF-8 of s, cut on a character boundary to at most max bytes
QByteArray utf8Fit(const QString &s, int max)
{
    QByteArray b = s.toUtf8();
    if (b.size() <= max)
        return b;
    int n = max;
    while (n > 0 && (uint8_t(b[n]) & 0xc0) == 0x80)
        --n;
    return b.left(n);
}

} // namespace

// One open dial. Writes go through a thread of their own: a hidraw write
// waits for the dial to take the report, about a millisecond each, and an
// icon is over a hundred of them. Ring frames do not queue -- only the
// newest is kept, and it goes ahead of anything else waiting.
class Dials::Device
{
public:
    Device(int fd, QString node, int side)
        : fd(fd), node(std::move(node)), side(side), m_thread([this] { run(); })
    {
    }

    ~Device()
    {
        {
            std::lock_guard lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_one();
        m_thread.join();
        ::close(fd);
    }

    void send(const Report &r)
    {
        {
            std::lock_guard lock(m_mutex);
            // A dial that has stopped taking reports is about to be dropped;
            // do not hoard what it would never read.
            if (m_queue.size() < 1024)
                m_queue.push_back(r);
        }
        m_cv.notify_one();
    }

    void sendRing(const Report &r)
    {
        {
            std::lock_guard lock(m_mutex);
            m_ring = r;
        }
        m_cv.notify_one();
    }

    const int fd;
    const QString node;
    const int side;
    std::unique_ptr<QSocketNotifier> notifier;
    std::optional<DialFace> sent; // what the dial is showing, as far as we know
    bool down = false;

private:
    void run()
    {
        for (;;) {
            Report r;
            {
                std::unique_lock lock(m_mutex);
                m_cv.wait(lock, [this] { return m_stop || m_ring || !m_queue.empty(); });
                if (m_stop)
                    return;
                if (m_ring) {
                    r = *m_ring;
                    m_ring.reset();
                } else {
                    r = m_queue.front();
                    m_queue.pop_front();
                }
            }
            // Gone dials are noticed and closed by the reader
            if (::write(fd, r.data(), r.size()) < 0 && errno != EINTR && errno != EAGAIN)
                return;
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::deque<Report> m_queue;
    std::optional<Report> m_ring;
    bool m_stop = false;
    std::thread m_thread;
};

Dials::Dials(QObject *parent)
    : QObject(parent)
{
    m_scan.setInterval(1000);
    connect(&m_scan, &QTimer::timeout, this, &Dials::scan);
    m_scan.start();
    QTimer::singleShot(0, this, &Dials::scan);
}

Dials::~Dials() = default;

void Dials::scan()
{
    if (m_dev[0] && m_dev[1])
        return;

    const QDir sys(QStringLiteral("/sys/class/hidraw"));
    const QStringList nodes = sys.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    // Forget refusals for nodes that have gone; their number may come back
    // as a different device.
    for (auto it = m_refused.begin(); it != m_refused.end();)
        it = nodes.contains(it->mid(5)) ? std::next(it) : m_refused.erase(it);

    for (const QString &name : nodes) {
        const QString node = QStringLiteral("/dev/") + name;
        if (m_refused.contains(node)
            || std::any_of(m_dev.begin(), m_dev.end(), [&](const auto &d) { return d && d->node == node; }))
            continue;
        QFile uevent(sys.filePath(name + QStringLiteral("/device/uevent")));
        if (uevent.open(QIODevice::ReadOnly) && uevent.readAll().contains(HidName))
            open(node);
    }
}

void Dials::open(const QString &node)
{
    const int fd = ::open(node.toLocal8Bit().constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        // The HMI starts before udev has applied the rule that lets it in, so
        // a refusal at boot is temporary: the next scan tries again
        const bool denied = errno == EACCES || errno == EPERM;
        if (!denied || !m_denied.contains(node))
            qWarning("dials: cannot open %s: %s%s", qPrintable(node), strerror(errno),
                     denied ? " (waiting for the udev rule that gives the godwit group access)" : "");
        if (denied)
            m_denied.insert(node);
        else
            m_refused.insert(node);
        return;
    }
    if (m_denied.remove(node))
        qInfo("dials: %s is accessible now", qPrintable(node));

    uint8_t info[DialProto::ReportLen + 1] = { DialProto::FeatureInfo };
    const int n = ::ioctl(fd, HIDIOCGFEATURE(sizeof(info)), info);
    if (n < 3 || info[1] != DialProto::Version || info[2] > 1) {
        qWarning("dials: %s is not a protocol %d dial (version %d)", qPrintable(node),
                 DialProto::Version, n >= 2 ? info[1] : -1);
        ::close(fd);
        m_refused.insert(node);
        return;
    }
    const int side = info[2];
    if (m_dev[side]) {
        qWarning("dials: %s is a second %s dial; ignored", qPrintable(node), side ? "right" : "left");
        ::close(fd);
        m_refused.insert(node);
        return;
    }
    const auto *ver = reinterpret_cast<const char *>(info + 3);
    qInfo("dials: %s dial on %s, firmware %s", side ? "right" : "left", qPrintable(node),
          qPrintable(QString::fromUtf8(ver, qstrnlen(ver, sizeof(info) - 3))));

    auto dev = std::make_unique<Device>(fd, node, side);
    dev->notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Read);
    connect(dev->notifier.get(), &QSocketNotifier::activated, this, [this, side] { readFrom(side); });
    m_dev[side] = std::move(dev);
    sendFace(side);
}

void Dials::readFrom(int side)
{
    Device *dev = m_dev[side].get();
    uint8_t buf[DialProto::ReportLen + 1];
    while (dev) {
        const ssize_t n = ::read(dev->fd, buf, sizeof(buf));
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            return;
        if (n <= 0) {
            drop(side);
            return;
        }
        if (buf[0] == DialProto::InEvent && n >= 3) {
            switch (buf[1]) {
            case DialProto::EvDown:
                dev->down = true;
                emit down(side);
                break;
            case DialProto::EvUp:
                dev->down = false;
                emit up(side);
                break;
            case DialProto::EvTurn:
                emit turned(side, int8_t(buf[2]));
                break;
            }
        } else if (buf[0] == DialProto::InIconReq && n >= 3) {
            const auto *name = reinterpret_cast<const char *>(buf + 2);
            sendIcon(*dev, QString::fromUtf8(name, qstrnlen(name, n - 2)), buf[1]);
        }
        dev = m_dev[side].get(); // a handler may have dropped it
    }
}

void Dials::drop(int side)
{
    std::unique_ptr<Device> dev = std::move(m_dev[side]);
    if (!dev)
        return;
    qInfo("dials: %s dial gone", side ? "right" : "left");
    // This runs inside the notifier's own signal, so it is deleted later
    dev->notifier->setEnabled(false);
    dev->notifier.release()->deleteLater();
    // A knob unplugged while held is a knob released
    if (dev->down)
        emit up(side);
}

void Dials::setRing(int side, const std::array<uint8_t, 15> &rgb)
{
    if (side < 0 || side > 1 || !m_dev[side])
        return;
    Report r = report(DialProto::OutRing);
    std::copy(rgb.begin(), rgb.end(), r.begin() + 1);
    m_dev[side]->sendRing(r);
}

void Dials::setFace(int side, const DialFace &face)
{
    if (side < 0 || side > 1)
        return;
    m_face[side] = face;
    if (m_dev[side])
        sendFace(side);
}

// Changed text fields, then the style report that makes the dial redraw
void Dials::sendFace(int side)
{
    Device &dev = *m_dev[side];
    const DialFace &f = m_face[side];
    if (dev.sent && *dev.sent == f)
        return;

    static constexpr QString DialFace::*Fields[DialProto::FieldCount] = {
        &DialFace::label, &DialFace::value, &DialFace::unit, &DialFace::sub, &DialFace::icon,
    };
    for (int i = 0; i < DialProto::FieldCount; ++i) {
        if (dev.sent && (*dev.sent).*Fields[i] == f.*Fields[i])
            continue;
        Report r = report(DialProto::OutText);
        r[1] = uint8_t(i);
        const QByteArray text = utf8Fit(f.*Fields[i], DialProto::TextMax);
        std::memcpy(r.data() + 2, text.constData(), text.size());
        dev.send(r);
    }

    const DialProto::Face style{
        uint8_t((f.dark ? DialProto::FaceDark : 0) | (f.small ? DialProto::FaceSmall : 0)),
        f.color,
        uint8_t(std::clamp(f.valuePx, 8, 120)),
        uint8_t(std::clamp(f.iconPx, 8, DialProto::IconMaxPx)),
        uint8_t(std::clamp(f.pagerCount, 0, 255)),
        f.pagerOn < 0 || f.pagerOn > 254 ? DialProto::PagerNone : uint8_t(f.pagerOn),
        uint8_t(std::clamp(f.backlight, 0, 100)),
    };
    Report r = report(DialProto::OutFace);
    std::memcpy(r.data() + 1, &style, sizeof(style));
    dev.send(r);
    dev.sent = f;
}

// The same SVG the screen uses, as 8-bit alpha the dial tints itself
void Dials::sendIcon(Device &dev, const QString &name, int px)
{
    if (px < 1 || px > DialProto::IconMaxPx)
        return;
    QImage img = IconProvider::render(name, Qt::white, QSize(px, px));
    if (img.isNull()) {
        // An unknown name still gets an answer, or the dial keeps asking
        img = QImage(px, px, QImage::Format_Alpha8);
        img.fill(0);
    }
    img = img.convertToFormat(QImage::Format_Alpha8);

    Report begin = report(DialProto::OutIconBegin);
    begin[1] = uint8_t(px);
    const QByteArray n = utf8Fit(name, DialProto::ReportLen - 1);
    std::memcpy(begin.data() + 2, n.constData(), n.size());
    dev.send(begin);

    QByteArray alpha;
    alpha.reserve(px * px);
    for (int y = 0; y < px; ++y)
        alpha.append(reinterpret_cast<const char *>(img.constScanLine(y)), px);
    for (int off = 0; off < alpha.size(); off += DialProto::IconChunk) {
        const int len = std::min<int>(DialProto::IconChunk, alpha.size() - off);
        Report r = report(DialProto::OutIconData);
        r[1] = uint8_t(off & 0xff);
        r[2] = uint8_t(off >> 8);
        r[3] = uint8_t(len);
        std::memcpy(r.data() + 4, alpha.constData() + off, len);
        dev.send(r);
    }
}
