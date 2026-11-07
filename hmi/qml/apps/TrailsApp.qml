import QtQuick
import Godwit

// The GPX tracks on the head unit: which ones are on the map, and in what
// color. The right knob moves through them, presses to show or hide one and
// holds to see it on the map. All of that works while driving; bringing tracks
// in and changing them waits for Park.
AppPage {
    id: page
    readonly property var list: Hmi.trailRows
    readonly property int n: list.length
    readonly property int sel: Hmi.trailIdx()
    readonly property var cur: list[sel] || null
    readonly property bool parked: Hmi.gear === "P"

    // A track's line: the same one the map draws
    component Swatch: Rectangle {
        width: 8; height: 40; radius: 4
    }
    component ActionButton: Rectangle {
        id: ab
        property string icon
        property string label
        property bool primary: false
        signal tapped
        height: primary ? 56 : 48
        radius: height / 2
        color: primary ? (abTap.pressed ? Theme.accSoft : Theme.acc) : (abTap.pressed ? Theme.accSoft : Theme.track)
        Row {
            anchors.centerIn: parent
            spacing: 8
            Icon { visible: ab.icon !== ""; name: ab.icon; size: ab.primary ? 22 : 18; color: ab.primary ? Theme.onAcc : Theme.tx; anchors.verticalCenter: parent.verticalCenter }
            Text { text: ab.label; color: ab.primary ? Theme.onAcc : Theme.tx; font.pixelSize: ab.primary ? 18 : 15; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
        }
        TapHandler { id: abTap; onTapped: ab.tapped() }
    }

    // ------------------------------------------------------- nothing to show
    Column {
        visible: page.n === 0
        anchors.centerIn: parent
        spacing: 12
        Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "trail-sign"; size: 48; color: Theme.tx2 }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "No trails yet"
            color: Theme.tx; font.pixelSize: 24; font.weight: Font.Bold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: !page.parked ? "Press record on the map to save your route. USB drives come in while in Park."
                : Trails.usb ? "Bring them in from the USB drive."
                : "Plug in a USB drive with GPX files, or press record on the map."
            color: Theme.tx2; font.pixelSize: 16
        }
        UsbImport { folder: Trails; noun: "GPX files"; visible: page.parked && (Trails.usb || Trails.importing); anchors.horizontalCenter: parent.horizontalCenter }
    }

    // -------------------------------------------------------------- the list
    Item {
        anchors.fill: parent
        visible: page.n > 0

        Row {
            id: head
            height: 44
            spacing: 10
            Text {
                text: page.n + (page.n === 1 ? " trail" : " trails")
                color: Theme.tx; font.pixelSize: 20; font.weight: Font.Bold
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                text: "· " + Trails.onCount + " on the map"
                color: Theme.tx2; font.pixelSize: 15
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        UsbImport { folder: Trails; noun: "GPX files"; visible: page.parked && (Trails.usb || Trails.importing); anchors.right: parent.right; anchors.verticalCenter: head.verticalCenter }

        Card {
            id: listCard
            y: 52
            width: 556
            height: parent.height - y
            ListView {
                id: listView
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                spacing: 2
                model: page.n
                currentIndex: page.sel
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    id: row
                    required property int index
                    readonly property var r: page.list[index] || ({})
                    width: listView.width
                    height: 68
                    radius: 14
                    color: index === page.sel ? Theme.accSoft : "transparent"
                    Swatch { x: 14; anchors.verticalCenter: parent.verticalCenter; color: row.r.color || Theme.line; opacity: row.r.on ? 1 : 0.35 }
                    Column {
                        x: 36
                        width: parent.width - 36 - 86
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text { width: parent.width; text: row.r.n || ""; color: row.r.bad ? Theme.tx2 : Theme.tx; font.pixelSize: 18; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        Num { width: parent.width; text: Hmi.trailSub(row.r); color: Theme.tx2; font.pixelSize: 14 }
                    }
                    Item {
                        width: parent.width - 86
                        height: parent.height
                        TapHandler { onTapped: Hmi.upd("trails", { file: row.r.file }) }
                    }
                    // Beside the row's own tap area, not on it, and wider than
                    // the switch looks
                    Item {
                        visible: !!row.r.ready && !row.r.bad
                        anchors.right: parent.right
                        width: 86
                        height: parent.height
                        Switch { anchors.centerIn: parent; on: !!row.r.on }
                        TapHandler { onTapped: Hmi.trailToggle(row.r) }
                    }
                }
            }
        }

        // ---------------------------------------------------------- the chosen one
        Card {
            id: detail
            anchors.right: parent.right
            y: 52
            width: 404
            height: parent.height - y

            Column {
                x: 16; y: 14
                width: parent.width - 32
                spacing: 10

                // Its shape, as a file's name alone often says little
                Rectangle {
                    width: parent.width
                    height: page.parked ? 112 : 150
                    radius: 14
                    color: Theme.track
                    Canvas {
                        id: shapeCanvas
                        anchors.fill: parent
                        anchors.margins: 12
                        readonly property var s: page.cur && page.cur.ready ? Trails.shape(page.cur.file) : null
                        readonly property color tint: page.cur ? page.cur.color || Theme.line : Theme.line
                        onSChanged: requestPaint()
                        onTintChanged: requestPaint()
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d");
                            ctx.reset();
                            if (!s || !s.parts || !s.parts.length) return;
                            var f = Math.min(width / Math.max(s.w, 1e-6), height / Math.max(s.h, 1e-6));
                            var ox = (width - s.w * f) / 2, oy = (height - s.h * f) / 2;
                            ctx.strokeStyle = tint;
                            ctx.lineWidth = 3;
                            ctx.lineCap = "round";
                            ctx.lineJoin = "round";
                            for (var i = 0; i < s.parts.length; ++i) {
                                var p = s.parts[i];
                                ctx.beginPath();
                                ctx.moveTo(ox + p[0] * f, oy + p[1] * f);
                                for (var j = 2; j < p.length; j += 2)
                                    ctx.lineTo(ox + p[j] * f, oy + p[j + 1] * f);
                                ctx.stroke();
                            }
                        }
                    }
                }

                Column {
                    width: parent.width
                    spacing: 2
                    Text { width: parent.width; text: page.cur ? page.cur.n : ""; color: Theme.tx; font.pixelSize: 22; font.weight: Font.Bold; elide: Text.ElideRight }
                    Num { width: parent.width; text: Hmi.trailSub(page.cur); color: Theme.tx2; font.pixelSize: 15 }
                }

                // Colors: each its own 47 px target, touching its neighbours
                Row {
                    visible: page.parked
                    width: parent.width
                    Repeater {
                        model: Trails.colors
                        delegate: Item {
                            id: swatch
                            required property string modelData
                            readonly property bool on: !!page.cur && String(page.cur.color).toLowerCase() === modelData.toLowerCase()
                            width: parent.width / Trails.colors.length
                            height: 48
                            Rectangle {
                                anchors.centerIn: parent
                                width: 36; height: 36; radius: 18
                                color: swatch.modelData
                            }
                            Rectangle {
                                visible: swatch.on
                                anchors.centerIn: parent
                                width: 45; height: 45; radius: 22.5
                                color: "transparent"
                                border.width: 3
                                border.color: Theme.tx
                            }
                            TapHandler { onTapped: if (page.cur) Trails.setColor(page.cur.file, swatch.modelData) }
                        }
                    }
                }

                ActionButton {
                    width: parent.width
                    primary: true
                    icon: "map-outline"
                    label: "Show on Map"
                    opacity: page.cur && page.cur.ready && !page.cur.bad ? 1 : 0.4
                    onTapped: Hmi.trailShow(page.cur)
                }

                Row {
                    width: parent.width
                    spacing: 8
                    ActionButton {
                        width: page.parked ? 156 : parent.width
                        icon: "navigate"
                        label: "Route to Trail"
                        opacity: page.cur && page.cur.ready && !page.cur.bad ? 1 : 0.4
                        onTapped: Hmi.trailRoute(page.cur)
                    }
                    ActionButton {
                        visible: page.parked
                        width: (parent.width - 156 - 16) / 2
                        label: "Rename"
                        onTapped: Hmi.trailRename(page.cur)
                    }
                    ActionButton {
                        visible: page.parked
                        width: (parent.width - 156 - 16) / 2
                        label: "Delete"
                        onTapped: Hmi.trailDelete(page.cur)
                    }
                }
                Row {
                    visible: !page.parked
                    spacing: 8
                    Icon { name: "lock-closed-outline"; size: 16; color: Theme.tx2; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: "Color, name and USB import are available in Park"; color: Theme.tx2; font.pixelSize: 14; anchors.verticalCenter: parent.verticalCenter }
                }
            }
        }
    }

    ConfirmSheet {}
}
