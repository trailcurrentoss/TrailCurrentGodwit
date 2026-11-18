import QtQuick
import Godwit

// Track: 0-60, quarter mile and lap timing at a track from the Tracks list.
// Only the track is drawn, as a flat vector outline: no street map. The right
// knob turns between modes, presses to arm, stage or start a session, and
// holds for the Tracks list.
AppPage {
    id: page
    readonly property var cur: Tracks.current || {}
    readonly property bool hasTrack: !!cur.id
    readonly property string mode: Timing.mode
    readonly property string st: Timing.state
    readonly property bool live: st === "running"
    readonly property real sideW: 262
    readonly property real mainW: width - sideW - 14
    readonly property int topH: 52

    function miText(m) { return (m / 1609.344).toFixed(m < 1609 ? 2 : 1) + " mi"; }
    function typeText(t) { return { road: "Road course", oval: "Oval", drag: "Drag strip" }[t] || ""; }
    function placeText(t) { return [t.place, t.state].filter(Boolean).join(", "); }
    function trackLine(t) {
        var len = t.type === "drag" ? (t.length_m < 300 ? "1/8 mi strip" : "1/4 mi strip") : miText(t.length_m);
        return [placeText(t), len].filter(Boolean).join(" · ");
    }
    function statusText() {
        if (mode === "laps") {
            if (!hasTrack || cur.type === "drag") return "Choose a road course or oval";
            if (st === "running") {
                var n = Timing.laps.filter(function (l) { return l.kind === "lap"; }).length;
                return Timing.laps.length === 0 ? "Out lap · cross start/finish to start timing" : "Lap " + (n + 1) + " · press to stop";
            }
            return "Ready · press to start session";
        }
        if (st === "armed") return "Armed · launch when ready";
        if (st === "staged") return Timing.tree >= 4 ? "Green · go" : "Staged";
        if (st === "running") return Timing.tree === 5 ? "Red light · still timing" : "Running";
        if (st === "done") return "Done · press to arm again";
        return mode === "quarter" ? "Ready · press to stage" : "Ready · press to arm";
    }
    function statusColor() {
        if (Timing.tree === 5) return Theme.heat;
        if (st === "running" || Timing.tree >= 4) return Theme.acc;
        if (st === "armed" || st === "staged") return Theme.warn;
        return Theme.line;
    }

    // Small label above a value, in a grey box: the split and sector boxes
    component Stat: Rectangle {
        property string label
        property string value
        height: 52
        radius: 10
        color: Theme.track
        Column {
            x: 10; y: 8
            width: parent.width - 20
            spacing: 2
            Text { text: parent.parent.label; color: Theme.tx2; font.pixelSize: 11; font.weight: Font.DemiBold; font.capitalization: Font.AllUppercase }
            Num { width: parent.width; text: parent.parent.value; font.pixelSize: 17; font.weight: Font.DemiBold }
        }
    }

    // ----------------------------------------------------------------- top
    Row {
        id: tabs
        spacing: 8
        height: page.topH
        Repeater {
            model: [{ m: "zero60", t: "0–60", i: "speedometer" }, { m: "quarter", t: "1/4 Mile", i: "flag" },
                    { m: "laps", t: "Lap Timing", i: "time-outline" }]
            delegate: Rectangle {
                id: tab
                required property var modelData
                readonly property bool on: page.mode === modelData.m
                height: 44
                width: tabRow.implicitWidth + 32
                radius: 22
                color: on ? Theme.acc : Theme.card
                Row {
                    id: tabRow
                    anchors.centerIn: parent
                    spacing: 8
                    Icon { name: tab.modelData.i; size: 18; color: tab.on ? Theme.onAcc : Theme.tx; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: tab.modelData.t; color: tab.on ? Theme.onAcc : Theme.tx; font.pixelSize: 16; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                }
                TapHandler {
                    onTapped: {
                        if (page.st !== "ready" && page.st !== "done") { Hmi.toast("Stop first to change mode"); return; }
                        Hmi.trackMode(tab.modelData.m);
                    }
                }
            }
        }
    }

    // The track being timed: tap for the Tracks list
    Card {
        id: venue
        x: page.width - width
        width: page.sideW
        height: 48
        radius: 24
        Row {
            x: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 10
            Icon { name: "flag"; size: 18; color: page.hasTrack ? Theme.acc : Theme.tx2; anchors.verticalCenter: parent.verticalCenter }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: venue.width - 16 - 18 - 10 - 40
                Text { width: parent.width; text: page.hasTrack ? page.cur.name : "Choose a track"; color: Theme.tx; font.pixelSize: 14; font.weight: Font.Bold; elide: Text.ElideRight }
                Text {
                    visible: page.hasTrack
                    width: parent.width
                    text: page.trackLine(page.cur)
                    color: Theme.tx2; font.pixelSize: 11; elide: Text.ElideRight
                }
            }
        }
        Icon { anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter; name: "chevron-forward"; rotation: 90; size: 16; color: Theme.tx2 }
        TapHandler { onTapped: Hmi.trackPicker(true) }
    }

    // ---------------------------------------------------------------- main
    Card {
        id: main
        y: page.topH + 10
        width: page.mainW
        height: page.height - y

        // Status and the closed-course reminder
        Row {
            x: 18; y: 16
            spacing: 8
            Rectangle { width: 10; height: 10; radius: 5; color: page.statusColor(); anchors.verticalCenter: parent.verticalCenter }
            Text { text: page.statusText(); color: Theme.tx; font.pixelSize: 15; font.weight: Font.DemiBold }
        }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 18; y: 17
            spacing: 6
            Icon { name: "warning"; size: 14; color: Theme.tx2; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Closed course use only"; color: Theme.tx2; font.pixelSize: 12; font.weight: Font.DemiBold }
        }

        // 0-60 --------------------------------------------------------
        Item {
            visible: page.mode === "zero60"
            x: 18; y: 50
            width: parent.width - 36
            height: parent.height - 66
            readonly property var lv: Timing.live
            Row {
                y: 10
                Num { text: page.live ? Timing.elapsed.toFixed(2) : Hmi.secText(Timing.runs.length && page.st === "done" ? Timing.runs[0].ms : 0, 2).replace("--", "0.00"); font.pixelSize: 104; font.weight: Font.Light; font.letterSpacing: -3 }
                Text { text: "s"; color: Theme.tx2; font.pixelSize: 26; font.weight: Font.DemiBold; anchors.baseline: parent.children[0].baseline }
            }
            Row {
                anchors.right: parent.right; y: 44
                spacing: 6
                Num { text: Math.round(Timing.speed); font.pixelSize: 58; font.weight: Font.Light }
                Text { text: "mph"; color: Theme.tx2; font.pixelSize: 18; font.weight: Font.DemiBold; anchors.baseline: parent.children[0].baseline }
            }
            // 0 to 60 mph
            Item {
                y: 136
                width: parent.width; height: 34
                Rectangle { width: parent.width; height: 12; radius: 6; color: Theme.track }
                Rectangle { width: parent.width * Math.min(Timing.speed, 60) / 60; height: 12; radius: 6; color: Theme.acc; visible: Timing.speed > 0.5 }
                Rectangle { x: parent.width / 2 - 1; y: -6; width: 2; height: 24; color: Theme.tx }
                Text { y: 20; text: "0"; color: Theme.tx2; font.pixelSize: 11; font.weight: Font.DemiBold }
                Text { y: 20; x: parent.width / 2 - width / 2; text: "30"; color: Theme.tx2; font.pixelSize: 11; font.weight: Font.DemiBold }
                Text { y: 20; anchors.right: parent.right; text: "60 mph"; color: Theme.tx2; font.pixelSize: 11; font.weight: Font.DemiBold }
            }
            // Speed through the run, against the best one here
            SpeedTrace {
                y: 182
                width: parent.width
                height: parent.height - 60 - y
                trace: parent.lv.trace || []
                // Not when this run is the best: the two lines would be one
                best: page.st === "done" && Timing.runs.length && Timing.runs[0].ms === Timing.bestRun ? [] : Timing.bestTrace
            }
            Row {
                anchors.bottom: parent.bottom
                width: parent.width
                spacing: 8
                Stat { width: (parent.width - 16) / 3; label: "0–30"; value: Hmi.secText(parent.parent.lv.s30 || 0, 2) + (parent.parent.lv.s30 ? " s" : "") }
                Stat { width: (parent.width - 16) / 3; label: "0–60"; value: Hmi.secText(parent.parent.lv.ms || 0, 2) + (parent.parent.lv.ms ? " s" : "") }
                Stat { width: (parent.width - 16) / 3; label: "Peak g"; value: Timing.imu ? Timing.gPeak.toFixed(2) + " g" : "--" }
            }
        }

        // Quarter mile ------------------------------------------------
        Item {
            id: quarter
            visible: page.mode === "quarter"
            x: 18; y: 50
            width: parent.width - 36
            height: parent.height - 66
            readonly property var lv: Timing.live
            readonly property var sp: lv.splits || {}
            // The tree: a Blender render (hmi/tools/art/drag_tree_blender.py),
            // dark, with each lamp lit by showing its own patch of the lit
            // render and a soft glow. Staged lights pre-stage and stage; the
            // ambers count down one at a time; green, or red for a start
            // before the green.
            Item {
                id: tree
                height: 256
                width: Math.round(height * 91 / 512)
                readonly property string theme: Theme.dark ? "dark" : "light"
                // Each lens as fractions of the image: x0, y0, x1, y1
                readonly property var lamps: ({
                    prestage_l: [0.1698, 0.0451, 0.5, 0.1036], prestage_r: [0.5616, 0.0425, 0.8918, 0.1011],
                    stage_l: [0.1698, 0.1304, 0.5, 0.1889], stage_r: [0.5616, 0.1278, 0.8918, 0.1864],
                    amber1: [0.2034, 0.2159, 0.8582, 0.332], amber2: [0.2034, 0.3608, 0.8582, 0.4768],
                    amber3: [0.2034, 0.5056, 0.8582, 0.6217], green: [0.2034, 0.6505, 0.8582, 0.7665],
                    red: [0.2034, 0.7953, 0.8582, 0.9114]
                })
                function lit(name) {
                    var t = Timing.tree, staged = page.mode === "quarter" && page.st === "staged";
                    if (name.indexOf("stage") >= 0) return staged;
                    if (name.indexOf("amber") === 0) return staged && t === Number(name.charAt(5));
                    if (name === "green") return t === 4 && (staged || page.st === "running" || page.st === "done");
                    return t === 5 || (page.st === "done" && Timing.runs.length > 0 && !!Timing.runs[0].red);
                }
                Image {
                    anchors.fill: parent
                    source: "../../assets/track/tree_off_" + tree.theme + ".png"
                    smooth: true; mipmap: true
                }
                Repeater {
                    model: [["prestage_l", "stage"], ["prestage_r", "stage"], ["stage_l", "stage"], ["stage_r", "stage"],
                            ["amber1", "amber"], ["amber2", "amber"], ["amber3", "amber"], ["green", "green"], ["red", "red"]]
                    delegate: Item {
                        id: lamp
                        required property var modelData
                        readonly property var b: tree.lamps[modelData[0]]
                        readonly property bool on: tree.lit(modelData[0])
                        x: b[0] * tree.width; y: b[1] * tree.height
                        width: (b[2] - b[0]) * tree.width; height: (b[3] - b[1]) * tree.height
                        // LEDs: on at once, off with a short tail
                        Image {
                            anchors.centerIn: parent
                            width: parent.width * 2.4; height: width
                            source: "../../assets/track/glow_" + lamp.modelData[1] + ".png"
                            opacity: lamp.on ? (Theme.dark ? 0.9 : 0.6) : 0
                            Behavior on opacity { NumberAnimation { duration: lamp.on ? 40 : 160 } }
                        }
                        Item {
                            anchors.fill: parent
                            clip: true
                            opacity: lamp.on ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: lamp.on ? 30 : 140 } }
                            Image {
                                x: -lamp.x; y: -lamp.y
                                width: tree.width; height: tree.height
                                source: "../../assets/track/tree_on_" + tree.theme + ".png"
                                smooth: true; mipmap: true
                            }
                        }
                    }
                }
            }
            Row {
                x: tree.width + 24; y: 4
                Num { text: page.live ? Timing.elapsed.toFixed(3) : Hmi.secText(page.st === "done" && Timing.runs.length ? Timing.runs[0].et : 0, 3).replace("--", "0.000"); font.pixelSize: 96; font.weight: Font.Light; font.letterSpacing: -3 }
                Text { text: "s ET"; color: Theme.tx2; font.pixelSize: 22; font.weight: Font.DemiBold; anchors.baseline: parent.children[0].baseline }
            }
            Row {
                x: tree.width + 24; y: 112
                spacing: 6
                Num { text: page.live ? Timing.speed.toFixed(1) : (page.st === "done" && page.mode === "quarter" && Timing.runs.length && Timing.runs[0].trap ? Timing.runs[0].trap.toFixed(1) : "0.0"); font.pixelSize: 34; font.weight: Font.Light }
                Text { text: "mph"; color: Theme.tx2; font.pixelSize: 15; font.weight: Font.DemiBold; anchors.baseline: parent.children[0].baseline }
            }
            // 60 ft to the quarter, by distance
            Item {
                x: tree.width + 24; y: 180
                width: parent.width - x - 8; height: 36
                readonly property var marks: [[60, "60'"], [330, "330'"], [660, "1/8"], [1000, "1000'"], [1320, "1/4"]]
                Rectangle { width: parent.width; height: 12; radius: 6; color: Theme.track }
                Repeater {
                    model: parent.marks
                    delegate: Item {
                        required property var modelData
                        x: parent.width * modelData[0] / 1320 - 1
                        Rectangle { y: -6; width: 2; height: 24; color: quarter.sp[String(modelData[0])] ? Theme.acc : Theme.tx }
                        Text { y: 20; x: -width / 2; text: modelData[1]; color: Theme.tx2; font.pixelSize: 11; font.weight: Font.DemiBold }
                    }
                }
                Rectangle {
                    // How far down the strip, from the splits reached
                    readonly property real frac: { var f = 0; [60, 330, 660, 1000, 1320].forEach(function (d) { if (quarter.sp[String(d)]) f = d / 1320; }); return f; }
                    x: Math.max(0, parent.width * frac - 10); y: -4; width: 20; height: 20; radius: 10; color: Theme.acc
                }
            }
            Row {
                anchors.bottom: parent.bottom
                width: parent.width
                spacing: 8
                readonly property real w: (width - 40) / 6
                Stat { width: parent.w; label: "RT"; value: quarter.lv.rt ? quarter.lv.rt.toFixed(3) : "--" }
                Repeater {
                    model: [[60, "60'"], [330, "330'"], [660, "1/8"], [1000, "1000'"], [1320, "1/4"]]
                    delegate: Stat {
                        required property var modelData
                        width: parent.w
                        label: modelData[1]
                        value: Hmi.secText(quarter.sp[String(modelData[0])] || 0, 3)
                    }
                }
            }
        }

        // Lap timing --------------------------------------------------
        Item {
            id: laps
            visible: page.mode === "laps"
            x: 18; y: 46
            width: parent.width - 36
            height: parent.height - 58
            readonly property var shape: page.cur.shape || ({})
            readonly property var lastLap: Timing.laps.filter(function (l) { return l.kind === "lap"; })[0]

            TrackDrawing {
                id: drawing
                width: parent.width - 236
                height: parent.height - 18
                shape: laps.shape
                car: Tracks.car
                fix: Tracks.fix
                sectorColors: Timing.sectorColors
            }
            Text {
                visible: page.hasTrack && page.cur.type !== "drag"
                anchors.bottom: parent.bottom
                text: page.cur.gate && page.cur.gate.placed === "pit_lane" ? "Start/finish estimated beside the pit lane"
                    : "Start/finish estimated on the longest straight"
                color: Theme.tx2; font.pixelSize: 11; font.weight: Font.DemiBold
            }
            Text {
                visible: !page.hasTrack || page.cur.type === "drag"
                anchors.centerIn: drawing
                width: drawing.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: !page.hasTrack ? "Hold the right knob or tap the track name to choose a road course or oval"
                                     : "Lap timing needs a road course or oval. 0–60 and 1/4 mile work here."
                color: Theme.tx2; font.pixelSize: 15
            }
            Column {
                anchors.right: parent.right
                width: 220
                anchors.verticalCenter: drawing.verticalCenter
                spacing: 8
                // Running: this lap's time, coloured once a sector is judged.
                // Just over the line: the lap that finished, for a few seconds.
                readonly property bool running: page.live && Timing.laps.length > 0 && !Timing.holding
                Text { text: parent.running ? "This lap" : "Last lap"; color: Theme.tx2; font.pixelSize: 12; font.weight: Font.DemiBold; font.capitalization: Font.AllUppercase; font.letterSpacing: 1 }
                Num {
                    width: parent.width
                    text: parent.running ? Hmi.lapText(Timing.elapsed * 1000)
                        : Timing.holding ? Hmi.lapText(Timing.laps[0].ms)
                        : Hmi.lapText(laps.lastLap ? laps.lastLap.ms : 0)
                    color: page.live && Timing.ahead > 0 ? Theme.acc : page.live && Timing.ahead < 0 ? Theme.heat : Theme.tx
                    Behavior on color { ColorAnimation { duration: 250 } }
                    font.pixelSize: 40; font.weight: Font.Light; font.letterSpacing: -1
                }
                Rectangle {
                    width: bestRow.implicitWidth + 24; height: 36; radius: 18
                    color: Theme.track
                    Text { id: bestRow; anchors.centerIn: parent; text: "Best " + Hmi.lapText(Timing.bestLap); color: Theme.tx; font.pixelSize: 17; font.weight: Font.Bold; font.features: { "tnum": 1 } }
                }
                // On course for a faster, level or slower lap, by colour only:
                // no sector times to read while driving
                Row {
                    spacing: 6
                    Repeater {
                        model: Math.max(Timing.sectorCount, 3)
                        delegate: Rectangle {
                            id: sec
                            required property int index
                            readonly property int n: Math.max(Timing.sectorCount, 3)
                            readonly property string state: Timing.sectorColors[index] || ""
                            width: (220 - 6 * (n - 1)) / n
                            height: 44
                            radius: 10
                            color: state === "green" ? Theme.acc : state === "yellow" ? Theme.warn : state === "red" ? Theme.heat : Theme.track
                            Behavior on color { ColorAnimation { duration: 350 } }
                            Text {
                                anchors.centerIn: parent
                                text: "S" + (sec.index + 1)
                                color: sec.state ? Theme.onAlert : Theme.tx2
                                font.pixelSize: 16; font.weight: Font.Bold
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- side
    Card {
        id: gcard
        x: page.width - page.sideW
        y: page.topH + 10
        width: page.sideW
        height: 160
        Text { x: 16; y: 14; text: "G-Force"; color: Theme.tx; font.pixelSize: 16; font.weight: Font.Bold }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 16; y: 16
            text: Timing.imu ? "IMU · Peak " + Timing.gPeak.toFixed(2) + " g" : "IMU not connected"
            color: Theme.tx2; font.pixelSize: 11; font.weight: Font.DemiBold
        }
        // Lateral left to right, acceleration and braking up and down; rings at 0.5 and 1 g
        Item {
            id: gplot
            x: 16; y: 42
            width: 104; height: 104
            Repeater {
                model: [1, 0.66, 0.33]
                delegate: Rectangle {
                    required property real modelData
                    width: gplot.width * modelData; height: width; radius: width / 2
                    anchors.centerIn: parent
                    color: "transparent"; border.width: 1; border.color: Theme.line
                }
            }
            Rectangle { anchors.centerIn: parent; width: parent.width; height: 1; color: Theme.line }
            Rectangle { anchors.centerIn: parent; width: 1; height: parent.height; color: Theme.line }
            // The last second or so of readings, fading behind the dot
            Canvas {
                id: gtrail
                anchors.fill: parent
                property var pts: []
                Connections {
                    target: Timing
                    function onTick() {
                        if (!Timing.imu) { if (gtrail.pts.length) { gtrail.pts = []; gtrail.requestPaint(); } return; }
                        var p = gtrail.pts.slice(-24);
                        p.push([Timing.gLat, Timing.gAcc]);
                        gtrail.pts = p;
                        gtrail.requestPaint();
                    }
                }
                onPaint: {
                    var g = getContext("2d"); g.reset();
                    var P = pts, cx = width / 2, cy = height / 2, r = width / 2;
                    g.fillStyle = Theme.acc;
                    for (var i = 0; i < P.length; ++i) {
                        g.globalAlpha = 0.05 + 0.35 * i / P.length;
                        var x = cx + Math.max(-1.5, Math.min(1.5, P[i][0])) / 1.5 * r, y = cy - Math.max(-1.5, Math.min(1.5, P[i][1])) / 1.5 * r;
                        g.beginPath(); g.arc(x, y, 2 + 3 * i / P.length, 0, Math.PI * 2); g.fill();
                    }
                }
            }
            Rectangle {
                width: 14; height: 14; radius: 7
                color: Timing.imu ? Theme.acc : Theme.line
                x: gplot.width / 2 - 7 + Math.max(-1.5, Math.min(1.5, Timing.gLat)) / 1.5 * gplot.width / 2
                y: gplot.height / 2 - 7 - Math.max(-1.5, Math.min(1.5, Timing.gAcc)) / 1.5 * gplot.height / 2
            }
        }
        Column {
            x: 136; y: 56
            spacing: 6
            Repeater {
                model: [["Lateral", Timing.gLat], ["Accel", Timing.gAcc]]
                delegate: Column {
                    required property var modelData
                    Text { text: modelData[0]; color: Theme.tx2; font.pixelSize: 10; font.weight: Font.DemiBold; font.capitalization: Font.AllUppercase; font.letterSpacing: 1 }
                    Num { text: Timing.imu ? modelData[1].toFixed(2) + " g" : "-- g"; font.pixelSize: 19; font.weight: Font.Bold }
                }
            }
        }
    }

    // Laps or runs, newest first
    Card {
        id: runsCard
        x: page.width - page.sideW
        y: gcard.y + gcard.height + 12
        width: page.sideW
        height: page.height - y
        readonly property bool lapsMode: page.mode === "laps"
        readonly property var rows: lapsMode ? Timing.laps : Timing.runs
        Text {
            x: 16; y: 14
            width: parent.width - 130
            text: runsCard.lapsMode ? "Laps" : (page.mode === "quarter" ? "1/4 Mile Runs" : "0–60 Runs")
            color: Theme.tx; font.pixelSize: 15; font.weight: Font.Bold; elide: Text.ElideRight
        }
        Text {
            anchors.right: parent.right; anchors.rightMargin: 16; y: 16
            readonly property real b: runsCard.lapsMode ? Timing.bestLap : Timing.bestRun
            visible: b > 0
            text: "Best " + (runsCard.lapsMode ? Hmi.lapText(b) : (b / 1000).toFixed(page.mode === "quarter" ? 3 : 2))
            color: Theme.acc; font.pixelSize: 12; font.weight: Font.Bold; font.features: { "tnum": 1 }
        }
        Text {
            visible: runsCard.rows.length === 0
            x: 16; y: 48
            width: parent.width - 32
            wrapMode: Text.WordWrap
            text: runsCard.lapsMode ? "Laps appear here as you cross start/finish." : "Runs appear here."
            color: Theme.tx2; font.pixelSize: 13
        }
        ListView {
            x: 16; y: 44
            width: parent.width - 32
            height: parent.height - 52
            clip: true
            spacing: 8
            model: runsCard.rows
            delegate: Item {
                id: row
                required property var modelData
                width: ListView.view.width
                height: 40
                readonly property bool isLap: Timing.mode === "laps"
                Column {
                    Num {
                        text: row.isLap ? Hmi.lapText(row.modelData.ms)
                            : Timing.mode === "quarter" ? (row.modelData.et / 1000).toFixed(3) + " s @ " + row.modelData.trap.toFixed(1)
                            : (row.modelData.ms / 1000).toFixed(2) + " s"
                        font.pixelSize: 15; font.weight: Font.Bold
                    }
                    Text {
                        text: row.isLap ? (row.modelData.kind === "out" ? "Out lap" : row.modelData.kind === "in" ? "In lap"
                                           : "Lap " + row.modelData.n + (row.modelData.delta && !row.modelData.best ? " · " + Hmi.deltaText(row.modelData.delta) : ""))
                            : Timing.mode === "quarter" ? (row.modelData.rt ? "RT " + row.modelData.rt.toFixed(3) + " · " : "") + "60' " + Hmi.secText(row.modelData.splits["60"] || 0, 3)
                            : "0–30 " + Hmi.secText(row.modelData.s30 || 0, 2) + " s" + (row.modelData.peak ? " · " + row.modelData.peak.toFixed(2) + " g" : "")
                        color: Theme.tx2; font.pixelSize: 11; font.features: { "tnum": 1 }
                    }
                }
                // The best lap's trophy: it pops in with a shine when a new best is set
                Image {
                    id: trophy
                    visible: row.isLap ? row.modelData.best : false
                    anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                    height: 30
                    fillMode: Image.PreserveAspectFit
                    smooth: true; mipmap: true
                    source: Theme.dark ? "../../assets/track/trophy_dark.png" : "../../assets/track/trophy_light.png"
                    onVisibleChanged: if (visible) pop.restart()
                    SequentialAnimation {
                        id: pop
                        ParallelAnimation {
                            NumberAnimation { target: trophy; property: "scale"; from: 0.4; to: 1.25; duration: 260; easing.type: Easing.OutBack }
                            NumberAnimation { target: shine; property: "opacity"; from: 0; to: 0.9; duration: 200 }
                        }
                        ParallelAnimation {
                            NumberAnimation { target: trophy; property: "scale"; to: 1; duration: 220; easing.type: Easing.InOutQuad }
                            NumberAnimation { target: shine; property: "opacity"; to: 0; duration: 700; easing.type: Easing.OutQuad }
                        }
                    }
                    Rectangle {
                        id: shine
                        anchors.centerIn: parent
                        width: parent.height * 1.6; height: width; radius: width / 2
                        color: "#FFD86B"
                        opacity: 0
                        z: -1
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ Tracks list
    TrackPicker {
        anchors.fill: parent
        visible: Hmi.trackUi.picker
    }
}
