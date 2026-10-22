import QtQuick
import Godwit
import "../DemoData.js" as D

// The feed fills the screen. Park distance is bottom-left on the driver side;
// the camera switcher is top-right, next to the knob that drives it.
AppPage {
    id: page
    readonly property var cm: Hmi.cams
    readonly property var cam: D.CAMS[cm.sel]
    property real dl: 0
    property real dr: 0
    readonly property real dmin: Math.min(dl, dr)

    // Park sensors are simulated until the CAN backend lands; refresh with the clock
    Connections {
        target: Hmi
        function onClockChanged() { page.dl = D.camDist(Hmi.secs()); page.dr = D.camDistR(Hmi.secs()); }
    }

    Rectangle {
        anchors.fill: parent
        radius: 22
        color: "#0b0b0b"
        clip: true

        // The USB camera assigned to this view (Settings > Cameras). It
        // streams only while this page shows, which Reverse also brings up.
        CameraView {
            id: feed
            anchors.fill: parent
            source: Hmi.camSource(page.cam.n)
            active: page.visible && source !== ""
            // Rear and side cameras show the scene as a mirror would
            mirror: page.cam.n !== "Front"
        }

        // Guidelines on / off, as the right knob's press
        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 14
            width: guideRow.implicitWidth + 32
            height: 52
            radius: 26
            color: Qt.rgba(0, 0, 0, 0.6)
            Row {
                id: guideRow
                anchors.centerIn: parent
                spacing: 8
                Rectangle { width: 10; height: 10; radius: 5; color: page.cm.guides ? "#74FE00" : "#6a6a6a"; anchors.verticalCenter: parent.verticalCenter }
                Text { text: page.cm.guides ? "Guides on" : "Guides off"; color: "#ffffff"; font.pixelSize: 16; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
            }
            TapHandler { onTapped: Hmi.upd("cams", { guides: !page.cm.guides }) }
        }
        Column {
            visible: !feed.streaming
            anchors.centerIn: parent
            spacing: 10
            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "videocam"; size: 40; color: "#6a6a6a" }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: feed.source === "" ? page.cam.n + " camera not connected"
                    : "Starting " + (feed.camera || Hmi.camLabel(feed.source))
                color: "#9a9a9a"; font.pixelSize: 18; font.weight: Font.DemiBold
            }
            Text {
                visible: feed.source === ""
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Plug in a USB camera, or choose one in Settings > Cameras."
                color: "#6a6a6a"; font.pixelSize: 15
            }
        }

        Item {
            visible: page.cm.guides && page.cam.n === "Rear"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            width: 560; height: 240
            Rectangle { x: 0; width: 560; y: 240 - 34 - 5; height: 5; radius: 3; color: "#74FE00" }
            Rectangle { x: 80; width: 400; y: 240 - 118 - 5; height: 5; radius: 3; color: "#FFC107" }
            Rectangle { x: 150; width: 260; y: 240 - 192 - 5; height: 5; radius: 3; color: "#FF5453" }
        }

        Rectangle {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            width: camRow.implicitWidth + 8
            height: 52
            radius: 24
            color: Qt.rgba(0, 0, 0, 0.6)
            Row {
                id: camRow
                anchors.centerIn: parent
                spacing: 4
                Repeater {
                    model: D.CAMS.length
                    delegate: Rectangle {
                        required property int index
                        readonly property bool on: index === page.cm.sel
                        width: camText.implicitWidth + 40
                        height: 44
                        radius: 22
                        color: on ? "#ffffff" : "transparent"
                        Text { id: camText; anchors.centerIn: parent; text: D.CAMS[parent.index].n; color: parent.on ? "#000000" : "#ffffff"; font.pixelSize: 16; font.weight: Font.Bold }
                        TapHandler { onTapped: Hmi.upd("cams", { sel: parent.index }) }
                    }
                }
            }
        }

        Rectangle {
            visible: Hmi.gear === "R" && page.cam.n === "Rear"
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: 14
            width: distRow.implicitWidth + 32
            height: 50
            radius: 18
            color: Qt.rgba(0, 0, 0, 0.7)
            Row {
                id: distRow
                anchors.centerIn: parent
                spacing: 10
                Rectangle {
                    width: 12; height: 12; radius: 6
                    anchors.verticalCenter: parent.verticalCenter
                    color: page.dmin < 0.8 ? "#FF5453" : page.dmin < 1.6 ? "#FFC107" : "#74FE00"
                }
                Num { text: "L " + page.dl.toFixed(1) + " m · R " + page.dr.toFixed(1) + " m"; color: "#ffffff"; font.pixelSize: 22; font.weight: Font.Bold }
            }
        }
    }
}
