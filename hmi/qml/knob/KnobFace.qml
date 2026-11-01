import QtQuick
import QtQuick.Shapes
import Godwit

// What a 1.28" round encoder display shows, at its native 240x240.
// enc is one of Hmi.encs. The dial firmware's face.c draws the same.
Rectangle {
    id: face
    property var enc

    width: 240
    height: 240
    radius: 120
    color: Theme.screen

    // An arc of radius r (its centre line) and width w from a to b, degrees
    // clockwise from 12 o'clock
    component Arc: Shape {
        id: arcShape
        property real a
        property real b
        property real r
        property real w
        property color color
        property bool rounded: true
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: arcShape.color
            strokeWidth: arcShape.w
            fillColor: "transparent"
            capStyle: arcShape.rounded ? ShapePath.RoundCap : ShapePath.FlatCap
            PathAngleArc {
                centerX: 120; centerY: 120
                radiusX: arcShape.r; radiusY: arcShape.r
                startAngle: arcShape.a - 90
                sweepAngle: arcShape.b - arcShape.a
            }
        }
    }
    component Disc: Rectangle {
        property point at
        property real r
        x: at.x - r
        y: at.y - r
        width: 2 * r
        height: 2 * r
        radius: r
    }

    // The scale round the edge, open at the bottom
    Item {
        id: gauge
        anchors.fill: parent
        readonly property string kind: face.enc.scale || ""
        readonly property int steps: face.enc.scaleSteps || 0
        readonly property int pos: face.enc.scalePos || 0
        readonly property real sweep: 135
        readonly property real r: 111       // centre line of the arcs
        readonly property real w: 6
        readonly property real cut: 2.2     // the neutral cut leans this far, and is as wide
        function pt(deg, rr) {
            var a = deg * Math.PI / 180;
            return Qt.point(120 + rr * Math.sin(a), 120 - rr * Math.cos(a));
        }
        // The segment p-q lengthened by half again at each end, along its line
        function stretch(p, q) {
            var dx = (q.x - p.x) / 2, dy = (q.y - p.y) / 2;
            return [Qt.point(p.x - dx, p.y - dy), Qt.point(q.x + dx, q.y + dy)];
        }

        // Split: cool left, heat right, a slanted cut at 12 o'clock, a bead at the setting
        Item {
            id: split
            anchors.fill: parent
            visible: gauge.kind === "split"
            readonly property var l: gauge.stretch(gauge.pt(0, gauge.r + gauge.w / 2), gauge.pt(-gauge.cut, gauge.r - gauge.w / 2))
            readonly property var rt: gauge.stretch(gauge.pt(gauge.cut, gauge.r + gauge.w / 2), gauge.pt(0, gauge.r - gauge.w / 2))
            readonly property int mid: (gauge.steps - 1) / 2

            Arc { a: -gauge.sweep; b: 0; r: gauge.r; w: gauge.w; color: Theme.cool; rounded: false }
            Arc { a: 0; b: gauge.sweep; r: gauge.r; w: gauge.w; color: Theme.heat; rounded: false }
            Disc { at: gauge.pt(-gauge.sweep, gauge.r); r: gauge.w / 2; color: Theme.cool }
            Disc { at: gauge.pt(gauge.sweep, gauge.r); r: gauge.w / 2; color: Theme.heat }
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: "transparent"
                    fillColor: Theme.screen
                    startX: split.l[0].x; startY: split.l[0].y
                    PathLine { x: split.rt[0].x; y: split.rt[0].y }
                    PathLine { x: split.rt[1].x; y: split.rt[1].y }
                    PathLine { x: split.l[1].x; y: split.l[1].y }
                }
            }
            Disc {
                visible: gauge.steps > 1
                at: gauge.pt(-gauge.sweep + 2 * gauge.sweep * gauge.pos / Math.max(1, gauge.steps - 1), gauge.r)
                r: 10.5
                color: Theme.screen
                Rectangle {
                    anchors.centerIn: parent
                    width: 15; height: 15; radius: 7.5
                    color: gauge.pos < split.mid ? Theme.cool : gauge.pos > split.mid ? Theme.heat : Theme.tx
                }
            }
        }

        // Dots: across the top, growing left to right, lit up to the setting
        Repeater {
            model: gauge.kind === "dots" ? gauge.steps : 0
            delegate: Disc {
                required property int index
                readonly property real f: gauge.steps > 1 ? index / (gauge.steps - 1) : 1
                at: gauge.pt(-90 + 180 * f, 108)
                r: 3 + 6 * f
                color: index < gauge.pos ? Theme.acc : Theme.line
            }
        }

        // Ramp: the volume wedge, thin at the left end and thick at the right,
        // filled up to the setting
        Shape {
            id: ramp
            anchors.fill: parent
            visible: gauge.kind === "ramp"
            preferredRendererType: Shape.CurveRenderer
            readonly property real ro: gauge.r + gauge.w / 2
            readonly property real fill: -gauge.sweep + 2 * gauge.sweep * (gauge.steps > 0 ? gauge.pos / gauge.steps : 0)
            readonly property color on: face.enc.color === "tx" ? Theme.acc : Theme.named(face.enc.color)
            // The band from a to b: outer edge on the outer circle, inner edge
            // stepping in as it widens from 2 to 14 px
            function band(a, b) {
                var pts = [], n = Math.max(2, Math.ceil((b - a) / 3));
                for (var i = 0; i <= n; i++) pts.push(gauge.pt(a + (b - a) * i / n, ro));
                for (i = n; i >= 0; i--) {
                    var d = a + (b - a) * i / n;
                    pts.push(gauge.pt(d, ro - (2 + 12 * (d + gauge.sweep) / (2 * gauge.sweep))));
                }
                return pts;
            }
            ShapePath {
                strokeColor: "transparent"
                fillColor: Theme.line
                PathPolyline { path: ramp.band(Math.max(ramp.fill, -gauge.sweep), gauge.sweep) }
            }
            ShapePath {
                strokeColor: "transparent"
                fillColor: ramp.on
                PathPolyline { path: ramp.fill > -gauge.sweep ? ramp.band(-gauge.sweep, ramp.fill) : [] }
            }
        }

        // Select: equal segments, the chosen one lit
        Repeater {
            model: gauge.kind === "select" ? gauge.steps : 0
            delegate: Arc {
                required property int index
                readonly property real seg: (2 * gauge.sweep - (gauge.steps - 1) * 9) / gauge.steps
                a: -gauge.sweep + index * (seg + 9)
                b: a + seg
                r: gauge.r
                w: gauge.w
                color: index === gauge.pos ? Theme.acc : Theme.line
            }
        }
    }

    // Text stays inside this radius: the scales take the ring outside it.
    // The width of that circle across the band y..y+h, at its far edge.
    readonly property real textR: 96
    function chord(y, h) {
        var dy = Math.max(Math.abs(y - 120), Math.abs(y + h - 120));
        return dy >= textR ? 0 : 2 * Math.sqrt(textR * textR - dy * dy);
    }
    readonly property bool hasValue: face.enc.value !== ""
    readonly property bool hasIcon: face.enc.icon !== ""

    // The value, or without one the icon, at the centre; the unit off the
    // value's right on its baseline
    Num {
        id: value
        visible: face.hasValue
        anchors.centerIn: parent
        width: Math.min(implicitWidth, face.chord(y, height) - (unit.visible ? 2 * (unit.implicitWidth + 2) : 0))
        horizontalAlignment: Text.AlignHCenter
        text: face.enc.value
        color: Theme.named(face.enc.color)
        font.pixelSize: face.enc.vSize
        font.weight: face.enc.small ? Font.Bold : Font.Normal
        font.letterSpacing: -2
        elide: Text.ElideRight
    }
    Text {
        id: unit
        visible: face.hasValue && text !== ""
        anchors.left: value.right
        anchors.leftMargin: 2
        anchors.baseline: value.baseline
        text: face.enc.unit
        color: Theme.tx2
        // A degree sign at the value's own size sits up where it belongs
        font.pixelSize: text === "°" ? face.enc.vSize : 22
    }
    Icon {
        id: bigIcon
        visible: !face.hasValue && face.hasIcon
        anchors.centerIn: parent
        name: face.enc.icon
        color: Theme.named(face.enc.color)
        size: face.enc.iconSize
    }
    readonly property Item mid: hasValue ? value : hasIcon ? bigIcon : null

    // Above the centre: the label, and the icon when there is a value
    Column {
        id: top
        anchors.horizontalCenter: parent.horizontalCenter
        y: (face.mid ? face.mid.y : 120) - height - (face.mid ? 4 : 2)
        spacing: 2
        Text {
            visible: text !== ""
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, face.chord(top.y + y, height))
            horizontalAlignment: Text.AlignHCenter
            text: face.enc.label.toUpperCase()
            color: Theme.tx2
            font.pixelSize: 14
            font.weight: Font.Bold
            font.letterSpacing: 0.6
            elide: Text.ElideRight
        }
        Icon {
            visible: face.hasValue && face.hasIcon
            anchors.horizontalCenter: parent.horizontalCenter
            name: face.enc.icon
            color: Theme.named(face.enc.color)
            size: face.enc.iconSize
        }
    }

    // Below the centre: the sub line and the pager
    Column {
        id: bottom
        anchors.horizontalCenter: parent.horizontalCenter
        y: face.mid ? face.mid.y + face.mid.height + 4 : 122
        spacing: 2
        Text {
            visible: text !== ""
            anchors.horizontalCenter: parent.horizontalCenter
            width: Math.min(implicitWidth, face.chord(bottom.y + y, height))
            horizontalAlignment: Text.AlignHCenter
            text: face.enc.sub
            color: Theme.tx2
            font.pixelSize: 16
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Row {
            visible: face.enc.pager.length > 0
            anchors.horizontalCenter: parent.horizontalCenter
            topPadding: 8
            spacing: 6
            Repeater {
                model: face.enc.pager
                delegate: Rectangle {
                    required property var modelData
                    width: modelData.on ? 26 : 8
                    height: 8
                    radius: 4
                    color: modelData.on ? Theme.acc : Theme.line
                    Behavior on width { NumberAnimation { duration: 200 } }
                }
            }
        }
    }
}
