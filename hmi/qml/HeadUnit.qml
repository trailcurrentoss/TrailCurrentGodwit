import QtQuick
import Godwit
import "DemoData.js" as D

// The 10.1" 1024x600 head unit screen
Item {
    id: screen
    width: 1024
    height: 600
    clip: true
    focus: true

    // The screen being opened loads synchronously, so the first frame already
    // has it. The rest load in the background shortly after and stay alive.
    property bool preload: false
    Timer { interval: 1500; running: true; onTriggered: screen.preload = true }

    Rectangle { anchors.fill: parent; color: Theme.bg }

    Repeater {
        model: D.APPS
        delegate: Loader {
            id: page
            required property var modelData
            readonly property bool current: !Hmi.launcher && Hmi.app === modelData.id
            property bool visited: false
            onCurrentChanged: if (current) visited = true
            Component.onCompleted: if (current) visited = true

            // Navigation runs under the status and bottom bars; other apps sit between them
            readonly property bool fullBleed: modelData.id === "nav"
            x: fullBleed ? 0 : 24
            y: fullBleed ? 0 : 40 + 18
            width: fullBleed ? screen.width : screen.width - 48
            height: fullBleed ? screen.height : screen.height - 40 - 76 - 36

            active: visited || screen.preload
            asynchronous: !current
            visible: current && status === Loader.Ready
            source: {
                var n = modelData.id;
                return "apps/" + { nav: "Nav", media: "Media", radio: "Radio", climate: "Climate", phone: "Phone",
                                   energy: "Energy", water: "Water", level: "Level", lights: "Lights",
                                   cams: "Cams", settings: "Settings" }[n] + "App.qml";
            }
        }
    }

    Loader {
        x: 0; y: 40
        width: screen.width
        height: screen.height - 40 - 76
        active: Hmi.launcher
        sourceComponent: Launcher {}
    }

    StatusBar { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top }
    BottomBar { anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom }

    Loader {
        x: 24; y: 40 + 10
        width: screen.width - 48
        active: Hmi.incoming
        sourceComponent: IncomingBanner {}
    }

    // A knob's touch controls, above that knob's end of the bottom bar
    Loader {
        active: Hmi.knobPanel >= 0
        x: Hmi.knobPanel === 1 ? screen.width - 12 - (item ? item.width : 0) : 12
        y: screen.height - 76 - 12 - (item ? item.height : 0)
        sourceComponent: KnobPanel { side: Hmi.knobPanel }
    }

    // Alert banner: vehicle interlocks first, then shore power, then transient alerts
    Rectangle {
        id: alertBanner
        readonly property string msg: Hmi.interlock() ? (Hmi.level.jacks === "Retracting" ? "Retracting jacks · stay stopped" : "Jacks down · retract before driving")
                                    : (Hmi.gear !== "P" && Hmi.energy.shore) ? "Shore power connected · unplug before driving"
                                    : Hmi.alert ? Hmi.alert.msg : ""
        visible: msg !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        y: 40 + (Hmi.incoming ? 104 : 12)
        width: alertRow.implicitWidth + 48
        height: 54
        radius: 24
        color: Theme.heat
        Row {
            id: alertRow
            anchors.centerIn: parent
            spacing: 10
            Icon { name: "warning"; color: Theme.onAlert; size: 24; anchors.verticalCenter: parent.verticalCenter }
            Text { text: alertBanner.msg; color: Theme.onAlert; font.pixelSize: 20; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
        }
    }

    VolumeHud {
        z: 10
        anchors.horizontalCenter: parent.horizontalCenter
        y: 40 + (Hmi.incoming ? 104 : 12)
    }

    Rectangle {
        visible: Hmi.toastMsg !== ""
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 76 + 14
        width: toastText.implicitWidth + 44
        height: toastText.implicitHeight + 24
        radius: 24
        color: Theme.tx
        Text { id: toastText; anchors.centerIn: parent; text: Hmi.toastMsg; color: Theme.bg; font.pixelSize: 17; font.weight: Font.DemiBold }
    }

    // Screen brightness. Replace with the panel backlight once it is wired.
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: (100 - Hmi.prefs.bright) / 100 * 0.6
    }

    // Keyboard stand-ins for the knobs, as in the mockup:
    // left a/d turn, s press; right j/l turn, k press; Shift+press = hold
    Keys.onPressed: (event) => {
        var map = { "a": [0, -1], "d": [0, 1], "s": [0, 0], "j": [1, -1], "l": [1, 1], "k": [1, 0] };
        var m = map[event.text.toLowerCase()];
        if (!m) return;
        event.accepted = true;
        if (m[1] !== 0) { Knobs.turn(m[0], m[1]); return; }
        if (event.isAutoRepeat) return;
        if (event.modifiers & Qt.ShiftModifier) Knobs.hold(m[0]); else Knobs.press(m[0]);
    }
}
