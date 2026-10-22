import QtQuick
import Godwit

// Head unit entry point: the screen alone, full screen on the panel
Window {
    width: 1024
    height: 600
    visible: true
    visibility: Qt.platform.pluginName.startsWith("eglfs") ? Window.FullScreen : Window.Windowed
    color: "#000000"
    title: "Godwit"

    // Laid out for the 1024x600 head unit panel. On another display, scale by
    // whichever dimension is tighter and let the layout take up the rest of
    // the other, so the screen is filled without stretching anything.
    HeadUnit {
        readonly property real k: Math.min(parent.width / 1024, parent.height / 600)
        width: parent.width / k
        height: parent.height / k
        scale: k
        transformOrigin: Item.TopLeft
    }
}
