import QtQuick
import Godwit
import "../DemoData.js" as D

// Transport on the driver side; Up Next on the right, beside the knob that browses it
AppPage {
    id: page
    readonly property var m: Hmi.media
    readonly property var tr: D.TRACKS[m.idx]
    readonly property int start: Math.max(0, Math.min(m.sel - 2, D.TRACKS.length - 5))

    Column {
        anchors.left: parent.left
        anchors.right: queue.left
        anchors.rightMargin: 24
        anchors.verticalCenter: parent.verticalCenter
        spacing: 22

        Row {
            spacing: 24
            Rectangle {
                width: 200; height: 200; radius: 22
                color: D.ARTS[page.m.idx % D.ARTS.length]
                Behavior on color { ColorAnimation { duration: 300 } }
                Icon { anchors.centerIn: parent; name: "musical-notes"; color: Qt.rgba(1, 1, 1, 0.85); size: 72 }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: 360
                spacing: 6
                Row {
                    spacing: 6
                    Icon { name: "bluetooth"; color: Theme.tx2; size: 16; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: page.m.src; color: Theme.tx2; font.pixelSize: 15; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                    Pill {
                        visible: Hmi.inCall
                        text: "Paused for call"
                        color: Theme.heat
                        fg: Theme.onAlert
                        fontSize: 13
                        vpad: 3
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text { width: parent.width; text: page.tr[0]; color: Theme.tx; font.pixelSize: 38; font.weight: Font.Bold; font.letterSpacing: -0.8; elide: Text.ElideRight }
                Text { width: parent.width; text: page.tr[1]; color: Theme.tx2; font.pixelSize: 21; elide: Text.ElideRight }
            }
        }

        Column {
            width: parent.width
            spacing: 6
            Rectangle {
                width: parent.width; height: 6; radius: 3
                color: Theme.track
                clip: true
                Rectangle {
                    height: parent.height
                    width: parent.width * page.m.pos / page.tr[2]
                    color: Theme.tx
                    Behavior on width { NumberAnimation { duration: 1000 } }
                }
            }
            Item {
                width: parent.width
                height: 18
                Num { text: D.fmtT(page.m.pos); color: Theme.tx2; font.pixelSize: 14; font.weight: Font.DemiBold }
                Num { anchors.right: parent.right; text: D.fmtT(page.tr[2]); color: Theme.tx2; font.pixelSize: 14; font.weight: Font.DemiBold }
            }
        }

        Row {
            spacing: 28
            Item {
                width: 64; height: 64
                anchors.verticalCenter: parent.verticalCenter
                Icon { anchors.centerIn: parent; name: "play-skip-back"; size: 32 }
                TapHandler { onTapped: Hmi.upd("media", function (m) { var i = (m.idx + D.TRACKS.length - 1) % D.TRACKS.length; return { idx: i, sel: i, pos: 0 }; }) }
            }
            Rectangle {
                width: 84; height: 84; radius: 42
                color: Theme.tx
                Icon { anchors.centerIn: parent; name: page.m.playing ? "pause" : "play"; color: Theme.bg; size: 38 }
                TapHandler {
                    onTapped: {
                        if (Hmi.inCall) { Hmi.toast("Audio paused during call"); return; }
                        Hmi.upd("media", function (m) { return { playing: !m.playing }; });
                    }
                }
            }
            Item {
                width: 64; height: 64
                anchors.verticalCenter: parent.verticalCenter
                Icon { anchors.centerIn: parent; name: "play-skip-forward"; size: 32 }
                TapHandler { onTapped: Hmi.upd("media", function (m) { var i = (m.idx + 1) % D.TRACKS.length; return { idx: i, sel: i, pos: 0 }; }) }
            }
        }
    }

    Card {
        id: queue
        anchors.right: parent.right
        width: 360
        height: parent.height

        Item {
            id: head
            x: 24; y: 18
            width: parent.width - 48
            height: 28
            Text { id: upNext; text: "Up Next"; color: Theme.tx; font.pixelSize: 20; font.weight: Font.Bold }
            Pill {
                anchors.right: parent.right
                anchors.verticalCenter: upNext.verticalCenter
                icon: "shuffle"
                text: page.m.shuffle ? "Shuffle on" : "Shuffle off"
                color: page.m.shuffle ? Theme.acc : Theme.track
                fg: page.m.shuffle ? Theme.onAcc : Theme.tx
                fontSize: 13
                vpad: 7
                TapHandler {
                    onTapped: {
                        var on = page.m.shuffle;
                        Hmi.upd("media", { shuffle: !on });
                        Hmi.toast(on ? "Shuffle off" : "Shuffle on");
                    }
                }
            }
        }
        Column {
            anchors.top: head.bottom
            anchors.topMargin: 8
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 16
            spacing: 4
            Repeater {
                model: 5
                delegate: Rectangle {
                    id: qrow
                    required property int index
                    readonly property int i: page.start + index
                    readonly property var t: D.TRACKS[i]
                    width: parent.width
                    height: (parent.height - 16) / 5
                    radius: 14
                    color: i === page.m.sel ? Theme.accSoft : "transparent"
                    Behavior on color { ColorAnimation { duration: 200 } }
                    Column {
                        x: 14
                        width: parent.width - 28
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text { width: parent.width; text: qrow.t[0]; color: qrow.i === page.m.idx ? Theme.acc : Theme.tx; font.pixelSize: 17; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        Text { width: parent.width; text: qrow.t[1] + " · " + D.fmtT(qrow.t[2]); color: Theme.tx2; font.pixelSize: 14; elide: Text.ElideRight }
                    }
                    TapHandler { onTapped: Hmi.playTrack(qrow.i) }
                }
            }
        }
    }
}
