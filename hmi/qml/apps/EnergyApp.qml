import QtQuick
import Godwit

// Battery level is the biggest number, with time remaining beside it. On the
// head unit both come from Solstice on the TrailCurrent bus.
AppPage {
    id: page
    readonly property var en: Hmi.energy
    readonly property bool live: !!en.live
    readonly property bool known: en.soc >= 0
    readonly property int loads: 120 + en.circuits.reduce(function (a, c) { return a + (c.on ? c.w : 0); }, 0)
    // Hours left: Solstice's own estimate, or the demo's from its loads
    readonly property real hrs: live ? (en.ttg >= 0 ? en.ttg / 60 : -1) : en.soc * 2.8 / (loads / 12.8)
    readonly property color socCol: Hmi.lowBattery ? Theme.heat : Theme.acc
    function dash(ok, text) { return ok ? text : "\u2014"; }

    Column {
        anchors.left: parent.left
        anchors.right: list.left
        anchors.rightMargin: 28
        anchors.verticalCenter: parent.verticalCenter
        spacing: 10

        Text { text: "Battery Level"; color: Theme.tx2; font.pixelSize: 17; font.weight: Font.DemiBold }
        Row {
            spacing: 16
            Num { id: soc; text: page.known ? page.en.soc + "%" : "\u2014"; color: page.known ? page.socCol : Theme.tx2; font.pixelSize: 120; font.letterSpacing: -4; lineHeight: 1 }
            Text {
                anchors.baseline: soc.baseline
                text: page.live && !page.en.fresh ? "Solstice is not reporting"
                    : page.hrs >= 0 ? Math.floor(page.hrs) + "h " + Math.round((page.hrs % 1) * 60) + "m remaining"
                    : page.en.amps > 0 ? "Charging" : ""
                color: Theme.tx2
                font.pixelSize: 20
            }
        }
        Rectangle {
            width: parent.width; height: 24; radius: 12
            color: Theme.track
            Rectangle {
                width: parent.width * Math.max(0, page.en.soc) / 100; height: parent.height; radius: 12
                color: page.socCol
                Behavior on width { NumberAnimation { duration: 300 } }
            }
        }
        Item { width: 1; height: 4 }
        Row {
            width: parent.width
            Repeater {
                model: page.live ? [
                    { l: "Solar Input", v: page.dash(page.en.fresh && page.en.solar, page.en.solarW + " W"), warn: false },
                    { l: "Battery", v: page.dash(page.en.fresh && page.en.watts !== undefined, page.en.watts + " W"), warn: false },
                    { l: "Charge Status", v: page.dash(page.en.fresh && page.en.charge !== "", page.en.charge), warn: page.en.charge === "Fault" }
                ] : [
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
