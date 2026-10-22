import QtQuick
import Godwit
import "../Ring.js" as Ring

// Desktop stand-in for one encoder: LED ring, knurled knob and round display.
// Drag round it to turn (one detent per 20 degrees), click to press, hold
// 550 ms to hold, or use the mouse wheel. Input goes through Knobs, the same
// path the hardware takes.
Item {
    id: knob
    property int side: 0
    readonly property var enc: Hmi.encs[side]

    width: 240
    height: 240

    // Well and light-pipe track
    Rectangle { anchors.fill: parent; radius: 120; color: "#060606" }
    Rectangle {
        x: 2; y: 2; width: 236; height: 236; radius: 118
        color: "transparent"
        border.width: 15
        border.color: "#1a1a1a"
    }

    // LEDs: each lights a ±9° wedge of the light pipe that fades out to ±36°,
    // plus a soft glow, like the mockup's conic gradient
    Canvas {
        id: ring
        x: -28; y: -28
        width: 296; height: 296
        renderStrategy: Canvas.Cooperative
        property var px: Hmi.ring[knob.side] || []
        onPxChanged: requestPaint()
        onPaint: {
            var ctx = getContext("2d");
            ctx.reset();
            var cx = width / 2, cy = height / 2;
            for (var k = 0; k < px.length; k++) {
                var p = px[k];
                var peak = Math.max(p.r, p.g, p.b);
                if (peak < 4) continue;
                var a = peak / 255;
                var col = "rgba(" + Math.round(p.r / a) + "," + Math.round(p.g / a) + "," + Math.round(p.b / a) + ",";
                var mid = (Ring.POS[k] - 90) * Math.PI / 180;
                // glow
                ctx.lineWidth = 34;
                ctx.strokeStyle = col + (a * 0.28).toFixed(3) + ")";
                ctx.beginPath(); ctx.arc(cx, cy, 108, mid - 0.35, mid + 0.35); ctx.stroke();
                // pipe
                ctx.lineWidth = 15;
                for (var d = 9; d <= 36; d += 3) {
                    var f = d <= 9 ? 1 : 1 - (d - 9) / 27;
                    ctx.strokeStyle = col + (a * f).toFixed(3) + ")";
                    var w = 3 * Math.PI / 180, o = d * Math.PI / 180;
                    ctx.beginPath(); ctx.arc(cx, cy, 109, mid - o, mid - o + w); ctx.stroke();
                    ctx.beginPath(); ctx.arc(cx, cy, 109, mid + o - w, mid + o); ctx.stroke();
                }
                ctx.strokeStyle = col + a.toFixed(3) + ")";
                ctx.beginPath(); ctx.arc(cx, cy, 109, mid - 9 * Math.PI / 180, mid + 9 * Math.PI / 180); ctx.stroke();
            }
        }
    }

    // LED positions on the bezel
    Repeater {
        model: Ring.POS
        delegate: Rectangle {
            required property real modelData
            readonly property real r: modelData * Math.PI / 180
            x: Math.round(120 + 109 * Math.sin(r) - 4)
            y: Math.round(120 - 109 * Math.cos(r) - 4)
            width: 8; height: 8; radius: 2
            color: "#2c2c2c"
            opacity: 0.6
        }
    }

    // Knurled knob, rotating 15° per detent
    Item {
        x: 22; y: 22
        width: 196; height: 196
        rotation: Hmi.rot[knob.side]
        Behavior on rotation { NumberAnimation { duration: 120; easing.type: Easing.InOutQuad } }
        Canvas {
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d"), c = width / 2;
                for (var i = 0; i < 144; i++) {
                    ctx.fillStyle = i % 2 ? "#151515" : "#2f2f2f";
                    ctx.beginPath(); ctx.moveTo(c, c);
                    ctx.arc(c, c, c, i * 2.5 * Math.PI / 180, (i + 1) * 2.5 * Math.PI / 180);
                    ctx.closePath(); ctx.fill();
                }
            }
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: 12
            radius: width / 2
            gradient: Gradient { GradientStop { position: 0; color: "#3a3a3a" } GradientStop { position: 1; color: "#141414" } }
        }
        Rectangle { x: 96; y: 3; width: 4; height: 10; radius: 2; color: "#d0d0d0" }
    }

    // Round display: native 240x240 shown at 150 px
    Rectangle {
        x: 45; y: 45
        width: 150; height: 150; radius: 75
        color: "#000000"
        border.width: Hmi.flash[knob.side] ? 4 : 2
        border.color: Hmi.flash[knob.side] ? "#7BC96A" : "#050505"
        KnobFace {
            enc: knob.enc
            scale: 0.625
            transformOrigin: Item.TopLeft
        }
        Rectangle {
            anchors.fill: parent
            radius: 75
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.16) }
                GradientStop { position: 0.42; color: Qt.rgba(1, 1, 1, 0) }
            }
        }
    }

    // Input
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        property real lastAngle: 0
        property real acc: 0
        function angle(m) { return Math.atan2(m.y - height / 2, m.x - width / 2) * 180 / Math.PI; }
        onEntered: Hmi.hover[knob.side] = true
        onExited: Hmi.hover[knob.side] = false
        onPressed: (m) => { lastAngle = angle(m); acc = 0; Knobs.down(knob.side); }
        onPositionChanged: (m) => {
            if (!pressed) return;
            var a = angle(m), delta = a - lastAngle;
            if (delta > 180) delta -= 360;
            if (delta < -180) delta += 360;
            lastAngle = a;
            acc += delta;
            while (Math.abs(acc) >= 20) {
                var dir = acc > 0 ? 1 : -1;
                acc -= dir * 20;
                Knobs.turn(knob.side, dir);
            }
        }
        onReleased: Knobs.up(knob.side)
        onCanceled: Knobs.up(knob.side)
        onWheel: (w) => {
            acc += w.angleDelta.y;
            if (Math.abs(acc) >= 120) { Knobs.turn(knob.side, acc > 0 ? -1 : 1); acc = 0; }
        }
    }

    Text {
        y: 252
        anchors.horizontalCenter: parent.horizontalCenter
        width: 360
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: knob.enc.note
        color: "#9a9a9a"
        font.pixelSize: 13
        lineHeight: 18
        lineHeightMode: Text.FixedHeight
    }
}
