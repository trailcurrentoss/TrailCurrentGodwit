import QtQuick
import Godwit

// A track as a flat vector drawing, north up, and nothing else: no street map.
// Every raceway sits on its band; the main loop takes each sector's colour as
// the lap goes (green ahead of the best lap, yellow level, red behind); the
// start/finish line is chequered across the band; the car is a dot with a
// soft glow and a short fading trail to show which way it is going; without
// a fix it is a hollow grey ring where the car was last seen.
Item {
    id: root
    // Tracks.current.shape: metres, origin top left
    property var shape: ({})
    // [x, y] in the shape's metres, or empty
    property var car: []
    // false when the GNSS has lost its fix: the dot stays at the last
    // position, hollow and grey, with no glow
    property bool fix: true
    // Timing.sectorColors: "green", "yellow", "red" or "" for each sector
    property var sectorColors: []

    readonly property bool hasShape: !!(shape && shape.w)
    readonly property real pad: 18
    readonly property real k: hasShape ? Math.min((width - 2 * pad) / Math.max(shape.w, 1), (height - 2 * pad) / Math.max(shape.h, 1)) : 1
    readonly property real ox: hasShape ? (width - shape.w * k) / 2 : 0
    readonly property real oy: hasShape ? (height - shape.h * k) / 2 : 0
    readonly property real band: Math.max(8, Math.min(width, height) * 0.04)
    readonly property bool carOn: !!(car && car.length === 2)

    function colourOf(c) { return c === "green" ? Theme.acc : c === "yellow" ? Theme.warn : c === "red" ? Theme.heat : "transparent"; }
    function sx(x) { return ox + x * k; }
    function sy(y) { return oy + y * k; }

    // The car's last few positions, newest last
    property var trail: []
    onCarChanged: {
        if (carOn) {
            var last = trail.length ? trail[trail.length - 1] : null;
            if (last && last[0] === car[0] && last[1] === car[1])
                return;
            var t = trail.slice(-11);
            t.push([car[0], car[1]]);
            trail = t;
        } else {
            trail = [];
        }
        trailCanvas.requestPaint();
    }
    onShapeChanged: { trail = []; base.requestPaint(); trailCanvas.requestPaint(); }
    onSectorColorsChanged: base.requestPaint()
    onWidthChanged: base.requestPaint()
    onHeightChanged: base.requestPaint()
    Connections {
        target: Theme
        function onDarkChanged() { base.requestPaint(); trailCanvas.requestPaint(); }
    }

    Canvas {
        id: base
        anchors.fill: parent
        onPaint: {
            var g = getContext("2d");
            g.reset();
            if (!root.hasShape)
                return;
            g.lineCap = "round";
            g.lineJoin = "round";
            function line(l) {
                g.beginPath();
                for (var i = 0; i + 1 < l.length; i += 2) {
                    var x = root.sx(l[i]), y = root.sy(l[i + 1]);
                    if (i) g.lineTo(x, y); else g.moveTo(x, y);
                }
                g.stroke();
            }
            var S = root.shape, b = root.band;
            // Pit lanes: dashed, under everything
            g.strokeStyle = Theme.line;
            g.lineWidth = Math.max(2, b * 0.22);
            g.setLineDash([3, 3]);
            (S.pit || []).forEach(line);
            g.setLineDash([]);
            // The band, then each sector's colour over the main loop
            // On white the card's own track grey is too faint for the band
            g.strokeStyle = Theme.dark ? Theme.track : "#dedede";
            g.lineWidth = b;
            (S.lines || []).forEach(line);
            var LS = S.loopSectors || [];
            for (var s = 0; s < LS.length; ++s) {
                var c = root.sectorColors[s];
                if (!c) continue;
                g.strokeStyle = root.colourOf(c);
                g.globalAlpha = 0.8;
                g.lineWidth = b;
                line(LS[s]);
                g.globalAlpha = 1;
            }
            // The centre line
            g.strokeStyle = Theme.dark ? "#6a6a6a" : "#b4b4b4";
            g.lineWidth = Math.max(1.5, b * 0.18);
            (S.lines || []).forEach(line);
            // Sector lines, as small dots on the track
            g.fillStyle = Theme.tx;
            (S.sectors || []).forEach(function (p) {
                g.beginPath();
                g.arc(root.sx(p[0]), root.sy(p[1]), Math.max(4, b * 0.45), 0, Math.PI * 2);
                g.fill();
            });
            // Start/finish: a chequered strip across the band
            var gt = S.gate;
            if (gt && gt.length === 4) {
                var x1 = root.sx(gt[0]), y1 = root.sy(gt[1]), x2 = root.sx(gt[2]), y2 = root.sy(gt[3]);
                var cx = (x1 + x2) / 2, cy = (y1 + y2) / 2;
                var ang = Math.atan2(y2 - y1, x2 - x1);
                var len = b * 1.9, cols = 6, sq = len / cols;
                g.save();
                g.translate(cx, cy);
                g.rotate(ang);
                g.fillStyle = Theme.card;
                g.fillRect(-len / 2 - 1, -sq - 1, len + 2, sq * 2 + 2);
                g.fillStyle = Theme.tx;
                for (var col = 0; col < cols; ++col)
                    for (var row = 0; row < 2; ++row)
                        if ((col + row) % 2 === 0)
                            g.fillRect(-len / 2 + col * sq, -sq + row * sq, sq, sq);
                g.restore();
            }
        }
    }

    // Where the car has just been, fading away behind it
    Canvas {
        id: trailCanvas
        anchors.fill: parent
        onPaint: {
            var g = getContext("2d");
            g.reset();
            var T = root.trail;
            if (T.length < 2 || !root.hasShape)
                return;
            g.lineCap = "round";
            g.strokeStyle = Theme.acc;
            for (var i = 1; i < T.length; ++i) {
                g.globalAlpha = 0.08 + 0.6 * i / T.length;
                g.lineWidth = root.band * (0.25 + 0.45 * i / T.length);
                g.beginPath();
                g.moveTo(root.sx(T[i - 1][0]), root.sy(T[i - 1][1]));
                g.lineTo(root.sx(T[i][0]), root.sy(T[i][1]));
                g.stroke();
            }
        }
    }

    // The car
    Item {
        visible: root.carOn && root.hasShape
        x: root.carOn ? root.sx(root.car[0]) : 0
        y: root.carOn ? root.sy(root.car[1]) : 0
        Behavior on x { NumberAnimation { duration: 180 } }
        Behavior on y { NumberAnimation { duration: 180 } }
        Rectangle {
            id: glow
            property real v: 0.6
            width: root.band * 3.2; height: width; radius: width / 2
            x: -width / 2; y: -height / 2
            visible: root.fix
            color: Theme.acc
            opacity: 0.22 * v
            SequentialAnimation on v {
                loops: Animation.Infinite
                running: root.visible && root.fix
                NumberAnimation { from: 0.6; to: 1; duration: 900; easing.type: Easing.InOutSine }
                NumberAnimation { from: 1; to: 0.6; duration: 900; easing.type: Easing.InOutSine }
            }
        }
        Rectangle {
            width: root.band * 1.5; height: width; radius: width / 2
            x: -width / 2; y: -height / 2
            color: root.fix ? Theme.acc : Theme.card
            border.width: root.fix ? 2 : 3
            border.color: root.fix ? Theme.card : Theme.tx2
            Behavior on color { ColorAnimation { duration: 200 } }
        }
    }
}
