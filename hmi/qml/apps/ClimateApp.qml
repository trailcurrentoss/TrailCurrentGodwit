import QtQuick
import Godwit
import "../DemoData.js" as D

// Climate: the van seen from above, with each zone's airflow and seat heat
// drawn where they happen, and each zone's controls on its own side. Every
// control works by touch as well as by that side's knob.
AppPage {
    id: page
    readonly property var cl: Hmi.clim
    readonly property bool callMode: Hmi.inCall && !Hmi.incoming

    function zoneColor(z) { return z.t > cl.cabin ? Theme.heat : z.t < cl.cabin ? Theme.cool : Theme.acc; }
    // Which glow / streak sprite a zone uses: warm, cool or neutral air
    function fxKind(z) { return z.t > cl.cabin ? "heat" : z.t < cl.cabin ? "cool" : "air"; }

    // Row heading; the dot marks the row the knob is adjusting
    component RowLabel: Row {
        id: rowLabel
        property string text
        property bool active
        property bool mirrored
        layoutDirection: mirrored ? Qt.RightToLeft : Qt.LeftToRight
        spacing: 6
        Rectangle {
            visible: rowLabel.active
            width: 6; height: 6; radius: 3
            color: Theme.acc
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: rowLabel.text
            color: rowLabel.active ? Theme.acc : Theme.tx2
            font.pixelSize: 13
            font.weight: Font.Bold
            font.letterSpacing: 1.2
        }
    }

    // ------------------------------------------------------------ zone panel
    component Zone: Item {
        id: zone
        property string key
        property int side
        readonly property var zs: page.cl[key]
        readonly property int m: page.cl.mode[side]
        readonly property bool mirrored: side === 1
        // The row the knob is on: 0 temp, 1 fan, 2 seat (none during a call)
        readonly property int knobRow: page.callMode ? (Hmi.czone === side ? 0 : -1) : m
        readonly property int align: mirrored ? Text.AlignRight : Text.AlignLeft

        Column {
            anchors.fill: parent
            spacing: 14

            // Zone name
            Item {
                width: parent.width
                height: 18
                Text {
                    x: zone.mirrored ? parent.width - width : 0
                    text: zone.mirrored ? "PASSENGER" : "DRIVER"
                    color: Theme.tx2
                    font.pixelSize: 13
                    font.weight: Font.Bold
                    font.letterSpacing: 1.6
                }
            }

            // Temperature with touch steppers
            Row {
                width: parent.width
                height: 96
                layoutDirection: zone.mirrored ? Qt.RightToLeft : Qt.LeftToRight
                spacing: 14
                Num {
                    anchors.verticalCenter: parent.verticalCenter
                    text: Hmi.tempNum(zone.zs.t) + "°"
                    color: page.zoneColor(zone.zs)
                    font.pixelSize: 92
                    font.letterSpacing: -3
                    Behavior on color { ColorAnimation { duration: 400 } }
                    Rectangle {
                        visible: zone.knobRow === 0
                        anchors.top: parent.bottom
                        anchors.topMargin: -6
                        x: zone.mirrored ? parent.width - width : 0
                        width: 28; height: 3; radius: 2
                        color: Theme.acc
                    }
                }
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Repeater {
                        model: [1, -1]
                        delegate: Rectangle {
                            id: stepBtn
                            required property int modelData
                            width: 52; height: 40; radius: 20
                            color: tapStep.pressed ? Theme.accSoft : Theme.track
                            Icon { anchors.centerIn: parent; name: stepBtn.modelData > 0 ? "chevron-back" : "chevron-forward"; rotation: 90; size: 22 }
                            TapHandler { id: tapStep; onTapped: Hmi.nudgeTemp(zone.key, stepBtn.modelData) }
                        }
                    }
                }
            }
            Text {
                width: parent.width
                horizontalAlignment: zone.align
                text: (zone.zs.t > page.cl.cabin ? "Heating" : zone.zs.t < page.cl.cabin ? "Cooling" : "Holding")
                      + " · cabin " + Hmi.tempNum(page.cl.cabin) + "°"
                color: Theme.tx2
                font.pixelSize: 15
            }

            // Airflow
            Rectangle {
                width: parent.width
                height: 48
                radius: 16
                color: Theme.track
                Row {
                    anchors.fill: parent
                    anchors.margins: 4
                    layoutDirection: zone.mirrored ? Qt.RightToLeft : Qt.LeftToRight
                    spacing: 2
                    Repeater {
                        model: D.AIR
                        delegate: Rectangle {
                            id: airBtn
                            required property var modelData
                            readonly property bool on: zone.zs.air === modelData.k
                            width: (parent.width - 6) / 4
                            height: parent.height
                            radius: 12
                            color: on ? Theme.raise : "transparent"
                            Behavior on color { ColorAnimation { duration: 200 } }
                            Icon {
                                anchors.centerIn: parent
                                name: airBtn.modelData.i
                                size: 22
                                color: airBtn.on ? page.zoneColor(zone.zs) : Theme.tx2
                            }
                            TapHandler { onTapped: Hmi.setZone(zone.key, { air: airBtn.modelData.k }) }
                        }
                    }
                }
            }

            // Fan
            Column {
                width: parent.width
                spacing: 8
                Item {
                    width: parent.width
                    height: 16
                    RowLabel { x: zone.mirrored ? parent.width - width : 0; text: "FAN"; active: zone.knobRow === 1; mirrored: zone.mirrored }
                    Num {
                        x: zone.mirrored ? 0 : parent.width - width
                        text: zone.zs.fan ? zone.zs.fan + " of 7" : (page.cl.auto ? "Auto" : "Off")
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                }
                Row {
                    width: parent.width
                    height: 24
                    spacing: 4
                    layoutDirection: zone.mirrored ? Qt.RightToLeft : Qt.LeftToRight
                    Repeater {
                        model: 7
                        delegate: Rectangle {
                            id: fanBar
                            required property int index
                            width: (parent.width - 24) / 7
                            height: 12 + index * 2
                            anchors.bottom: parent.bottom
                            radius: 4
                            color: index < zone.zs.fan ? Theme.acc : Theme.track
                            Behavior on color { ColorAnimation { duration: 200 } }
                            TapHandler { margin: 10; onTapped: Hmi.setZone(zone.key, { fan: zone.zs.fan === fanBar.index + 1 ? 0 : fanBar.index + 1 }) }
                        }
                    }
                }
            }

            // Seat: vent on the cool side, heat on the warm side
            Column {
                width: parent.width
                spacing: 8
                Item {
                    width: parent.width
                    height: 16
                    RowLabel { x: zone.mirrored ? parent.width - width : 0; text: "SEAT"; active: zone.knobRow === 2; mirrored: zone.mirrored }
                    Text {
                        x: zone.mirrored ? 0 : parent.width - width
                        text: zone.zs.seat > 0 ? "Heat " + zone.zs.seat : zone.zs.seat < 0 ? "Vent " + (-zone.zs.seat) : "Off"
                        color: zone.zs.seat > 0 ? Theme.heat : zone.zs.seat < 0 ? Theme.cool : Theme.tx
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                }
                Row {
                    id: seatRow
                    width: parent.width
                    spacing: 4
                    readonly property real unit: (width - 24) / 6.3
                    Repeater {
                        model: [-3, -2, -1, 0, 1, 2, 3]
                        delegate: Rectangle {
                            id: seatCell
                            required property int modelData
                            readonly property int s: zone.zs.seat
                            width: modelData === 0 ? seatRow.unit * 0.3 : seatRow.unit
                            height: 14
                            radius: 4
                            color: modelData === 0 ? Theme.line
                                 : (s < 0 && modelData < 0 && modelData >= s) ? Theme.cool
                                 : (s > 0 && modelData > 0 && modelData <= s) ? Theme.heat : Theme.track
                            Behavior on color { ColorAnimation { duration: 200 } }
                            TapHandler { margin: 10; onTapped: Hmi.setZone(zone.key, { seat: seatCell.modelData }) }
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ the van
    Item {
        id: vanArea
        anchors.horizontalCenter: parent.horizontalCenter
        width: 340
        height: parent.height
        clip: true

        // Image coordinates (van_top_*.png is 412 x 1103) to screen
        readonly property real s: van.width / 412
        function vx(x) { return van.x + x * s; }
        function vy(y) { return van.y + y * s; }

        Image {
            id: van
            anchors.horizontalCenter: parent.horizontalCenter
            y: 2
            width: 300
            height: width * 1103 / 412
            source: Theme.dark ? "../../assets/vehicle/van_top_dark.png" : "../../assets/vehicle/van_top_light.png"
            sourceSize.width: 412
            smooth: true
            mipmap: true
        }

        // Per zone: seat glow, footwell glow and airflow streams. The streams
        // start at that side's dash vents: the driver's sit either side of the
        // steering wheel, so their air reads against the floor, not the wheel.
        Repeater {
            model: [{ key: "drv", seatX: 132, vents: [-52, 46, 58] }, { key: "pas", seatX: 278, vents: [-26, 0, 26] }]
            delegate: Item {
                id: fxz
                required property var modelData
                readonly property var zs: page.cl[modelData.key]
                readonly property string kind: page.fxKind(zs)
                readonly property bool face: zs.air === "face" || zs.air === "bi"
                readonly property bool feet: zs.air !== "face"
                readonly property bool flowing: zs.fan > 0 || page.cl.auto
                readonly property int fan: zs.fan || 3
                anchors.fill: parent

                // Seat heat or vent, breathing slowly
                Image {
                    readonly property real size: 150 * vanArea.s
                    x: vanArea.vx(fxz.modelData.seatX) - size / 2
                    y: vanArea.vy(352) - size / 2
                    width: size; height: size
                    source: "../../assets/fx/glow_" + (fxz.zs.seat < 0 ? "cool" : "heat") + ".png"
                    opacity: Math.abs(fxz.zs.seat) / 3 * breathe.v
                    Behavior on opacity { NumberAnimation { duration: 400 } }
                }
                // Feet: a soft glow in the footwell
                Image {
                    readonly property real size: 110 * vanArea.s
                    x: vanArea.vx(fxz.modelData.seatX) - size / 2
                    y: vanArea.vy(285) - size / 2
                    width: size; height: size
                    source: "../../assets/fx/glow_" + fxz.kind + ".png"
                    opacity: fxz.feet && fxz.flowing ? 0.35 + 0.35 * breathe.v : 0
                    Behavior on opacity { NumberAnimation { duration: 400 } }
                }
                // Face: streams from the dash vents to the seat
                Repeater {
                    model: 3
                    delegate: Image {
                        id: streak
                        required property int index
                        readonly property real startX: vanArea.vx(fxz.modelData.seatX + fxz.modelData.vents[index])
                        property real t: 0
                        x: startX + (vanArea.vx(fxz.modelData.seatX) - startX) * t * 0.45 - width / 2
                        y: vanArea.vy(232) + (vanArea.vy(318) - vanArea.vy(232)) * t - height / 2
                        width: 7
                        height: 44
                        source: "../../assets/fx/streak_" + fxz.kind + ".png"
                        visible: fxz.face && fxz.flowing && page.visible
                        opacity: Math.sin(t * Math.PI) * (Theme.dark ? 0.9 : 0.75)
                        NumberAnimation on t {
                            running: streak.visible
                            from: 0; to: 1
                            loops: Animation.Infinite
                            duration: 1700 - fxz.fan * 150 + streak.index * 130
                            // A running animation keeps its old duration until restarted
                            onDurationChanged: if (running) restart()
                        }
                    }
                }
            }
        }
        // Defrost: the windshield lights up across its width
        Image {
            readonly property bool on: page.cl.drv.air === "defrost" || page.cl.pas.air === "defrost" || page.cl.defF
            x: vanArea.vx(70); y: vanArea.vy(150)
            width: 272 * vanArea.s; height: 56 * vanArea.s
            source: "../../assets/fx/glow_" + (page.cl.defF ? "air" : page.fxKind(page.cl.drv)) + ".png"
            opacity: on ? 0.45 + 0.4 * breathe.v : 0
            Behavior on opacity { NumberAnimation { duration: 500 } }
        }

        // The van fades into the screen below the cab
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 190
            gradient: Gradient {
                GradientStop { position: 0; color: "transparent" }
                GradientStop { position: 0.5; color: Theme.bg }
            }
        }
        Num {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 62
            text: "Cabin " + Hmi.tempStr(page.cl.cabin) + "  ·  Outside " + Hmi.tempStr(page.cl.out)
            color: Theme.tx2
            font.pixelSize: 14
            font.weight: Font.DemiBold
        }
    }

    // One slow breath shared by every glow
    QtObject {
        id: breathe
        property real v: 0.75
        property SequentialAnimation anim: SequentialAnimation {
            running: page.visible
            loops: Animation.Infinite
            NumberAnimation { target: breathe; property: "v"; from: 0.6; to: 1; duration: 1600; easing.type: Easing.InOutSine }
            NumberAnimation { target: breathe; property: "v"; from: 1; to: 0.6; duration: 1600; easing.type: Easing.InOutSine }
        }
    }

    Zone { key: "drv"; side: 0; x: 0; y: 0; width: 290; height: parent.height - 64 }
    Zone { key: "pas"; side: 1; x: parent.width - 290; y: 0; width: 290; height: parent.height - 64 }

    // Toggles, floating along the bottom
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        spacing: 8
        Repeater {
            model: [["auto", "Auto", "sync"], ["ac", "A/C", "snow"], ["recirc", "Recirc", "refresh"],
                    ["sync", "Sync", "link"], ["defF", "Front Defrost", "defrost-front"], ["defR", "Rear Defrost", "defrost-rear"]]
            delegate: Rectangle {
                id: tog
                required property var modelData
                readonly property bool on: !!page.cl[modelData[0]]
                width: togRow.implicitWidth + 32
                height: 46
                radius: 23
                color: on ? Theme.acc : Theme.track
                Behavior on color { ColorAnimation { duration: 200 } }
                Row {
                    id: togRow
                    anchors.centerIn: parent
                    spacing: 8
                    Icon { name: tog.modelData[2]; color: tog.on ? Theme.onAcc : Theme.tx; size: 18; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: tog.modelData[1]; color: tog.on ? Theme.onAcc : Theme.tx; font.pixelSize: 14; font.weight: Font.Bold; anchors.verticalCenter: parent.verticalCenter }
                }
                TapHandler { onTapped: Hmi.toggleClim(tog.modelData[0]) }
            }
        }
    }
}
