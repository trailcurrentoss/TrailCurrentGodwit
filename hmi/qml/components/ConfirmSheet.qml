import QtQuick
import Godwit

// Hmi.confirmBox, over the whole of an app's page: the page's own margins are
// taken back so it covers everything between the bars
Rectangle {
    anchors.fill: parent
    anchors.margins: -24
    visible: !!Hmi.confirmBox
    color: Qt.rgba(0, 0, 0, Theme.dark ? 0.6 : 0.3)
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onClicked: Hmi.confirmBox = null }

    Card {
        anchors.centerIn: parent
        width: 540
        height: confirmCol.implicitHeight + 56
        large: true
        // Keep taps on the card from closing it or reaching the page behind
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
        Column {
            id: confirmCol
            anchors.centerIn: parent
            width: parent.width - 56
            spacing: 12
            Text { width: parent.width; wrapMode: Text.WordWrap; text: Hmi.confirmBox ? Hmi.confirmBox.title : ""; color: Theme.tx; font.pixelSize: 24; font.weight: Font.Bold }
            Text { width: parent.width; wrapMode: Text.WordWrap; text: Hmi.confirmBox ? Hmi.confirmBox.body : ""; color: Theme.tx2; font.pixelSize: 17 }
            Item { width: 1; height: 6 }
            Row {
                anchors.right: parent.right
                spacing: 12
                Rectangle {
                    width: 140; height: 52; radius: 26
                    color: Theme.track
                    Text { anchors.centerIn: parent; text: "Cancel"; color: Theme.tx; font.pixelSize: 17; font.weight: Font.Bold }
                    TapHandler { onTapped: Hmi.confirmBox = null }
                }
                Rectangle {
                    width: 160; height: 52; radius: 26
                    color: Theme.heat
                    Text { anchors.centerIn: parent; text: Hmi.confirmBox ? Hmi.confirmBox.ok : ""; color: Theme.onAlert; font.pixelSize: 17; font.weight: Font.Bold }
                    TapHandler { onTapped: Hmi.doConfirm() }
                }
            }
        }
    }
}
