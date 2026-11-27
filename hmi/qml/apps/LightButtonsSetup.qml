import QtQuick
import Godwit
import "../FaIcons.js" as Fa

// Settings › TrailCurrent › Light Buttons: the buttons the Lights screen shows,
// each pointed at a Torrent channel or a Switchback relay by address and
// output. Three pages: the list (tcButtons), one button (tcButton) and its
// icon (tcIcon). Every change is saved as it is made. Any number of buttons
// may drive one output, so rearranging is only choosing each button's target.
Item {
    id: root
    property string pg
    readonly property var buttons: TrailCurrent.lights
    readonly property int bi: typeof Hmi.setArg === "number" ? Hmi.setArg : -1
    readonly property var cur: bi >= 0 && bi < buttons.length ? buttons[bi] : null

    function stateText(b) { return !b.fresh ? "No data" : !b.on ? "Off" : b.dim ? b.b + "%" : "On"; }

    // "‹ Title" heading of a card: tap it, or hold the right knob, to go back
    component Head: Item {
        property string title
        property string hint: "Hold to go back"
        width: parent.width
        height: 54
        Row {
            x: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Icon { name: "chevron-back"; size: 22; anchors.verticalCenter: parent.verticalCenter }
            Text {
                text: parent.parent.title
                color: Theme.tx; font.pixelSize: 20; font.weight: Font.Bold
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, root.width * 0.4)
                elide: Text.ElideRight
            }
        }
        Text {
            anchors.right: parent.right
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            text: parent.hint
            color: Theme.tx2; font.pixelSize: 14
        }
        TapHandler { onTapped: Hmi.backSet() }
    }
    component Dot: Rectangle {
        property bool on
        width: 8; height: 8; radius: 4
        color: on ? Theme.acc : Theme.line
    }
    component Chip: Rectangle {
        id: chip
        property string text
        property bool on
        property bool dot: false
        property bool dotOn: false
        signal tapped
        height: 44
        width: chipRow.implicitWidth + 32
        radius: 22
        color: on ? Theme.acc : Theme.track
        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 8
            Rectangle {
                visible: chip.dot
                width: 8; height: 8; radius: 4
                anchors.verticalCenter: parent.verticalCenter
                color: chip.on ? Theme.onAcc : chip.dotOn ? Theme.acc : Theme.line
            }
            Text { text: chip.text; color: chip.on ? Theme.onAcc : Theme.tx; font.pixelSize: 16; font.weight: Font.DemiBold }
        }
        TapHandler { onTapped: chip.tapped() }
    }

    // ---------------------------------------------------------------- list
    Item {
        id: listPage
        anchors.fill: parent
        visible: root.pg === "tcButtons"
        readonly property int sel: Math.min(Hmi.setSel.tcButtons || 0, root.buttons.length)

        Card {
            id: listCard
            anchors.left: parent.left
            anchors.right: modsCard.left
            anchors.rightMargin: 24
            height: parent.height
            Head { id: listHead; title: "Light Buttons" }
            Flickable {
                id: listFlick
                anchors.top: listHead.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 12
                anchors.topMargin: 0
                clip: true
                contentHeight: listCol.height
                boundsBehavior: Flickable.StopAtBounds
                readonly property int sel: listPage.sel
                onSelChanged: {
                    var top = sel * 52;
                    if (top < contentY) contentY = top;
                    else if (top + 50 > contentY + height) contentY = top + 50 - height;
                }
                Column {
                    id: listCol
                    width: parent.width
                    spacing: 2
                    Repeater {
                        model: root.buttons.length
                        delegate: Rectangle {
                            id: brow
                            required property int index
                            readonly property var b: root.buttons[index]
                            width: listCol.width; height: 50; radius: 14
                            color: index === listFlick.sel ? Theme.accSoft : "transparent"
                            Row {
                                x: 14
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 14
                                FaIcon { key: brow.b.icon; size: 28; anchors.verticalCenter: parent.verticalCenter }
                                Text {
                                    width: 170
                                    text: brow.b.n
                                    color: Theme.tx; font.pixelSize: 17; font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Dot { on: brow.b.fresh; anchors.verticalCenter: parent.verticalCenter }
                                Text {
                                    text: Hmi.tcTarget(brow.b) + (brow.b.dim ? " · Dims" : "")
                                    color: Theme.tx2; font.pixelSize: 15
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                            Text {
                                anchors.right: chev.left
                                anchors.rightMargin: 12
                                anchors.verticalCenter: parent.verticalCenter
                                text: root.stateText(brow.b)
                                color: Theme.tx2; font.pixelSize: 15
                            }
                            Icon { id: chev; name: "chevron-forward"; size: 18; color: Theme.tx2; anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter }
                            TapHandler { onTapped: { Hmi.selSet(brow.index); Hmi.openSet("tcButton", brow.index); } }
                        }
                    }
                    Rectangle {
                        width: listCol.width; height: 50; radius: 14
                        color: listFlick.sel === root.buttons.length ? Theme.accSoft : "transparent"
                        Row {
                            x: 14
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 14
                            Text { text: "+"; width: 28; horizontalAlignment: Text.AlignHCenter; color: Theme.acc; font.pixelSize: 30; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: "Add Button"; color: Theme.acc; font.pixelSize: 17; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
                        }
                        TapHandler { onTapped: Hmi.tcAddButton() }
                    }
                }
            }
        }

        // The modules heard, and how many of their outputs are on buttons
        Card {
            id: modsCard
            anchors.right: parent.right
            width: 290
            height: parent.height
            Column {
                x: 22; y: 20
                width: parent.width - 44
                spacing: 12
                Text { text: "ON THE TRAILCURRENT BUS"; color: Theme.tx2; font.pixelSize: 13; font.weight: Font.Bold; font.letterSpacing: 1 }
                Repeater {
                    model: TrailCurrent.modules.filter(function (m) { return /^(torrent|switchback)/.test(m.id); })
                    delegate: Row {
                        id: mrow
                        required property var modelData
                        readonly property string module: modelData.id.replace(/\d+$/, "")
                        readonly property int inst: +modelData.id.replace(/^\D+/, "")
                        readonly property int used: root.buttons.filter(function (b) { return b.module === mrow.module && b.inst === mrow.inst; }).length
                        spacing: 10
                        Dot { on: mrow.modelData.fresh; y: 7 }
                        Column {
                            width: 220
                            Text { text: mrow.modelData.n; color: Theme.tx; font.pixelSize: 17; font.weight: Font.Bold }
                            Text {
                                width: parent.width
                                wrapMode: Text.WordWrap
                                text: (mrow.modelData.fresh ? "Reporting" : "Not heard") + " · " + mrow.used + (mrow.used === 1 ? " button" : " buttons")
                                color: Theme.tx2; font.pixelSize: 14
                            }
                        }
                    }
                }
                Text {
                    visible: !TrailCurrent.modules.some(function (m) { return /^(torrent|switchback)/.test(m.id); })
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: TrailCurrent.blocked || "No Torrent or Switchback heard yet."
                    color: Theme.tx2; font.pixelSize: 15
                }
            }
            Text {
                x: 22
                width: parent.width - 44
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 20
                wrapMode: Text.WordWrap
                text: "A button can point at a module that is not connected yet. It shows “No data” until the module reports."
                color: Theme.tx2; font.pixelSize: 14
            }
        }
    }

    // ------------------------------------------------------------ one button
    Item {
        id: editor
        anchors.fill: parent
        visible: root.pg === "tcButton" && !!root.cur
        readonly property var b: root.cur || ({ module: "switchback", inst: 0, ch: 0, n: "", icon: "lightbulb" })
        readonly property var fields: Hmi.tcFields(root.bi)
        readonly property string selField: fields[Math.min(Hmi.setSel.tcButton || 0, fields.length - 1)] || ""
        function pick(id) { Hmi.selSet(fields.indexOf(id)); }

        component FRow: Rectangle {
            id: frow
            property string field
            property string label
            default property alias content: val.data
            width: form.width
            height: 56
            radius: 14
            color: editor.selField === field ? Theme.accSoft : "transparent"
            border.width: editor.selField === field && Hmi.edit ? 2 : 0
            border.color: Theme.acc
            Text {
                x: 14; width: 104
                anchors.verticalCenter: parent.verticalCenter
                text: frow.label
                color: Theme.tx; font.pixelSize: 17; font.weight: Font.DemiBold
            }
            Row {
                id: val
                x: 132
                width: parent.width - 146
                anchors.verticalCenter: parent.verticalCenter
                spacing: 8
            }
        }

        Card {
            id: formCard
            anchors.left: parent.left
            anchors.right: sideCard.left
            anchors.rightMargin: 24
            height: parent.height
            Head { id: formHead; title: editor.b.n; hint: "Saved as you go · hold to go back" }
            Column {
                id: form
                anchors.top: formHead.bottom
                x: 12
                width: parent.width - 24
                spacing: 2

                FRow {
                    field: "label"; label: "Label"
                    Rectangle {
                        width: parent.width; height: 44; radius: 12
                        color: Theme.track
                        Text { x: 14; anchors.verticalCenter: parent.verticalCenter; text: editor.b.n; color: Theme.tx; font.pixelSize: 18; font.weight: Font.DemiBold }
                        TapHandler { onTapped: { editor.pick("label"); Hmi.tcFieldAct(root.bi, "label", 1); } }
                    }
                }
                FRow {
                    field: "icon"; label: "Icon"
                    Rectangle {
                        width: iconRow.implicitWidth + 30; height: 44; radius: 22
                        color: Theme.track
                        Row {
                            id: iconRow
                            anchors.centerIn: parent
                            spacing: 8
                            FaIcon { key: editor.b.icon; size: 26; anchors.verticalCenter: parent.verticalCenter }
                            Text { text: Fa.label(editor.b.icon); color: Theme.tx; font.pixelSize: 16; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                        }
                        TapHandler { onTapped: { editor.pick("icon"); Hmi.tcFieldAct(root.bi, "icon", 1); } }
                    }
                    Text { text: "Tap to choose"; color: Theme.tx2; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter }
                }
                FRow {
                    field: "module"; label: "Module"
                    Chip { text: "Torrent · dimmer"; on: editor.b.module === "torrent"; onTapped: { editor.pick("module"); if (!on) Hmi.tcFieldAct(root.bi, "module", 1); } }
                    Chip { text: "Switchback · relay"; on: editor.b.module === "switchback"; onTapped: { editor.pick("module"); if (!on) Hmi.tcFieldAct(root.bi, "module", 1); } }
                }
                FRow {
                    field: "inst"; label: "Address"
                    Repeater {
                        model: 3
                        delegate: Chip {
                            required property int index
                            text: String(index + 1)
                            dot: true
                            dotOn: Hmi.tcHeard(editor.b.module, index)
                            on: editor.b.inst === index
                            onTapped: { editor.pick("inst"); TrailCurrent.setButton(root.bi, { inst: index }); }
                        }
                    }
                    Text {
                        text: Hmi.tcModName(editor.b.module, editor.b.inst) + " · " + (Hmi.tcHeard(editor.b.module, editor.b.inst) ? "reporting" : "not heard")
                        color: Theme.tx2; font.pixelSize: 15
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                FRow {
                    field: "ch"; label: editor.b.module === "torrent" ? "Channel" : "Relay"
                    height: 64
                    Repeater {
                        model: 8
                        delegate: Rectangle {
                            id: cell
                            required property int index
                            readonly property var out: Hmi.tcOutput(editor.b.module, editor.b.inst, index)
                            // Other buttons on this output: its users, less this button once
                            readonly property var others: {
                                var u = out ? out.users.slice() : [];
                                if (mine) { var k = u.indexOf(editor.b.n); if (k >= 0) u.splice(k, 1); }
                                return u;
                            }
                            readonly property bool mine: editor.b.ch === index
                            width: (parent.width - 7 * 6) / 8 - 0.01
                            height: 56; radius: 14
                            color: mine ? Theme.acc : Theme.track
                            Rectangle {
                                visible: !!cell.out && cell.out.fresh && cell.out.on
                                x: parent.width - 14; y: 7
                                width: 7; height: 7; radius: 4
                                color: cell.mine ? Theme.onAcc : Theme.acc
                            }
                            Column {
                                anchors.centerIn: parent
                                width: parent.width - 6
                                Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; text: cell.index + 1; color: cell.mine ? Theme.onAcc : Theme.tx; font.pixelSize: 19; font.weight: Font.Bold }
                                Text {
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    elide: Text.ElideRight
                                    text: cell.mine ? (cell.others.length ? "+ " + cell.others.join(", ") : "This") : cell.others.length ? cell.others.join(", ") : "Free"
                                    color: cell.mine ? Theme.onAcc : Theme.tx2
                                    font.pixelSize: 11
                                }
                            }
                            TapHandler { onTapped: { editor.pick("ch"); TrailCurrent.setButton(root.bi, { ch: cell.index }); } }
                        }
                    }
                }
                FRow {
                    visible: editor.b.module === "torrent"
                    field: "dim"; label: "Dimmable"
                    Switch { on: !!editor.b.dim; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: editor.b.dim ? "Knob and − / + set brightness" : "On and off only"; color: Theme.tx2; font.pixelSize: 15; anchors.verticalCenter: parent.verticalCenter }
                    TapHandler { onTapped: { editor.pick("dim"); Hmi.tcFieldAct(root.bi, "dim", 1); } }
                }
            }
        }

        Card {
            id: sideCard
            anchors.right: parent.right
            width: 290
            height: parent.height
            Column {
                x: 22; y: 20
                width: parent.width - 44
                spacing: 12
                // As it looks in Lights
                Rectangle {
                    width: parent.width; height: 150; radius: 22
                    color: editor.b.on && editor.b.fresh ? Theme.accSoft : Theme.track
                    FaIcon { x: 18; y: 16; key: editor.b.icon; size: 34; color: editor.b.on && editor.b.fresh ? Theme.acc : Theme.tx2 }
                    Column {
                        x: 18
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 16
                        width: parent.width - 36
                        Text { width: parent.width; text: editor.b.n; color: Theme.tx; font.pixelSize: 20; font.weight: Font.Bold; elide: Text.ElideRight }
                        Text { text: root.cur ? root.stateText(root.cur) : ""; color: Theme.tx2; font.pixelSize: 16 }
                    }
                }
                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: Hmi.tcHeard(editor.b.module, editor.b.inst)
                        ? "Test switches the output for 3 seconds, then back, so you can see which load it is."
                        : Hmi.tcModName(editor.b.module, editor.b.inst) + " is not reporting, so the button cannot be tested yet."
                    color: Theme.tx2; font.pixelSize: 14
                }
            }
            Column {
                x: 22
                width: parent.width - 44
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 22
                spacing: 10
                Rectangle {
                    width: parent.width; height: 56; radius: 28
                    color: Theme.track
                    border.width: editor.selField === "test" ? 3 : 0
                    border.color: Theme.acc
                    opacity: Hmi.tcHeard(editor.b.module, editor.b.inst) ? 1 : 0.45
                    Text { anchors.centerIn: parent; text: "Test"; color: Theme.tx; font.pixelSize: 17; font.weight: Font.Bold }
                    TapHandler { onTapped: { editor.pick("test"); Hmi.tcFieldAct(root.bi, "test", 1); } }
                }
                Rectangle {
                    width: parent.width; height: 56; radius: 28
                    color: Theme.track
                    border.width: editor.selField === "delete" ? 3 : 0
                    border.color: Theme.heat
                    Text { anchors.centerIn: parent; text: "Delete Button"; color: Theme.heat; font.pixelSize: 17; font.weight: Font.Bold }
                    TapHandler { onTapped: { editor.pick("delete"); Hmi.tcFieldAct(root.bi, "delete", 1); } }
                }
            }
        }
    }

    // ------------------------------------------------------------ the icon
    Card {
        anchors.fill: parent
        visible: root.pg === "tcIcon" && !!root.cur
        Head { id: iconHead; title: "Icon for " + (root.cur ? root.cur.n : ""); hint: "Turn to move · press to choose · hold to go back" }
        Flickable {
            id: iconFlick
            anchors.top: iconHead.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 20
            anchors.topMargin: 0
            clip: true
            contentHeight: groups.height
            boundsBehavior: Flickable.StopAtBounds
            readonly property real cellW: (width - 9 * 8) / 10
            Column {
                id: groups
                width: parent.width
                spacing: 14
                Repeater {
                    model: Fa.GROUPS
                    delegate: Column {
                        id: grp
                        required property string modelData
                        spacing: 8
                        Text { text: grp.modelData.toUpperCase(); color: Theme.tx2; font.pixelSize: 13; font.weight: Font.Bold; font.letterSpacing: 1 }
                        Grid {
                            columns: 10
                            spacing: 8
                            Repeater {
                                model: Fa.ICONS.filter(function (i) { return i.group === grp.modelData; })
                                delegate: Rectangle {
                                    id: ic
                                    required property var modelData
                                    readonly property int idx: Fa.ICONS.indexOf(modelData)
                                    readonly property bool chosen: root.cur && root.cur.icon === modelData.key
                                    readonly property bool knob: Hmi.tcIconSel === idx
                                    width: iconFlick.cellW; height: 84; radius: 18
                                    color: chosen ? Theme.accSoft : Theme.track
                                    border.width: knob || chosen ? 3 : 0
                                    border.color: Theme.acc
                                    onKnobChanged: if (knob) {
                                        var y = mapToItem(groups, 0, 0).y;
                                        if (y < iconFlick.contentY) iconFlick.contentY = y;
                                        else if (y + height > iconFlick.contentY + iconFlick.height) iconFlick.contentY = y + height - iconFlick.height;
                                    }
                                    Column {
                                        anchors.centerIn: parent
                                        width: parent.width - 6
                                        spacing: 6
                                        FaIcon { key: ic.modelData.key; size: 32; anchors.horizontalCenter: parent.horizontalCenter; color: ic.chosen ? Theme.acc : Theme.tx }
                                        Text {
                                            width: parent.width
                                            horizontalAlignment: Text.AlignHCenter
                                            elide: Text.ElideRight
                                            text: ic.modelData.label
                                            color: ic.chosen ? Theme.acc : Theme.tx2
                                            font.pixelSize: 12; font.weight: Font.DemiBold
                                        }
                                    }
                                    TapHandler { onTapped: Hmi.tcPickIcon(ic.modelData.key) }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
