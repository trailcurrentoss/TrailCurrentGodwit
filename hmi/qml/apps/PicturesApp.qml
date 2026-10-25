import QtQuick
import Godwit

// The media library's photos: a grid to choose from, then one photo filling
// the page. The right knob moves through them, presses to open one or start
// the slideshow and holds to go back to the grid. Photos only show in Park.
AppPage {
    id: page
    readonly property var pv: Hmi.pics
    readonly property var list: Photos.photos
    readonly property int n: list.length
    readonly property int sel: Hmi.picSel()
    readonly property bool parked: Hmi.gear === "P"

    // Slideshow: a new photo every few seconds, starting over after the last
    Timer {
        id: slide
        interval: 5000
        repeat: true
        running: page.visible && page.parked && page.pv.view && page.pv.play && page.n > 1
        onTriggered: Hmi.upd("pics", { sel: (page.sel + 1) % page.n })
    }
    // Turning the knob mid-slideshow gives the new photo its full time
    onSelChanged: {
        pager.follow();
        if (slide.running) slide.restart();
    }

    // Bringing photos in from a USB drive: a button while the drive has new
    // ones, progress while they copy
    component UsbImport: Item {
        visible: Photos.usb || Photos.importing
        implicitWidth: Photos.importing ? 240 : Photos.usbNew ? usbBtn.width : usbNote.implicitWidth
        implicitHeight: 44
        Column {
            visible: Photos.importing
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            spacing: 6
            Text { text: "Importing " + Photos.importDone + " of " + Photos.importTotal; color: Theme.tx; font.pixelSize: 15; font.weight: Font.DemiBold }
            Rectangle {
                width: parent.width; height: 6; radius: 3
                color: Theme.track
                Rectangle {
                    height: parent.height; radius: 3
                    width: parent.width * (Photos.importTotal ? Photos.importDone / Photos.importTotal : 0)
                    color: Theme.acc
                }
            }
        }
        Rectangle {
            id: usbBtn
            visible: !Photos.importing && Photos.usbNew > 0
            width: usbRow.implicitWidth + 36
            height: 44
            radius: 22
            color: usbTap.pressed ? Theme.accSoft : Theme.acc
            Row {
                id: usbRow
                anchors.centerIn: parent
                spacing: 8
                Icon { name: "arrow-down"; size: 20; color: Theme.onAcc; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "Import " + Photos.usbNew + " from USB"; color: Theme.onAcc; font.pixelSize: 16; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
            }
            TapHandler { id: usbTap; onTapped: Photos.importUsb() }
        }
        Text {
            id: usbNote
            visible: !Photos.importing && Photos.usbNew === 0
            anchors.verticalCenter: parent.verticalCenter
            text: "USB drive · no new photos"
            color: Theme.tx2; font.pixelSize: 15
        }
    }

    // Not parked, or nothing to show
    Column {
        visible: !page.parked || page.n === 0
        anchors.centerIn: parent
        spacing: 12
        Icon { anchors.horizontalCenter: parent.horizontalCenter; name: page.parked ? "images" : "lock-closed"; size: 48; color: Theme.tx2 }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: !page.parked ? "Pictures show in Park" : "No photos yet"
            color: Theme.tx; font.pixelSize: 24; font.weight: Font.Bold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: !page.parked ? "Shift to Park to look at your photos."
                : Photos.usb ? "Bring them in from the USB drive."
                : Photos.skipped ? Photos.skipped + (Photos.skipped === 1 ? " file is" : " files are") + " in a format the head unit cannot show."
                : "Plug in a USB drive with photos to bring them in."
            color: Theme.tx2; font.pixelSize: 16
        }
        UsbImport { visible: page.parked && (Photos.usb || Photos.importing); anchors.horizontalCenter: parent.horizontalCenter }
    }

    // --------------------------------------------------------------- grid
    Item {
        anchors.fill: parent
        visible: page.parked && page.n > 0 && !page.pv.view

        Row {
            id: head
            height: 44
            spacing: 10
            Text {
                text: page.n + (page.n === 1 ? " photo" : " photos")
                color: Theme.tx; font.pixelSize: 20; font.weight: Font.Bold
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: Photos.skipped > 0
                text: "· " + Photos.skipped + " in a format that cannot be shown"
                color: Theme.tx2; font.pixelSize: 15
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        UsbImport { anchors.right: parent.right; anchors.verticalCenter: head.verticalCenter }

        GridView {
            id: grid
            anchors.top: head.bottom
            anchors.topMargin: 8
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            // Room for the selection ring at the edges
            leftMargin: 3; rightMargin: 3; topMargin: 3; bottomMargin: 3
            clip: true
            cellWidth: (width - 6) / 5
            cellHeight: Math.round(cellWidth * 0.75)
            model: page.list
            currentIndex: page.sel
            onCurrentIndexChanged: positionViewAtIndex(currentIndex, GridView.Contain)
            boundsBehavior: Flickable.StopAtBounds

            delegate: Item {
                id: cell
                required property var modelData
                required property int index
                width: grid.cellWidth
                height: grid.cellHeight

                Item {
                    anchors.fill: parent
                    anchors.margins: 6
                    Rectangle { anchors.fill: parent; color: Theme.track }
                    Image {
                        anchors.fill: parent
                        source: cell.modelData.url
                        // JPEG decodes straight to the smaller size, so a
                        // phone's 12 MP photo costs little here
                        sourceSize: Qt.size(320, 240)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        autoTransform: true
                    }
                    // Rounds the corners by covering them in the page colour,
                    // which costs nothing next to masking each photo
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -6
                        radius: 14 + 6
                        color: "transparent"
                        border.width: 6
                        border.color: Theme.bg
                    }
                    Rectangle {
                        visible: cell.index === page.sel
                        anchors.fill: parent
                        anchors.margins: -3
                        radius: 14 + 3
                        color: "transparent"
                        border.width: 3
                        border.color: Theme.acc
                    }
                }
                TapHandler { onTapped: Hmi.upd("pics", { sel: cell.index, view: true }) }
            }
        }
    }

    // ------------------------------------------------------------- viewer
    Rectangle {
        anchors.fill: parent
        visible: page.parked && page.n > 0 && page.pv.view
        radius: 22
        color: "#0b0b0b"
        clip: true

        ListView {
            id: pager
            anchors.fill: parent
            orientation: ListView.Horizontal
            snapMode: ListView.SnapOneItem
            highlightRangeMode: ListView.StrictlyEnforceRange
            highlightMoveDuration: 300
            highlightMoveVelocity: -1
            boundsBehavior: Flickable.StopAtBounds
            model: page.list
            // Follows the selection. Not a binding: the view moves its own
            // index while it has no width yet, and that must not reach Hmi.
            function follow() { if (currentIndex !== page.sel) currentIndex = page.sel; }
            onWidthChanged: { follow(); positionViewAtIndex(page.sel, ListView.Beginning); }
            onCountChanged: follow()
            // A swipe moves the selection, as the knob does
            onMovementEnded: if (currentIndex !== page.sel) Hmi.upd("pics", { sel: currentIndex })

            delegate: Item {
                id: slot
                required property var modelData
                width: pager.width
                height: pager.height
                Image {
                    id: photo
                    anchors.fill: parent
                    source: slot.modelData.url
                    // The page is scaled up on a 1080p screen
                    sourceSize: Qt.size(1920, 1080)
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    autoTransform: true
                    // Each is several MB decoded; let them go once swiped past
                    cache: false
                }
                Text {
                    visible: photo.status !== Image.Ready
                    anchors.centerIn: parent
                    text: photo.status === Image.Error ? "This photo cannot be shown" : "Loading"
                    color: "#9a9a9a"; font.pixelSize: 18; font.weight: Font.DemiBold
                }
            }
        }

        component Glass: Rectangle {
            height: 52
            radius: 26
            color: Qt.rgba(0, 0, 0, 0.6)
        }

        Glass {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 14
            width: backRow.implicitWidth + 36
            Row {
                id: backRow
                anchors.centerIn: parent
                spacing: 6
                Icon { name: "chevron-back"; size: 22; color: "#ffffff"; anchors.verticalCenter: parent.verticalCenter }
                Text { text: "All Photos"; color: "#ffffff"; font.pixelSize: 16; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
            }
            TapHandler { onTapped: Hmi.upd("pics", { view: false, play: false }) }
        }

        Row {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 14
            spacing: 8
            Glass {
                width: countRow.implicitWidth + 36
                Row {
                    id: countRow
                    anchors.centerIn: parent
                    spacing: 10
                    Text {
                        text: page.list[page.sel] ? page.list[page.sel].n : ""
                        color: "#ffffff"; font.pixelSize: 16; font.weight: Font.Bold
                        width: Math.min(implicitWidth, 260); elide: Text.ElideMiddle
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Num { text: (page.sel + 1) + " / " + page.n; color: "#bbbbbb"; font.pixelSize: 16; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                }
            }
            Glass {
                width: 52
                Icon { anchors.centerIn: parent; name: page.pv.play ? "pause" : "play"; size: 24; color: "#ffffff" }
                TapHandler { onTapped: Hmi.upd("pics", { play: !page.pv.play }) }
            }
        }

        // Previous and next, for touch; a swipe does the same
        Repeater {
            model: [-1, 1]
            delegate: Glass {
                required property int modelData
                readonly property int to: page.sel + modelData
                visible: to >= 0 && to < page.n
                width: 52
                anchors.verticalCenter: parent.verticalCenter
                x: modelData < 0 ? 14 : parent.width - width - 14
                Icon { anchors.centerIn: parent; name: parent.modelData < 0 ? "chevron-back" : "chevron-forward"; size: 26; color: "#ffffff" }
                TapHandler { onTapped: Hmi.upd("pics", { sel: parent.to }) }
            }
        }
    }
}
