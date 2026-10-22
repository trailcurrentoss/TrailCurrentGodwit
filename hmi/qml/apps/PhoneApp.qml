import QtQuick
import Godwit

// Favorites or Contacts on the left, Recents on the right. On Favorites one
// selection runs through favorites then recents, following the right knob;
// on Contacts it runs through the contact list.
AppPage {
    id: page
    readonly property int sel: Hmi.phone.sel
    readonly property bool allTab: Hmi.phone.tab === "all"
    // Why a list is empty, on the head unit
    readonly property string emptyWhy: Hmi.phoneReal && !Calls.available ? "Connect a phone in Settings > Bluetooth & Phones."
        : Phonebook.syncing ? "Reading contacts from " + Calls.phoneName + "…"
        : "Allow contact sharing for Godwit on your phone: Bluetooth settings, Godwit, Sync Contacts."

    Loader {
        anchors.fill: parent
        active: Hmi.inCall && !Hmi.incoming
        sourceComponent: CallView {}
    }

    component Avatar: Rectangle {
        property string ini
        property bool on
        width: 76; height: 76; radius: width / 2
        color: on ? Theme.acc : Theme.track
        Behavior on color { ColorAnimation { duration: 200 } }
        Text { anchors.centerIn: parent; text: parent.ini; color: parent.on ? Theme.onAcc : Theme.tx; font.pixelSize: parent.width * 0.37; font.weight: Font.Bold }
    }

    Item {
        anchors.fill: parent
        visible: !(Hmi.inCall && !Hmi.incoming)

        Item {
            anchors.left: parent.left
            anchors.right: recents.left
            anchors.rightMargin: 24
            height: parent.height

            Row {
                id: tabs
                spacing: 8
                Repeater {
                    model: [["fav", "Favorites"], ["all", "Contacts"]]
                    delegate: Rectangle {
                        required property var modelData
                        readonly property bool on: Hmi.phone.tab === modelData[0]
                        width: tabText.implicitWidth + 48
                        height: 48
                        radius: 24
                        color: on ? Theme.tx : Theme.track
                        Text { id: tabText; anchors.centerIn: parent; text: parent.modelData[1]; color: parent.on ? Theme.bg : Theme.tx; font.pixelSize: 18; font.weight: Font.Bold }
                        TapHandler { onTapped: Hmi.phoneTab(parent.modelData[0]) }
                    }
                }
            }

            Grid {
                id: favs
                visible: !page.allTab
                anchors.top: tabs.bottom
                anchors.topMargin: 14
                anchors.bottom: parent.bottom
                width: parent.width
                columns: 3
                spacing: 12
                Repeater {
                    model: Hmi.favs
                    delegate: Card {
                        id: fav
                        required property int index
                        required property var modelData
                        readonly property bool on: page.sel === index
                        width: (favs.width - 24) / 3
                        height: (favs.height - 12) / 2
                        selected: on
                        Column {
                            anchors.centerIn: parent
                            width: parent.width - 20
                            spacing: 10
                            Avatar { anchors.horizontalCenter: parent.horizontalCenter; ini: fav.modelData.ini; on: fav.on }
                            Text { width: parent.width; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; text: fav.modelData.n; color: Theme.tx; font.pixelSize: 18; font.weight: Font.DemiBold }
                        }
                        TapHandler { onTapped: { Hmi.upd("phone", { sel: fav.index }); Hmi.startCall(fav.modelData); } }
                    }
                }
            }
            Text {
                visible: !page.allTab && Hmi.favs.length === 0
                anchors.top: tabs.bottom
                anchors.topMargin: 40
                width: parent.width
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                text: Hmi.contacts.length ? "The people you call most appear here." : page.emptyWhy
                color: Theme.tx2
                font.pixelSize: 18
            }

            Card {
                visible: page.allTab
                anchors.top: tabs.bottom
                anchors.topMargin: 14
                anchors.bottom: parent.bottom
                width: parent.width
                ListView {
                    id: contactList
                    anchors.fill: parent
                    anchors.margins: 10
                    clip: true
                    model: Hmi.contacts
                    currentIndex: page.allTab ? page.sel : -1
                    highlightMoveDuration: 150
                    onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                    delegate: Rectangle {
                        id: crow
                        required property int index
                        required property var modelData
                        readonly property bool on: page.allTab && page.sel === index
                        width: contactList.width
                        height: 68
                        radius: 14
                        color: on ? Theme.accSoft : "transparent"
                        Avatar { x: 10; anchors.verticalCenter: parent.verticalCenter; width: 48; height: 48; ini: crow.modelData.ini; on: crow.on }
                        Text {
                            x: 74; y: 12
                            width: parent.width - 84
                            elide: Text.ElideRight
                            text: crow.modelData.n
                            color: Theme.tx; font.pixelSize: 19; font.weight: Font.DemiBold
                        }
                        Text { x: 74; y: 38; text: crow.modelData.num; color: Theme.tx2; font.pixelSize: 15 }
                        TapHandler { onTapped: { Hmi.upd("phone", { sel: crow.index }); Hmi.startCall(crow.modelData); } }
                    }
                }
                Text {
                    visible: Hmi.contacts.length === 0
                    anchors.centerIn: parent
                    width: parent.width - 60
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignHCenter
                    text: page.emptyWhy
                    color: Theme.tx2
                    font.pixelSize: 18
                }
            }
        }

        Card {
            id: recents
            anchors.right: parent.right
            width: 340
            height: parent.height
            Text { id: recTitle; x: 24; y: 18; text: "Recents"; color: Theme.tx; font.pixelSize: 20; font.weight: Font.Bold }
            ListView {
                id: recList
                anchors.top: recTitle.bottom
                anchors.topMargin: 8
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 16
                clip: true
                spacing: 4
                model: Hmi.recents
                currentIndex: page.allTab ? -1 : page.sel - Hmi.favs.length
                onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                delegate: Rectangle {
                    id: rec
                    required property int index
                    required property var modelData
                    readonly property bool on: !page.allTab && page.sel === index + Hmi.favs.length
                    readonly property bool missed: modelData.type === "missed"
                    width: recList.width
                    height: 60
                    radius: 14
                    color: on ? Theme.accSoft : "transparent"
                    Behavior on color { ColorAnimation { duration: 200 } }
                    Icon {
                        x: 12
                        anchors.verticalCenter: parent.verticalCenter
                        name: ({ "in": "arrow-down", out: "arrow-up", missed: "alert-circle" })[rec.modelData.type]
                        color: rec.missed ? Theme.heat : Theme.tx2
                        size: 18
                    }
                    Text {
                        x: 46
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - 46 - when.width - 24
                        elide: Text.ElideRight
                        text: rec.modelData.n
                        color: rec.missed ? Theme.heat : Theme.tx
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Text {
                        id: when
                        anchors.right: parent.right
                        anchors.rightMargin: 12
                        anchors.verticalCenter: parent.verticalCenter
                        text: rec.modelData.t
                        color: Theme.tx2
                        font.pixelSize: 14
                    }
                    TapHandler {
                        onTapped: {
                            if (!page.allTab) Hmi.upd("phone", { sel: rec.index + Hmi.favs.length });
                            Hmi.startCall(rec.modelData);
                        }
                    }
                }
            }
            Text {
                visible: Hmi.recents.length === 0
                anchors.centerIn: parent
                width: parent.width - 48
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                text: Hmi.phoneReal && !Calls.available ? "No phone connected" : "No recent calls"
                color: Theme.tx2
                font.pixelSize: 16
            }
        }
    }
}
