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
    connect(&m_dials, &Dials::turned, this, &Knobs::dialTurned);
    for (int side = 0; side < 2; ++side) {
        Spin &sp = m_spin[side];
        sp.confirm.setSingleShot(true);
        sp.confirm.setInterval(ConfirmMs);
        // Nothing contradicted it: a deliberate click the other way
        connect(&sp.confirm, &QTimer::timeout, this, [this, side] {
            Spin &s = m_spin[side];
            s.dir = s.pending > 0 ? 1 : -1;
            emitDetents(side, std::exchange(s.pending, 0));
        });
    }
}

static bool validSide(int side)
{
    return side == 0 || side == 1;
}

void Knobs::emitDetents(int side, int detents)
{
    for (int i = 0; i < std::abs(detents); ++i)
        turn(side, detents);
}

// The dials misread direction now and then, more so turned fast: a detent
// against the spin in the middle of it, or a wrong first detent as a new flick
// starts. Recorded on hardware, 47 of 286 reports were such. So:
//  - a reversal within StreamMs of the previous detent is dropped;
//  - after a pause, a reversal waits for the next detent to confirm it, or
//    ConfirmMs with nothing against it (a deliberate single click back);
//  - detents in the current direction pass straight through.
void Knobs::dialTurned(int side, int detents)
{
    if (!validSide(side) || detents == 0)
        return;
    Spin &sp = m_spin[side];
    const int dir = detents > 0 ? 1 : -1;
    const qint64 gap = sp.last.isValid() ? sp.last.restart() : (sp.last.start(), StreamMs);

    if (sp.pending != 0) {
        sp.confirm.stop();
        const int pdir = sp.pending > 0 ? 1 : -1;
        if (dir == pdir) {
            sp.dir = dir;
            emitDetents(side, std::exchange(sp.pending, 0) + detents);
        } else {
            sp.pending = 0; // it was the misread
            if (dir == sp.dir)
                emitDetents(side, detents);
        }
        return;
    }
    if (sp.dir == 0 || dir == sp.dir) {
        sp.dir = dir;
        emitDetents(side, detents);
    } else if (gap >= StreamMs) {
        sp.pending = detents;
        sp.confirm.start();
    }
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
