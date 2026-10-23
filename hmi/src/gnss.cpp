#include "gnss.h"

#include <QFileInfo>
#include <QLoggingCategory>
#include <QTimer>

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(lcGnss, "godwit.gnss")

namespace {

constexpr quint16 Address = 0x20;
constexpr int DataLen = 29;          // registers 0-28
constexpr quint8 RegLonDir = 12;     // 'E' / 'W', not beside the longitude
constexpr quint8 RegLatDir = 18;     // 'N' / 'S'
constexpr quint8 RegSleep = 35;      // 0x00 = receiver on
constexpr double MphPerKnot = 1.150779;

QString devicePath()
{
    return qEnvironmentVariable("GODWIT_GNSS_DEVICE", QStringLiteral("/dev/gnss-i2c"));
}

// Degrees, whole minutes, and minutes x 100000 in three bytes
double degrees(const quint8 *b)
{
    const quint32 frac = (quint32(b[2]) << 16) | (quint32(b[3]) << 8) | b[4];
    return b[0] + (b[1] + frac / 100000.0) / 60.0;
}

// High byte (bit 7 a sign flag, ignored as in Bearing), low byte, hundredths
double value(const quint8 *b)
{
    return (((b[0] & 0x7f) << 8) | b[1]) + b[2] / 100.0;
}

} // namespace

class GnssWorker : public QObject
{
    Q_OBJECT
public:
    void start()
    {
        m_timer = new QTimer(this);
        m_timer->setInterval(1000);
        connect(m_timer, &QTimer::timeout, this, &GnssWorker::poll);
        m_timer->start();
        poll();
    }
    ~GnssWorker() override
    {
        if (m_fd >= 0)
            ::close(m_fd);
    }

signals:
    void reading(const Gnss::Reading &r);

private:
    bool open()
    {
        if (m_fd >= 0)
            return true;
        const QString path = devicePath();
        if (!QFileInfo::exists(path))
            return false;
        m_fd = ::open(QFile::encodeName(path).constData(), O_RDWR | O_CLOEXEC);
        if (m_fd < 0) {
            if (!m_warned)
                qCWarning(lcGnss) << "cannot open" << path << ::strerror(errno);
            m_warned = true;
            return false;
        }
        // Wake the receiver, as Bearing does
        const quint8 on[2] = { RegSleep, 0x00 };
        if (::ioctl(m_fd, I2C_SLAVE, Address) < 0 || ::write(m_fd, on, 2) != 2)
            qCWarning(lcGnss) << "no receiver at 0x20 on" << path;
        else
            qCInfo(lcGnss) << "receiver on" << path;
        return true;
    }

    // Register 0 onwards: a write of the register number, then a read
    bool readRegs(quint8 *buf)
    {
        quint8 reg = 0;
        i2c_msg msgs[2] = {
            { Address, 0, 1, &reg },
            { Address, I2C_M_RD, DataLen, buf },
        };
        i2c_rdwr_ioctl_data xfer = { msgs, 2 };
        return ::ioctl(m_fd, I2C_RDWR, &xfer) == 2;
    }

    void poll()
    {
        Gnss::Reading r;
        quint8 b[DataLen];
        if (!open() || !readRegs(b)) {
            if (m_wasAvailable)
                qCWarning(lcGnss) << "receiver stopped answering";
            m_wasAvailable = false;
            emit reading(r);
            return;
        }
        r.available = true;
        if (!m_wasAvailable)
            qCInfo(lcGnss) << "receiver answering";
        m_wasAvailable = true;

        const int year = (b[0] << 8) | b[1];
        r.utc = QDateTime(QDate(year, b[2], b[3]), QTime(b[4], b[5], b[6]), QTimeZone::UTC);
        r.lat = degrees(b + 7) * (b[RegLatDir] == 'S' ? -1 : 1);
        r.lon = degrees(b + 13) * (b[RegLonDir] == 'W' ? -1 : 1);
        r.satellites = b[19];
        r.altitude = value(b + 20);
        r.speed = value(b + 23) * MphPerKnot;
        r.course = value(b + 26);
        // No fix flag: a fix has satellites, a hemisphere and a position
        r.fix = r.satellites > 0
            && (b[RegLatDir] == 'N' || b[RegLatDir] == 'S')
            && (b[RegLonDir] == 'E' || b[RegLonDir] == 'W')
            && (r.lat != 0 || r.lon != 0);
        if (r.fix != m_hadFix)
            qCInfo(lcGnss) << (r.fix ? "fix" : "no fix") << r.satellites << "satellites";
        m_hadFix = r.fix;
        emit reading(r);
    }

    QTimer *m_timer = nullptr;
    int m_fd = -1;
    bool m_warned = false;
    bool m_wasAvailable = false;
    bool m_hadFix = false;
};

Gnss::Gnss(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<Gnss::Reading>();
    m_worker = new GnssWorker;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, m_worker, &GnssWorker::start);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &GnssWorker::reading, this, [this](const Gnss::Reading &r) {
        m_r = r;
        emit updated();
    });
    m_thread.setObjectName(QStringLiteral("gnss"));
    m_thread.start(QThread::LowPriority);
}

Gnss::~Gnss()
{
    m_thread.quit();
    m_thread.wait();
}

#include "gnss.moc"
