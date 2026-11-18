import QtQuick
import Godwit

// Head unit entry point: the screen alone, full screen on the panel
Window {
    id: window
    width: 1024
    height: 600
    visible: true
    visibility: Qt.platform.pluginName.startsWith("eglfs") ? Window.FullScreen : Window.Windowed
    color: "#000000"
    title: "Godwit"

    // Clockwise turn, in degrees, from the panel's own orientation to the way
    // it is mounted. Set from GODWIT_ROTATION. A portrait panel (720x1280)
    // fitted in landscape needs 90 or 270.
    property int screenRotation: 0
    readonly property bool sideways: screenRotation % 180 !== 0

    // The screen as the driver sees it, turned to the way it is mounted. Touch
    // follows: Qt maps each point through this item's rotation.
    Item {
        width: window.sideways ? window.height : window.width
        height: window.sideways ? window.width : window.height
        anchors.centerIn: parent
        rotation: window.screenRotation

        // Laid out for the 1024x600 head unit panel. On another display, scale
        // by whichever dimension is tighter and let the layout take up the rest
        // of the other, so the screen is filled without stretching anything.
        HeadUnit {
            readonly property real k: Math.min(parent.width / 1024, parent.height / 600)
            width: parent.width / k
            height: parent.height / k
            scale: k
            transformOrigin: Item.TopLeft
        }
    }
}
