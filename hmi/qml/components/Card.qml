import QtQuick
import Godwit

// Rounded panel. The light theme's soft shadow is a stretched 9-patch image
// rather than a blur, so cards cost nothing extra to draw.
Item {
    id: root
    property alias color: bg.color
    property real radius: 22
    property bool large: false
    property bool shadow: Theme.shadows
    // Selection outline: the mockup's "0 0 0 3px var(--k-acc)" ring
    property bool selected: false
    property bool inset: false
    default property alias content: bg.data

    BorderImage {
        visible: root.shadow
        anchors.fill: parent
        anchors.margins: -56
        source: root.large ? "../../assets/shadow-lg.png" : "../../assets/shadow.png"
        border { left: 80; right: 80; top: 80; bottom: 80 }
    }
    Rectangle {
        id: bg
        anchors.fill: parent
        radius: root.radius
        color: Theme.card
    }
    Rectangle {
        visible: root.selected
        anchors.fill: parent
        anchors.margins: root.inset ? 0 : -3
        radius: root.radius + (root.inset ? 0 : 3)
        color: "transparent"
        border.width: 3
        border.color: Theme.acc
    }
}
