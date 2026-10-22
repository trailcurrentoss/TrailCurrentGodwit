#include "knobs.h"

#include "dialprotocol.h"

#include <QHash>

#include <algorithm>
#include <cstdlib>

Knobs::Knobs(QObject *parent)
    : QObject(parent)
{
    for (int s = 0; s < 2; ++s) {
        State &st = m_state[s];
        st.timer.setSingleShot(true);
        st.timer.setInterval(HoldMs);
        connect(&st.timer, &QTimer::timeout, this, [this, s] {
            State &st = m_state[s];
            if (st.down && !st.moved) {
                st.long_ = true;
                emit holdCancelled(s); // ends the progress sweep; the hold itself fires next
                emit held(s);
            }
        });
    }

    connect(&m_dials, &Dials::down, this, &Knobs::down);
    connect(&m_dials, &Dials::up, this, &Knobs::up);
    // A fast spin can report several detents at once; each one is a turn
    connect(&m_dials, &Dials::turned, this, [this](int side, int detents) {
        if ((side != 0 && side != 1) || detents == 0)
            return;
        // The contacts chatter as they settle into a detent, and the dial can
        // report that as one detent backwards right after a forward one. No
        // hand reverses a detented knob that fast, so drop it.
        const int dir = detents > 0 ? 1 : -1;
        Spin &sp = m_spin[side];
        if (sp.dir != 0 && dir != sp.dir && sp.since.isValid() && sp.since.elapsed() < ReverseMs)
            return;
        sp.dir = dir;
        sp.since.restart();
        for (int i = 0; i < std::abs(detents); ++i)
            turn(side, detents);
    });
}

static bool validSide(int side)
{
    return side == 0 || side == 1;
}

void Knobs::down(int side)
{
    if (!validSide(side))
        return;
    State &st = m_state[side];
    st.down = true;
    st.moved = false;
    st.long_ = false;
    st.timer.start();
    emit holdStarted(side);
}

void Knobs::up(int side)
{
    if (!validSide(side))
        return;
    State &st = m_state[side];
    if (!st.down)
        return;
    st.down = false;
    st.timer.stop();
    if (!st.long_)
        emit holdCancelled(side);
    if (!st.moved && !st.long_)
        emit pressed(side);
}

void Knobs::turn(int side, int dir)
{
    if (!validSide(side) || dir == 0)
        return;
    State &st = m_state[side];
    if (st.down && !st.moved) {
        // Turning while pressed cancels both the press and the hold
        st.moved = true;
        st.timer.stop();
        emit holdCancelled(side);
    }
    emit turned(side, dir > 0 ? 1 : -1);
}

void Knobs::press(int side)
{
    if (validSide(side))
        emit pressed(side);
}

void Knobs::hold(int side)
{
    if (validSide(side))
        emit held(side);
}

void Knobs::setRing(int side, const QVariantList &leds)
{
    if (!validSide(side))
        return;
    std::array<uint8_t, 15> rgb{};
    for (int k = 0; k < 5 && k < leds.size(); ++k) {
        const QVariantMap led = leds[k].toMap();
        rgb[k * 3 + 0] = uint8_t(std::clamp(led.value(QStringLiteral("r")).toInt(), 0, 255));
        rgb[k * 3 + 1] = uint8_t(std::clamp(led.value(QStringLiteral("g")).toInt(), 0, 255));
        rgb[k * 3 + 2] = uint8_t(std::clamp(led.value(QStringLiteral("b")).toInt(), 0, 255));
    }
    m_dials.setRing(side, rgb);
}

void Knobs::setFace(int side, const QVariantMap &f)
{
    if (!validSide(side))
        return;
    static const QHash<QString, uint8_t> colors = {
        { QStringLiteral("acc"), DialProto::Acc },
        { QStringLiteral("heat"), DialProto::Heat },
        { QStringLiteral("cool"), DialProto::Cool },
        { QStringLiteral("warn"), DialProto::Warn },
    };
    DialFace face;
    // KnobFace upper-cases the label as it draws it; the dial is sent it ready
    face.label = f.value(QStringLiteral("label")).toString().toUpper();
    face.value = f.value(QStringLiteral("value")).toString();
    face.unit = f.value(QStringLiteral("unit")).toString();
    face.sub = f.value(QStringLiteral("sub")).toString();
    face.icon = f.value(QStringLiteral("icon")).toString();
    face.color = colors.value(f.value(QStringLiteral("color")).toString(), DialProto::Tx);
    face.small = f.value(QStringLiteral("small")).toBool();
    face.dark = f.value(QStringLiteral("dark")).toBool();
    face.valuePx = f.value(QStringLiteral("vSize")).toInt();
    face.iconPx = f.value(QStringLiteral("iconSize")).toInt();
    face.pagerCount = f.value(QStringLiteral("pagerCount")).toInt();
    face.pagerOn = f.value(QStringLiteral("pagerOn"), -1).toInt();
    face.backlight = f.value(QStringLiteral("bright"), 100).toInt();
    m_dials.setFace(side, face);
}
