import QtQuick
import Godwit
import "../Legal.js" as L

// Driver safety notice, over everything at every boot until accepted here
// or with either knob (Hmi.owner gives both knobs to it meanwhile). Cameras
// open from here without accepting, and the notice returns when they close.
Rectangle {
    id: notice
    color: Theme.bg

    // Nothing behind the notice can be touched
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    Card {
        anchors.centerIn: parent
        width: 860
        height: Math.min(col.implicitHeight + 56, notice.height - 32)
        large: true

        Column {
            id: col
            anchors.centerIn: parent
            width: parent.width - 72
            spacing: 14
            // The terms scroll inside the card when they outgrow the screen
            readonly property real bodyMax: notice.height - 32 - 56 - demo.height - titleRow.height - actions.height - spacing * 4

            Text {
                id: demo
                width: col.width
                wrapMode: Text.WordWrap
                text: L.DEMO_NOTICE
                color: Theme.warn
                font.pixelSize: 18
                font.weight: Font.Bold
            }
            Row {
                id: titleRow
                spacing: 14
                Rectangle {
                    width: 52; height: 52; radius: 26
                    color: Qt.rgba(1, 0.76, 0.03, 0.18)
                    Icon { anchors.centerIn: parent; name: "car"; size: 28; color: Theme.warn }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: L.SAFETY_TITLE
                    color: Theme.tx
                    font.pixelSize: 30
                    font.weight: Font.Bold
                    font.letterSpacing: -0.4
                }
            }
            Flickable {
                id: body
                width: col.width
                height: Math.min(terms.implicitHeight, col.bodyMax)
                contentHeight: terms.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: terms
                    width: body.width
                    spacing: 10
                    Repeater {
                        model: L.SAFETY
                        delegate: Text {
                            required property string modelData
                            width: terms.width
                            wrapMode: Text.WordWrap
                            text: modelData
                            color: Theme.tx2
                            font.pixelSize: 16
                        }
                    }
                }
                Connections {
                    target: Hmi
                    function onDocScroll(d) { body.contentY = Math.max(0, Math.min(body.contentY + d * 60, body.contentHeight - body.height)); }
                }
            }
            Item {
                id: actions
                width: col.width
                height: 56
                Rectangle {
                    id: camsBtn
                    width: 170; height: 56; radius: 28
                    color: Theme.track
                    Row {
                        anchors.centerIn: parent
                        spacing: 8
                        Icon { name: "videocam"; size: 22; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: "Cameras"; color: Theme.tx; font.pixelSize: 18; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
                    }
                    TapHandler { onTapped: Hmi.openApp("cams") }
                }
                Text {
                    anchors.left: camsBtn.right
                    anchors.leftMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    text: body.contentHeight > body.height ? "Knobs: turn to scroll · press to accept · hold for cameras"
                                                           : "Knobs: press to accept · hold for cameras"
                    color: Theme.tx2
                    font.pixelSize: 14
                }
                Rectangle {
                    anchors.right: parent.right
                    width: 200; height: 56; radius: 28
                    color: Theme.acc
                    Text { anchors.centerIn: parent; text: "I Accept"; color: Theme.onAcc; font.pixelSize: 18; font.weight: Font.Bold }
                    TapHandler { onTapped: Hmi.acceptNotice() }
                }
            }
        }
    }
}
