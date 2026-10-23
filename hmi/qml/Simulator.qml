import QtQuick
import Godwit

// Desktop simulator: the head unit as in the design mockup, with both
// encoders on screen and controls for vehicle events.
Window {
    id: win
    width: 1104
    height: 1130
    visible: true
    color: Theme.dark ? "#0a0a0a" : "#e8e8e8"
    title: "Godwit simulator"

    component SimButton: Card {
        id: btn
        property string icon
        property string text
        signal clicked
        width: row.implicitWidth + 32
        height: 38
        radius: 19
        Row {
            id: row
            anchors.centerIn: parent
            spacing: 8
            Icon { visible: btn.icon !== ""; name: btn.icon; size: 16; anchors.verticalCenter: parent.verticalCenter }
            Text { text: btn.text; color: Theme.tx; font.pixelSize: 14; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
        }
        TapHandler { onTapped: btn.clicked() }
    }

    Item {
        id: stage
        width: 1104
        height: 1130
        anchors.centerIn: parent
        scale: Math.min(win.width / width, win.height / height)

        Item {
            id: header
            width: parent.width
            height: 56
            Column {
                x: 20
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text { text: "Godwit head unit"; color: Theme.tx; font.pixelSize: 22; font.weight: Font.Bold; font.letterSpacing: -0.3 }
                Text { text: "Keys: left a / d / s, right j / l / k, Shift + press = hold"; color: Theme.tx2; font.pixelSize: 14 }
            }
            Row {
                anchors.right: parent.right
                anchors.rightMargin: 20
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
                Card {
                    width: gears.implicitWidth + 8
                    height: 38
                    radius: 19
                    Row {
                        id: gears
                        anchors.centerIn: parent
                        spacing: 4
                        Repeater {
                            model: ["P", "R", "D"]
                            delegate: Rectangle {
                                required property string modelData
                                readonly property bool on: Hmi.gear === modelData
                                width: 40; height: 30; radius: 15
                                color: on ? (modelData === "R" ? Theme.heat : Theme.acc) : "transparent"
                                Text {
                                    anchors.centerIn: parent
                                    text: parent.modelData
                                    color: parent.on ? (parent.modelData === "R" ? Theme.onAlert : Theme.onAcc) : Theme.tx
                                    font.pixelSize: 15
                                    font.weight: Font.Bold
                                }
                                TapHandler { onTapped: Hmi.setGear(parent.modelData) }
                            }
                        }
                    }
                }
                SimButton { icon: "call"; text: "Incoming Call"; onClicked: Hmi.simIncoming() }
                SimButton { icon: "chatbubble-ellipses"; text: "Text Message"; onClicked: Hmi.simMessage() }
                SimButton { icon: "battery-dead"; text: Hmi.lowBattery ? "Restore Battery" : "Low Battery"; onClicked: Hmi.simBattery() }
                SimButton { icon: Theme.dark ? "sunny" : "moon"; text: Theme.dark ? "Light" : "Dark"; onClicked: Hmi.toggleTheme() }
            }
        }

        // The unit
        Rectangle {
            id: unit
            y: 70
            width: 1104
            height: 1000
            radius: 32
            color: "#141414"

            Rectangle { x: 28; y: 28; width: 1048; height: 624; radius: 14; color: "#000000" }
            HeadUnit { id: screen; x: 40; y: 40 }
            Rectangle { y: 668; width: parent.width; height: 1; color: Qt.rgba(1, 1, 1, 0.05) }
            Text {
                y: 800
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: "TRAILCURRENT"
                color: "#3a3a3a"
                font.pixelSize: 12
                font.weight: Font.Bold
                font.letterSpacing: 5
            }
            KnobSim { side: 0; x: 110; y: 690 }
            KnobSim { side: 1; x: 754; y: 690 }
        }
    }

    // Keyboard focus stays with the screen so the knob keys work
    Component.onCompleted: screen.forceActiveFocus()
}
