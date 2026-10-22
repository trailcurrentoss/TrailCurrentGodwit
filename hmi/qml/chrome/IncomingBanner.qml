import QtQuick
import Godwit
import "../DemoData.js" as D

// Incoming call banner. Answer sits above the driver knob, Decline above the passenger knob.
AppPage {
    id: banner
    height: 96
    readonly property var c: Hmi.inCall ? D.CONTACTS[Hmi.phone.call.who] : D.CONTACTS[0]

    Card {
        anchors.fill: parent
        radius: 26
        large: true

        Rectangle {
            id: answer
            x: 14
            width: 260; height: 68; radius: 34
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.acc
            Row {
                anchors.centerIn: parent
                spacing: 10
                Icon { name: "call"; color: Theme.onAcc; size: 24; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Answer"; color: Theme.onAcc; font.pixelSize: 20; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
            }
            TapHandler { onTapped: Hmi.answerCall() }
        }

        Row {
            anchors.left: answer.right
            anchors.leftMargin: 16
            anchors.right: decline.left
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14
            Rectangle {
                width: 56; height: 56; radius: 28
                color: Theme.accSoft
                Text { anchors.centerIn: parent; text: banner.c.ini; color: Theme.acc; font.pixelSize: 22; font.weight: Font.Bold }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                Text { text: banner.c.n; color: Theme.tx; font.pixelSize: 24; font.weight: Font.Bold }
                Text {
                    text: Hmi.phone.silent ? "Incoming call · silenced" : "Incoming call · mobile"
                    color: Theme.tx2
                    font.pixelSize: 15
                }
            }
        }

        Rectangle {
            id: decline
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            width: 180; height: 56; radius: 28
            color: Theme.heat
            Row {
                anchors.centerIn: parent
                spacing: 8
                Icon { name: "close"; color: Theme.onAlert; size: 22; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Decline"; color: Theme.onAlert; font.pixelSize: 17; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
            }
            TapHandler { onTapped: Hmi.endCall("Call declined") }
        }
    }
}
