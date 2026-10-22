import QtQuick
import Godwit

// The list + edit pattern shared by Energy, Water, Lighting and Settings.
// The right knob moves the selection; touch selects, then a second tap acts.
Card {
    id: root
    property string app

    readonly property var items: Hmi.rows(app)
    readonly property int sel: Hmi.rsel[app] || 0

    Column {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 2

        Repeater {
            model: root.items.length
            delegate: Rectangle {
                id: row
                required property int index
                readonly property var r: root.items[index]
                readonly property bool isSel: index === root.sel
                readonly property bool editing: isSel && Hmi.edit

                width: parent.width
                height: (root.height - 24 - 2 * (root.items.length - 1)) / root.items.length
                radius: 14
                color: isSel ? Theme.accSoft : "transparent"
                Behavior on color { ColorAnimation { duration: 200 } }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    anchors.right: steppers.visible ? steppers.left : value.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.r.n
                    color: Theme.tx
                    font.pixelSize: 17
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                // While editing, - / + adjust the value by touch
                Row {
                    id: steppers
                    visible: row.editing
                    anchors.right: value.left
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Repeater {
                        model: [-1, 1]
                        delegate: Rectangle {
                            id: stepper
                            required property int modelData
                            width: 40; height: 40; radius: 20
                            color: stepTap.pressed ? Theme.accSoft : Theme.card
                            Icon { anchors.centerIn: parent; name: stepper.modelData < 0 ? "chevron-back" : "chevron-forward"; size: 18 }
                            TapHandler { id: stepTap; onTapped: row.r.adj(stepper.modelData) }
                        }
                    }
                }
                Rectangle {
                    id: value
                    anchors.right: sw.visible ? sw.left : parent.right
                    anchors.rightMargin: sw.visible ? 12 : 14
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.r.v !== ""
                    width: valueText.implicitWidth + 20
                    height: valueText.implicitHeight + 8
                    radius: 12
                    color: row.editing ? Theme.acc : "transparent"
                    Num {
                        id: valueText
                        anchors.centerIn: parent
                        text: row.r.v
                        color: row.editing ? Theme.onAcc : Theme.tx2
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                }
                Switch {
                    id: sw
                    visible: row.r.kind !== "val"
                    on: !!row.r.on
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                }
                TapHandler {
                    onTapped: {
                        if (!row.isSel) { Hmi.selectRow(root.app, row.index); return; }
                        if (row.r.kind === "val") Hmi.edit = !Hmi.edit;
                        else row.r.toggle();
                    }
                }
            }
        }
    }
}
