import QtQuick
import Godwit

// Battery level is the biggest number, with time remaining beside it
AppPage {
    id: page
    readonly property var en: Hmi.energy
    readonly property int loads: 120 + en.circuits.reduce(function (a, c) { return a + (c.on ? c.w : 0); }, 0)
    readonly property real hrs: en.soc * 2.8 / (loads / 12.8)
    readonly property color socCol: Hmi.lowBattery ? Theme.heat : Theme.acc

    Column {
        anchors.left: parent.left
        anchors.right: list.left
        anchors.rightMargin: 28
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10

        Text { text: "Battery Level"; color: Theme.tx2; font.pixelSize: 17; font.weight: Font.DemiBold }
        Row {
            spacing: 16
            Num { id: soc; text: page.en.soc + "%"; color: page.socCol; font.pixelSize: 120; font.letterSpacing: -4; lineHeight: 1 }
            Text {
                anchors.baseline: soc.baseline
                text: Math.floor(page.hrs) + "h " + Math.round((page.hrs % 1) * 60) + "m remaining"
                color: Theme.tx2
                font.pixelSize: 20
            }
        }
        Rectangle {
            width: parent.width; height: 24; radius: 12
            color: Theme.track
            Rectangle {
                width: parent.width * page.en.soc / 100; height: parent.height; radius: 12
                color: page.socCol
                Behavior on width { NumberAnimation { duration: 300 } }
            }
        }
        Item { width: 1; height: 4 }
        Row {
            width: parent.width
            Repeater {
                model: [
                    { l: "Solar Input", v: "450 W", warn: true },
                    { l: "Loads", v: page.loads + " W", warn: false },
                    { l: "Charge Status", v: page.en.soc >= 95 ? "Float" : page.en.soc >= 80 ? "Absorption" : "Bulk", warn: false }
                ]
                delegate: Column {
                    required property var modelData
                    width: parent.width / 3
                    Text { text: modelData.l; color: Theme.tx2; font.pixelSize: 14; font.weight: Font.DemiBold }
                    Num { text: modelData.v; color: modelData.warn ? Theme.warn : Theme.tx; font.pixelSize: 30 }
                }
            }
        }
    }

    ListRows {
        id: list
        app: "energy"
        anchors.right: parent.right
        width: 400
        height: parent.height
    }
}
