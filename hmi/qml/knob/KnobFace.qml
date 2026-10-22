import QtQuick
import Godwit

// What a 1.28" round encoder display shows, at its native 240x240.
// enc is one of Hmi.encs.
Rectangle {
    id: face
    property var enc

    width: 240
    height: 240
    radius: 120
    color: Theme.screen

    Column {
        anchors.centerIn: parent
        spacing: 2

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, 164)
            horizontalAlignment: Text.AlignHCenter
            text: face.enc.label.toUpperCase()
            color: Theme.tx2
            font.pixelSize: 19
            font.weight: Font.Bold
            font.letterSpacing: 0.6
            elide: Text.ElideRight
        }
        Icon {
            visible: face.enc.icon !== ""
            anchors.horizontalCenter: parent.horizontalCenter
            name: face.enc.icon
            color: Theme.named(face.enc.color)
            size: face.enc.iconSize
        }
        Row {
            visible: face.enc.value !== ""
            anchors.horizontalCenter: parent.horizontalCenter
            Num {
                id: value
                width: Math.min(implicitWidth, 196)
                text: face.enc.value
                color: Theme.named(face.enc.color)
                font.pixelSize: face.enc.vSize
                font.weight: face.enc.small ? Font.Bold : Font.Normal
                font.letterSpacing: -2
            }
            Text {
                anchors.baseline: value.baseline
                text: face.enc.unit
                color: Theme.tx2
                font.pixelSize: 30
            }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, 170)
            horizontalAlignment: Text.AlignHCenter
            text: face.enc.sub
            color: Theme.tx2
            font.pixelSize: 19
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Row {
            visible: face.enc.pager.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            topPadding: 8
            spacing: 6
            Repeater {
                model: face.enc.pager
                delegate: Rectangle {
                    required property var modelData
                    width: modelData.on ? 26 : 8
                    height: 8
                    radius: 4
                    color: modelData.on ? Theme.acc : Theme.line
                    Behavior on width { NumberAnimation { duration: 200 } }
                }
            }
        }
    }
}
