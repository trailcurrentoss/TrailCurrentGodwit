import QtQuick
import Godwit
import "../DemoData.js" as D

// All apps in one 6-column grid; the right knob moves the highlight
AppPage {
    enterMs: 200
    Rectangle { anchors.fill: parent; color: Theme.bg }

    Grid {
        id: grid
        anchors.fill: parent
        anchors.topMargin: 22
        anchors.bottomMargin: 22
        anchors.leftMargin: 28
        anchors.rightMargin: 28
        columns: 6
        columnSpacing: 14
        rowSpacing: 14
        verticalItemAlignment: Grid.AlignVCenter

        Repeater {
            model: D.APPS.length
            delegate: Item {
                id: cell
                required property int index
                readonly property var a: D.APPS[index]
                readonly property bool on: index === Hmi.lsel
                width: (grid.width - 5 * grid.columnSpacing) / 6
                height: (grid.height - grid.rowSpacing) / 2

                Card {
                    id: tile
                    width: 88; height: 88; radius: 26
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: (parent.height - height - 10 - label.height) / 2
                    color: cell.on ? Theme.acc : Theme.card
                    Icon { anchors.centerIn: parent; name: cell.a.icon; color: cell.on ? Theme.onAcc : Theme.tx; size: 38 }
                }
                Text {
                    id: label
                    anchors.top: tile.bottom
                    anchors.topMargin: 10
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: cell.a.name
                    color: Theme.tx
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }
                TapHandler { onTapped: Hmi.openApp(cell.a.id) }
            }
        }
    }
}
