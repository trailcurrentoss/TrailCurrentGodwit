import QtQuick
import Godwit
import "../DemoData.js" as D

// Now playing on the driver side, Up Next or Browse on the right, beside the
// knob that moves through them, and the sources along the bottom.
AppPage {
    id: page
    readonly property string src: Hmi.audio === "radio" ? Hmi.lastMusic : Hmi.audio
    readonly property bool bt: src === "bt"
    readonly property var s: bt ? null : Music.source(src)
    readonly property var p: Bt.player
    readonly property bool playing: bt ? Hmi.btPlaying : !!s && s.playing
    readonly property string panel: Hmi.media.panel
    // {title, artist, album, art, dur (s)}
    readonly property var tr: bt ? { title: p.title || "", artist: p.artist || "", album: p.album || "", art: "",
                                     dur: Math.max(0, (p.duration || 0) / 1000) }
                                 : s ? s.track : ({})
    readonly property bool hasTrack: !!tr.title
    // Sources report their position on changes only; count on from there
    readonly property real pos: {
        void Hmi.clock;
        var d = tr.dur || 0;
        var at = bt ? p.at : s ? s.positionAt : 0, base = bt ? (p.position || 0) / 1000 : s ? s.position : 0;
        var x = base + (playing && at ? (Date.now() - at) / 1000 : 0);
        return d > 0 ? Math.min(d, x) : x;
    }
    readonly property var queue: s && s.hasQueue ? s.queue : []
    readonly property int sel: Math.min(Hmi.media.sel, Math.max(0, queue.length - 1))
    readonly property int bsel: Math.min(Hmi.media.bsel || 0, Math.max(0, (s ? s.lists.length : 0) - 1))

    // What the screen says when nothing is playing
    readonly property string idleTitle: bt ? (Hmi.btPhone ? "Not playing" : "No phone connected")
                                           : !s ? "" : "Not playing"
    readonly property string idleSub: bt ? (Hmi.btPhone ? "Start music on the phone and it plays here."
                                                        : "Connect a phone in Settings › Bluetooth & Phones.")
                                         : s ? s.note : ""

    // ---------------------------------------------------------- now playing
    Item {
        id: top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: tiles.top
        anchors.bottomMargin: 16

        Column {
            anchors.left: parent.left
            anchors.right: card.left
            anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            spacing: 16

            Row {
                spacing: 22
                Rectangle {
                    id: art
                    width: 150; height: 150; radius: 20
                    color: page.bt ? Theme.cool : D.ARTS[Math.abs(page.src.length + (page.tr.title || "").length) % D.ARTS.length]
                    clip: true
                    Behavior on color { ColorAnimation { duration: 300 } }
                    Icon { anchors.centerIn: parent; visible: cover.status !== Image.Ready; name: "musical-notes"; color: Qt.rgba(1, 1, 1, 0.85); size: 60 }
                    Image {
                        id: cover
                        anchors.fill: parent
                        source: page.tr.art || ""
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        sourceSize: Qt.size(300, 300)
                        visible: status === Image.Ready
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    width: top.width - 360 - 24 - 150 - 22
                    spacing: 6
                    Row {
                        spacing: 6
                        Icon { name: D.SRC[page.src].icon; color: Theme.tx2; size: 16; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            text: page.bt ? "Bluetooth" + (Hmi.btPhone ? " · " + (Bt.player.name || Calls.phoneName || "Phone") : "")
                                          : D.SRC[page.src].n + (page.s && page.s.queueTitle ? " · " + page.s.queueTitle : "")
                            color: Theme.tx2; font.pixelSize: 15; font.weight: Font.DemiBold
                            width: Math.min(implicitWidth, 300); elide: Text.ElideRight
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Pill {
                            visible: Hmi.muted || Hmi.inCall
                            text: Hmi.inCall ? "Paused for call" : "Muted"
                            color: Theme.heat
                            fg: Theme.onAlert
                            fontSize: 13
                            vpad: 3
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    Text {
                        width: parent.width
                        text: page.hasTrack ? page.tr.title : page.idleTitle
                        color: Theme.tx; font.pixelSize: page.hasTrack ? 34 : 28; font.weight: Font.Bold; font.letterSpacing: -0.8
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        text: page.hasTrack ? (page.tr.artist || page.tr.album || "") : page.idleSub
                        color: Theme.tx2; font.pixelSize: page.hasTrack ? 20 : 16
                        wrapMode: page.hasTrack ? Text.NoWrap : Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 6
                visible: page.hasTrack
                Rectangle {
                    width: parent.width; height: 6; radius: 3
                    color: Theme.track
                    clip: true
                    Rectangle {
                        height: parent.height
                        width: page.tr.dur > 0 ? parent.width * Math.min(1, page.pos / page.tr.dur) : 0
                        color: Theme.tx
                        Behavior on width { NumberAnimation { duration: 1000 } }
                    }
                }
                Item {
                    width: parent.width
                    height: 18
                    visible: page.tr.dur > 0
                    Num { text: D.fmtT(Math.floor(page.pos)); color: Theme.tx2; font.pixelSize: 14; font.weight: Font.DemiBold }
                    Num { anchors.right: parent.right; text: D.fmtT(Math.round(page.tr.dur)); color: Theme.tx2; font.pixelSize: 14; font.weight: Font.DemiBold }
                }
            }

            Row {
                spacing: 28
                opacity: page.bt ? (Hmi.btPlayer ? 1 : 0.35) : page.s ? 1 : 0.35
                Item {
                    width: 64; height: 64
                    anchors.verticalCenter: parent.verticalCenter
                    Icon { anchors.centerIn: parent; name: "play-skip-back"; size: 32 }
                    TapHandler { onTapped: { if (page.bt) Bt.previous(); else if (page.s) { Hmi.takeSource(page.src); page.s.previous(); } } }
                }
                Rectangle {
                    width: 76; height: 76; radius: 38
                    color: Theme.tx
                    Icon { anchors.centerIn: parent; visible: !(page.s && page.s.busy); name: page.playing ? "pause" : "play"; color: Theme.bg; size: 34 }
                    Text { anchors.centerIn: parent; visible: !!page.s && page.s.busy; text: "…"; color: Theme.bg; font.pixelSize: 30; font.weight: Font.Bold }
                    TapHandler {
                        onTapped: {
                            if (Hmi.inCall) { Hmi.toast("Audio paused during call"); return; }
                            if (Hmi.audio !== page.src) Hmi.takeSource(page.src);
                            Hmi.srcToggle();
                        }
                    }
                }
                Item {
                    width: 64; height: 64
                    anchors.verticalCenter: parent.verticalCenter
                    Icon { anchors.centerIn: parent; name: "play-skip-forward"; size: 32 }
                    TapHandler { onTapped: { if (page.bt) Bt.next(); else if (page.s) { Hmi.takeSource(page.src); page.s.next(); } } }
                }
            }
        }
        // Copying a drive's music into the head unit's own library, beside
        // the transport
        UsbImport {
            visible: (page.src === "local" || page.src === "usb") && (Music.library.usb || Music.library.importing)
            anchors.right: card.left
            anchors.rightMargin: 24
            anchors.bottom: parent.bottom
            anchors.bottomMargin: (parent.height - 288) / 2 + 16
            folder: Music.library
            noun: "songs"
        }
        Connections {
            target: Music.library
            function onImported(count, failed) {
                Hmi.toast(count + (count === 1 ? " song" : " songs") + " imported" + (failed ? " · " + failed + " could not be copied" : ""));
            }
        }

        // ---------------------------------------------------- right card
        Card {
            id: card
            anchors.right: parent.right
            width: 360
            height: parent.height

            Item {
                id: head
                x: 22; y: 16
                width: parent.width - 44
                height: 34
                Row {
                    spacing: 6
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !page.bt && !!page.s
                    Pill {
                        text: "Up Next"
                        visible: !!page.s && page.s.hasQueue
                        color: page.panel === "queue" ? Theme.tx : Theme.track
                        fg: page.panel === "queue" ? Theme.bg : Theme.tx
                        fontSize: 13; vpad: 7
                        TapHandler { onTapped: Hmi.upd("media", { panel: "queue" }) }
                    }
                    Pill {
                        text: "Browse"
                        visible: !!page.s && page.s.lists.length > 0
                        color: page.panel === "browse" ? Theme.tx : Theme.track
                        fg: page.panel === "browse" ? Theme.bg : Theme.tx
                        fontSize: 13; vpad: 7
                        TapHandler { onTapped: Hmi.upd("media", { panel: "browse" }) }
                    }
                }
                Text {
                    visible: page.bt || !page.s
                    text: "Bluetooth"
                    color: Theme.tx; font.pixelSize: 20; font.weight: Font.Bold
                    anchors.verticalCenter: parent.verticalCenter
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Pill {
                        visible: !!page.s && page.s.hasQueue && page.panel === "queue" && page.queue.length > 1
                        icon: "shuffle"
                        text: page.s && page.s.shuffle ? "Shuffle on" : "Shuffle off"
                        color: page.s && page.s.shuffle ? Theme.acc : Theme.track
                        fg: page.s && page.s.shuffle ? Theme.onAcc : Theme.tx
                        fontSize: 13; vpad: 7
                        TapHandler {
                            onTapped: {
                                var on = page.s.shuffle;
                                page.s.setShuffle(!on);
                                Hmi.toast(on ? "Shuffle off" : "Shuffle on");
                            }
                        }
                    }
                }
            }

            // A note in the middle of the card: the phone, an empty queue
            Column {
                id: note
                readonly property bool shown: page.bt || (page.panel === "queue" && page.queue.length === 0)
                                              || (page.panel === "browse" && !!page.s && page.s.lists.length === 0)
                visible: shown
                anchors.centerIn: parent
                width: parent.width - 60
                spacing: 12
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: page.bt ? "bluetooth" : page.src === "usb" ? "hardware-chip-outline" : "musical-notes"
                    size: 40; color: page.bt ? Theme.cool : Theme.acc
                }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    text: page.bt ? (Hmi.btPlayer ? "Playing from " + (Calls.phoneName || "your phone") : Hmi.btPhone ? (Calls.phoneName || "Phone") + " connected" : "No phone connected")
                        : page.s && page.s.lists.length ? "Nothing queued" : page.s ? page.s.name : ""
                    color: Theme.tx; font.pixelSize: 20; font.weight: Font.Bold
                }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    text: page.bt ? (Hmi.btPlayer ? (page.p.album ? page.p.album : "Choose what plays on the phone.")
                                     : Hmi.btPhone ? "Start music on the phone and it plays here."
                                     : "Connect a phone in Settings › Bluetooth & Phones to play its music.")
                        : page.s && page.s.lists.length ? "Choose something in Browse." : page.s ? page.s.note : ""
                    color: Theme.tx2; font.pixelSize: 16
                }
            }

            // Up Next
            ListView {
                id: qlist
                visible: !page.bt && page.panel === "queue" && page.queue.length > 0
                anchors.top: head.bottom
                anchors.topMargin: 10
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 12
                clip: true
                spacing: 2
                model: page.queue
                currentIndex: page.sel
                highlightFollowsCurrentItem: false
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    id: qrow
                    required property int index
                    required property var modelData
                    width: qlist.width
                    height: 54
                    radius: 14
                    color: index === page.sel ? Theme.accSoft : "transparent"
                    Behavior on color { ColorAnimation { duration: 200 } }
                    Column {
                        x: 12
                        width: parent.width - 24
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        Text { width: parent.width; text: qrow.modelData.title; color: qrow.index === page.s.queueIndex ? Theme.acc : Theme.tx; font.pixelSize: 17; font.weight: Font.DemiBold; elide: Text.ElideRight }
                        Text { width: parent.width; text: [qrow.modelData.artist || "", qrow.modelData.dur > 0 ? D.fmtT(qrow.modelData.dur) : ""].filter(function (x) { return x; }).join(" · "); color: Theme.tx2; font.pixelSize: 14; elide: Text.ElideRight }
                    }
                    TapHandler { onTapped: Hmi.playTrack(qrow.index, page.src) }
                }
            }

            // Browse: the source's lists
            Column {
                id: browse
                visible: !page.bt && page.panel === "browse" && !note.shown
                anchors.top: head.bottom
                anchors.topMargin: 10
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 12
                spacing: 8
                ListView {
                    id: blist
                    width: parent.width
                    height: parent.height
                    clip: true
                    spacing: 2
                    model: page.s ? page.s.lists : []
                    currentIndex: page.bsel
                    onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: Rectangle {
                        id: brow
                        required property int index
                        required property var modelData
                        width: blist.width
                        height: 54
                        radius: 14
                        color: index === page.bsel ? Theme.accSoft : "transparent"
                        Icon { x: 12; anchors.verticalCenter: parent.verticalCenter; name: brow.modelData.icon || "musical-notes"; size: 20; color: Theme.tx2 }
                        Column {
                            x: 44
                            width: parent.width - 56
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text { width: parent.width; text: brow.modelData.title; color: Theme.tx; font.pixelSize: 17; font.weight: Font.DemiBold; elide: Text.ElideRight }
                            Text { width: parent.width; text: brow.modelData.sub || ""; color: Theme.tx2; font.pixelSize: 14; elide: Text.ElideRight }
                        }
                        TapHandler { onTapped: Hmi.openMusicList(brow.index) }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------- sources
    Row {
        id: tiles
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 60
        spacing: 14
        Repeater {
            model: Hmi.musicSources
            delegate: Rectangle {
                id: tile
                required property string modelData
                readonly property var ms: Music.source(modelData)
                readonly property bool on: page.src === modelData
                width: (tiles.width - (Hmi.musicSources.length - 1) * tiles.spacing) / Hmi.musicSources.length
                height: tiles.height
                radius: 14
                color: on ? Theme.acc : tileTap.pressed ? Theme.accSoft : Theme.track
                Behavior on color { ColorAnimation { duration: 200 } }
                Column {
                    anchors.centerIn: parent
                    spacing: 4
                    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: D.SRC[tile.modelData].icon; size: 20; color: tile.on ? Theme.onAcc : Theme.tx }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: D.SRC[tile.modelData].n
                        color: tile.on ? Theme.onAcc : Theme.tx
                        font.pixelSize: 15; font.weight: Font.DemiBold
                    }
                }
                // Playing, while another is on screen
                Rectangle {
                    visible: !tile.on && ((!!tile.ms && tile.ms.playing) || (tile.modelData === "bt" && Hmi.btPlaying))
                    anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 8
                    width: 8; height: 8; radius: 4
                    color: Theme.acc
                }
                TapHandler { id: tileTap; onTapped: Hmi.chooseSource(tile.modelData) }
            }
        }
    }

}
