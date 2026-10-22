import QtQuick
import Godwit
import "../DemoData.js" as D

// Favorites then Recents: one selection runs through both, following the right knob
AppPage {
    id: page
    readonly property int sel: Hmi.phone.sel

    Loader {
        anchors.fill: parent
        active: Hmi.inCall && !Hmi.incoming
        sourceComponent: CallView {}
    }

    Item {
        anchors.fill: parent
        visible: !(Hmi.inCall && !Hmi.incoming)

        Column {
            anchors.left: parent.left
            anchors.right: recents.left
            anchors.rightMargin: 24
            height: parent.height
            spacing: 12
            Text { id: favTitle; text: "Favorites"; color: Theme.tx; font.pixelSize: 28; font.weight: Font.Bold; font.letterSpacing: -0.4 }
            Grid {
                id: favs
                width: parent.width
                height: parent.height - favTitle.height - 12
                columns: 3
                spacing: 12
                Repeater {
                    model: D.FAVS.length
                    delegate: Card {
                        id: fav
                        required property int index
                        readonly property var c: D.CONTACTS[D.FAVS[index]]
                        readonly property bool on: page.sel === index
                        width: (favs.width - 24) / 3
                        height: (favs.height - 12) / 2
                        selected: on
                        Column {
                            anchors.centerIn: parent
                            spacing: 10
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: 76; height: 76; radius: 38
                                color: fav.on ? Theme.acc : Theme.track
                                Behavior on color { ColorAnimation { duration: 200 } }
                                Text { anchors.centerIn: parent; text: fav.c.ini; color: fav.on ? Theme.onAcc : Theme.tx; font.pixelSize: 28; font.weight: Font.Bold }
                            }
                            Text { anchors.horizontalCenter: parent.horizontalCenter; text: fav.c.n; color: Theme.tx; font.pixelSize: 18; font.weight: Font.DemiBold }
                        }
                        TapHandler { onTapped: fav.on ? Hmi.startCall(D.FAVS[fav.index]) : Hmi.upd("phone", { sel: fav.index }) }
                    }
                }
            }
        }

        Card {
            id: recents
            anchors.right: parent.right
            width: 340
            height: parent.height
            Text { id: recTitle; x: 24; y: 18; text: "Recents"; color: Theme.tx; font.pixelSize: 20; font.weight: Font.Bold }
            Column {
                anchors.top: recTitle.bottom
                anchors.topMargin: 8
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 16
                spacing: 4
                Repeater {
                    model: D.RECENTS.length
                    delegate: Rectangle {
                        id: rec
                        required property int index
                        readonly property var r: D.RECENTS[index]
                        readonly property bool on: page.sel === index + 6
                        readonly property bool missed: r.type === "missed"
                        width: parent.width
                        height: (parent.height - 20) / 6
                        radius: 14
                        color: on ? Theme.accSoft : "transparent"
                        Behavior on color { ColorAnimation { duration: 200 } }
                        Icon {
                            x: 12
                            anchors.verticalCenter: parent.verticalCenter
                            name: ({ "in": "arrow-down", out: "arrow-up", missed: "alert-circle" })[rec.r.type]
                            color: rec.missed ? Theme.heat : Theme.tx2
                            size: 18
                        }
                        Text {
                            x: 46
                            anchors.verticalCenter: parent.verticalCenter
                            text: D.CONTACTS[rec.r.c].n
                            color: rec.missed ? Theme.heat : Theme.tx
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            text: rec.r.t
                            color: Theme.tx2
                            font.pixelSize: 14
                        }
                        TapHandler { onTapped: rec.on ? Hmi.startCall(rec.r.c) : Hmi.upd("phone", { sel: rec.index + 6 }) }
                    }
                }
            }
        }
    }
}
