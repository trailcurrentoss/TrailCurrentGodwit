import QtQuick
import Godwit

// New text message banner. Read sits above the driver knob, Dismiss above the
// passenger knob. Only the sender is shown; the text is read aloud.
AppPage {
    id: banner
    height: 96
    readonly property var m: Hmi.msgNote || ({ n: "", ini: "" })

    Card {
        anchors.fill: parent
        radius: 26
        large: true

        Rectangle {
            id: read
            x: 14
            width: 260; height: 68; radius: 34
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.acc
            Row {
                anchors.centerIn: parent
                spacing: 10
                Icon { name: "volume-high"; color: Theme.onAcc; size: 24; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Read"; color: Theme.onAcc; font.pixelSize: 20; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
            }
            TapHandler { onTapped: Hmi.readMessage() }
        }

        Row {
            anchors.left: read.right
            anchors.leftMargin: 16
            anchors.right: dismiss.left
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
                Text { text: banner.m.n; color: Theme.tx; font.pixelSize: 24; font.weight: Font.Bold; width: parent.width; elide: Text.ElideRight }
                Row {
                    spacing: 6
                    Icon { name: "chatbubble-ellipses"; color: Theme.tx2; size: 16; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "New message"; color: Theme.tx2; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter }
                }
            }
        }

        Rectangle {
            id: dismiss
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 180; height: 56; radius: 28
            color: Theme.track
            Row {
                anchors.centerIn: parent
                spacing: 8
                Icon { name: "close"; color: Theme.tx; size: 22; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Dismiss"; color: Theme.tx; font.pixelSize: 17; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
            }
            TapHandler { onTapped: Hmi.dismissMessage() }
        }
    }
}
