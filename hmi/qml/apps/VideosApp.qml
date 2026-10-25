import QtQuick
import QtMultimedia
import Godwit
import "../DemoData.js" as D

// The media library's videos: a grid to choose from, then the player, in the
// page or across the whole screen. The right knob chooses a video and presses
// to play it; in the player it skips 10 s per detent, presses to pause and
// holds to leave full screen, then the player. Videos only play in Park.
AppPage {
    id: page
    readonly property var vs: Hmi.vids
    readonly property var list: Videos.items
    readonly property int n: list.length
    readonly property int sel: Hmi.vidSel()
    readonly property var cur: list[sel] || null
    readonly property bool parked: Hmi.gear === "P"
    readonly property bool open: parked && n > 0 && vs.view

    MediaPlayer {
        id: player
        source: page.open && page.cur ? page.cur.url : ""
        videoOutput: video
        audioOutput: AudioOutput { device: Videos.audioOutput }
        onSourceChanged: { Hmi.vidPos = 0; page.sync(); }
        onMediaStatusChanged: {
            if (mediaStatus === MediaPlayer.LoadedMedia) page.sync();
            if (mediaStatus === MediaPlayer.EndOfMedia) { Hmi.upd("vids", { play: false }); controls.show(); }
        }
        onPositionChanged: { var s = Math.floor(position / 1000); if (s !== Hmi.vidPos) Hmi.vidPos = s; }
        onDurationChanged: Hmi.vidDur = Math.round(duration / 1000)
    }
    // The player follows Hmi.vids.play, which the knob, the screen, calls and
    // the gear all change
    readonly property bool want: open && vs.play && visible
    onWantChanged: sync()
    function sync() {
        if (want) player.play();
        else if (player.playbackState === MediaPlayer.PlayingState) player.pause();
    }
    function seekTo(ms) { player.setPosition(Math.max(0, Math.min(player.duration - 500, ms))); }
    Connections {
        target: Hmi
        function onVidSeek(secs) { page.seekTo(player.position + secs * 1000); controls.show(); }
    }

    // Not parked, or nothing to show
    Column {
        visible: !page.parked || page.n === 0
        anchors.centerIn: parent
        spacing: 12
        Icon { anchors.horizontalCenter: parent.horizontalCenter; name: page.parked ? "film" : "lock-closed"; size: 48; color: Theme.tx2 }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: !page.parked ? "Videos play in Park" : "No videos yet"
            color: Theme.tx; font.pixelSize: 24; font.weight: Font.Bold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: !page.parked ? "Shift to Park to watch your videos."
                : Videos.usb ? "Bring them in from the USB drive."
                : Videos.skipped ? Videos.skipped + (Videos.skipped === 1 ? " file is" : " files are") + " in a format the head unit cannot play."
                : "Plug in a USB drive with videos to bring them in."
            color: Theme.tx2; font.pixelSize: 16
        }
        UsbImport { folder: Videos; noun: "videos"; visible: page.parked && (Videos.usb || Videos.importing); anchors.horizontalCenter: parent.horizontalCenter }
    }

    // --------------------------------------------------------------- grid
    Item {
        anchors.fill: parent
        visible: page.parked && page.n > 0 && !page.vs.view

        Row {
            id: head
            height: 44
            spacing: 10
            Text {
                text: page.n + (page.n === 1 ? " video" : " videos")
                color: Theme.tx; font.pixelSize: 20; font.weight: Font.Bold
                anchors.verticalCenter: parent.verticalCenter
            }
            Text {
                visible: Videos.skipped > 0
                text: "· " + Videos.skipped + " in a format that cannot be played"
                color: Theme.tx2; font.pixelSize: 15
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        UsbImport { folder: Videos; noun: "videos"; anchors.right: parent.right; anchors.verticalCenter: head.verticalCenter }

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
            cellWidth: (width - 6) / 4
            cellHeight: Math.round((cellWidth - 12) * 9 / 16) + 12 + 30
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
                    id: frame
                    x: 6; y: 6
                    width: parent.width - 12
                    height: Math.round(width * 9 / 16)
                    Rectangle { anchors.fill: parent; color: Theme.track }
                    Icon { visible: !cell.modelData.thumb; anchors.centerIn: parent; name: "film"; size: 34; color: Theme.tx2 }
                    Image {
                        anchors.fill: parent
                        source: cell.modelData.thumb
                        sourceSize: Qt.size(480, 270)
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                    Rectangle {
                        visible: cell.modelData.dur > 0
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: 8
                        width: durText.implicitWidth + 14
                        height: 24
                        radius: 8
                        color: Qt.rgba(0, 0, 0, 0.65)
                        Num { id: durText; anchors.centerIn: parent; text: D.fmtT(cell.modelData.dur); color: "#ffffff"; font.pixelSize: 13; font.weight: Font.Bold }
                    }
                    // Rounds the corners by covering them in the page colour,
                    // which costs nothing next to masking each still
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
                Text {
                    anchors.top: frame.bottom
                    anchors.topMargin: 6
                    x: 8
                    width: parent.width - 16
                    text: cell.modelData.n
                    color: Theme.tx; font.pixelSize: 14; font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                }
                TapHandler { onTapped: Hmi.setVid({ sel: cell.index, view: true, play: true }) }
            }
        }
    }

    // ------------------------------------------------------------- player
    Rectangle {
        anchors.fill: parent
        visible: page.open
        // Full screen: the page itself covers the screen (HeadUnit)
        radius: page.vs.full ? 0 : 22
        color: page.vs.full ? "#000000" : "#0b0b0b"
        clip: true

        VideoOutput {
            id: video
            anchors.fill: parent
            fillMode: VideoOutput.PreserveAspectFit
        }

        Text {
            visible: player.error !== MediaPlayer.NoError || player.mediaStatus === MediaPlayer.LoadingMedia
            anchors.centerIn: parent
            text: player.error !== MediaPlayer.NoError ? "This video cannot be played" : "Loading"
            color: "#9a9a9a"; font.pixelSize: 18; font.weight: Font.DemiBold
        }

        // A tap shows the controls, or hides them while the video plays. The
        // buttons over it take their own taps (ReleaseWithinBounds).
        TapHandler { onTapped: controls.shown ? controls.hide() : controls.show() }

        Item {
            id: controls
            anchors.fill: parent
            // Up while paused; while playing, for a few seconds after a touch
            property bool shown: true
            function show() { shown = true; hideTimer.restart(); }
            function hide() { if (page.vs.play) shown = false; }
            Timer { id: hideTimer; interval: 3500; running: page.vs.play && controls.shown; onTriggered: controls.hide() }
            Connections { target: page; function onVsChanged() { if (!page.vs.play) controls.shown = true; else controls.show(); } }
            opacity: shown ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: 200 } }

            component Glass: Rectangle {
                height: 52
                radius: 26
                color: Qt.rgba(0, 0, 0, 0.6)
            }

            // Back to the grid, or out of full screen
            Glass {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: 14
                width: backRow.implicitWidth + 36
                Row {
                    id: backRow
                    anchors.centerIn: parent
                    spacing: 8
                    Icon { name: page.vs.full ? "contract" : "chevron-back"; size: 22; color: "#ffffff"; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: page.vs.full ? "Exit Full Screen" : "All Videos"; color: "#ffffff"; font.pixelSize: 16; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
                }
                TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: Hmi.upd("vids", page.vs.full ? { full: false } : { view: false, play: false, full: false }) }
            }
            Glass {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 14
                width: Math.min(titleText.implicitWidth, 300) + 36
                Text {
                    id: titleText
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, 300)
                    text: page.cur ? page.cur.n : ""
                    color: "#ffffff"; font.pixelSize: 16; font.weight: Font.Bold
                    elide: Text.ElideMiddle
                }
            }

            // Play / pause in the middle, large enough to hit while seated
            Rectangle {
                anchors.centerIn: parent
                width: 88; height: 88; radius: 44
                color: Qt.rgba(0, 0, 0, 0.55)
                Icon { anchors.centerIn: parent; anchors.horizontalCenterOffset: page.vs.play ? 0 : 3; name: page.vs.play ? "pause" : "play"; size: 40; color: "#ffffff" }
                TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: Hmi.setVid({ play: !page.vs.play }) }
            }

            // Position, seek bar, length and full screen
            Glass {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 14
                height: 56
                radius: 28
                Num {
                    id: posText
                    anchors.left: parent.left
                    anchors.leftMargin: 22
                    anchors.verticalCenter: parent.verticalCenter
                    text: D.fmtT(Math.floor(player.position / 1000))
                    color: "#ffffff"; font.pixelSize: 15; font.weight: Font.Bold
                }
                Item {
                    id: bar
                    anchors.left: posText.right
                    anchors.right: durText2.left
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    anchors.verticalCenter: parent.verticalCenter
                    height: 40
                    readonly property real frac: player.duration > 0 ? player.position / player.duration : 0
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width; height: 6; radius: 3
                        color: Qt.rgba(1, 1, 1, 0.25)
                        Rectangle { width: parent.width * bar.frac; height: parent.height; radius: 3; color: "#ffffff" }
                    }
                    Rectangle {
                        x: bar.width * bar.frac - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        width: 18; height: 18; radius: 9
                        color: "#ffffff"
                    }
                    MouseArea {
                        anchors.fill: parent
                        function seek(mx) { page.seekTo(player.duration * Math.max(0, Math.min(1, mx / width))); controls.show(); }
                        onPressed: (mouse) => seek(mouse.x)
                        onPositionChanged: (mouse) => { if (pressed) seek(mouse.x); }
                    }
                }
                Num {
                    id: durText2
                    anchors.right: fullBtn.left
                    anchors.rightMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: D.fmtT(Math.round(player.duration / 1000))
                    color: "#bbbbbb"; font.pixelSize: 15; font.weight: Font.DemiBold
                }
                Rectangle {
                    id: fullBtn
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44; height: 44; radius: 22
                    color: fullTap.pressed ? Qt.rgba(1, 1, 1, 0.2) : "transparent"
                    Icon { anchors.centerIn: parent; name: page.vs.full ? "contract" : "expand"; size: 24; color: "#ffffff" }
                    TapHandler { id: fullTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: { Hmi.upd("vids", { full: !page.vs.full }); controls.show(); } }
                }
            }
        }
    }
}
