#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <array>
#include <cstdint>
#include <memory>

// What one encoder module's round display shows: the fields of an Hmi.encs
// entry that KnobFace.qml draws.
struct DialFace {
    QString label;
    QString value;
    QString unit;
    QString sub;
    QString icon;
    uint8_t color = 0; // DialProto::Color
    bool small = false;
    bool dark = false;
    int valuePx = 88;
    int iconPx = 84;
    int pagerCount = 0;
    int pagerOn = -1;
    int backlight = 100;

    bool operator==(const DialFace &) const = default;
};

// The two encoder modules on USB. Each is a vendor HID device (a hidraw node)
// speaking the dial protocol (dialprotocol.h): knob events come in, ring
// frames, display faces and icons go out.
//
// Dials are found by their USB name and told apart by the side each one
// reports, so it does not matter which port either is plugged into. A dial
// that is unplugged or reboots is picked up again within a second and sent
// its face afresh.
class Dials : public QObject
{
    Q_OBJECT

public:
    explicit Dials(QObject *parent = nullptr);
    ~Dials() override;

    // LED 0 lower-left round to LED 4 lower-right, {r, g, b} each
    void setRing(int side, const std::array<uint8_t, 15> &rgb);
    void setFace(int side, const DialFace &face);

signals:
    void down(int side);
    void up(int side);
    void turned(int side, int detents);

private:
    class Device;

    void scan();
    void open(const QString &node);
    void readFrom(int side);
    void drop(int side);
    void sendFace(int side);
    void sendIcon(Device &dev, const QString &name, int px);

    std::array<std::unique_ptr<Device>, 2> m_dev;
    std::array<DialFace, 2> m_face;
    QSet<QString> m_refused; // nodes not to retry until they go away
    QTimer m_scan;
};
