import QtQuick
import Godwit

// Rounded label with an optional leading icon
Rectangle {
    id: root
    property string text
    property string icon
    property color fg: Theme.acc
    property int fontSize: 14
    property int iconSize: 15
    property int hpad: 12
    property int vpad: 5

    implicitWidth: row.implicitWidth + hpad * 2
    implicitHeight: row.implicitHeight + vpad * 2
    radius: height / 2
    color: Theme.accSoft

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Icon { visible: root.icon !== ""; name: root.icon; color: root.fg; size: root.iconSize; anchors.verticalCenter: parent.verticalCenter }
        Num {
            text: root.text
            color: root.fg
            font.pixelSize: root.fontSize
            font.weight: Font.Bold
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
