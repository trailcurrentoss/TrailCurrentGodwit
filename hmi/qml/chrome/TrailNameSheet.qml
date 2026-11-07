import QtQuick
import Godwit

// Naming a track: after recording one, or renaming one in Trails. Only shown
// in Park, as it is typing; a track stopped on the move waits here until then,
// already saved under its date.
Rectangle {
    id: sheet
    color: Theme.bg
    // Swallow every press so nothing underneath reacts to it
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (w) => w.accepted = true }

    readonly property var t: Hmi.trailName || ({})

    Item {
        id: head
        x: 24; y: 18
        width: parent.width - 48
        height: 118
        Column {
            spacing: 4
            Text { text: sheet.t.title || ""; color: Theme.tx; font.pixelSize: 24; font.weight: Font.Bold }
            Text { text: sheet.t.note || ""; color: Theme.tx2; font.pixelSize: 15 }
        }
        Rectangle {
            y: 62
            width: 620; height: 52; radius: 14
            color: Theme.track
            Text {
                x: 18
                width: parent.width - 36
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: 22
                elide: Text.ElideLeft
                text: sheet.t.edit ? sheet.t.edit : (sheet.t.placeholder || "Name")
                color: sheet.t.edit ? Theme.tx : Theme.tx2
            }
        }
        Row {
            anchors.right: parent.right
            y: 62
            spacing: 10
            Rectangle {
                width: 150; height: 52; radius: 26
                color: Theme.track
                Text { anchors.centerIn: parent; text: sheet.t.keep ? "Keep Date" : "Cancel"; color: Theme.tx; font.pixelSize: 17; font.weight: Font.Bold }
                TapHandler { onTapped: Hmi.trailNameCancel() }
            }
            Rectangle {
                width: 150; height: 52; radius: 26
                color: Theme.acc
                opacity: Hmi.trailNameCanSave() ? 1 : 0.4
                Text { anchors.centerIn: parent; text: "Save"; color: Theme.onAcc; font.pixelSize: 17; font.weight: Font.Bold }
                TapHandler { onTapped: Hmi.trailNameSave() }
            }
        }
    }

    Keyboard {
        anchors.top: head.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 24
        anchors.topMargin: 10
        enterLabel: "Save"
        enterEnabled: Hmi.trailNameCanSave()
        onKey: (ch) => Hmi.trailNameKey(ch)
        onBackspace: Hmi.trailNameBackspace()
        onEnter: Hmi.trailNameSave()
    }
}
