import QtQuick
import Godwit

// Text with tabular figures, so changing numbers do not jitter sideways
Text {
    color: Theme.tx
    font.features: { "tnum": 1 }
    elide: Text.ElideRight
}
