import QtQuick
import Godwit

// Display-only toggle; the row it sits in handles touch
Rectangle {
    property bool on

    width: 48
    height: 30
    radius: 15
    color: on ? Theme.acc : Theme.line
    Behavior on color { ColorAnimation { duration: 200 } }

    Rectangle {
        x: parent.on ? 21 : 3
        y: 3
        width: 24
        height: 24
        radius: 12
        color: "#ffffff"
        Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
    }
}
