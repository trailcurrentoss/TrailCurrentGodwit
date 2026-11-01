import QtQuick
import Godwit

// "Hey Peregrine": listening, then what was heard while it is worked out.
// Done Talking sits above the driver knob, Cancel above the passenger knob.
AppPage {
    id: banner
    height: 96
    readonly property var a: Hmi.ask || ({ stage: "listening", heard: "" })
    readonly property bool listening: a.stage === "listening"
    // "Cancel the route?": End Route on the left, Keep Route on the right
    readonly property bool confirming: a.stage === "confirm"
    readonly property bool micOn: listening || (confirming && !!a.mic)

    Card {
        anchors.fill: parent
        radius: 26
        large: true

        Rectangle {
            id: left
            x: 14
            width: 260; height: 68; radius: 34
            anchors.verticalCenter: parent.verticalCenter
            color: banner.confirming ? Theme.heat : banner.listening ? Theme.acc : Theme.track
            Row {
                anchors.centerIn: parent
                spacing: 10
                visible: banner.listening || banner.confirming
                Icon { name: banner.confirming ? "flag" : "checkmark"; color: banner.confirming ? Theme.onAlert : Theme.onAcc; size: 24; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    text: banner.confirming ? "End Route" : "Done Talking"
                    color: banner.confirming ? Theme.onAlert : Theme.onAcc
                    font.pixelSize: 20; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter
                }
            }
            Icon { anchors.centerIn: parent; visible: !banner.listening && !banner.confirming; name: "sync"; color: Theme.tx2; size: 26 }
            TapHandler { onTapped: Hmi.o_ask(0, "press", 0) }
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
                Icon { anchors.centerIn: parent; name: banner.confirming ? "navigate" : "mic"; color: banner.micOn ? Theme.cool : Theme.acc; size: 28 }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 56 - parent.spacing
                spacing: 2
                Text {
                    text: banner.confirming ? "Cancel the route?" : banner.a.heard ? "“" + banner.a.heard + "”" : "Hey Peregrine"
                    color: Theme.tx; font.pixelSize: banner.a.heard ? 20 : 24; font.weight: Font.Bold
                    width: parent.width; elide: Text.ElideRight
                }
                Text { text: Hmi.askStage ? Hmi.askStage.sub : ""; color: Theme.tx2; font.pixelSize: 15 }
                // How loud the driver is while listening
                Rectangle {
                    visible: banner.micOn
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
            color: Theme.track
            Row {
                anchors.centerIn: parent
                spacing: 8
                Icon { name: banner.confirming ? "navigate" : "close"; color: Theme.tx; size: 22; anchors.verticalCenter: parent.verticalCenter }
                Text { text: banner.confirming ? "Keep Route" : "Cancel"; color: Theme.tx; font.pixelSize: 17; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
            }
            TapHandler { onTapped: Hmi.o_ask(1, "press", 0) }
        }
    }
}
