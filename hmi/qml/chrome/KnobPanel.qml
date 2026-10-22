import QtQuick
import Godwit

// A knob on the touchscreen: whatever the knob does right now, as buttons.
// Turn is - / + (hold to repeat), press and hold are the knob's own actions.
// Every button goes through Hmi.act, the same path as the physical knob.
Card {
    id: panel
    property int side: 0
    readonly property var enc: Hmi.encs[side]

    width: 340
    height: col.implicitHeight + 36
    radius: 26
    large: true

    // Closes itself once it has not been used for a while
    Timer { id: idle; interval: 8000; running: panel.visible; onTriggered: Hmi.knobPanel = -1 }
    function used() { idle.restart(); }

    component Btn: Rectangle {
        id: btn
        property string text
        property string icon
        property bool accent: false
        property bool repeat: false
        signal fire
        height: 56
        radius: 28
        color: tap.pressed ? Theme.accSoft : accent ? Theme.acc : Theme.track
        Row {
            anchors.centerIn: parent
            spacing: 8
            Icon { visible: btn.icon !== ""; name: btn.icon; size: 24; color: btn.accent ? Theme.onAcc : Theme.tx; anchors.verticalCenter: parent.verticalCenter }
            Text { visible: btn.text !== ""; text: btn.text; color: btn.accent ? Theme.onAcc : Theme.tx; font.pixelSize: 16; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
        }
        TapHandler { id: tap; onTapped: { btn.fire(); panel.used(); } }
        // Holding a - / + keeps turning, like spinning the knob
        Timer {
            running: btn.repeat && tap.pressed
            interval: 110
            repeat: true
            triggeredOnStart: false
            property int n: 0
            onRunningChanged: n = 0
            onTriggered: { if (++n > 3) { btn.fire(); panel.used(); } }
        }
    }

    Column {
        id: col
        x: 18; y: 18
        width: parent.width - 36
        spacing: 14

        Item {
            width: parent.width
            height: 22
            Text {
                text: (panel.side ? "Right knob" : "Left knob").toUpperCase()
                color: Theme.tx2
                font.pixelSize: 12
                font.weight: Font.Bold
                font.letterSpacing: 1.4
                anchors.verticalCenter: parent.verticalCenter
            }
            Rectangle {
                anchors.right: parent.right
                width: 32; height: 32; radius: 16
                color: Theme.track
                anchors.verticalCenter: parent.verticalCenter
                Icon { anchors.centerIn: parent; name: "close"; size: 18 }
                TapHandler { onTapped: Hmi.knobPanel = -1 }
            }
        }

        // What the knob controls and its current value
        Row {
            width: parent.width
            spacing: 12
            Btn { width: 64; icon: "chevron-back"; repeat: true; onFire: Hmi.act(panel.side, "turn", -1) }
            Column {
                width: parent.width - 2 * 64 - 24
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: panel.enc.name
                    color: Theme.tx
                    font.pixelSize: 17
                    font.weight: Font.Bold
                    elide: Text.ElideRight
                }
                Num {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: panel.enc.value !== "" ? panel.enc.value + panel.enc.unit : panel.enc.sub
                    color: Theme.named(panel.enc.color)
                    font.pixelSize: panel.enc.value !== "" && panel.enc.value.length < 6 ? 30 : 18
                    font.weight: Font.DemiBold
                }
            }
            Btn { width: 64; icon: "chevron-forward"; repeat: true; onFire: Hmi.act(panel.side, "turn", 1) }
        }

        // Press and hold, labelled with what they do right now
        Row {
            width: parent.width
            spacing: 12
            readonly property string pressTxt: panel.enc.hint.indexOf("Press · ") === 0 ? panel.enc.hint.split(" / ")[0].slice(8) : ""
            readonly property string holdTxt: { var m = panel.enc.hint.match(/Hold · (.*)$/); return m ? m[1] : ""; }
            Btn {
                visible: parent.pressTxt !== ""
                width: parent.holdTxt !== "" ? (parent.width - 12) / 2 : parent.width
                accent: true
                text: parent.pressTxt
                onFire: Hmi.act(panel.side, "press", 0)
            }
            Btn {
                visible: parent.holdTxt !== ""
                width: parent.pressTxt !== "" ? (parent.width - 12) / 2 : parent.width
                text: parent.holdTxt
                onFire: Hmi.act(panel.side, "hold", 0)
            }
        }
    }
}
