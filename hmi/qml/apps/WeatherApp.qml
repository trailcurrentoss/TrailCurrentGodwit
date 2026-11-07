import QtQuick
import Godwit

// Weather Radio: the frequency is the glance target, the seven channels stay in
// the same order on the right, and the right knob moves through them. The broadcast
// does not say who it is, so the name is that of the nearest transmitter on the
// channel.
AppPage {
    id: page
    readonly property var rows: Hmi.wxRows
    readonly property int sel: Hmi.wxSel
    readonly property var cur: rows[sel] || ({})
    readonly property bool heard: Hmi.wxHeard

    function barsOf(s) { return Math.round((s || 0) * 5); }

    Item {
        id: head
        anchors.left: parent.left
        anchors.right: main.right
        height: 52
        Rectangle {
            id: tile
            width: 52; height: 52; radius: 14
            color: Theme.accSoft
            Icon { anchors.centerIn: parent; name: "partly-sunny"; color: Theme.acc; size: 28 }
        }
        Column {
            anchors.left: tile.right
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            Text { text: "Weather Radio"; color: Theme.tx; font.pixelSize: 26; font.weight: Font.Bold }
            Text { text: "Spoken forecasts"; color: Theme.tx2; font.pixelSize: 15; font.weight: Font.DemiBold }
        }
        Rectangle {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: strongRow.implicitWidth + 40
            height: 46
            radius: 23
            color: Theme.acc
            Row {
                id: strongRow
                anchors.centerIn: parent
                spacing: 8
                Icon { name: "scan-outline"; color: Theme.onAcc; size: 18; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Strongest"; color: Theme.onAcc; font.pixelSize: 17; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
            }
            TapHandler { onTapped: Hmi.wxStrongest() }
        }
    }

    Card {
        id: main
        anchors.left: parent.left
        anchors.right: side.left
        anchors.rightMargin: 24
        anchors.top: head.bottom
        anchors.topMargin: 14
        anchors.bottom: parent.bottom

        Rectangle {
            x: 14; y: 14
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
                    color: page.heard ? Theme.acc : Theme.warn
                }
                Text {
                    text: Hmi.wxStatus()
                    color: Theme.tx
                    font.pixelSize: 14
                    font.weight: Font.Bold
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
        Row {
            anchors.right: parent.right
            anchors.rightMargin: 18
            y: 18
            spacing: 3
            height: 22
            Repeater {
                model: 5
                delegate: Rectangle {
                    required property int index
                    anchors.bottom: parent.bottom
                    width: 6
                    height: 4 + (index + 1) * 3.5
                    radius: 3
                    color: index < page.barsOf(page.cur.s) ? Theme.acc : Theme.track
                }
            }
        }
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: 14
            spacing: 2
            Row {
                spacing: 8
                Num { id: fq; text: page.cur.f || ""; font.pixelSize: 84; font.letterSpacing: -2; lineHeight: 1 }
                Text { anchors.baseline: fq.baseline; text: "MHz"; color: Theme.tx2; font.pixelSize: 22 }
            }
            Text {
                width: parent.width
                elide: Text.ElideRight
                text: Hmi.wxTitle()
                color: Theme.tx
                font.pixelSize: 28
                font.weight: Font.Bold
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: Hmi.wxHint()
                color: Theme.tx2
                font.pixelSize: 17
                font.weight: Font.DemiBold
            }
        }
    }

    Card {
        id: side
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 380

        Text {
            id: listHead
            x: 20; y: 14
            text: "CHANNELS · " + (page.sel + 1) + " OF " + page.rows.length
            color: Theme.tx2
            font.pixelSize: 13
            font.weight: Font.Bold
            font.letterSpacing: 1
        }
        Column {
            id: list
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            anchors.top: listHead.bottom
            anchors.topMargin: 6
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 8
            spacing: 1
            Repeater {
                model: page.rows.length
                delegate: Rectangle {
                    id: row
                    required property int index
                    readonly property var a: page.rows[index] || ({})
                    readonly property bool on: index === page.sel
                    width: list.width
                    height: (list.height - 6) / 7
                    radius: 12
                    color: on ? Theme.accSoft : "transparent"
                    Num {
                        id: rowF
                        x: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.a.f || ""
                        color: Theme.tx
                        font.pixelSize: 20
                        font.weight: Font.Bold
                    }
                    Text {
                        anchors.left: rowF.right
                        anchors.leftMargin: 12
                        anchors.right: bars.left
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight
                        text: row.a.n ? row.a.n + " · " + Hmi.wxDist(row.a.mi) : "--"
                        color: Theme.tx2
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }
                    Row {
                        id: bars
                        anchors.right: parent.right
                        anchors.rightMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        height: 18
                        Repeater {
                            model: 5
                            delegate: Rectangle {
                                required property int index
                                anchors.bottom: parent.bottom
                                width: 4
                                height: 4 + (index + 1) * 2.8
                                radius: 2
                                color: index < page.barsOf(row.a.s) ? Theme.acc : Theme.track
                            }
                        }
                    }
                    TapHandler { onTapped: Hmi.wxTune(row.index) }
                }
            }
        }
    }
}
