import QtQuick
import Godwit

// On-screen keyboard for short text such as Wi-Fi passwords.
// Letters, and a second page of numbers and symbols.
Item {
    id: kb
    property string enterLabel: "Done"
    property bool enterEnabled: true
    property bool shifted: false
    property bool symbols: false

    signal key(string ch)
    signal backspace
    signal enter

    readonly property real gap: 8
    readonly property real keyH: (height - 3 * gap) / 4
    readonly property real unit: (width - 9 * gap) / 10
    readonly property var rows: symbols
        ? ["1234567890", "@#$%&*-+=_", "!?.,'\"/:;"]
        : ["qwertyuiop", "asdfghjkl", "zxcvbnm"]

    component Key: Rectangle {
        id: k
        property string label
        property string icon
        property bool special: false
        property bool accent: false
        property bool active: true
        signal tapped
        height: kb.keyH
        radius: 10
        color: tap.pressed ? Theme.accSoft : accent ? Theme.acc : special ? Theme.track : Theme.card
        opacity: active ? 1 : 0.4
        Text {
            visible: k.icon === ""
            anchors.centerIn: parent
            text: k.label
            color: k.accent ? Theme.onAcc : Theme.tx
            font.pixelSize: k.label.length > 1 ? 17 : 24
            font.weight: k.label.length > 1 ? Font.Bold : Font.Normal
        }
        Icon { visible: k.icon !== ""; anchors.centerIn: parent; name: k.icon; size: 26; color: Theme.tx }
        TapHandler { id: tap; enabled: k.active; onTapped: k.tapped() }
    }

    Column {
        anchors.fill: parent
        spacing: kb.gap

        Repeater {
            model: 3
            delegate: Row {
                id: keyRow
                required property int index
                readonly property string chars: kb.rows[index]
                // The third row carries Shift and Backspace at its ends
                readonly property bool edges: index === 2
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: kb.gap

                Key {
                    visible: keyRow.edges
                    width: kb.unit * 1.5
                    special: true
                    icon: kb.symbols ? "" : "arrow-up-outline"
                    color: kb.shifted && !kb.symbols ? Theme.acc : Theme.track
                    onTapped: kb.shifted = !kb.shifted
                }
                Repeater {
                    model: keyRow.chars.length
                    delegate: Key {
                        required property int index
                        readonly property string ch: keyRow.chars.charAt(index)
                        width: kb.unit
                        label: kb.shifted && !kb.symbols ? ch.toUpperCase() : ch
                        onTapped: { kb.key(label); kb.shifted = false; }
                    }
                }
                Key {
                    visible: keyRow.edges
                    width: kb.unit * 1.5
                    special: true
                    icon: "backspace-outline"
                    onTapped: kb.backspace()
                }
            }
        }

        Row {
            spacing: kb.gap
            Key {
                width: kb.unit * 1.5
                special: true
                label: kb.symbols ? "ABC" : "123"
                onTapped: { kb.symbols = !kb.symbols; kb.shifted = false; }
            }
            Key {
                width: kb.width - kb.unit * 4 - kb.gap * 2
                label: "space"
                special: true
                onTapped: kb.key(" ")
            }
            Key {
                width: kb.unit * 2.5
                accent: true
                active: kb.enterEnabled
                label: kb.enterLabel
                onTapped: kb.enter()
            }
        }
    }
}
