import QtQuick
import Godwit

// Bringing files in from a USB drive: a button while the drive has new ones,
// progress while they copy, and a note when it has nothing new
Item {
    id: root
    // The library folder to import into: Photos or Videos
    required property var folder
    // What it holds, for the "no new ..." note
    property string noun: "files"

    visible: root.folder.usb || root.folder.importing
    implicitWidth: root.folder.importing ? 240 : root.folder.usbNew ? usbBtn.width : usbNote.implicitWidth
    implicitHeight: 44
    Column {
        visible: root.folder.importing
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        spacing: 6
        Text { text: "Importing " + root.folder.importDone + " of " + root.folder.importTotal; color: Theme.tx; font.pixelSize: 15; font.weight: Font.DemiBold }
        Rectangle {
            width: parent.width; height: 6; radius: 3
            color: Theme.track
            Rectangle {
                height: parent.height; radius: 3
                width: parent.width * root.folder.importProgress
                color: Theme.acc
            }
        }
    }
    Rectangle {
        id: usbBtn
        visible: !root.folder.importing && root.folder.usbNew > 0
        width: usbRow.implicitWidth + 36
        height: 44
        radius: 22
        color: usbTap.pressed ? Theme.accSoft : Theme.acc
        Row {
            id: usbRow
            anchors.centerIn: parent
            spacing: 8
            Icon { name: "arrow-down"; size: 20; color: Theme.onAcc; anchors.verticalCenter: parent.verticalCenter }
            Text { text: "Import " + root.folder.usbNew + " from USB"; color: Theme.onAcc; font.pixelSize: 16; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
        }
        TapHandler { id: usbTap; onTapped: root.folder.importUsb() }
    }
    Text {
        id: usbNote
        visible: !root.folder.importing && root.folder.usbNew === 0
        anchors.verticalCenter: parent.verticalCenter
        text: "USB drive · no new " + root.noun
        color: Theme.tx2; font.pixelSize: 15
    }
}
