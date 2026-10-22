import QtQuick
import Godwit
import "../DemoData.js" as D

// Volume level, shown for a moment whenever a volume changes, whether from a
// knob or a touch control, then fades away.
Item {
    id: hud
    width: 460
    height: 72

    property string label: ""
    property string icon: "volume-high"
    property color tint: Theme.acc
    property int level: 0
    property bool muted: false
    property bool shown: false
    property int phoneVol: Hmi.prefs.phoneVol

    function show(lbl, ic, col, lv, mt) {
        label = lbl; icon = ic; tint = col; level = lv; muted = mt;
        shown = true;
        hideTimer.restart();
    }
    function showAudio() {
        var m = Hmi.muted && Hmi.audio === "radio";
        show(D.SRC[Hmi.audio].n + " Volume", m ? "volume-mute" : "volume-high",
             m ? Theme.heat : Hmi.audio === "radio" ? Theme.warn : Theme.acc, Hmi.vol, m);
    }

    Connections {
        target: Hmi
        function onVolChanged() { hud.showAudio(); }
        function onMutedChanged() { hud.showAudio(); }
        function onGvolChanged() { hud.show("Guidance Volume", "navigate", Theme.cool, Hmi.gvol, false); }
    }
    onPhoneVolChanged: show("Phone Volume", "call", Theme.acc, phoneVol, false)

    Timer { id: hideTimer; interval: 1600; onTriggered: hud.shown = false }

    opacity: shown ? 1 : 0
    visible: opacity > 0
    Behavior on opacity { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
    transform: Translate {
        y: hud.shown ? 0 : -10
        Behavior on y { NumberAnimation { duration: 220; easing.type: Easing.OutCubic } }
    }

    Card {
        anchors.fill: parent
        radius: height / 2
        large: true

        // The dark theme has no shadow, so outline the card instead
        Rectangle {
            visible: Theme.dark
            anchors.fill: parent
            radius: height / 2
            color: "transparent"
            border.width: 1
            border.color: Theme.line
        }
        Rectangle {
            id: badge
            x: 12
            anchors.verticalCenter: parent.verticalCenter
            width: 48; height: 48; radius: 24
            color: Qt.rgba(hud.tint.r, hud.tint.g, hud.tint.b, 0.16)
            Icon { anchors.centerIn: parent; name: hud.icon; size: 24; color: hud.tint }
        }
        Text {
            id: name
            anchors.left: badge.right; anchors.leftMargin: 14
            anchors.top: parent.top; anchors.topMargin: 12
            text: hud.label
            color: Theme.tx2
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
        Rectangle {
            id: track
            anchors.left: badge.right; anchors.leftMargin: 14
            anchors.right: value.left; anchors.rightMargin: 16
            anchors.bottom: parent.bottom; anchors.bottomMargin: 16
            height: 10; radius: 5
            color: Theme.track
            Rectangle {
                width: hud.muted ? 0 : Math.max(height, parent.width * hud.level / 100)
                height: parent.height; radius: parent.radius
                color: hud.tint
                visible: hud.level > 0
                Behavior on width { NumberAnimation { duration: 90 } }
            }
        }
        Num {
            id: value
            anchors.right: parent.right; anchors.rightMargin: 24
            anchors.verticalCenter: parent.verticalCenter
            width: 56
            horizontalAlignment: Text.AlignRight
            text: hud.muted ? "—" : hud.level
            font.pixelSize: 28
            font.weight: Font.Bold
        }
    }
}
