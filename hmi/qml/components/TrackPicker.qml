import QtQuick
import Godwit

// The Tracks list: every road course, oval and drag strip, nearest first.
// Type chips filter it, the search field narrows it (typing only in Park,
// like every other keyboard), and the sort button cycles nearest, A–Z and
// longest. The right knob moves the highlighted row and presses to choose.
Card {
    id: picker
    radius: 22
    large: true
    readonly property bool parked: Hmi.gear === "P"
    readonly property bool searching: Hmi.trackUi.search && parked
    readonly property var rows: Tracks.results

    function sortText() { return { nearest: "Nearest first", name: "A–Z", length: "Longest first" }[Tracks.sort]; }
    function rowLine(t) {
        var len = t.type === "drag" ? (t.length_m < 300 ? "1/8 mi strip" : "1/4 mi strip")
                : (t.length_m / 1609.344).toFixed(2) + " mi " + (t.type === "oval" ? "oval" : "road course");
        return [[t.place, t.state].filter(Boolean).join(", "), len].filter(Boolean).join(" · ");
    }

    // Header: title, type chips, search, sort, done
    Row {
        id: head
        x: 18; y: 14
        spacing: 10
        Text { text: "Tracks"; color: Theme.tx; font.pixelSize: 22; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
        Repeater {
            model: [["", "All"], ["road", "Road Courses"], ["oval", "Ovals"], ["drag", "Drag Strips"]]
            delegate: Rectangle {
                id: chip
                required property var modelData
                readonly property bool on: Tracks.type === modelData[0]
                height: 36; radius: 18
                width: chipText.implicitWidth + 28
                color: on ? Theme.acc : Theme.track
                Behavior on color { ColorAnimation { duration: 160 } }
                Text { id: chipText; anchors.centerIn: parent; text: chip.modelData[1]; color: chip.on ? Theme.onAcc : Theme.tx; font.pixelSize: 14; font.weight: Font.DemiBold }
                TapHandler { onTapped: Hmi.trackType(chip.modelData[0]) }
            }
        }
    }
    Row {
        anchors.right: parent.right; anchors.rightMargin: 16
        y: 14
        spacing: 8
        // Search: shows what was typed; typing only in Park
        Rectangle {
            height: 36; radius: 18
            width: 170
            color: picker.searching ? Theme.accSoft : Theme.track
            Row {
                x: 12; anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Icon { name: "search"; size: 16; color: Theme.tx2; anchors.verticalCenter: parent.verticalCenter }
                Text {
                    width: 120
                    text: Tracks.query !== "" ? Tracks.query : picker.parked ? "Search" : "Search in Park"
                    color: Tracks.query !== "" ? Theme.tx : Theme.tx2
                    font.pixelSize: 14; elide: Text.ElideLeft
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            TapHandler {
                onTapped: {
                    if (!picker.parked) { Hmi.toast("Shift to Park to search"); return; }
                    Hmi.upd("trackUi", { search: !Hmi.trackUi.search });
                }
            }
        }
        Rectangle {
            height: 36; radius: 18
            width: sortRow.implicitWidth + 24
            color: Theme.track
            Row {
                id: sortRow
                anchors.centerIn: parent
                spacing: 6
                Icon { name: "swap-vertical"; size: 15; color: Theme.tx2; anchors.verticalCenter: parent.verticalCenter }
                Text { text: picker.sortText(); color: Theme.tx; font.pixelSize: 13; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
            }
            TapHandler { onTapped: Hmi.trackSort() }
        }
        Rectangle {
            height: 36; radius: 18; width: 72
            color: Theme.track
            Text { anchors.centerIn: parent; text: "Done"; color: Theme.tx; font.pixelSize: 14; font.weight: Font.DemiBold }
            TapHandler { onTapped: Hmi.trackPicker(false) }
        }
    }

    ListView {
        id: list
        x: 12
        y: head.y + 46
        width: parent.width - 24
        height: (picker.searching ? kb.y - 8 : parent.height - 10) - y
        clip: true
        model: picker.rows
        currentIndex: Hmi.trackUi.sel
        onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            readonly property bool sel: index === Hmi.trackUi.sel
            readonly property bool chosen: (Tracks.current || {}).id === modelData.id
            width: ListView.view.width
            height: 56
            radius: 14
            color: sel ? Theme.accSoft : "transparent"
            Behavior on color { ColorAnimation { duration: 120 } }
            Icon {
                x: 14; anchors.verticalCenter: parent.verticalCenter
                name: row.modelData.type === "drag" ? "speedometer" : "flag"
                size: 22; color: row.chosen ? Theme.acc : Theme.tx
            }
            Column {
                x: 50; anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 50 - 110
                Text { width: parent.width; text: row.modelData.name; color: row.chosen ? Theme.acc : Theme.tx; font.pixelSize: 17; font.weight: Font.Bold; elide: Text.ElideRight }
                Text { width: parent.width; text: picker.rowLine(row.modelData); color: Theme.tx2; font.pixelSize: 13; elide: Text.ElideRight }
            }
            Num {
                anchors.right: parent.right; anchors.rightMargin: 16
                anchors.verticalCenter: parent.verticalCenter
                text: row.modelData.mi >= 0 ? (row.modelData.mi < 10 ? row.modelData.mi.toFixed(1) : Math.round(row.modelData.mi)) + " mi" : ""
                color: Theme.tx2; font.pixelSize: 15; font.weight: Font.DemiBold
            }
            TapHandler { onTapped: Hmi.trackPick(row.modelData.id) }
        }
    }
    Text {
        visible: picker.rows.length === 0
        anchors.centerIn: list
        text: Tracks.available ? "No tracks match" : "No track data installed"
        color: Theme.tx2; font.pixelSize: 16
    }

    Keyboard {
        id: kb
        visible: picker.searching
        x: 16; width: parent.width - 32
        height: 190
        anchors.bottom: parent.bottom; anchors.bottomMargin: 14
        enterLabel: "Done"
        onKey: (ch) => { Tracks.query = Tracks.query + ch; Hmi.upd("trackUi", { sel: 0 }); }
        onBackspace: { Tracks.query = Tracks.query.slice(0, -1); Hmi.upd("trackUi", { sel: 0 }); }
        onEnter: Hmi.upd("trackUi", { search: false })
    }
}
