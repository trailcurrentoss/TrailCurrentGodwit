import QtQuick
import Godwit

// Lit zones fill green; the detail panel shows the zone being edited.
// On the head unit the zones are the TrailCurrent bus's lights: as many as
// its modules have, so the grid grows a column or two and scrolls.
AppPage {
    id: page
    readonly property var zones: Hmi.lights.zones
    readonly property int sel: Math.min(Hmi.rsel.lights || 0, Math.max(0, zones.length - 1))
    readonly property var cur: zones.length ? zones[sel] : null
    // Brightness can be set: always in the simulator, only on dimmable outputs on the bus
    readonly property bool curDims: !!cur && (!Hmi.tcLive || cur.dim)
    readonly property bool curOk: !!cur && (!Hmi.tcLive || cur.fresh)
    function shown(z) { return Hmi.tcLive && !z.fresh ? "No data" : !z.on ? "Off" : Hmi.tcLive && !z.dim ? "On" : z.b + "%"; }

    Flickable {
        id: flick
        anchors.left: parent.left
        anchors.right: detail.left
        anchors.rightMargin: 24
        height: parent.height
        visible: page.zones.length > 0
        clip: true
        contentWidth: width
        contentHeight: grid.height
        boundsBehavior: Flickable.StopAtBounds

        // Keep the knob's selection in view
        readonly property real tileH: grid.tileH
        function reveal() {
            var row = Math.floor(page.sel / grid.columns), top = row * (tileH + grid.spacing);
            if (top < contentY) contentY = top;
            else if (top + tileH > contentY + height) contentY = top + tileH - height;
        }
        Connections { target: page; function onSelChanged() { flick.reveal(); } }

        Grid {
            id: grid
            readonly property int n: page.zones.length
            columns: n <= 6 ? 3 : n <= 12 ? 4 : 5
            readonly property int rowsN: Math.ceil(n / columns)
            // Up to three rows fill the page; more scroll, at a height that stays easy to tap
            readonly property real tileH: Math.max(128, (flick.height - spacing * (Math.min(rowsN, 3) - 1)) / Math.max(2, Math.min(rowsN, 3)))
            width: flick.width
            spacing: 12
            Repeater {
                model: page.zones.length
                delegate: Card {
                    id: tile
                    required property int index
                    readonly property var zone: page.zones[index]
                    readonly property bool ok: !Hmi.tcLive || zone.fresh
                    width: (grid.width - grid.spacing * (grid.columns - 1)) / grid.columns
                    height: grid.tileH
                    color: zone.on && ok ? Theme.accSoft : Theme.card
                    shadow: !zone.on && Theme.shadows
                    selected: index === page.sel
                    inset: true
                    opacity: ok ? 1 : 0.55
                    Icon { x: 18; y: 16; name: tile.zone.on ? "bulb" : "bulb-outline"; color: tile.zone.on && tile.ok ? Theme.acc : Theme.tx2; size: 32 }
                    Column {
                        x: 18
                        width: parent.width - 36
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 16
                        spacing: 2
                        Text { width: parent.width; text: tile.zone.n; color: Theme.tx; font.pixelSize: 21; font.weight: Font.Bold; elide: Text.ElideRight }
                        Num { text: page.shown(tile.zone); color: Theme.tx2; font.pixelSize: 16 }
                        Item { width: 1; height: 6 }
                        Rectangle {
                            width: parent.width; height: 5; radius: 3
                            color: Theme.track
                            Rectangle {
                                width: parent.width * (tile.zone.on && tile.ok ? tile.zone.b : 0) / 100; height: parent.height; radius: 3
                                color: Theme.acc
                                Behavior on width { NumberAnimation { duration: 200 } }
                            }
                        }
                    }
                    TapHandler { onTapped: { Hmi.selectRow("lights", tile.index); Hmi.toggleZone(tile.index); } }
                }
            }
        }
    }

    // The bus has no lights yet, or none shown
    Card {
        visible: page.zones.length === 0
        anchors.left: parent.left
        anchors.right: detail.left
        anchors.rightMargin: 24
        height: parent.height
        Column {
            anchors.centerIn: parent
            width: parent.width - 80
            spacing: 10
            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "bulb-outline"; color: Theme.tx2; size: 48 }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: TrailCurrent.lights.length ? "All lights are hidden" : "No TrailCurrent lights found"
                color: Theme.tx; font.pixelSize: 24; font.weight: Font.Bold
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: TrailCurrent.lights.length ? "Show them in Settings › TrailCurrent › Lights."
                    : !TrailCurrent.ready ? "The TrailCurrent messages are not installed on this head unit."
                    : !TrailCurrent.iface ? TrailCurrent.blocked + ". See Settings › CAN Buses."
                    : "Lights appear here once their Torrent or Switchback module is heard on the bus."
                color: Theme.tx2; font.pixelSize: 16
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
            visible: !!page.cur
            x: 22; y: 22
            width: parent.width - 44
            spacing: 8
            Text { width: parent.width; text: page.cur ? page.cur.n : ""; color: Theme.tx; font.pixelSize: 28; font.weight: Font.Bold; font.letterSpacing: -0.4; elide: Text.ElideRight }
            Num { text: page.cur ? page.shown(page.cur) : ""; font.pixelSize: page.curOk ? 72 : 44; font.letterSpacing: -2 }
            Item { width: 1; height: 2 }
            Rectangle {
                width: parent.width; height: 10; radius: 5
                color: Theme.track
                Rectangle {
                    width: parent.width * (page.cur && page.cur.on && page.curOk ? page.cur.b : 0) / 100; height: parent.height; radius: 5
                    color: Theme.acc
                    Behavior on width { NumberAnimation { duration: 200 } }
                }
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                topPadding: 6
                text: !page.curOk ? "Its module is not reporting."
                    : !page.curDims ? "Press the right knob, or tap the tile, to switch it."
                    : Hmi.edit ? "Turn the right knob to set brightness. Press to finish."
                    : "Hold the right knob, or use - / +, to set brightness."
                color: Theme.tx2
                font.pixelSize: 15
            }
            // Brightness by touch, in the same 5% steps as the knob
            Row {
                visible: page.curDims && page.curOk
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
            visible: page.zones.length > 0
            x: 22
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 22
            width: parent.width - 44; height: 56; radius: 28
            color: Theme.track
            Text { anchors.centerIn: parent; text: "All Off"; color: Theme.tx; font.pixelSize: 17; font.weight: Font.Bold }
            TapHandler { onTapped: Hmi.allLightsOff() }
        }
    }
}
