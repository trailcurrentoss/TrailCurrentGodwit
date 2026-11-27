import QtQuick
import Godwit
import "../FaIcons.js" as Fa

// A light button's icon, by its key ("lightbulb", "caravan"): a Font Awesome
// Solid glyph, as on the other TrailCurrent screens
Text {
    property string key
    property int size: 24

    width: size
    height: size
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    font.family: "Font Awesome 6 Free"
    font.styleName: "Solid"
    font.pixelSize: Math.round(size * 0.82)
    color: Theme.tx
    text: Fa.glyph(key)
}
