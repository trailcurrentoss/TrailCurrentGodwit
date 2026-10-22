#pragma once

#include "dials.h"

#include <QObject>
#include <QQmlEngine>
#include <QElapsedTimer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <array>

// The two rotary encoders: input events in, ring LED frames out.
//
// Raw events (down, up, turn) come from whichever source drives the knobs:
// the keyboard, the simulator's on-screen knobs, or the USB HID encoder modules
// (Dials). This class turns them into press / hold / turn, so every source gets
// the same timing: a hold fires 550 ms after down unless the knob turns first.
class Knobs : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit Knobs(QObject *parent = nullptr);

    static constexpr int HoldMs = 550;

    Q_INVOKABLE void down(int side);
    Q_INVOKABLE void up(int side);
    Q_INVOKABLE void turn(int side, int dir);
    // Direct press / hold, for sources that already know (keyboard shortcuts)
    Q_INVOKABLE void press(int side);
    Q_INVOKABLE void hold(int side);

    // One frame of a ring: 5 premultiplied {r, g, b} objects, LED 0 lower-left
    // round to LED 4 lower-right. Forwarded to the ring hardware when present.
    Q_INVOKABLE void setRing(int side, const QVariantList &leds);
    // What a knob's round display shows: label, icon, value, unit, sub, color
    // (a Theme.named() name), small, vSize, iconSize, pagerCount, pagerOn,
    // dark, bright. Forwarded to the encoder module when present.
    Q_INVOKABLE void setFace(int side, const QVariantMap &face);

signals:
    void turned(int side, int dir);
    void pressed(int side);
    void held(int side);
    void holdStarted(int side);
    void holdCancelled(int side);

private:
    struct State {
        bool down = false;
        bool moved = false;
        bool long_ = false;
        QTimer timer;
    };
    std::array<State, 2> m_state;
    // Filtering the dials' direction misreads (see dialTurned)
    static constexpr int StreamMs = 120;
    static constexpr int ConfirmMs = 80;
    struct Spin {
        int dir = 0;              // accepted direction
        int pending = 0;          // a reversal waiting to be confirmed
        QElapsedTimer last;       // since the previous detent report
        QTimer confirm;
    };
    void dialTurned(int side, int detents);
    void emitDetents(int side, int detents);
    std::array<Spin, 2> m_spin;
    Dials m_dials;
};
