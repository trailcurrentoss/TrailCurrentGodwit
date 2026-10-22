import QtQuick
import QtQuick.Shapes
import Godwit

// Each tank fills to its level; a full black tank turns red here and on the ring
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
                readonly property color liquid: crit ? Theme.heat
                    : modelData.n === "Fresh" ? (Theme.dark ? "#3BA7F0" : "#1E88E5")
                    : modelData.n === "Grey" ? (Theme.dark ? "#9AA7AF" : "#6F7F89")
                    : (Theme.dark ? "#9A7448" : "#5D4632")
                width: parent.width / 3
                height: parent.height
                Column {
                    anchors.centerIn: parent
                    spacing: 8
                    // The rendered tank, with the water drawn over its body
                    // (assets/water/README.md gives the body's bounds)
                    Item {
                        id: art
                        readonly property var b: Theme.dark ? [0.021, 0.976, 0.066, 0.970] : [0.020, 0.974, 0.064, 0.955]
                        anchors.horizontalCenter: parent.horizontalCenter
                        height: 260
                        width: height * (Theme.dark ? 373 : 347) / 560
                        Image {
                            anchors.fill: parent
                            source: Theme.dark ? "../../assets/water/water_tank_dark.png" : "../../assets/water/water_tank_light.png"
                            sourceSize.height: 560
                            mipmap: true
                        }
                        Item {
                            id: body
                            property real shown: Math.max(0, Math.min(1, tank.modelData.p / 100))
                            Behavior on shown { NumberAnimation { duration: 400; easing.type: Easing.OutCubic } }
                            x: parent.width * art.b[0]
                            y: parent.height * art.b[2]
                            width: parent.width * (art.b[1] - art.b[0])
                            height: parent.height * (art.b[3] - art.b[2])
                            opacity: Theme.dark ? 0.78 : 0.62
                            layer.enabled: true    // one opacity for water and surface together

                            Item {
                                anchors.bottom: parent.bottom
                                width: parent.width
                                height: parent.height * body.shown
                                clip: true
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: body.height
                                    radius: width * 0.09
                                    // Deeper toward the bottom, like water in a tank
                                    gradient: Gradient {
                                        GradientStop { position: 0; color: Qt.lighter(tank.liquid, 1.25) }
                                        GradientStop { position: 1; color: Qt.darker(tank.liquid, 1.25) }
                                    }
                                }
                            }
                            // The surface, drifting gently
                            Item {
                                visible: body.shown > 0.02 && body.shown < 0.98
                                width: parent.width
                                height: 8
                                y: parent.height * (1 - body.shown) - height + 1
                                clip: true
                                Shape {
                                    id: wave
                                    readonly property real len: parent.width
                                    width: len * 2
                                    height: parent.height
                                    preferredRendererType: Shape.CurveRenderer
                                    NumberAnimation on x {
                                        from: 0; to: -wave.len; duration: 2800
                                        loops: Animation.Infinite; running: page.visible
                                    }
                                    ShapePath {
                                        strokeWidth: -1
                                        // Match the water just below the surface: the gradient's
                                        // light end when full, its dark end when nearly empty
                                        fillColor: Qt.lighter(tank.liquid, 0.75 + 0.5 * body.shown)
                                        startX: 0; startY: wave.height
                                        PathLine { x: 0; y: wave.height / 2 }
                                        PathCubic { x: wave.len; y: wave.height / 2; control1X: wave.len / 3; control1Y: 0; control2X: wave.len * 2 / 3; control2Y: wave.height }
                                        PathCubic { x: wave.len * 2; y: wave.height / 2; control1X: wave.len * 4 / 3; control1Y: 0; control2X: wave.len * 5 / 3; control2Y: wave.height }
                                        PathLine { x: wave.width; y: wave.height }
                                    }
                                }
                            }
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
