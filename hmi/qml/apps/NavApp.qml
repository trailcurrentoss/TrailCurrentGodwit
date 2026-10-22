import QtQuick
import Godwit
import "../DemoData.js" as D

// The map takes the whole screen. The maneuver card is top-left, nearest the
// driver's line of sight; search is top-right. Everything the right knob does
// to the map also has a button.
AppPage {
    id: page
    readonly property var route: Hmi.navCue()
    readonly property var cur: Hmi.routeActive ? Hmi.steps[Hmi.nav.step] : null
    readonly property var next: Hmi.routeActive ? Hmi.steps[Hmi.nav.step + 1] : null

    // Round map button
    component MapButton: Card {
        id: mb
        property string icon
        property string label
        property color iconColor: Theme.tx
        signal tapped
        width: label ? lbl.implicitWidth + 36 : 56
        height: 56
        radius: 28
        large: true
        Icon { visible: mb.icon !== ""; anchors.centerIn: parent; name: mb.icon; color: mb.iconColor; size: 26 }
        Num { id: lbl; visible: mb.label !== ""; anchors.centerIn: parent; text: mb.label; font.pixelSize: 16; font.weight: Font.Bold }
        TapHandler { onTapped: mb.tapped() }
    }

    Rectangle { anchors.fill: parent; color: Theme.track }

    Loader {
        id: mapLoader
        anchors.fill: parent
        source: "NavMap.qml"
    }
    Text {
        visible: mapLoader.status === Loader.Error || !Config.mapInstalled
        anchors.centerIn: parent
        text: mapLoader.status === Loader.Error ? "Map engine not available" : "No map installed · add a map bundle in Settings"
        color: Theme.tx2
        font.pixelSize: 17
    }

    // Vehicle position
    Rectangle {
        x: parent.width / 2 - 20
        y: parent.height * 0.6 - 20
        width: 40; height: 40; radius: 20
        color: Qt.rgba(82 / 255, 164 / 255, 65 / 255, 0.22)
        Rectangle {
            anchors.centerIn: parent
            width: 24; height: 24; radius: 12
            color: "#52A441"
            border.width: 4
            border.color: "#ffffff"
        }
    }

    // Maneuver card
    Card {
        visible: Hmi.routeActive
        x: 20; y: 56
        width: 380
        height: top.height + (thenRow.visible ? thenRow.height : 0)
        large: true
        radius: 22
        clip: true

        Item {
            id: top
            width: parent.width
            height: 104
            Rectangle {
                x: 16; y: 16
                width: 72; height: 72; radius: 18
                color: page.route.col
                Behavior on color { ColorAnimation { duration: 400 } }
                Icon { anchors.centerIn: parent; name: page.cur ? D.DIRS[page.cur.dir].i : ""; color: "#000000"; size: 44 }
            }
            Column {
                x: 104
                width: parent.width - 120
                anchors.verticalCenter: parent.verticalCenter
                Num { text: Hmi.fd(Hmi.nav.dist); font.pixelSize: 42; font.weight: Font.Bold; font.letterSpacing: -0.8 }
                Text { width: parent.width; text: page.cur ? page.cur.st : ""; color: Theme.tx2; font.pixelSize: 20; font.weight: Font.DemiBold; elide: Text.ElideRight }
            }
        }
        Rectangle {
            id: thenRow
            visible: !!page.next
            anchors.top: top.bottom
            width: parent.width
            height: 46
            color: Theme.track
            Row {
                x: 18
                anchors.verticalCenter: parent.verticalCenter
                spacing: 10
                Text { text: "Then"; color: Theme.tx2; font.pixelSize: 17; anchors.verticalCenter: parent.verticalCenter }
                Icon { name: page.next ? D.DIRS[page.next.dir].i : ""; size: 20; anchors.verticalCenter: parent.verticalCenter }
                Text { text: page.next ? page.next.st : ""; color: Theme.tx; font.pixelSize: 17; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
            }
        }
    }

    // Search
    Card {
        anchors.right: parent.right
        anchors.rightMargin: 20
        y: 56
        width: 300
        height: 56
        radius: 28
        large: true
        Row {
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12
            Icon { name: "search"; size: 22; color: Theme.tx2; anchors.verticalCenter: parent.verticalCenter }
            Text { text: Hmi.routeActive ? "Change destination" : "Where to?"; color: Theme.tx2; font.pixelSize: 18; anchors.verticalCenter: parent.verticalCenter }
        }
        TapHandler { onTapped: Hmi.openSearch() }
    }

    // ETA, destination and End
    Card {
        id: etaCard
        visible: Hmi.routeActive
        readonly property real miles: Hmi.routeMiles()
        readonly property int mins: Math.round(Hmi.routeMins())
        readonly property string eta: { void Hmi.clock; return Hmi.fmtTime(new Date(Date.now() + mins * 60000), true); }
        x: 20
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 92
        width: etaCol.implicitWidth + 40 + 56
        height: 72
        radius: 24
        large: true
        Column {
            id: etaCol
            x: 20
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Row {
                spacing: 12
                Num { id: etaTime; text: etaCard.eta; font.pixelSize: 26; font.weight: Font.Bold }
                Num { anchors.baseline: etaTime.baseline; text: Hmi.fmtMins(etaCard.mins) + " · " + Hmi.fd(etaCard.miles); color: Theme.tx2; font.pixelSize: 17 }
            }
            Text { text: Hmi.dest ? "to " + Hmi.dest.n : ""; color: Theme.tx2; font.pixelSize: 15; font.weight: Font.DemiBold }
        }
        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 48; height: 48; radius: 24
            color: Theme.heat
            Icon { anchors.centerIn: parent; name: "close"; color: Theme.onAlert; size: 24 }
            TapHandler { onTapped: Hmi.endRoute() }
        }
    }

    // Map controls: the right knob's zoom, recenter and 2D/3D, plus voice
    Column {
        anchors.right: parent.right
        anchors.rightMargin: 20
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 92
        spacing: 10
        MapButton { anchors.right: parent.right; icon: "chevron-back"; rotation: 90; onTapped: Hmi.navZoom(1) }
        MapButton { anchors.right: parent.right; icon: "chevron-forward"; rotation: 90; onTapped: Hmi.navZoom(-1) }
        Row {
            anchors.right: parent.right
            spacing: 10
            MapButton { icon: Hmi.nav.voice ? "volume-high" : "volume-mute"; iconColor: Hmi.nav.voice ? Theme.tx : Theme.heat
                        onTapped: { var v = Hmi.nav.voice; Hmi.upd("nav", { voice: !v }); Hmi.toast(v ? "Voice guidance off" : "Voice guidance on"); } }
            MapButton { label: Hmi.nav.view === "2D" ? "3D" : "2D"; onTapped: Hmi.navToggleView() }
            MapButton { icon: "locate"; onTapped: Hmi.navRecenter() }
        }
    }

    // ------------------------------------------------------------ search
    Rectangle {
        id: search
        anchors.fill: parent
        visible: !!Hmi.navSearch
        color: Theme.bg
        readonly property var rows: Hmi.searchRows()
        readonly property int sel: Hmi.navSearch ? Math.min(Hmi.navSearch.sel, Math.max(0, rows.length - 1)) : 0
        readonly property var preview: Hmi.navSearch ? Hmi.navSearch.preview : null
        readonly property bool parked: Hmi.gear === "P"
        // Swallow every press so nothing underneath (the map, the "Where to?"
        // button) also reacts to it. A bare TapHandler would let it through.
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }

        Item {
            id: searchBody
            x: 24
            y: 40 + 18
            width: parent.width - 48
            height: parent.height - 40 - 76 - 36

            // Leave search, closest to the driver. In the route preview it steps
            // back to the results instead.
            Rectangle {
                id: cancelSearch
                width: 150; height: 56; radius: 28
                color: cancelTap.pressed ? Theme.accSoft : Theme.track
                Row {
                    anchors.centerIn: parent
                    spacing: 6
                    Icon { anchors.verticalCenter: parent.verticalCenter; name: "chevron-back"; size: 24; color: Theme.tx }
                    Text { anchors.verticalCenter: parent.verticalCenter; text: search.preview ? "Back" : "Cancel"; color: Theme.tx; font.pixelSize: 19; font.weight: Font.Bold }
                }
                TapHandler { id: cancelTap; onTapped: search.preview ? Hmi.searchBack() : Hmi.closeSearch() }
            }
            // Field
            Rectangle {
                id: field
                anchors.left: cancelSearch.right
                anchors.leftMargin: 12
                width: parent.width - 380 - 20 - cancelSearch.width - 12
                height: 56
                radius: 28
                color: Theme.track
                Icon { x: 20; anchors.verticalCenter: parent.verticalCenter; name: "search"; size: 22; color: Theme.tx2 }
                Text {
                    x: 54
                    width: parent.width - 54 - 60
                    anchors.verticalCenter: parent.verticalCenter
                    text: Hmi.navSearch && Hmi.navSearch.q !== "" ? Hmi.navSearch.q : "Search destinations"
                    color: Hmi.navSearch && Hmi.navSearch.q !== "" ? Theme.tx : Theme.tx2
                    font.pixelSize: 20
                    elide: Text.ElideLeft
                }
                Icon {
                    visible: !!Hmi.navSearch && Hmi.navSearch.q !== ""
                    anchors.right: parent.right; anchors.rightMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    name: "close-circle"; size: 24; color: Theme.tx2
                    TapHandler { onTapped: Hmi.searchClear() }
                }
            }
            Keyboard {
                visible: search.parked
                anchors.top: field.bottom
                anchors.topMargin: 14
                anchors.bottom: parent.bottom
                width: parent.width - 380 - 20
                enterLabel: "Search"
                enterEnabled: search.rows.length > 0
                onKey: (ch) => Hmi.searchKey(ch)
                onBackspace: Hmi.searchBackspace()
                onEnter: Hmi.searchPick(0)
            }
            Card {
                visible: !search.parked
                anchors.top: field.bottom
                anchors.topMargin: 14
                anchors.bottom: parent.bottom
                width: parent.width - 380 - 20
                Column {
                    anchors.centerIn: parent
                    spacing: 10
                    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "lock-closed-outline"; size: 40; color: Theme.tx2 }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Typing is available in Park"; color: Theme.tx; font.pixelSize: 20; font.weight: Font.Bold }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Choose a saved place or a recent destination."; color: Theme.tx2; font.pixelSize: 16 }
                }
            }

            // Results, or the chosen place's route preview
            Card {
                anchors.right: parent.right
                width: 380
                height: parent.height

                Item {
                    id: resHead
                    width: parent.width
                    height: 52
                    Text {
                        x: 22; anchors.verticalCenter: parent.verticalCenter
                        text: search.preview ? "Route" : Hmi.navSearch && Hmi.navSearch.q !== "" ? "Results" : "Destinations"
                        color: Theme.tx; font.pixelSize: 20; font.weight: Font.Bold
                    }
                }

                ListView {
                    id: resList
                    visible: !search.preview
                    anchors.top: resHead.bottom
                    anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                    anchors.margins: 10; anchors.topMargin: 0
                    clip: true
                    spacing: 2
                    model: search.rows.length
                    currentIndex: search.sel
                    onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: Rectangle {
                        id: res
                        required property int index
                        readonly property var r: search.rows[index]
                        width: resList.width
                        height: 60
                        radius: 14
                        color: index === search.sel ? Theme.accSoft : "transparent"
                        Icon { x: 14; anchors.verticalCenter: parent.verticalCenter; name: res.r.icon || "location-outline"; size: 22; color: Theme.acc }
                        Column {
                            x: 50
                            width: parent.width - 50 - 70
                            anchors.verticalCenter: parent.verticalCenter
                            Text { width: parent.width; text: res.r.n; color: Theme.tx; font.pixelSize: 17; font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Text { width: parent.width; text: (res.r.group ? res.r.group + " · " : "") + res.r.a; color: Theme.tx2; font.pixelSize: 13; elide: Text.ElideRight }
                        }
                        Num {
                            anchors.right: parent.right; anchors.rightMargin: 14
                            anchors.verticalCenter: parent.verticalCenter
                            text: Hmi.fd(res.r.mi); color: Theme.tx2; font.pixelSize: 14; font.weight: Font.DemiBold
                        }
                        TapHandler { onTapped: Hmi.searchPick(res.index) }
                    }
                    Text {
                        visible: search.rows.length === 0
                        anchors.centerIn: parent
                        text: "No places match"
                        color: Theme.tx2
                        font.pixelSize: 16
                    }
                }

                // Route preview
                Column {
                    id: previewCol
                    visible: !!search.preview
                    anchors.top: resHead.bottom
                    anchors.topMargin: 10
                    x: 22
                    width: parent.width - 44
                    spacing: 10
                    // Valhalla's route to the previewed place
                    readonly property var route: Router.route
                    readonly property bool ready: !!route.steps
                    readonly property real mi: ready ? route.mi : 0
                    readonly property int mins: ready ? Math.max(1, Math.round(route.mins)) : 0
                    Text { width: parent.width; text: search.preview ? search.preview.n : ""; color: Theme.tx; font.pixelSize: 26; font.weight: Font.Bold; wrapMode: Text.WordWrap }
                    Text { width: parent.width; text: search.preview ? search.preview.a : ""; color: Theme.tx2; font.pixelSize: 16; wrapMode: Text.WordWrap }
                    Item { width: 1; height: 8 }
                    Row {
                        visible: previewCol.ready
                        spacing: 14
                        Num { id: pvMin; text: Hmi.fmtMins(previewCol.mins); font.pixelSize: 40; font.weight: Font.Bold }
                        Num { anchors.baseline: pvMin.baseline; text: Hmi.fd(previewCol.mi); color: Theme.tx2; font.pixelSize: 18 }
                    }
                    Text {
                        visible: previewCol.ready
                        text: "Arrive " + (function () { void Hmi.clock; return Hmi.fmtTime(new Date(Date.now() + previewCol.mins * 60000), true); })()
                        color: Theme.tx2; font.pixelSize: 16
                    }
                    Text {
                        visible: !previewCol.ready
                        text: !Router.available ? "Routing is not installed" : Router.busy ? "Finding route…" : "No route found"
                        color: Theme.tx2; font.pixelSize: 20
                    }
                    Item { width: 1; height: 14 }
                    Rectangle {
                        width: parent.width; height: 60; radius: 30
                        color: Theme.acc
                        opacity: previewCol.ready ? 1 : 0.4
                        Row {
                            anchors.centerIn: parent
                            spacing: 10
                            Icon { name: "navigate"; color: Theme.onAcc; size: 22; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: "Start"; color: Theme.onAcc; font.pixelSize: 19; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
                        }
                        TapHandler { onTapped: Hmi.startRoute(search.preview) }
                    }
                }
            }
        }
    }
}
