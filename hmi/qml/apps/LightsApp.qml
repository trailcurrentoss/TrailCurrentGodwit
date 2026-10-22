import QtQuick
import Godwit

// Lit zones fill green; the detail panel shows the zone being edited
AppPage {
    id: page
    readonly property var zones: Hmi.lights.zones
    readonly property int sel: Hmi.rsel.lights || 0
    readonly property var cur: zones[sel]

    Grid {
        id: grid
        anchors.left: parent.left
        anchors.right: detail.left
        anchors.rightMargin: 24
        height: parent.height
        columns: 3
        spacing: 12
        Repeater {
            model: page.zones.length
            delegate: Card {
                id: tile
                required property int index
                readonly property var zone: page.zones[index]
                width: (grid.width - 24) / 3
                height: (grid.height - 12) / 2
                color: z.on ? Theme.accSoft : Theme.card
                shadow: !z.on && Theme.shadows
                selected: index === page.sel
                inset: true
                Icon { x: 18; y: 16; name: tile.zone.on ? "bulb" : "bulb-outline"; color: tile.zone.on ? Theme.acc : Theme.tx2; size: 32 }
                Column {
                    x: 18
                    width: parent.width - 36
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 16
                    spacing: 2
                    Text { text: tile.zone.n; color: Theme.tx; font.pixelSize: 21; font.weight: Font.Bold }
                    Num { text: tile.zone.on ? tile.zone.b + "%" : "Off"; color: Theme.tx2; font.pixelSize: 16 }
                    Item { width: 1; height: 6 }
                    Rectangle {
                        width: parent.width; height: 5; radius: 3
                        color: Theme.track
                        Rectangle {
                            width: parent.width * (tile.zone.on ? tile.zone.b : 0) / 100; height: parent.height; radius: 3
                            color: Theme.acc
                            Behavior on width { NumberAnimation { duration: 200 } }
                        }
                    }
                }
                TapHandler { onTapped: { Hmi.selectRow("lights", tile.index); Hmi.toggleZone(tile.index); } }
            }
        }
    }

    Card {
        id: detail
        anchors.right: parent.right
        width: 290
        height: parent.height
        selected: Hmi.edit
        Column {
            x: 22; y: 22
            width: parent.width - 44
            spacing: 8
            Text { text: page.cur.n; color: Theme.tx; font.pixelSize: 28; font.weight: Font.Bold; font.letterSpacing: -0.4 }
            Num { text: page.cur.on ? page.cur.b + "%" : "Off"; font.pixelSize: 72; font.letterSpacing: -2 }
            Item { width: 1; height: 2 }
            Rectangle {
                width: parent.width; height: 10; radius: 5
                color: Theme.track
                Rectangle {
                    width: parent.width * (page.cur.on ? page.cur.b : 0) / 100; height: parent.height; radius: 5
                    color: Theme.acc
                    Behavior on width { NumberAnimation { duration: 200 } }
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                topPadding: 6
                text: Hmi.edit ? "Turn the right knob to set brightness. Press to finish." : "Hold the right knob, or use - / +, to set brightness."
                color: Theme.tx2
                font.pixelSize: 15
            }
            // Brightness by touch, in the same 5% steps as the knob
            Row {
                spacing: 12
                topPadding: 6
                Repeater {
                    model: [-1, 1]
                    delegate: Rectangle {
                        id: dimBtn
                        required property int modelData
                        width: 64; height: 52; radius: 26
                        color: dimTap.pressed ? Theme.accSoft : Theme.track
                        Icon { anchors.centerIn: parent; name: dimBtn.modelData < 0 ? "chevron-back" : "chevron-forward"; size: 22 }
                        TapHandler { id: dimTap; onTapped: Hmi.rows("lights")[page.sel].adj(dimBtn.modelData) }
                    }
                }
            }
        }
        Rectangle {
            x: 22
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 22
            width: parent.width - 44; height: 56; radius: 28
            color: Theme.track
            Text { anchors.centerIn: parent; text: "All Off"; color: Theme.tx; font.pixelSize: 17; font.weight: Font.Bold }
            TapHandler { onTapped: Hmi.upd("lights", function (x) { return { zones: x.zones.map(function (q) { return Object.assign({}, q, { on: false }); }) }; }) }
        }
    }
}
