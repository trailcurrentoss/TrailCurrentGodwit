import QtQuick
import Godwit
import "../Ring.js" as Ring

// Pitch and roll either side of the bubble; the outlined card is the axis the right knob trims
AppPage {
    id: page
    readonly property var lv: Hmi.level
    readonly property bool okP: Math.abs(lv.pitch) < 0.2
    readonly property bool okR: Math.abs(lv.roll) < 0.2
    readonly property bool parked: Hmi.gear === "P"
    function f(x) { return (x > 0 ? "+" : "") + x.toFixed(1) + "°"; }

    component Axis: Card {
        id: ax
        property string name
        property string value
        property string note
        property color valueColor
        property bool mirrored: false
        property string axis
        selected: page.lv.axis === axis
        height: axCol.implicitHeight + 40
        Column {
            id: axCol
            x: 20; y: 20
            width: parent.width - 40
            spacing: 6
            Text { width: parent.width; horizontalAlignment: ax.mirrored ? Text.AlignRight : Text.AlignLeft; text: ax.name; color: Theme.tx2; font.pixelSize: 17; font.weight: Font.DemiBold }
            Num { width: parent.width; horizontalAlignment: ax.mirrored ? Text.AlignRight : Text.AlignLeft; text: ax.value; color: ax.valueColor; font.pixelSize: 72; font.letterSpacing: -2 }
            Text { width: parent.width; horizontalAlignment: ax.mirrored ? Text.AlignRight : Text.AlignLeft; text: ax.note; color: Theme.tx; font.pixelSize: 19; font.weight: Font.DemiBold }
            // Trim by touch, in the same 0.1 degree steps as the knob
            Row {
                x: ax.mirrored ? parent.width - width : 0
                spacing: 10
                topPadding: 6
                Repeater {
                    model: [1, -1]
                    delegate: Rectangle {
                        id: trimBtn
                        required property int modelData
                        width: 56; height: 44; radius: 22
                        color: trimTap.pressed ? Theme.accSoft : Theme.track
                        Icon { anchors.centerIn: parent; name: trimBtn.modelData > 0 ? "chevron-back" : "chevron-forward"; rotation: 90; size: 20 }
                        TapHandler { id: trimTap; onTapped: Hmi.levelTrim(ax.axis, trimBtn.modelData) }
                    }
                }
            }
        }
        TapHandler { onTapped: Hmi.upd("level", { axis: ax.axis }) }
    }

    readonly property real sideW: (width - 330 - 48) / 2

    Axis {
        x: 0
        width: page.sideW
        anchors.verticalCenter: parent.verticalCenter
        axis: "pitch"; name: "Pitch"; value: page.f(page.lv.pitch)
        note: page.okP ? "Level" : (page.lv.pitch > 0 ? "Nose up" : "Nose down")
        valueColor: page.okP ? Theme.acc : Theme.warn
    }

    Column {
        x: page.sideW + 24
        width: 330
        anchors.verticalCenter: parent.verticalCenter
        spacing: 14
        Card {
            id: bubbleCard
            anchors.horizontalCenter: parent.horizontalCenter
            width: 300; height: 300; radius: 150
            shadow: false
            Rectangle { x: 24; y: 149; width: 252; height: 2; color: Theme.line }
            Rectangle { x: 149; y: 24; width: 2; height: 252; color: Theme.line }
            Rectangle { x: 108; y: 108; width: 84; height: 84; radius: 42; color: "transparent"; border.width: 2; border.color: Theme.line }
            Rectangle {
                readonly property bool ok: page.okP && page.okR
                x: Math.round(150 + Ring.clamp(-page.lv.roll * 26, -110, 110) - 20)
                y: Math.round(150 + Ring.clamp(-page.lv.pitch * 26, -110, 110) - 20)
                width: 40; height: 40; radius: 20
                color: ok ? "#52A441" : "#FFC107"
                Behavior on x { NumberAnimation { duration: 350 } }
                Behavior on y { NumberAnimation { duration: 350 } }
                Behavior on color { ColorAnimation { duration: 350 } }
            }
            Text { anchors.horizontalCenter: parent.horizontalCenter; y: 12; text: "FRONT"; color: Theme.tx2; font.pixelSize: 12; font.weight: Font.Bold; font.letterSpacing: 1 }
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10
            Rectangle {
                height: 60
                width: autoRow.implicitWidth + 48
                radius: 30
                color: page.parked ? Theme.acc : Theme.track
                Row {
                    id: autoRow
                    anchors.centerIn: parent
                    spacing: 10
                    Icon { name: page.parked ? "locate" : "lock-closed"; color: page.parked ? Theme.onAcc : Theme.tx2; size: 22; anchors.verticalCenter: parent.verticalCenter }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: !page.parked ? "Park to Level" : page.lv.auto ? "Leveling" : (page.okP && page.okR) ? "Level" : "Auto Level"
                        color: page.parked ? Theme.onAcc : Theme.tx2
                        font.pixelSize: 18
                        font.weight: Font.Bold
                    }
                }
                TapHandler { onTapped: Hmi.autoLevel() }
            }
            Rectangle {
                height: 60
                width: jackText.implicitWidth + 40
                radius: 30
                color: Theme.track
                Text { id: jackText; anchors.centerIn: parent; text: "Jacks " + page.lv.jacks; color: Theme.tx; font.pixelSize: 16; font.weight: Font.Bold }
                TapHandler { onTapped: if (page.lv.jacks === "Down") Hmi.retract() }
            }
        }
    }

    Axis {
        x: parent.width - page.sideW
        width: page.sideW
        anchors.verticalCenter: parent.verticalCenter
        mirrored: true
        axis: "roll"; name: "Roll"; value: page.f(page.lv.roll)
        note: page.okR ? "Level" : (page.lv.roll > 0 ? "Tilted right" : "Tilted left")
        valueColor: page.okR ? Theme.acc : Theme.warn
    }
}
