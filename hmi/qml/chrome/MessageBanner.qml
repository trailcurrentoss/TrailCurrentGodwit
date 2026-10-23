import QtQuick
import Godwit

// Text message banner: a new message, reading it, and replying by voice.
// The left action sits above the driver knob, the right above the passenger
// knob. Only the sender is shown; the texts are spoken.
AppPage {
    id: banner
    height: 96
    readonly property var m: Hmi.msgNote || ({ n: "", ini: "", stage: "new" })
    readonly property var st: Hmi.msgStage || ({ sub: "", l: null, r: null })

    Card {
        anchors.fill: parent
        radius: 26
        large: true

        Rectangle {
            id: left
            x: 14
            width: 260; height: 68; radius: 34
            anchors.verticalCenter: parent.verticalCenter
            color: banner.st.l ? Theme.acc : Theme.track
            Row {
                anchors.centerIn: parent
                spacing: 10
                visible: !!banner.st.l
                Icon { name: banner.st.l ? banner.st.l[1] : ""; color: Theme.onAcc; size: 24; anchors.verticalCenter: parent.verticalCenter }
                Text { text: banner.st.l ? banner.st.l[0] : ""; color: Theme.onAcc; font.pixelSize: 20; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
            }
            Icon { anchors.centerIn: parent; visible: !banner.st.l; name: "sync"; color: Theme.tx2; size: 26 }
            TapHandler { onTapped: Hmi.msgAct(0, "press"); onLongPressed: Hmi.msgAct(0, "hold") }
        }

        Row {
            anchors.left: left.right
            anchors.leftMargin: 16
            anchors.right: right.left
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14
            Rectangle {
                width: 56; height: 56; radius: 28
                color: Theme.accSoft
                Text { anchors.centerIn: parent; text: banner.m.ini; color: Theme.acc; font.pixelSize: 22; font.weight: Font.Bold }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 56 - parent.spacing
                spacing: 2
                Text { text: banner.m.n; color: Theme.tx; font.pixelSize: 24; font.weight: Font.Bold; width: parent.width; elide: Text.ElideRight }
                Row {
                    spacing: 6
                    Icon {
                        name: banner.m.stage === "listening" ? "mic" : "chatbubble-ellipses"
                        color: banner.m.stage === "listening" ? Theme.cool : Theme.tx2
                        size: 16; anchors.verticalCenter: parent.verticalCenter
                    }
                    Text { text: banner.st.sub; color: Theme.tx2; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter }
                }
                // How loud the driver is while listening
                Rectangle {
                    visible: banner.m.stage === "listening"
                    width: parent.width; height: 6; radius: 3
                    color: Theme.track
                    Rectangle {
                        width: parent.width * Dictation.level; height: parent.height; radius: 3
                        color: Theme.cool
                        Behavior on width { NumberAnimation { duration: 60 } }
                    }
                }
            }
        }

        Rectangle {
            id: right
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 180; height: 56; radius: 28
            visible: !!banner.st.r
            color: Theme.track
            Row {
                anchors.centerIn: parent
                spacing: 8
                Icon { name: banner.st.r ? banner.st.r[1] : ""; color: Theme.tx; size: 22; anchors.verticalCenter: parent.verticalCenter }
                Text { text: banner.st.r ? banner.st.r[0] : ""; color: Theme.tx; font.pixelSize: 17; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
            }
            TapHandler { onTapped: Hmi.msgAct(1, "press") }
        }
    }
}
