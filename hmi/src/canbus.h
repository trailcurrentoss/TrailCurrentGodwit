#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

// The two CAN ports, as chosen in Settings › CAN, and how they are doing.
//
// Which interface is on which port, and each port's name, come from one
// place: godwit-can-apply works them out from the board's port map and
// publishes them in /run/godwit-can/ports. Everything here reads that, so
// the screens, the TrailCurrent bus and the port settings always agree. The
// name is the one printed on the board beside the port's terminal (CAN_0 and
// CAN_1 on the Waveshare 2-CH CAN FD HAT), or CAN 1 and CAN 2 without a map.
//
// The choices are saved to can.conf in the HMI's state folder. The HMI's user
// may not reconfigure a network interface, so after saving it leaves a file
// for godwit-can.path, which runs godwit-can-apply as root. Only on the head
// unit (eglfs): the simulator saves but touches nothing.
//
// Detecting a port's rates works the same way: the HMI asks, godwit-can-apply
// listens at each rate in listen-only mode (it never transmits) and reports
// its progress and result in can-detect.out, beside the request.
class CanBus : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // One map per port: role (Vehicle, TrailCurrent, Off), fd, rate, drate, listen
    Q_PROPERTY(QVariantList ports READ ports NOTIFY portsChanged)
    // One map per port: name ("CAN_0"), iface ("" if not found), up, fps (frames a second
    // received or sent), eps (receive errors a second: traffic at a bit rate
    // that does not match the bus). Averaged over the last few seconds.
    Q_PROPERTY(QVariantList status READ status NOTIFY statusChanged)
    // The rate detection in progress or last finished, empty if none:
    // port (0 or 1), state (running, done, failed), step, steps, rate (the
    // arbitration rate being tried, or found), and when done: result (found,
    // nobus, errors), drate and data (found, nofast, errors), frames
    Q_PROPERTY(QVariantMap detection READ detection NOTIFY detectionChanged)

public:
    explicit CanBus(QObject *parent = nullptr);

    QVariantList ports() const { return m_ports; }
    QVariantList status() const { return m_status; }
    QVariantMap detection() const { return m_detection; }

    // Save both ports and apply them. Anything invalid is refused whole.
    Q_INVOKABLE bool save(const QVariantList &ports);
    // Find the rates of the bus on a port (0 or 1). False if it cannot start.
    Q_INVOKABLE bool detect(int port);

    // The interface on each port, port 1 first; "" for one not found
    QStringList interfaces() const { return m_ifaces; }
    // The port's name, as printed on the board: "CAN_0"; "CAN 1" without a map
    QString portName(int port) const { return m_names.value(port); }

signals:
    void portsChanged();
    void statusChanged();
    void detectionChanged();

private:
    static QVariantList defaults();
    static bool valid(const QVariantList &ports);
    QString confPath() const;
    QVariantList load() const;
    void write();
    void apply();
    void poll();
    void readDetection();
    void readPorts();

    struct Sample { quint64 frames = 0; quint64 errors = 0; };
    QVariantList m_ports;
    QVariantList m_status;
    // From godwit-can-apply's /run/godwit-can/ports, per port
    QStringList m_ifaces, m_names;
    // Per port, the counters at each of the last few polls, newest last
    QList<QList<Sample>> m_history;
    QVariantMap m_detection;
    qint64 m_detectSince = 0;
    QTimer m_poll;
};
