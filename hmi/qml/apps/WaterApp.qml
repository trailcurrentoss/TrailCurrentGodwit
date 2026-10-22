import QtQuick
import Godwit

// Capsules fill like the tanks; a full black tank turns red here and on the ring
AppPage {
    id: page
    readonly property var wt: Hmi.water

    Row {
        anchors.left: parent.left
        anchors.right: side.left
        anchors.rightMargin: 24
        height: parent.height
        Repeater {
            model: page.wt.tanks
            delegate: Item {
                id: tank
                required property var modelData
                readonly property bool crit: modelData.n === "Black" && modelData.p >= 85
                readonly property var g: modelData.n === "Fresh" ? ["#0088cc", "#48E6FE"]
                                       : modelData.n === "Grey" ? ["#555555", "#9E9E9E"]
                                       : crit ? ["#111111", "#FF5453"] : ["#111111", "#424242"]
                width: parent.width / 3
                height: parent.height
                Column {
                    anchors.centerIn: parent
                    spacing: 8
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: 88; height: 250; radius: 44
                        color: Theme.track
                        clip: true
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: parent.height * tank.modelData.p / 100
                            gradient: Gradient {
                                GradientStop { position: 0; color: tank.g[1] }
                                GradientStop { position: 1; color: tank.g[0] }
                            }
                            Behavior on height { NumberAnimation { duration: 300 } }
                        }
                    }
                    Num { anchors.horizontalCenter: parent.horizontalCenter; text: tank.modelData.p + "%"; color: tank.crit ? Theme.heat : Theme.tx; font.pixelSize: 36 }
                    Text { anchors.horizontalCenter: parent.horizontalCenter; text: tank.modelData.n; color: Theme.tx; font.pixelSize: 18; font.weight: Font.Bold }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: Hmi.volNum(tank.modelData.p * tank.modelData.cap / 100) + " of " + Hmi.volNum(tank.modelData.cap) + " " + Hmi.volUnit()
                        color: Theme.tx2
                        font.pixelSize: 14
                    }
                }
            }
        }
    }

    Column {
        id: side
        anchors.right: parent.right
        width: 360
        height: parent.height
        spacing: 12
        ListRows { app: "water"; width: parent.width; height: 220 }
        Card {
            width: parent.width
            height: parent.height - 220 - 12
            Column {
                x: 20
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text { text: "Fresh Remaining"; color: Theme.tx2; font.pixelSize: 15; font.weight: Font.DemiBold }
                Num { text: (page.wt.tanks[0].p * page.wt.tanks[0].cap / 100 / 9).toFixed(1) + " days"; font.pixelSize: 40; font.letterSpacing: -1 }
                Text { text: "at " + Hmi.volNum(9) + " " + Hmi.volUnit() + "/day"; color: Theme.tx2; font.pixelSize: 14 }
            }
        }
    }
}
