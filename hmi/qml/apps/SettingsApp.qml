import QtQuick
import Godwit

// Settings: the selected row's detail on the left, the list on the right
// beside the knob that drives it. Wi-Fi join and phone pairing take the whole
// screen; confirmations sit over everything.
AppPage {
    id: page
    readonly property string pg: Hmi.setPage
    readonly property var rows: Hmi.setRows(pg, Hmi.setArg)
    readonly property int sel: Math.min(Hmi.setSel[pg] || 0, rows.length - 1)
    readonly property var cur: rows[sel]
    readonly property bool fullPage: pg === "wifiJoin" || pg === "btPair"

    // Signal strength as 4 bars
    component Bars: Row {
        property real sig: 0
        property color on: Theme.tx
        property real scaleBy: 1
        spacing: 3 * scaleBy
        Repeater {
            model: 4
            delegate: Rectangle {
                required property int index
                anchors.bottom: parent.bottom
                width: 4 * parent.scaleBy
                height: (5 + index * 4) * parent.scaleBy
                radius: 2
                color: index < Math.max(1, Math.round(parent.sig * 4)) ? parent.on : Theme.line
            }
        }
    }

    // ------------------------------------------------------------ list pages
    Item {
        anchors.fill: parent
        visible: !page.fullPage

        // Detail of the selected row
        Column {
            anchors.left: parent.left
            anchors.leftMargin: 12
            anchors.right: list.left
            anchors.rightMargin: 36
            anchors.verticalCenter: parent.verticalCenter
            spacing: 14

            Text {
                text: Hmi.setStack.map(function (p, i) { return Hmi.setTitle(p, Hmi.setArgs[i]); }).join("  ›  ")
                color: Theme.tx2
                font.pixelSize: 16
                font.weight: Font.DemiBold
                width: parent.width
                elide: Text.ElideLeft
            }
            Row {
                spacing: 16
                Rectangle {
                    visible: !!(page.cur && (page.cur.icon || page.cur.kind === "net" || page.cur.kind === "phone"))
                    width: 64; height: 64; radius: 32
                    color: page.cur && page.cur.danger ? Qt.rgba(1, 0.33, 0.33, 0.15) : Theme.accSoft
                    anchors.verticalCenter: parent.verticalCenter
                    Icon {
                        anchors.centerIn: parent
                        size: 32
                        color: page.cur && page.cur.danger ? Theme.heat : Theme.acc
                        name: !page.cur ? "" : page.cur.kind === "net" ? "wifi" : page.cur.kind === "phone" ? "phone-portrait-outline" : (page.cur.icon || "")
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.cur ? page.cur.n : ""
                    color: page.cur && page.cur.danger ? Theme.heat : Theme.tx
                    font.pixelSize: 30
                    font.weight: Font.Bold
                    font.letterSpacing: -0.4
                }
            }

            // Big value for switches, values, choices and info
            Num {
                visible: !!page.cur && ["sw", "val", "choice", "info"].indexOf(page.cur.kind) >= 0
                width: parent.width
                text: page.cur ? page.cur.v : ""
                font.pixelSize: page.cur && page.cur.kind === "info" ? 48 : 80
                font.letterSpacing: -2
                color: page.cur && page.cur.kind === "sw" && page.cur.on ? Theme.acc : Theme.tx
            }
            Rectangle {
                visible: !!page.cur && page.cur.kind === "val"
                width: parent.width; height: 12; radius: 6
                color: Theme.track
                Rectangle {
                    width: parent.width * (page.cur && page.cur.frac !== undefined ? page.cur.frac : 0)
                    height: parent.height; radius: 6
                    color: Theme.acc
                    Behavior on width { NumberAnimation { duration: 200 } }
                }
            }
            // Touch steppers for values
            Row {
                visible: !!page.cur && page.cur.kind === "val"
                spacing: 12
                Repeater {
                    model: [-1, 1]
                    delegate: Rectangle {
                        id: setStep
                        required property int modelData
                        width: 64; height: 52; radius: 26
                        color: setStepTap.pressed ? Theme.accSoft : Theme.track
                        Icon { anchors.centerIn: parent; name: setStep.modelData < 0 ? "chevron-back" : "chevron-forward"; size: 22 }
                        TapHandler { id: setStepTap; onTapped: page.cur.adj(setStep.modelData) }
                    }
                }
            }
            // Every option of a choice, current one filled
            Flow {
                visible: !!page.cur && page.cur.kind === "choice"
                width: parent.width
                spacing: 8
                Repeater {
                    model: page.cur && page.cur.opts ? page.cur.opts : []
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        readonly property bool on: index === page.cur.idx
                        width: optText.implicitWidth + 28; height: 40; radius: 20
                        color: on ? Theme.acc : Theme.track
                        Text { id: optText; anchors.centerIn: parent; text: parent.modelData; color: parent.on ? Theme.onAcc : Theme.tx; font.pixelSize: 15; font.weight: Font.DemiBold }
                        TapHandler { onTapped: page.cur.adj(index - page.cur.idx) }
                    }
                }
            }
            // Wi-Fi network detail
            Row {
                visible: !!page.cur && page.cur.kind === "net"
                spacing: 14
                Bars { sig: page.cur && page.cur.sig ? page.cur.sig : 0; on: Theme.acc; scaleBy: 2.2; anchors.bottom: parent.bottom }
                Text {
                    anchors.bottom: parent.bottom
                    text: page.cur && page.cur.kind === "net" ? page.cur.v + " · " + page.cur.sec + " · " + Math.round(page.cur.sig * 100) + "%" : ""
                    color: Theme.tx2
                    font.pixelSize: 20
                }
            }
            // Summary under page rows and phones
            Text {
                visible: !!page.cur && (page.cur.kind === "page" || page.cur.kind === "phone") && page.cur.v !== ""
                width: parent.width
                text: page.cur ? page.cur.v : ""
                color: Theme.tx2
                font.pixelSize: 22
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                color: Theme.tx2
                font.pixelSize: 16
                text: {
                    if (!page.cur) return "";
                    if (page.cur.desc) return page.cur.desc;
                    if (Hmi.edit) return "Turn the right knob to adjust. Press to finish.";
                    switch (page.cur.kind) {
                    case "page": return "Tap, or press the right knob, to open.";
                    case "sw": return "Tap anywhere on the row, or press the knob, to turn " + (page.cur.on ? "off." : "on.");
                    case "val": case "choice": return "Tap or press, then turn the right knob to change.";
                    case "net": return page.cur.connected || page.cur.saved ? "Press for network details." : "Press to connect.";
                    case "phone": return "Press for options.";
                    case "action": return "Tap, or press the right knob.";
                    }
                    return "";
                }
            }
        }

        // The list
        Card {
            id: list
            anchors.right: parent.right
            width: 420
            height: parent.height

            Item {
                id: head
                width: parent.width
                height: 54
                Row {
                    x: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Icon {
                        visible: Hmi.setStack.length > 1
                        name: "chevron-back"
                        size: 22
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Text {
                        text: Hmi.setTitle(page.pg, Hmi.setArg)
                        color: Theme.tx
                        font.pixelSize: 20
                        font.weight: Font.Bold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
                Text {
                    visible: Hmi.setStack.length > 1
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Hold to go back"
                    color: Theme.tx2
                    font.pixelSize: 13
                }
                TapHandler { enabled: Hmi.setStack.length > 1; onTapped: Hmi.backSet() }
            }

            ListView {
                id: lv
                anchors.top: head.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 10
                anchors.topMargin: 0
                clip: true
                spacing: 2
                model: page.rows.length
                currentIndex: page.sel
                highlightFollowsCurrentItem: false
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                boundsBehavior: Flickable.StopAtBounds

                delegate: Rectangle {
                    id: row
                    required property int index
                    readonly property var r: page.rows[index]
                    readonly property bool isSel: index === page.sel
                    readonly property bool editing: isSel && Hmi.edit
                    width: lv.width
                    height: 56
                    radius: 14
                    color: isSel ? Theme.accSoft : "transparent"
                    Behavior on color { ColorAnimation { duration: 200 } }

                    Row {
                        id: lead
                        x: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 12
                        Icon {
                            visible: !!row.r.icon
                            name: row.r.icon || ""
                            size: 22
                            color: row.r.danger ? Theme.heat : Theme.tx
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Icon {
                            visible: row.r.kind === "net" && row.r.connected
                            name: "checkmark"
                            size: 20
                            color: Theme.acc
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    // The name gives way to the value and controls on the right
                    Text {
                        anchors.left: lead.right
                        anchors.leftMargin: lead.width > 0 ? 12 : 0
                        anchors.right: trail.left
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: row.r.n
                        color: row.r.danger ? Theme.heat : Theme.tx
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    Row {
                        id: trail
                        anchors.right: parent.right
                        anchors.rightMargin: 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10

                        // Value, or the summary of a page
                        Rectangle {
                            visible: row.r.v !== "" && row.r.kind !== "sw" && row.r.kind !== "action"
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.min(valText.implicitWidth, 170) + 20
                            height: valText.implicitHeight + 8
                            radius: 12
                            color: row.editing ? Theme.acc : "transparent"
                            Num {
                                id: valText
                                anchors.centerIn: parent
                                width: Math.min(implicitWidth, 170)
                                text: row.r.v
                                color: row.editing ? Theme.onAcc : Theme.tx2
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }
                        }
                        Icon {
                            visible: row.r.kind === "net" && row.r.sec !== "Open"
                            name: "lock-closed-outline"
                            size: 16
                            color: Theme.tx2
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Bars {
                            visible: row.r.kind === "net"
                            sig: row.r.sig || 0
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Switch {
                            visible: row.r.kind === "sw"
                            on: !!row.r.on
                            anchors.verticalCenter: parent.verticalCenter
                        }
                        Icon {
                            visible: row.r.kind === "page" || row.r.kind === "phone" || (row.r.kind === "net" && (row.r.connected || row.r.saved))
                            name: "chevron-forward"
                            size: 18
                            color: Theme.tx2
                            anchors.verticalCenter: parent.verticalCenter
                        }
                    }
                    TapHandler { onTapped: Hmi.setTap(row.index) }
                }
            }
        }
    }

    // ------------------------------------------------------------ Wi-Fi join
    Item {
        anchors.fill: parent
        visible: page.pg === "wifiJoin" && !!Hmi.join
        readonly property bool parked: Hmi.gear === "P"

        Item {
            id: joinHead
            width: parent.width
            height: 110
            Column {
                spacing: 4
                Text { text: "Join " + (Hmi.join ? Hmi.join.ssid : ""); color: Theme.tx; font.pixelSize: 26; font.weight: Font.Bold }
                Text { text: (Hmi.join ? Hmi.join.sec : "") + " · Enter the network password"; color: Theme.tx2; font.pixelSize: 16 }
            }
            Rectangle {
                id: pwField
                y: 56
                width: 600; height: 52; radius: 14
                color: Theme.track
                Num {
                    x: 18
                    width: parent.width - 36
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: 22
                    text: !Hmi.join ? "" : Hmi.join.pw === "" ? "Password" : Hmi.join.show ? Hmi.join.pw : "•".repeat(Hmi.join.pw.length)
                    color: Hmi.join && Hmi.join.pw !== "" ? Theme.tx : Theme.tx2
                }
            }
            Row {
                anchors.right: parent.right
                y: 56
                spacing: 10
                Rectangle {
                    width: 52; height: 52; radius: 26
                    color: Theme.track
                    Icon { anchors.centerIn: parent; name: Hmi.join && Hmi.join.show ? "eye-off-outline" : "eye-outline"; size: 24 }
                    TapHandler { onTapped: Hmi.join = Object.assign({}, Hmi.join, { show: !Hmi.join.show }) }
                }
                Rectangle {
                    width: 120; height: 52; radius: 26
                    color: Theme.track
                    Text { anchors.centerIn: parent; text: "Cancel"; color: Theme.tx; font.pixelSize: 17; font.weight: Font.Bold }
                    TapHandler { onTapped: Hmi.backSet() }
                }
                Rectangle {
                    width: 150; height: 52; radius: 26
                    color: Theme.acc
                    opacity: Hmi.joinCanConnect() ? 1 : 0.4
                    Text {
                        anchors.centerIn: parent
                        text: Hmi.join && Hmi.join.busy ? "Connecting" : "Connect"
                        color: Theme.onAcc
                        font.pixelSize: 17
                        font.weight: Font.Bold
                    }
                    TapHandler { onTapped: Hmi.joinConnect() }
                }
            }
        }

        Keyboard {
            visible: parent.parked
            anchors.top: joinHead.bottom
            anchors.topMargin: 14
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            enterLabel: "Connect"
            enterEnabled: Hmi.joinCanConnect()
            onKey: (ch) => Hmi.joinKey(ch)
            onBackspace: Hmi.joinBackspace()
            onEnter: Hmi.joinConnect()
        }
        // Typing is locked out while the vehicle can move
        Card {
            visible: !parent.parked
            anchors.top: joinHead.bottom
            anchors.topMargin: 14
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            Column {
                anchors.centerIn: parent
                spacing: 10
                Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "lock-closed-outline"; size: 44; color: Theme.tx2 }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Shift to Park to enter a password"; color: Theme.tx; font.pixelSize: 22; font.weight: Font.Bold }
                Text { anchors.horizontalCenter: parent.horizontalCenter; text: "Passwords can't be typed while the vehicle can move."; color: Theme.tx2; font.pixelSize: 16 }
            }
        }
    }

    // ------------------------------------------------------------ phone pairing
    Card {
        id: pairCard
        anchors.centerIn: parent
        width: 620
        height: 380
        visible: page.pg === "btPair"
        readonly property var p: Hmi.pairing || {}

        Column {
            anchors.centerIn: parent
            width: parent.width - 80
            spacing: 14

            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 96; height: 96
                Rectangle {
                    id: pulse
                    anchors.centerIn: parent
                    width: 96; height: 96; radius: 48
                    color: Theme.accSoft
                    SequentialAnimation on scale {
                        running: page.visible && page.pg === "btPair" && (Hmi.pairing || {}).stage === "search"
                        loops: Animation.Infinite
                        NumberAnimation { from: 0.85; to: 1.1; duration: 900; easing.type: Easing.InOutSine }
                        NumberAnimation { from: 1.1; to: 0.85; duration: 900; easing.type: Easing.InOutSine }
                    }
                }
                Icon { anchors.centerIn: parent; name: "bluetooth"; size: 44; color: Theme.acc }
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.tx
                font.pixelSize: 26
                font.weight: Font.Bold
                text: pairCard.p.stage === "confirm" ? "Pair with " + pairCard.p.phone.name + "?"
                    : pairCard.p.stage === "busy" ? "Pairing with " + pairCard.p.phone.name
                    : "Godwit is ready to pair"
            }
            Num {
                visible: pairCard.p.stage === "confirm"
                anchors.horizontalCenter: parent.horizontalCenter
                text: pairCard.p.code || ""
                font.pixelSize: 64
                font.letterSpacing: 2
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.tx2
                font.pixelSize: 17
                text: pairCard.p.stage === "confirm" ? "If it matches your phone, tap Pair here and on the phone."
                    : pairCard.p.stage === "busy" ? ""
                    : "On your phone, open Bluetooth settings and choose “Godwit”."
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 12
                Rectangle {
                    width: 140; height: 52; radius: 26
                    color: Theme.track
                    Text { anchors.centerIn: parent; text: "Cancel"; color: Theme.tx; font.pixelSize: 17; font.weight: Font.Bold }
                    TapHandler { onTapped: Hmi.backSet() }
                }
                Rectangle {
                    visible: (Hmi.pairing || {}).stage === "confirm"
                    width: 140; height: 52; radius: 26
                    color: Theme.acc
                    Text { anchors.centerIn: parent; text: "Pair"; color: Theme.onAcc; font.pixelSize: 17; font.weight: Font.Bold }
                    TapHandler { onTapped: Hmi.pairAccept() }
                }
            }
        }
    }

    // ------------------------------------------------------------ confirmation
    Rectangle {
        anchors.fill: parent
        anchors.margins: -24
        visible: !!Hmi.confirmBox
        color: Qt.rgba(0, 0, 0, Theme.dark ? 0.6 : 0.3)
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onClicked: Hmi.confirmBox = null }

        Card {
            anchors.centerIn: parent
            width: 540
            height: confirmCol.implicitHeight + 56
            large: true
            // Keep taps on the card from closing it or reaching the page behind
            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
            Column {
                id: confirmCol
                anchors.centerIn: parent
                width: parent.width - 56
                spacing: 12
                Text { width: parent.width; wrapMode: Text.WordWrap; text: Hmi.confirmBox ? Hmi.confirmBox.title : ""; color: Theme.tx; font.pixelSize: 24; font.weight: Font.Bold }
                Text { width: parent.width; wrapMode: Text.WordWrap; text: Hmi.confirmBox ? Hmi.confirmBox.body : ""; color: Theme.tx2; font.pixelSize: 17 }
                Item { width: 1; height: 6 }
                Row {
                    anchors.right: parent.right
                    spacing: 12
                    Rectangle {
                        width: 140; height: 52; radius: 26
                        color: Theme.track
                        Text { anchors.centerIn: parent; text: "Cancel"; color: Theme.tx; font.pixelSize: 17; font.weight: Font.Bold }
                        TapHandler { onTapped: Hmi.confirmBox = null }
                    }
                    Rectangle {
                        width: 160; height: 52; radius: 26
                        color: Theme.heat
                        Text { anchors.centerIn: parent; text: Hmi.confirmBox ? Hmi.confirmBox.ok : ""; color: Theme.onAlert; font.pixelSize: 17; font.weight: Font.Bold }
                        TapHandler { onTapped: Hmi.doConfirm() }
                    }
                }
            }
        }
    }
}
