import QtQuick
import Godwit

// Tinted Ionicon, rasterised once per name/colour/size by the icon provider
Image {
    property string name
    property color color: Theme.tx
    property int size: 24

    width: size
    height: size
    sourceSize: Qt.size(size, size)
    source: name ? "image://icon/" + name + "/" + color : ""
}
