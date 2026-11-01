import QtQuick
import Godwit
import "../DemoData.js" as D

// Now playing on the driver side, Up Next or Browse on the right, beside the
// knob that moves through them, and the sources along the bottom. A source's
// settings (accounts, names, passwords) open over the page and can only be
// changed in Park.
AppPage {
    id: page
    readonly property string src: Hmi.audio === "radio" ? Hmi.lastMusic : Hmi.audio
    readonly property bool bt: src === "bt"
    readonly property var s: bt ? null : Music.source(src)
    readonly property var p: Bt.player
    readonly property bool playing: bt ? Hmi.btPlaying : !!s && s.playing
    readonly property bool parked: Hmi.gear === "P"
    readonly property string panel: Hmi.media.panel
    // A setup page for the streaming sources
    readonly property bool hasSetup: src === "spotify" || src === "apple" || src === "youtube"
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
                                           : !s ? "" : s.status === "off" ? "Not available"
                                           : s.status === "setup" ? "Set up " + s.name
                                           : s.status === "starting" ? s.name
                                           : s.status === "error" ? s.name : "Not playing"
    readonly property string idleSub: bt ? (Hmi.btPhone ? "Start music on the phone and it plays here."
                                                        : "Connect a phone in Settings › Bluetooth & Phones.")
                                         : s ? s.note : ""

    // ---------------------------------------------------------- setup state
    property var setup: ({})
    function loadSetup() { setup = Music.setup(src); }
    Connections { target: Music; function onSetupChanged() { page.loadSetup(); } }
    onSrcChanged: loadSetup()
    Component.onCompleted: loadSetup()
    // The field being typed into ("" for none), or "search"
    property string editKey: ""
    property string editText: ""
    property bool editSecret: false
    property bool showSecret: false
    function edit(key, value, secret) { editKey = key; editText = value || ""; editSecret = !!secret; showSecret = false; }
    function finishEdit() {
        if (editKey === "search") {
            if (editText.trim() !== "") { s.search(editText); Hmi.upd("media", { panel: "browse", bsel: 0 }); }
        } else if (editKey !== "") {
            var v = {}; v[editKey] = editText;
            Music.saveSetup(src, v);
        }
        editKey = "";
    }
    // Typing is only possible in Park; shifting out closes the keyboard
    onParkedChanged: if (!parked) editKey = ""

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
                            visible: Hmi.inCall
                            text: "Paused for call"
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
                opacity: page.bt ? (Hmi.btPlayer ? 1 : 0.35) : page.s && page.s.status === "ready" ? 1 : 0.35
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
                        visible: !!page.s && (page.s.lists.length > 0 || page.s.canSearch)
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
                    Pill {
                        visible: page.hasSetup
                        icon: "settings-outline"
                        text: ""
                        hpad: 9
                        color: page.panel === "setup" ? Theme.tx : Theme.track
                        fg: page.panel === "setup" ? Theme.bg : Theme.tx
                        fontSize: 13; vpad: 7
                        TapHandler { onTapped: { if (page.panel === "setup") Hmi.closeMusicSetup(); else Hmi.upd("media", { panel: "setup" }); } }
                    }
                }
            }

            // A note in the middle of the card: the phone, AirPlay, an empty queue
            Column {
                id: note
                readonly property bool shown: page.bt || (page.panel === "queue" && page.queue.length === 0)
                                              || (page.panel === "browse" && !!page.s && page.s.lists.length === 0 && !page.s.canSearch)
                visible: shown && page.panel !== "setup"
                anchors.centerIn: parent
                width: parent.width - 60
                spacing: 12
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: page.bt ? "bluetooth" : page.src === "apple" ? "phone-portrait-outline" : page.src === "usb" ? "hardware-chip-outline" : "musical-notes"
                    size: 40; color: page.bt ? Theme.cool : Theme.acc
                }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    text: page.bt ? (Hmi.btPlayer ? "Playing from " + (Calls.phoneName || "your phone") : Hmi.btPhone ? (Calls.phoneName || "Phone") + " connected" : "No phone connected")
                        : page.src === "apple" ? (page.hasTrack ? "Playing from your iPhone" : "Play from your iPhone")
                        : page.s && page.s.lists.length ? "Nothing queued" : page.s ? page.s.name : ""
                    color: Theme.tx; font.pixelSize: 20; font.weight: Font.Bold
                }
                Text {
                    width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WordWrap
                    text: page.bt ? (Hmi.btPlayer ? (page.p.album ? page.p.album : "Choose what plays on the phone.")
                                     : Hmi.btPhone ? "Start music on the phone and it plays here."
                                     : "Connect a phone in Settings › Bluetooth & Phones to play its music.")
                        : page.src === "apple" ? (page.s.status === "ready" ? "In Apple Music, tap AirPlay and choose " + (page.setup.fields ? page.setup.fields[0].value : "this head unit") + ". The iPhone and head unit must share a Wi-Fi network." : page.s.note)
                        : page.s && page.s.lists.length ? "Choose something in Browse." : page.s ? page.s.note : ""
                    color: Theme.tx2; font.pixelSize: 16
                }
                Pill {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: !!page.s && page.s.status === "setup"
                    icon: "settings-outline"
                    text: "Set Up"
                    color: Theme.acc; fg: Theme.onAcc
                    fontSize: 15; vpad: 9
                    TapHandler { onTapped: Hmi.upd("media", { panel: "setup" }) }
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

            // Browse: search (where the source has it), then its lists
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
                Rectangle {
                    visible: !!page.s && page.s.canSearch
                    width: parent.width; height: 44; radius: 22
                    color: Theme.track
                    Row {
                        x: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 8
                        Icon { name: "search"; size: 18; color: Theme.tx2; anchors.verticalCenter: parent.verticalCenter }
                        Text {
                            text: page.s && page.s.busy ? "Searching…" : page.parked ? "Search " + (page.s ? page.s.name : "") : "Search in Park"
                            color: Theme.tx2; font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    TapHandler {
                        onTapped: {
                            if (!page.parked) { Hmi.toast("Shift to Park to search"); return; }
                            page.edit("search", "", false);
                        }
                    }
                }
                ListView {
                    id: blist
                    width: parent.width
                    height: parent.height - (page.s && page.s.canSearch ? 52 : 0)
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
                readonly property bool off: !!ms && ms.status === "off"
                width: (tiles.width - 5 * tiles.spacing) / 6
                height: tiles.height
                radius: 14
                color: on ? Theme.acc : tileTap.pressed ? Theme.accSoft : Theme.track
                Behavior on color { ColorAnimation { duration: 200 } }
                opacity: off ? 0.45 : 1
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
                // Needs setting up, or plays now
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

    // ---------------------------------------------------------- setup
    Card {
        id: setupCard
        visible: page.hasSetup && page.panel === "setup" && page.editKey === ""
        anchors.fill: top
        large: true

        Flickable {
            anchors.fill: parent
            anchors.margins: 24
            contentHeight: setupCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            Column {
                id: setupCol
                width: parent.width
                spacing: 14

                Item {
                    width: parent.width
                    height: 48
                    Row {
                        spacing: 10
                        anchors.verticalCenter: parent.verticalCenter
                        // Back: the chevron and the title together, a target
                        // big enough to hit, as on the other pages
                        Rectangle {
                            width: backRow.implicitWidth + 24; height: 48; radius: 24
                            anchors.verticalCenter: parent.verticalCenter
                            color: backTap.pressed ? Theme.accSoft : "transparent"
                            Row {
                                id: backRow
                                x: 8
                                spacing: 8
                                anchors.verticalCenter: parent.verticalCenter
                                Icon { name: "chevron-back"; size: 26; anchors.verticalCenter: parent.verticalCenter }
                                Text { text: (page.setup.title || "") + " Settings"; color: Theme.tx; font.pixelSize: 24; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
                            }
                            TapHandler { id: backTap; onTapped: Hmi.closeMusicSetup() }
                        }
                        Pill {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: page.setup.installed !== false
                            text: !page.setup.enabled ? "Off" : page.setup.signedIn ? "Signed in" + (page.setup.account ? " · " + page.setup.account : "") : page.s ? page.s.status === "ready" ? "Ready" : page.s.status === "setup" ? "Not signed in" : page.s.status === "error" ? "Problem" : "Starting" : ""
                            color: page.setup.enabled && (page.setup.signedIn || (page.s && page.s.status === "ready")) ? Theme.accSoft : Theme.track
                            fg: page.setup.enabled && (page.setup.signedIn || (page.s && page.s.status === "ready")) ? Theme.acc : Theme.tx2
                            fontSize: 13
                        }
                    }
                    Row {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        visible: page.parked && page.setup.installed !== false
                        Repeater {
                            model: page.setup.actions || []
                            delegate: Rectangle {
                                id: act
                                required property var modelData
                                readonly property bool main: modelData.id === "signin" || modelData.id === "enable"
                                width: actText.implicitWidth + 40; height: 44; radius: 22
                                color: actTap.pressed ? Theme.accSoft : main ? Theme.acc : Theme.track
                                Text { id: actText; anchors.centerIn: parent; text: act.modelData.label; color: act.main ? Theme.onAcc : Theme.tx; font.pixelSize: 16; font.weight: Font.Bold }
                                TapHandler { id: actTap; onTapped: Music.setupAction(page.src, act.modelData.id) }
                            }
                        }
                    }
                }

                // Not on this head unit's image
                Text {
                    visible: page.setup.installed === false
                    width: parent.width; wrapMode: Text.WordWrap
                    text: page.s ? page.s.note + ". It is an optional part of the head unit's software." : ""
                    color: Theme.tx2; font.pixelSize: 17
                }

                // A code to enter on a phone, while signing in
                Rectangle {
                    visible: !!page.setup.pairing && !!page.setup.pairing.code
                    width: parent.width; height: 92; radius: 18
                    color: Theme.accSoft
                    Row {
                        anchors.centerIn: parent
                        spacing: 22
                        Icon { name: "phone-portrait-outline"; size: 36; color: Theme.acc; anchors.verticalCenter: parent.verticalCenter }
                        Column {
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text { text: "On a phone, go to " + (page.setup.pairing ? String(page.setup.pairing.url || "").replace(/^https?:\/\/(www\.)?/, "") : ""); color: Theme.tx; font.pixelSize: 17 }
                            Text { text: "and enter this code"; color: Theme.tx2; font.pixelSize: 15 }
                        }
                        Num { text: page.setup.pairing ? page.setup.pairing.code || "" : ""; color: Theme.tx; font.pixelSize: 36; font.weight: Font.Bold; font.letterSpacing: 2; anchors.verticalCenter: parent.verticalCenter }
                    }
                }

                Repeater {
                    model: page.setup.installed === false ? [] : page.setup.fields || []
                    delegate: Rectangle {
                        id: fld
                        required property var modelData
                        width: setupCol.width; height: 64; radius: 16
                        color: Theme.track
                        Column {
                            x: 18
                            width: parent.width - 150
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Text { text: fld.modelData.label; color: Theme.tx; font.pixelSize: 17; font.weight: Font.DemiBold }
                            Text {
                                width: parent.width; elide: Text.ElideMiddle
                                text: fld.modelData.value === "" ? (fld.modelData.hint || "Not set") : fld.modelData.secret ? "•".repeat(Math.min(16, fld.modelData.value.length)) : fld.modelData.value
                                color: Theme.tx2; font.pixelSize: 14
                            }
                        }
                        Text {
                            anchors.right: parent.right; anchors.rightMargin: 18; anchors.verticalCenter: parent.verticalCenter
                            text: page.parked ? "Change" : "Park to change"
                            color: page.parked ? Theme.acc : Theme.tx2; font.pixelSize: 15; font.weight: Font.Bold
                        }
                        TapHandler {
                            onTapped: {
                                if (!page.parked) { Hmi.toast("Shift to Park to change settings"); return; }
                                page.edit(fld.modelData.key, fld.modelData.value, fld.modelData.secret);
                            }
                        }
                    }
                }

                Repeater {
                    model: page.setup.steps || []
                    delegate: Row {
                        id: step
                        required property int index
                        required property string modelData
                        width: setupCol.width
                        spacing: 12
                        Rectangle {
                            width: 26; height: 26; radius: 13
                            color: Theme.track
                            Num { anchors.centerIn: parent; text: step.index + 1; color: Theme.tx2; font.pixelSize: 13; font.weight: Font.Bold }
                        }
                        Text { width: parent.width - 38; text: step.modelData; color: Theme.tx2; font.pixelSize: 16; wrapMode: Text.WordWrap }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------- typing
    // A setting or a search, over the whole page; in Park only
    Rectangle {
        visible: page.editKey !== ""
        anchors.fill: parent
        color: Theme.bg

        Item {
            id: editHead
            width: parent.width
            height: 68
            Rectangle {
                width: parent.width - 330; height: 52; radius: 14
                color: Theme.track
                Num {
                    x: 18
                    width: parent.width - 36
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: 20
                    elide: Text.ElideLeft
                    text: page.editText === "" ? (page.editKey === "search" ? "Song, artist or album" : "Type here")
                        : page.editSecret && !page.showSecret ? "•".repeat(page.editText.length) : page.editText
                    color: page.editText !== "" ? Theme.tx : Theme.tx2
                }
            }
            Row {
                anchors.right: parent.right
                spacing: 10
                Rectangle {
                    visible: page.editSecret
                    width: 52; height: 52; radius: 26
                    color: Theme.track
                    Icon { anchors.centerIn: parent; name: page.showSecret ? "eye-off-outline" : "eye-outline"; size: 24 }
                    TapHandler { onTapped: page.showSecret = !page.showSecret }
                }
                Rectangle {
                    width: 110; height: 52; radius: 26
                    color: Theme.track
                    Text { anchors.centerIn: parent; text: "Cancel"; color: Theme.tx; font.pixelSize: 17; font.weight: Font.Bold }
                    TapHandler { onTapped: page.editKey = "" }
                }
                Rectangle {
                    width: 120; height: 52; radius: 26
                    color: Theme.acc
                    Text { anchors.centerIn: parent; text: page.editKey === "search" ? "Search" : "Save"; color: Theme.onAcc; font.pixelSize: 17; font.weight: Font.Bold }
                    TapHandler { onTapped: page.finishEdit() }
                }
            }
        }
        Keyboard {
            anchors.top: editHead.bottom
            anchors.topMargin: 8
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            enterLabel: page.editKey === "search" ? "Search" : "Save"
            onKey: (ch) => page.editText += ch
            onBackspace: page.editText = page.editText.slice(0, -1)
            onEnter: page.finishEdit()
        }
    }
}
