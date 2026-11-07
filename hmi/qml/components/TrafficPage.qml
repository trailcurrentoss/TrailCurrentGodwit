import QtQuick
import Godwit

// What the tuner hears around the vehicle, on a scope: north up, the vehicle at
// the centre. The right knob moves through them; pressing it changes the range.
// The Aircraft and Ships apps differ only in the properties below.
AppPage {
    id: page
    // The app this page is, and what to call things in it
    property string appId
    property string icon
    property string title
    property string frequency
    property string noun
    property string emptyTitle
    // The first two tiles for the one chosen, which may be null: [{ l, v }, { l, v }]
    property var tiles: []
    // How long since a position was heard before it is shown as where it was, not
    // where it is
    property int staleMs: 20000

    // Pages stay loaded once visited, and only the app that is open has rows
    readonly property var rows: Hmi.app === appId ? Hmi.trackRows : []
    readonly property int sel: Hmi.trackSel
    readonly property var cur: rows.length ? rows[sel] || null : null
    readonly property var trk: Hmi.app === appId ? Hmi.track : ({ range: 1, list: false })
    readonly property int range: trk.range
    readonly property bool located: Hmi.trackLocated
    readonly property bool parked: Hmi.gear === "P"
    readonly property bool listing: trk.list && parked

    function fmtRing(nm) { return (nm % 1 ? nm.toFixed(1) : nm) + " nm"; }

    // ------------------------------------------------------------ header
    Item {
        id: head
        anchors.left: parent.left
        anchors.right: scopeCard.right
        height: 52

        Rectangle {
            id: tile
            width: 52; height: 52; radius: 14
            color: Theme.accSoft
            Icon { anchors.centerIn: parent; name: page.icon; color: Theme.acc; size: 28 }
        }
        Column {
            anchors.left: tile.right
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            Text { text: page.title; color: Theme.tx; font.pixelSize: 26; font.weight: Font.Bold }
            Num { text: page.frequency; color: Theme.tx2; font.pixelSize: 15; font.weight: Font.DemiBold }
        }
        Rectangle {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: rangeRow.implicitWidth + 40
            height: 46
            radius: 23
            color: Theme.acc
            Row {
                id: rangeRow
                anchors.centerIn: parent
                spacing: 8
                Icon { name: "scan-outline"; color: Theme.onAcc; size: 18; anchors.verticalCenter: parent.verticalCenter }
                Num { text: "Range " + page.range + " nm"; color: Theme.onAcc; font.pixelSize: 17; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
            }
            TapHandler { onTapped: Hmi.trackNextRange() }
        }
    }

    // ------------------------------------------------------------ scope
    Card {
        id: scopeCard
        anchors.left: parent.left
        anchors.right: side.left
        anchors.rightMargin: 24
        anchors.top: head.bottom
        anchors.topMargin: 14
        anchors.bottom: parent.bottom

        Rectangle {
            x: 14; y: 14
            z: 2
            width: stateRow.implicitWidth + 24
            height: 30
            radius: 15
            color: Theme.track
            Row {
                id: stateRow
                anchors.centerIn: parent
                spacing: 8
                Rectangle {
                    width: 10; height: 10; radius: 5
                    anchors.verticalCenter: parent.verticalCenter
                    color: Hmi.trackListening ? Theme.acc : Theme.warn
                }
                Num {
                    text: Hmi.trackStatus()
                    color: Theme.tx
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }

        Item {
            id: scope
            readonly property real radius: (parent.height - 44) / 2
            readonly property real pxPerNm: radius / page.range
            anchors.horizontalCenter: parent.horizontalCenter
            y: 32
            width: radius * 2
            height: radius * 2

            Repeater {
                model: 3
                delegate: Rectangle {
                    required property int index
                    readonly property real r: scope.radius * (index + 1) / 3
                    x: scope.radius - r
                    y: scope.radius - r
                    width: r * 2; height: r * 2; radius: r
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.line
                }
            }
            Rectangle {
                x: scope.radius
                width: 1
                height: parent.height
                color: Theme.line
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.top
                anchors.bottomMargin: 3
                text: "N"
                color: Theme.tx2
                font.pixelSize: 13
                font.weight: Font.Bold
            }
            Repeater {
                model: 3
                delegate: Num {
                    required property int index
                    readonly property real r: scope.radius * (3 - index) / 3
                    x: scope.radius + 5
                    y: scope.radius - r + 5
                    text: page.fmtRing(page.range * (3 - index) / 3)
                    color: Theme.tx2
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                }
            }

            // Each aircraft in range. Only so many are named, nearest first, or
            // a busy sky at long range would be unreadable.
            Repeater {
                model: page.located ? page.rows.length : 0
                delegate: Item {
                    id: blip
                    required property int index
                    readonly property var a: page.rows[index] || ({})
                    readonly property bool on: index === page.sel
                    readonly property real rad: (a.brg || 0) * Math.PI / 180
                    readonly property real nm: a.dist === undefined ? Infinity : a.dist
                    readonly property real px: scope.radius + Math.sin(rad) * nm * scope.pxPerNm
                    readonly property real py: scope.radius - Math.cos(rad) * nm * scope.pxPerNm
                    // Past the last ring, or still to send where it is
                    visible: nm <= page.range
                    // Not heard from for a while: where it was, not where it is
                    readonly property bool stale: a.at !== undefined && Hmi.clock - a.at > page.staleMs
                    z: on ? 2 : 1
                    x: px - width / 2
                    y: py - height / 2
                    width: 44; height: 44

                    Rectangle {
                        visible: blip.on
                        anchors.centerIn: parent
                        width: 32; height: 32; radius: 16
                        color: Theme.accSoft
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: blip.on ? 16 : 12
                        height: width
                        radius: width / 2
                        color: blip.on ? Theme.acc : Theme.tx
                        opacity: blip.stale ? 0.4 : 1
                    }
                    Num {
                        visible: blip.on || blip.index < 10
                        // To the other side if it would run off the card or over the vehicle
                        readonly property bool flip: blip.px + implicitWidth + 30 > scopeCard.width - scope.x
                            || (page.located && blip.px < scope.radius && blip.px + implicitWidth + 30 > scope.radius && Math.abs(blip.py - scope.radius) < 16)
                        anchors.verticalCenter: parent.verticalCenter
                        x: flip ? -implicitWidth + 8 : parent.width - 8
                        text: blip.a.name || ""
                        color: blip.on ? Theme.acc : Theme.tx2
                        opacity: blip.stale ? 0.6 : 1
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    TapHandler { onTapped: Hmi.trackSelect(blip.index) }
                }
            }

            // The vehicle
            Rectangle {
                visible: page.located
                anchors.centerIn: parent
                width: 14; height: 14; radius: 2
                rotation: 45
                color: Theme.tx
            }
        }
    }

    // ------------------------------------------------------------ side card
    Card {
        id: side
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 340

        // Details of the one chosen
        Item {
            anchors.fill: parent
            anchors.margins: 20
            visible: !page.listing

            Text {
                id: eyebrow
                text: page.cur ? page.noun + " · " + (page.sel + 1) + " OF " + page.rows.length : page.noun
                color: Theme.tx2
                font.pixelSize: 13
                font.weight: Font.Bold
                font.letterSpacing: 1
            }
            Num {
                id: callsign
                anchors.top: eyebrow.bottom
                anchors.topMargin: 10
                width: parent.width
                text: page.cur ? page.cur.name : page.emptyTitle
                color: Theme.tx
                // A ship's name can be 20 characters
                height: 44
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 22
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: page.cur ? 36 : 28
                font.weight: Font.Bold
                font.letterSpacing: -0.4
            }
            Text {
                id: kind
                anchors.top: callsign.bottom
                width: parent.width
                elide: Text.ElideRight
                text: page.cur ? page.cur.sub : Hmi.trackHint()
                wrapMode: page.cur ? Text.NoWrap : Text.WordWrap
                color: Theme.tx2
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }
            Grid {
                id: tiles
                anchors.top: kind.bottom
                anchors.topMargin: 16
                anchors.bottom: foot.top
                anchors.bottomMargin: 12
                width: parent.width
                columns: 2
                spacing: 10
                visible: !!page.cur
                Repeater {
                    model: page.tiles.concat([
                        { l: "DISTANCE", v: page.cur && page.cur.dist !== undefined ? page.cur.dist.toFixed(1) + " nm" : "--" },
                        { l: "BEARING", v: page.cur && page.cur.brg !== undefined ? Math.round(page.cur.brg) % 360 + "°" : "--" }
                    ])
                    delegate: Rectangle {
                        required property var modelData
                        width: (tiles.width - 10) / 2
                        height: (tiles.height - 10) / 2
                        radius: 16
                        color: Theme.track
                        Column {
                            x: 14
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 4
                            Text { text: modelData.l; color: Theme.tx2; font.pixelSize: 13; font.weight: Font.DemiBold; font.letterSpacing: 0.6 }
                            Num { text: modelData.v; color: Theme.tx; font.pixelSize: 24; font.weight: Font.Bold }
                        }
                    }
                }
            }
            Item {
                id: foot
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 26
                // Driving: the one line says why there is no list
                Row {
                    visible: !page.parked
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Icon { name: "car"; color: Theme.tx2; size: 16; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "Driving layout · full list in Park"; color: Theme.tx2; font.pixelSize: 14; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                }
                Row {
                    visible: page.parked && page.rows.length > 0
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 8
                    Icon { name: "list"; color: Theme.acc; size: 18; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "Full list"; color: Theme.acc; font.pixelSize: 16; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
                }
                TapHandler {
                    enabled: page.parked && page.rows.length > 0
                    onTapped: Hmi.upd(Hmi.trackKey, { list: true })
                }
            }
        }

        // In Park: all of them, the nearest first
        Item {
            anchors.fill: parent
            anchors.margins: 12
            visible: page.listing

            Text {
                id: listHead
                x: 8; y: 8
                text: page.noun + " · " + page.rows.length + " HEARD"
                color: Theme.tx2
                font.pixelSize: 13
                font.weight: Font.Bold
                font.letterSpacing: 1
            }
            Rectangle {
                anchors.right: parent.right
                y: -2
                width: 40; height: 40; radius: 20
                color: closeTap.pressed ? Theme.accSoft : Theme.track
                Icon { anchors.centerIn: parent; name: "close"; size: 22 }
                TapHandler { id: closeTap; onTapped: Hmi.upd(Hmi.trackKey, { list: false }) }
            }
            ListView {
                id: rowsView
                anchors.fill: parent
                anchors.topMargin: 46
                clip: true
                spacing: 2
                model: page.rows.length
                currentIndex: page.sel
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                delegate: Rectangle {
                    id: row
                    required property int index
                    readonly property var a: page.rows[index] || ({})
                    readonly property bool on: index === page.sel
                    width: rowsView.width
                    height: 62
                    radius: 14
                    color: on ? Theme.accSoft : "transparent"
                    Column {
                        x: 12
                        width: parent.width - 100
                        anchors.verticalCenter: parent.verticalCenter
                        Num { width: parent.width; text: row.a.name || ""; color: Theme.tx; font.pixelSize: 19; font.weight: Font.Bold }
                        Text { width: parent.width; elide: Text.ElideRight; text: row.a.sub || ""; color: Theme.tx2; font.pixelSize: 14 }
                    }
                    Num {
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.a.dist === undefined ? "--" : row.a.dist.toFixed(1) + " nm"
                        color: row.on ? Theme.acc : Theme.tx
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    TapHandler { onTapped: { Hmi.trackSelect(row.index); Hmi.upd(Hmi.trackKey, { list: false }); } }
                }
            }
        }
    }
}
