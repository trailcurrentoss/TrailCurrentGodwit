import QtQuick
import Godwit
import "../Ring.js" as Ring
import "../DemoData.js" as D

// Frequency and station name are the glance targets; the tuner needle takes the signal ring's colour
AppPage {
    id: page
    readonly property var rad: Hmi.radio
    readonly property bool fm: rad.band === "FM"
    readonly property real f: fm ? rad.fm : rad.am
    readonly property var sg: Hmi.sig()
    readonly property color sc: Ring.sigCol(sg.sig)
    readonly property real lo: fm ? 87.9 : 530
    readonly property real step: fm ? 0.2 : 10
    readonly property int ticks: fm ? 101 : 118
    function fmtF(b, x) { return b === "FM" ? x.toFixed(1) : String(x); }

    Column {
        anchors.left: parent.left
        anchors.right: presets.left
        anchors.rightMargin: 24
        anchors.top: parent.top
        anchors.bottom: dial.top
        anchors.bottomMargin: 16
        spacing: 4
        topPadding: (height - implicitHeight) / 2

        Row {
            spacing: 12
            Pill { text: page.rad.band; fontSize: 14; hpad: 14; anchors.verticalCenter: parent.verticalCenter }
            Text { text: page.sg.stn ? page.sg.stn.g : "No station"; color: Theme.tx2; font.pixelSize: 17; anchors.verticalCenter: parent.verticalCenter }
            Row {
                spacing: 3
                height: 22
                anchors.verticalCenter: parent.verticalCenter
                Repeater {
                    model: 5
                    delegate: Rectangle {
                        required property int index
                        anchors.bottom: parent.bottom
                        width: 6
                        height: 4 + (index + 1) * 3.5
                        radius: 3
                        color: index + 1 <= Math.round(page.sg.sig * 5) ? page.sc : Theme.track
                        Behavior on color { ColorAnimation { duration: 200 } }
                    }
                }
            }
            Pill {
                visible: Hmi.muted || Hmi.inCall
                text: Hmi.inCall ? "Paused for call" : "Muted"
                color: Theme.heat
                fg: Theme.onAlert
                fontSize: 13
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Row {
            spacing: 10
            Num { id: freq; text: page.fmtF(page.rad.band, page.f); font.pixelSize: 136; font.letterSpacing: -4; lineHeight: 1 }
            Text { anchors.baseline: freq.baseline; text: page.fm ? "MHz" : "kHz"; color: Theme.tx2; font.pixelSize: 26 }
        }
        Text { text: page.sg.stn ? page.sg.stn.n : "Searching"; color: Theme.tx; font.pixelSize: 30; font.weight: Font.Bold; font.letterSpacing: -0.4 }
        Text { text: page.sg.stn ? page.sg.stn.rt : "--"; color: Theme.tx2; font.pixelSize: 18 }
    }

    Grid {
        id: presets
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: dial.top
        anchors.bottomMargin: 16
        width: 330
        columns: 2
        spacing: 10
        Repeater {
            model: page.rad.presets.length
            delegate: Card {
                id: preset
                required property int index
                readonly property var p: page.rad.presets[index]
                readonly property var ps: (p.b === "FM" ? D.FM : D.AM).find(function (x) { return Math.abs(x.f - preset.p.f) < 0.05; })
                readonly property bool on: p.b === page.rad.band && Math.abs(p.f - page.f) < 0.05
                width: (presets.width - 10) / 2
                height: (presets.height - 20) / 3
                radius: 18
                color: on ? Theme.acc : Theme.card
                Column {
                    x: 16
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Num { text: page.fmtF(preset.p.b, preset.p.f); color: preset.on ? Theme.onAcc : Theme.tx; font.pixelSize: 24; font.weight: Font.DemiBold }
                    Text { text: (preset.index + 1) + " · " + (preset.ps ? preset.ps.n : preset.p.b); color: preset.on ? Theme.onAcc : Theme.tx; opacity: 0.8; font.pixelSize: 14; font.weight: Font.DemiBold }
                }
                // Tap to listen; press and hold to save the current station here
                TapHandler { onTapped: Hmi.tuneTo(preset.p.b, preset.p.f); onLongPressed: Hmi.savePreset(preset.index) }
            }
        }
    }

    // Tuner dial: ticks slide under a fixed needle
    Card {
        id: dial
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 96
        radius: 20
        clip: true

        Item {
            id: scale
            x: parent.width / 2 - Math.round((page.f - page.lo) / page.step * 12)
            height: parent.height
            Behavior on x { NumberAnimation { duration: 180; easing.type: Easing.InOutQuad } }
            Repeater {
                model: page.ticks
                delegate: Rectangle {
                    required property int index
                    x: index * 12
                    y: 16
                    width: 2
                    height: index % 5 === 0 ? 30 : 14
                    radius: 1
                    color: Theme.tx2
                    opacity: index % 5 === 0 ? 0.9 : 0.45
                }
            }
            Repeater {
                model: Math.ceil(page.ticks / 10)
                delegate: Num {
                    required property int index
                    x: index * 120 - width / 2
                    y: 56
                    text: page.fm ? Math.round(page.lo + index * 10 * page.step) : page.lo + index * 10 * page.step
                    color: Theme.tx2
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
            }
        }
        // Fade the ends, as the mockup's mask does
        Rectangle {
            width: parent.width * 0.18; height: parent.height
            gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: Theme.card } GradientStop { position: 1; color: "transparent" } }
        }
        Rectangle {
            anchors.right: parent.right
            width: parent.width * 0.18; height: parent.height
            gradient: Gradient { orientation: Gradient.Horizontal; GradientStop { position: 0; color: "transparent" } GradientStop { position: 1; color: Theme.card } }
        }
        Rectangle {
            x: parent.width / 2 - 2
            y: 8
            width: 4; height: 52; radius: 2
            color: page.sc
            Behavior on color { ColorAnimation { duration: 200 } }
        }

        // Drag the scale to tune, one step per tick
        DragHandler {
            target: null
            yAxis.enabled: false
            property real acc: 0
            property real last: 0
            onActiveChanged: { acc = 0; last = 0; }
            onTranslationChanged: {
                acc += translation.x - last;
                last = translation.x;
                while (Math.abs(acc) >= 12) {
                    var step = acc > 0 ? -1 : 1;
                    acc += step * 12;
                    Hmi.radioTune(step);
                }
            }
        }
        // Seek down / up at the ends of the dial
        Repeater {
            model: [-1, 1]
            delegate: Rectangle {
                id: seekBtn
                required property int modelData
                x: modelData < 0 ? 14 : dial.width - width - 14
                anchors.verticalCenter: parent.verticalCenter
                width: 60; height: 60; radius: 30
                color: seekTap.pressed ? Theme.accSoft : Theme.track
                Icon { anchors.centerIn: parent; name: seekBtn.modelData < 0 ? "play-skip-back" : "play-skip-forward"; size: 24 }
                TapHandler { id: seekTap; onTapped: Hmi.seek(seekBtn.modelData) }
            }
        }
    }
}
