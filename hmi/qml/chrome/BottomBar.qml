import QtQuick
import Godwit
import "../DemoData.js" as D

// 76 px bottom bar: what the left knob does, the dock, what the right knob does.
// Each knob indicator sits above its knob.
Rectangle {
    id: bar
    height: 76
    color: Theme.chrome

    component KnobHint: Item {
        id: hint
        property var enc
        property int side: 0
        property bool mirrored: false
        width: 260
        height: parent.height
        // Tap for the knob's touch controls
        Rectangle {
            anchors.fill: parent
            anchors.margins: 6
            radius: 16
            color: Hmi.knobPanel === hint.side ? Theme.accSoft : "transparent"
        }
        TapHandler { onTapped: Hmi.knobPanel = Hmi.knobPanel === hint.side ? -1 : hint.side }

        Rectangle {
            id: dot
            width: 26; height: 26; radius: 13
            color: "transparent"
            border.width: 4
            border.color: hint.enc.idc
            anchors.verticalCenter: parent.verticalCenter
            x: hint.mirrored ? hint.width - 18 - width : 18
            Behavior on border.color { ColorAnimation { duration: 300 } }
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            x: hint.mirrored ? dot.x - 12 - width : dot.x + dot.width + 12
            width: hint.width - 18 - 26 - 12 - 4
            Text {
                width: parent.width
                horizontalAlignment: hint.mirrored ? Text.AlignRight : Text.AlignLeft
                text: hint.enc.name
                color: Theme.tx
                font.pixelSize: 17
                font.weight: Font.Bold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                horizontalAlignment: hint.mirrored ? Text.AlignRight : Text.AlignLeft
                text: hint.enc.hint || "--"
                color: Theme.tx2
                font.pixelSize: 13
                elide: Text.ElideRight
            }
        }
    }

    KnobHint { anchors.left: parent.left; enc: Hmi.encs[0]; side: 0 }
    KnobHint { anchors.right: parent.right; enc: Hmi.encs[1]; side: 1; mirrored: true }

    Row {
        anchors.centerIn: parent
        spacing: 6
        Repeater {
            model: D.DOCK
            delegate: Rectangle {
                required property string modelData
                readonly property bool on: !Hmi.launcher && Hmi.app === modelData
                readonly property var a: D.APPS.find(function (x) { return x.id === modelData; })
                width: 72; height: 56; radius: 18
                color: on ? Theme.accSoft : "transparent"
                Behavior on color { ColorAnimation { duration: 200 } }
                Icon { anchors.centerIn: parent; name: parent.on ? parent.a.icon : parent.a.icon + "-outline"; color: parent.on ? Theme.acc : Theme.tx; size: 28 }
                TapHandler { onTapped: Hmi.openApp(parent.modelData) }
            }
        }
        Rectangle {
            readonly property bool on: Hmi.launcher || D.DOCK.indexOf(Hmi.app) < 0
            width: 72; height: 56; radius: 18
            color: on ? Theme.accSoft : "transparent"
            Behavior on color { ColorAnimation { duration: 200 } }
            Icon { anchors.centerIn: parent; name: parent.on ? "apps" : "apps-outline"; color: parent.on ? Theme.acc : Theme.tx; size: 28 }
            TapHandler { onTapped: Hmi.toggleLauncher() }
        }
    }
}
