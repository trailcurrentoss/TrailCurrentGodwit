import QtQuick
import Godwit

// Leveling the parked van: its tilt from the side and from behind, the van
// from above with a bull's-eye level and what each wheel needs, and what to do
// in plain words. The tilt comes from Plateau on the TrailCurrent bus; what to
// raise, and with what, from Leveling (Settings › Leveling). The vans are
// drawn four times steeper than they are, so a one-degree slope shows.
AppPage {
    id: page
    readonly property var lv: Hmi.level
    readonly property bool live: !!lv.live
    readonly property bool known: !live || !!lv.fresh
    readonly property real pitch: known ? lv.pitch : 0
    readonly property real roll: known ? lv.roll : 0
    readonly property var g: Leveling.guide(pitch, roll)
    readonly property bool sized: Leveling.sized
    readonly property bool level: known && (sized ? !!g.level : Math.abs(pitch) < 0.2 && Math.abs(roll) < 0.2)
    readonly property bool steep: known && sized && !!g.steep
    readonly property string kind: Leveling.leveler === "blocks" ? "block" : Leveling.leveler === "ramps" ? "ramp" : "board"
    readonly property string th: Theme.dark ? "dark" : "light"
    readonly property string art: "../../assets/"
    readonly property int exaggerate: 4
    readonly property var names: ["Front left", "Front right", "Rear left", "Rear right"]
    function raise(i) { return sized && g.raiseMm ? g.raiseMm[i] : 0; }
    function deg(x) { return (x > 0 ? "+" : x < 0 ? "−" : "") + Math.abs(x).toFixed(1) + "°"; }
    function pieces(mm) { var n = Leveling.piecesFor(mm); return n + " " + (Leveling.leveler === "blocks" ? (n === 1 ? "block" : "blocks") : (n === 1 ? "board" : "boards")); }

    // ------------------------------------------------------------ a tilt
    component TiltView: Card {
        id: tv
        property string title
        property real angle
        property string note
        property url van
        property real vanW
        property real vanH
        property var wheels      // x fractions of the image where the tyres touch
        property real foot       // y fraction where they touch
        property var lowMm       // [first wheel, second wheel]: how far each end must rise
        property bool endOn      // the leveler seen end on (from behind)
        readonly property color valCol: !page.known ? Theme.tx2 : Math.abs(angle) < 0.2 ? Theme.acc : page.steep ? Theme.heat : Theme.warn
        readonly property real groundY: height - 44
        clip: true

        // Where level ground would be
        Repeater {
            model: Math.ceil(tv.width / 14)
            delegate: Rectangle {
                required property int index
                x: 14 + index * 14; y: tv.groundY
                width: 8; height: 2; radius: 1
                color: Theme.line
            }
        }
        // The ground and the van, turned together
        Item {
            width: tv.width; height: tv.height
            rotation: page.known ? tv.angle * page.exaggerate : 0
            Behavior on rotation { SpringAnimation { spring: 2.2; damping: 0.28 } }
            Rectangle { x: -60; width: tv.width + 120; y: tv.groundY; height: 220; color: Theme.track }
            Image {
                id: vanImg
                width: tv.vanW; height: tv.vanH
                x: (tv.width - width) / 2
                y: tv.groundY - height * tv.foot
                source: tv.van
                smooth: true; mipmap: true
                opacity: page.known ? 1 : 0.45
                Behavior on opacity { NumberAnimation { duration: 300 } }
            }
            // What to drive the low end onto, set down beside its wheel
            Repeater {
                model: 2
                delegate: Item {
                    id: aid
                    required property int index
                    readonly property real mm: page.known && page.sized && !page.steep && tv.lowMm ? tv.lowMm[index] : 0
                    readonly property bool ramp: page.kind === "ramp"
                    visible: mm >= 10
                    opacity: visible ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: 300 } }
                    readonly property real wx: vanImg.x + tv.wheels[index] * tv.vanW
                    // Ramps point their low end at the wheel
                    width: ramp ? (tv.endOn ? 36 : 92) : (tv.endOn ? 36 : 54)
                    height: stack.height
                    // A ramp's low end touches its tyre; a stack sits just ahead of it
                    x: ramp && !tv.endOn ? (index === 0 ? wx - 13 - width : wx + 13)
                                         : wx - width / 2 + (index === 0 ? -1 : 1) * (tv.endOn ? 34 : 44)
                    y: tv.groundY - height - 1
                    Column {
                        id: stack
                        visible: !aid.ramp
                        spacing: 1
                        Repeater {
                            model: aid.visible && !aid.ramp ? Leveling.piecesFor(aid.mm) : 0
                            delegate: Image {
                                width: aid.width; height: tv.endOn ? 8 : 6
                                source: page.art + "level/" + page.kind + "_" + (tv.endOn ? "end" : "side") + "_" + page.th + ".png"
                                smooth: true
                            }
                        }
                    }
                    Image {
                        id: rampImg
                        visible: aid.ramp
                        width: aid.width
                        height: tv.endOn ? 19 : 22
                        y: -height
                        source: page.art + "level/ramp_" + (tv.endOn ? "end" : "side") + "_" + page.th + ".png"
                        // Its low end faces the wheel that drives onto it: the
                        // front wheel is to the ramp's right, the rear's to its left
                        mirror: !tv.endOn && aid.index === 0
                        smooth: true
                        // Where to stop: u of the way up from the low end
                        Rectangle {
                            visible: !tv.endOn
                            readonly property real u: Leveling.rampAt(aid.mm)
                            x: (aid.index === 0 ? 1 - u : u) * rampImg.width - width / 2
                            y: rampImg.height * (1 - Math.sin(Math.PI / 2 * u)) - height / 2
                            width: 8; height: 8; radius: 4
                            color: Theme.acc
                            border.width: 1.5; border.color: Theme.card
                        }
                    }
                }
            }
        }
        Text { x: 20; y: 16; text: tv.title; color: Theme.tx2; font.pixelSize: 15; font.weight: Font.DemiBold }
        Num {
            anchors.right: parent.right; anchors.rightMargin: 20; y: 10
            text: page.known ? page.deg(tv.angle) : "—"
            color: tv.valCol
            font.pixelSize: 34; font.weight: Font.DemiBold
            Behavior on color { ColorAnimation { duration: 300 } }
        }
        Text { anchors.right: parent.right; anchors.rightMargin: 20; y: 52; text: page.known ? tv.note : ""; color: Theme.tx2; font.pixelSize: 13; font.weight: Font.DemiBold }
        Text { x: 14; anchors.bottom: parent.bottom; anchors.bottomMargin: 10; text: "Drawn 4× steeper"; color: Theme.tx2; font.pixelSize: 11; opacity: 0.8 }
    }

    TiltView {
        x: 0; y: 0; width: 320; height: 214
        title: "Pitch"; angle: page.pitch
        note: Math.abs(page.pitch) < 0.2 ? "Level" : page.pitch > 0 ? "Nose up" : "Nose down"
        van: page.art + "energy/van_day_" + page.th + ".png"
        vanW: 196; vanH: 196 * 253 / 480
        wheels: [0.15, 0.79]; foot: 0.985
        lowMm: [page.g.frontMm || 0, page.g.rearMm || 0]
    }
    TiltView {
        x: 0; y: 234; width: 320; height: 214
        title: "Roll"; angle: page.roll
        note: Math.abs(page.roll) < 0.2 ? "Level" : page.roll > 0 ? "Leaning right" : "Leaning left"
        van: page.art + "level/van_rear_" + page.th + ".png"
        vanW: 110; vanH: 140
        wheels: [0.12, 0.89]; foot: 1.0
        endOn: true
        lowMm: [page.g.leftMm || 0, page.g.rightMm || 0]
    }

    // ------------------------------------------------------------ from above
    Item {
        id: overhead
        x: 344; y: 0; width: 324; height: parent.height
        readonly property real vh: 404
        readonly property real vw: vh * 412 / 1103
        Image {
            id: vanTop
            anchors.horizontalCenter: parent.horizontalCenter
            y: 22; width: overhead.vw; height: overhead.vh
            source: page.art + "vehicle/van_top_" + page.th + ".png"
            sourceSize.width: 412
            smooth: true; mipmap: true
            opacity: page.known ? 1 : 0.45
        }
        Repeater {
            model: 4
            delegate: Item {
                id: wh
                required property int index
                readonly property bool onRight: index % 2 === 1
                readonly property real mm: page.raise(index)
                readonly property bool ok: page.known && page.sized && mm < Leveling.levelMm
                readonly property real ay: vanTop.y + (index < 2 ? 0.165 : 0.79) * overhead.vh
                Rectangle {   // tyre
                    x: wh.onRight ? vanTop.x + vanTop.width - 6 : vanTop.x - 8
                    y: wh.ay - 22
                    width: 14; height: 44; radius: 6
                    color: !page.known || !page.sized ? Theme.line : wh.ok ? Theme.acc : page.steep ? Theme.heat : Theme.warn
                    Behavior on color { ColorAnimation { duration: 300 } }
                }
                Rectangle {   // what it needs
                    visible: page.known && page.sized
                    x: wh.onRight ? vanTop.x + vanTop.width + 14 : vanTop.x - 16 - width
                    y: wh.ay - height / 2
                    width: chip.implicitWidth + 18; height: 30; radius: 15
                    color: wh.ok ? Theme.accSoft : Theme.track
                    Behavior on color { ColorAnimation { duration: 300 } }
                    Num {
                        id: chip
                        anchors.centerIn: parent
                        text: wh.ok ? "✓" : "+" + Hmi.lenStr(wh.mm)
                        color: wh.ok ? Theme.acc : page.steep ? Theme.heat : Theme.tx
                        font.pixelSize: 13; font.weight: Font.Bold
                    }
                }
            }
        }
        // The bull's-eye; its bubble floats to the high side
        Image {
            id: vial
            anchors.horizontalCenter: vanTop.horizontalCenter
            y: vanTop.y + overhead.vh * 0.47 - height / 2
            width: 150; height: 150
            source: page.art + "level/vial_" + page.th + ".png"
            smooth: true; mipmap: true
            Rectangle {
                anchors.centerIn: parent
                width: 156; height: 156; radius: 78
                color: "transparent"; border.width: 4; border.color: Theme.acc
                opacity: page.level ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: 400 } }
            }
            Image {
                readonly property real gain: 18      // px a degree: the outer ring at about 3.5°
                width: 26; height: 26
                x: vial.width / 2 - width / 2 + Math.max(-58, Math.min(58, -page.roll * gain))
                y: vial.height / 2 - height / 2 + Math.max(-58, Math.min(58, -page.pitch * gain))
                // Liquid: it overshoots a little and settles
                Behavior on x { SpringAnimation { spring: 2.5; damping: 0.22 } }
                Behavior on y { SpringAnimation { spring: 2.5; damping: 0.22 } }
                source: page.art + "level/bubble_" + page.th + ".png"
                visible: page.known
                smooth: true
            }
        }
    }

    // ------------------------------------------------------------ what to do
    Card {
        x: parent.width - width; y: 0; width: 284; height: parent.height
        Column {
            x: 22; y: 22; width: parent.width - 44
            spacing: 6
            Text { text: "Leveling"; color: Theme.tx2; font.pixelSize: 15; font.weight: Font.DemiBold }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: !page.known ? "No data" : page.level ? "Level" : !page.sized ? "Not level"
                    : page.steep ? "Too steep" : "Raise " + page.g.raiseMm.filter(function (m) { return m >= Leveling.levelMm; }).length + " wheels"
                color: !page.known ? Theme.tx2 : page.level ? Theme.acc : page.steep ? Theme.heat : Theme.tx
                font.pixelSize: 30; font.weight: Font.DemiBold
                Behavior on color { ColorAnimation { duration: 300 } }
            }
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                lineHeight: 1.15
                color: Theme.tx2; font.pixelSize: 15
                text: !page.known ? "Plateau is not reporting. Check that it is powered and on the TrailCurrent bus."
                    : !page.sized ? "Set the wheelbase and track width in Settings › Leveling to see how far to raise each wheel."
                    : page.level ? "Within " + Hmi.lenStr(Leveling.levelMm) + " at every wheel. Chock the wheels and set the parking brake."
                    : page.steep ? "A wheel needs more than " + Hmi.lenStr(Leveling.maxRaiseMm) + ", the most set in Settings. Find flatter ground, or turn the van around on this slope."
                    : page.kind === "ramp" ? "Drive the low wheels up the ramps to the marked height, then check again."
                    : "Drive the low wheels up onto " + (Leveling.leveler === "blocks" ? "blocks" : "boards") + " (" + Hmi.lenStr(Leveling.pieceMm) + " each), then check again."
            }
            Item { width: 1; height: 8 }
            Repeater {
                model: page.known && page.sized && !page.level && !page.steep ? 4 : 0
                delegate: Item {
                    required property int index
                    readonly property real mm: page.raise(index)
                    visible: mm >= Leveling.levelMm
                    width: parent.width; height: visible ? 50 : 0
                    Rectangle { anchors.fill: parent; radius: 14; color: Theme.track }
                    Text { x: 14; anchors.verticalCenter: parent.verticalCenter; text: page.names[index]; color: Theme.tx; font.pixelSize: 16; font.weight: Font.DemiBold }
                    Num {
                        anchors.right: parent.right; anchors.rightMargin: 14; anchors.verticalCenter: parent.verticalCenter
                        text: page.kind === "ramp" ? "Up to " + Hmi.lenStr(parent.mm) : Hmi.lenStr(parent.mm) + " · " + page.pieces(parent.mm)
                        color: Theme.tx2; font.pixelSize: 15
                    }
                }
            }
            Rectangle {
                visible: page.known && !page.sized
                width: setText.implicitWidth + 40; height: 48; radius: 24
                color: setTap.pressed ? Theme.accSoft : Theme.track
                Text { id: setText; anchors.centerIn: parent; text: "Leveling Settings"; color: Theme.tx; font.pixelSize: 15; font.weight: Font.Bold }
                TapHandler { id: setTap; onTapped: { Hmi.openApp("settings"); Hmi.openSet("leveling"); } }
            }
        }
        Text {
            anchors.left: parent.left; anchors.leftMargin: 22
            anchors.bottom: parent.bottom; anchors.bottomMargin: 18
            width: parent.width - 44
            elide: Text.ElideRight
            color: Theme.tx2; font.pixelSize: 12; font.weight: Font.DemiBold
            text: !page.live ? "Simulated" : !page.lv.fresh ? "Plateau · not heard"
                : "Plateau · " + (page.lv.calibrated ? "calibrated" : page.lv.calibrated === false ? "calibrating" : "reporting")
                  + " · " + ["floor", "left wall", "right wall"][Math.max(0, Math.min(2, page.lv.mounting || 0))] + " mount"
        }
    }
}
