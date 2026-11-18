import QtQuick
import Godwit

// Speed against time through a run, drawn over the best run at this place:
// where the launch bogged, where a shift cost time, where it pulled ahead.
// Traces are [[seconds, mph], ...] from the launch at [0, 0].
Item {
    id: root
    property var trace: []
    property var best: []
    // Speeds to rule across the chart, labelled at the left
    property var guides: [30, 60]
    readonly property bool hasData: (trace && trace.length > 1) || (best && best.length > 1)

    readonly property real padL: 26
    readonly property real padB: 16
    readonly property real tMax: Math.max(4, lastSecond(trace), lastSecond(best))
    readonly property real vMax: Math.max(guides[guides.length - 1] * 1.25, topSpeed(trace), topSpeed(best))

    function lastSecond(t) { return t && t.length ? t[t.length - 1][0] : 0; }
    function topSpeed(t) { var m = 0; for (var i = 0; t && i < t.length; ++i) m = Math.max(m, t[i][1]); return m * 1.05; }
    function px(s) { return padL + (width - padL - 4) * s / tMax; }
    function py(v) { return (height - padB) * (1 - v / vMax); }

    onTraceChanged: chart.requestPaint()
    onBestChanged: chart.requestPaint()
    onWidthChanged: chart.requestPaint()
    onHeightChanged: chart.requestPaint()
    Connections {
        target: Theme
        function onDarkChanged() { chart.requestPaint(); }
    }

    Canvas {
        id: chart
        anchors.fill: parent
        onPaint: {
            var g = getContext("2d");
            g.reset();
            g.font = "600 11px sans-serif";
            g.textBaseline = "middle";
            // Speed guides
            g.strokeStyle = Theme.line;
            g.lineWidth = 1;
            g.fillStyle = Theme.tx2;
            root.guides.forEach(function (v) {
                var y = Math.round(root.py(v)) + 0.5;
                g.setLineDash([3, 4]);
                g.beginPath(); g.moveTo(root.padL, y); g.lineTo(root.width, y); g.stroke();
                g.setLineDash([]);
                g.fillText(String(v), 0, y);
            });
            // Seconds along the bottom
            var base = Math.round(root.py(0)) + 0.5;
            g.beginPath(); g.moveTo(root.padL, base); g.lineTo(root.width, base); g.stroke();
            g.textBaseline = "top";
            var step = root.tMax > 12 ? 2 : 1;
            for (var s = 0; s <= root.tMax + 0.001; s += step) {
                var x = root.px(s);
                g.textAlign = s === 0 ? "left" : "center";
                if (x < root.width - 14)
                    g.fillText(s + (s === 0 ? " s" : ""), s === 0 ? x : x, base + 4);
            }
            g.textAlign = "left";
            function path(t) {
                g.beginPath();
                for (var i = 0; i < t.length; ++i) {
                    var px = root.px(t[i][0]), py = root.py(t[i][1]);
                    if (i) g.lineTo(px, py); else g.moveTo(px, py);
                }
            }
            g.lineJoin = "round";
            g.lineCap = "round";
            // The best run, as a dashed ghost
            var B = root.best || [];
            if (B.length > 1) {
                g.strokeStyle = Theme.tx2;
                g.lineWidth = 2;
                g.setLineDash([5, 4]);
                path(B);
                g.stroke();
                g.setLineDash([]);
            }
            // This run: a filled line with its end marked
            var T = root.trace || [];
            if (T.length > 1) {
                path(T);
                g.lineTo(root.px(T[T.length - 1][0]), root.py(0));
                g.lineTo(root.px(0), root.py(0));
                g.closePath();
                g.fillStyle = Theme.acc;
                g.globalAlpha = 0.14;
                g.fill();
                g.globalAlpha = 1;
                g.strokeStyle = Theme.acc;
                g.lineWidth = 3;
                path(T);
                g.stroke();
                var e = T[T.length - 1];
                g.fillStyle = Theme.acc;
                g.beginPath();
                g.arc(root.px(e[0]), root.py(e[1]), 4.5, 0, Math.PI * 2);
                g.fill();
            }
        }
    }

    // Which line is which, in the corner a launch never reaches
    Row {
        x: root.padL + 8
        y: -2
        spacing: 14
        visible: root.hasData
        Row {
            spacing: 6
            visible: root.trace && root.trace.length > 1
            Rectangle { width: 16; height: 3; radius: 1.5; color: Theme.acc; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "This run"; color: Theme.tx2; font.pixelSize: 11; font.weight: Font.DemiBold }
        }
        Row {
            spacing: 6
            visible: root.best && root.best.length > 1
            Row {
                spacing: 3
                anchors.verticalCenter: parent.verticalCenter
                Repeater { model: 3; Rectangle { width: 4; height: 2; color: Theme.tx2 } }
            }
            Text { text: "Best"; color: Theme.tx2; font.pixelSize: 11; font.weight: Font.DemiBold }
        }
    }
    Text {
        visible: !root.hasData
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -root.padB / 2
        text: "Your speed through the run is drawn here"
        color: Theme.tx2; font.pixelSize: 13
    }
}
