import QtQuick
import Godwit
import "../DemoData.js" as D

// 40 px top bar: clock, a centre pill (call, spoken guidance or next turn), weather and battery
Rectangle {
    id: bar
    height: 40
    color: Theme.chrome

    readonly property var route: Hmi.navCue()
    readonly property bool showTurn: Hmi.app !== "nav" && route.u > 0.5 && Hmi.gear === "D" && !Hmi.inCall && !Hmi.guideOn()

    Num {
        anchors.left: parent.left
        anchors.leftMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        font.pixelSize: 16
        font.weight: Font.Bold
        text: { void Hmi.clock; return Hmi.fmtTime(new Date(), false); }
    }

    Row {
        anchors.centerIn: parent
        Pill {
            visible: Hmi.inCall && !Hmi.incoming
            color: Theme.acc
            fg: Theme.onAcc
            icon: Hmi.phone.mic ? "call" : "mic-off"
            text: Hmi.inCall ? D.CONTACTS[Hmi.phone.call.who].n + " · " + D.fmtT(Hmi.phone.call.t) : ""
            vpad: 4
        }
        Pill {
            visible: Hmi.guideOn()
            color: "#48E6FE"
            fg: "#000000"
            icon: "navigate"
            text: Hmi.guideTxt
            vpad: 4
        }
        Rectangle {
            visible: bar.showTurn
            height: 30
            width: turnRow.implicitWidth + 16
            radius: 14
            color: bar.route.col
            Row {
                id: turnRow
                anchors.verticalCenter: parent.verticalCenter
                x: 4
                spacing: 8
                Rectangle {
                    width: 24; height: 24; radius: 10
                    color: Qt.rgba(0, 0, 0, 0.12)
                    Icon { anchors.centerIn: parent; name: bar.route.D.i; color: "#000000"; size: 16 }
                }
                Num {
                    anchors.verticalCenter: parent.verticalCenter
                    text: Hmi.fd(bar.route.N.dist) + " · " + bar.route.cur.st
                    color: "#000000"
                    font.pixelSize: 14
                    font.weight: Font.Bold
                }
            }
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 20
        anchors.verticalCenter: parent.verticalCenter
        spacing: 16
        Row {
            spacing: 5
            anchors.verticalCenter: parent.verticalCenter
            Icon { name: "partly-sunny"; size: 17; anchors.verticalCenter: parent.verticalCenter }
            Num { text: Hmi.tempStr(Hmi.clim.out); font.pixelSize: 16; anchors.verticalCenter: parent.verticalCenter }
        }
        Row {
            spacing: 5
            anchors.verticalCenter: parent.verticalCenter
            Icon {
                name: Hmi.lowBattery ? "battery-dead" : "battery-half"
                color: Hmi.lowBattery ? Theme.heat : Theme.tx
                size: 20
                anchors.verticalCenter: parent.verticalCenter
            }
            Num {
                text: Hmi.energy.soc + "%"
                color: Hmi.lowBattery ? Theme.heat : Theme.tx
                font.pixelSize: 16
                font.weight: Font.DemiBold
                anchors.verticalCenter: parent.verticalCenter
            }
        }
        Icon {
            visible: Hmi.bt.on && Hmi.bt.phones.some(function (p) { return p.connected; })
            name: "bluetooth"; size: 17; anchors.verticalCenter: parent.verticalCenter
        }
        Icon { visible: Hmi.wifi.on && Hmi.wifi.ssid !== ""; name: "wifi"; size: 18; anchors.verticalCenter: parent.verticalCenter }
        Icon { name: "cellular"; size: 16; anchors.verticalCenter: parent.verticalCenter }
    }
}
