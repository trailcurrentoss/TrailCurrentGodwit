import QtQuick
import Godwit
import "../DemoData.js" as D

// Active call. Mute sits above the left knob (press mutes, hold ends); End above the right.
Item {
    id: view
    readonly property var c: Hmi.phone.call || ({ n: "", num: "", ini: "", state: "", t: 0 })
    readonly property bool mic: Hmi.phone.mic

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: -55
        spacing: 10
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 132; height: 132; radius: 66
            color: Theme.accSoft
            Text { anchors.centerIn: parent; text: view.c.ini; color: Theme.acc; font.pixelSize: 48; font.weight: Font.Bold }
        }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: view.c.n; color: Theme.tx; font.pixelSize: 44; font.weight: Font.Bold; font.letterSpacing: -0.8 }
        Num {
            anchors.horizontalCenter: parent.horizontalCenter
            text: (view.c.state === "dialing" ? "Calling " + view.c.num : D.fmtT(view.c.t)) + (view.mic ? "" : " · Mic muted")
            color: Theme.tx2
            font.pixelSize: 20
        }
    }

    Column {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        spacing: 8
        Card {
            width: 260; height: 68; radius: 34
            color: view.mic ? Theme.card : Theme.heat
            Row {
                anchors.centerIn: parent
                spacing: 10
                Icon { name: view.mic ? "mic-off" : "mic"; color: view.mic ? Theme.tx : Theme.onAlert; size: 24; anchors.verticalCenter: parent.verticalCenter }
                Text { text: view.mic ? "Mute Mic" : "Unmute Mic"; color: view.mic ? Theme.tx : Theme.onAlert; font.pixelSize: 19; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
            }
            TapHandler { onTapped: Hmi.toggleMic() }
        }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Left knob · press mutes, hold ends"; color: Theme.tx2; font.pixelSize: 14; font.weight: Font.DemiBold }
    }

    Column {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: 8
        Rectangle {
            width: 220; height: 68; radius: 34
            color: Theme.heat
            Row {
                anchors.centerIn: parent
                spacing: 10
                Icon { name: "close"; color: Theme.onAlert; size: 24; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "End Call"; color: Theme.onAlert; font.pixelSize: 19; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
            }
            TapHandler { onTapped: Hmi.endCall("Call ended") }
        }
        Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Right knob · press"; color: Theme.tx2; font.pixelSize: 14; font.weight: Font.DemiBold }
    }
}
