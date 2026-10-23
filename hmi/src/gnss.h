#pragma once

#include <QDateTime>
#include <QObject>
#include <QQmlEngine>
#include <QThread>

class GnssWorker;

// The vehicle's position from the DFRobot Gravity GNSS receiver on I2C
// (address 0x20, header pins 3 and 5: i2c6, /dev/gnss-i2c by udev rule).
// Read once a second on its own thread. The register map is the one the
// Bearing firmware uses (TrailCurrentBearing/main/gnss.c).
//
// GODWIT_GNSS_DEVICE overrides /dev/gnss-i2c. Where it does not exist (the
// simulator), available stays false.
class Gnss : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // The receiver answered on the bus
    Q_PROPERTY(bool available READ available NOTIFY updated)
    // It has a position
    Q_PROPERTY(bool fix READ fix NOTIFY updated)
    Q_PROPERTY(double lat READ lat NOTIFY updated)
    Q_PROPERTY(double lon READ lon NOTIFY updated)
    Q_PROPERTY(double altitude READ altitude NOTIFY updated)       // metres
    Q_PROPERTY(double speed READ speed NOTIFY updated)             // mph
    Q_PROPERTY(double course READ course NOTIFY updated)           // degrees from north
    Q_PROPERTY(int satellites READ satellites NOTIFY updated)
    Q_PROPERTY(QDateTime utc READ utc NOTIFY updated)

public:
    struct Reading {
        bool available = false;
        bool fix = false;
        double lat = 0, lon = 0, altitude = 0, speed = 0, course = 0;
        int satellites = 0;
        QDateTime utc;
    };

    explicit Gnss(QObject *parent = nullptr);
    ~Gnss() override;

    bool available() const { return m_r.available; }
    bool fix() const { return m_r.fix; }
    double lat() const { return m_r.lat; }
    double lon() const { return m_r.lon; }
    double altitude() const { return m_r.altitude; }
    double speed() const { return m_r.speed; }
    double course() const { return m_r.course; }
    int satellites() const { return m_r.satellites; }
    QDateTime utc() const { return m_r.utc; }

signals:
    // Once a second while the receiver answers
    void updated();

private:
    Reading m_r;
    QThread m_thread;
    GnssWorker *m_worker = nullptr;
};

Q_DECLARE_METATYPE(Gnss::Reading)
