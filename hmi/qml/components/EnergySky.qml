import QtQuick
import Godwit

// The Energy screen's sky: the real sky over the camper, worked out on the
// head unit (Solar), and the van under it with its panels and lit window.
// Layers from assets/energy (see its README) are blended by the sun's height
// and by how cloudy the panels say it is, so the card changes gradually.
Item {
    id: sky
    property var en          // Hmi.energy
    width: 548
    height: 220

    readonly property string th: Theme.dark ? "dark" : "light"
    readonly property string art: "../../assets/energy/"
    readonly property real horizonY: height * 0.82
    readonly property bool shown: visible

    // The sun's height. Without a position, a guess from the panels and the
    // clock, so the card is never wrong by a whole day.
    readonly property real e: {
        void Solar.now;
        if (Solar.located) return Solar.sunElevation;
        if (en && en.solar && en.fresh && (en.solarW > 0 || en.panelV > 5)) return 40;
        var h = Solar.now.getHours();
        return h >= 8 && h < 17 ? 40 : h === 7 || h === 17 ? 3 : -20;
    }
    // 0 clear .. 1 overcast; nothing known reads as clear
    readonly property real cloud: Solar.cloud >= 0 ? Solar.cloud : 0
    function smooth(a, b, x) { var t = Math.max(0, Math.min(1, (x - a) / (b - a))); return t * t * (3 - 2 * t); }
    // Each phase's layer over the one before: night, dusk, low, day, overcast
    readonly property var weights: ({
        night: 1,
        dusk: smooth(-9, -3, e),
        low: smooth(-2, 2, e),
        day: smooth(6, 16, e),
        overcast: smooth(0.45, 1.0, cloud) * smooth(-2, 6, e)
    })
    readonly property var phases: ["night", "dusk", "low", "day", "overcast"]
    readonly property real dark: smooth(0, -6, e)        // 0 by day .. 1 after dusk

    // Height in the sky to a y in the card; low elevations are spread out so
    // the sun clears the mountains until it really sets
    function skyY(elev) { return horizonY - Math.sqrt(Math.max(0, Math.min(elev, 70)) / 70) * (horizonY - 28); }
    // Across the card: east on the left looking south, mirrored in the south
    function azX(az) {
        var f = Solar.southern ? ((450 - az) % 360) / 180 : (az - 90) / 180;
        return 40 + Math.max(0, Math.min(1, f)) * (width - 80);
    }
    // Keeps the sun or moon (radius r at x, y) clear of the label in the corner
    function clearOfPill(x, y, r) {
        return x - r < pillBox.x + pillBox.width + 6 && pillBox.visible ? Math.max(y, pillBox.y + pillBox.height + r + 4) : y;
    }
    readonly property real sunX: {
        void Solar.now;
        if (Solar.located && Solar.sunrise.getTime && Solar.sunset.getTime && !isNaN(Solar.sunrise.getTime()) && !isNaN(Solar.sunset.getTime())) {
            var f = (Solar.now.getTime() - Solar.sunrise.getTime()) / (Solar.sunset.getTime() - Solar.sunrise.getTime());
            f = Math.max(0, Math.min(1, f));
            return 40 + (Solar.southern ? 1 - f : f) * (width - 80);
        }
        return width * 0.72;
    }

    // Loads, for the window: what the camper is drawing now
    readonly property real loadW: en && en.fresh && en.watts !== undefined ? Math.max(0, (en.solarW || 0) - en.watts) : 150

    clip: true

    // ------------------------------------------------------------ sky
    Repeater {
        model: sky.phases
        delegate: Image {
            required property string modelData
            anchors.fill: parent
            source: sky.art + "sky_" + modelData + "_" + sky.th + ".jpg"
            opacity: sky.weights[modelData]
            visible: opacity > 0.005
            sourceSize.width: 1096
            smooth: true
            Behavior on opacity { NumberAnimation { duration: 2000 } }
        }
    }

    // Stars once it is dark, a few of them slowly twinkling
    Item {
        anchors.fill: parent
        opacity: sky.smooth(-4, -10, sky.e) * (1 - 0.8 * sky.cloud)
        visible: opacity > 0.01
        Repeater {
            model: 28
            delegate: Rectangle {
                id: star
                required property int index
                readonly property real r: Math.abs(Math.sin(index * 12.9898) * 43758.5453) % 1
                readonly property real q: Math.abs(Math.sin(index * 78.233) * 12543.123) % 1
                x: 8 + r * (sky.width - 16)
                y: 6 + q * sky.horizonY * 0.62
                width: r > 0.82 ? 2.5 : 1.6
                height: width
                radius: width / 2
                color: "white"
                property real tw: 1
                opacity: (0.35 + 0.6 * ((index * 7) % 5) / 4) * (1 - y / (sky.horizonY * 0.9)) * tw
                SequentialAnimation on tw {
                    running: sky.shown && star.index % 4 === 0 && sky.dark > 0.5
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.35; duration: 1400 + star.index * 90; easing.type: Easing.InOutSine }
                    NumberAnimation { to: 1; duration: 1400 + star.index * 90; easing.type: Easing.InOutSine }
                }
            }
        }
    }

    // ------------------------------------------------------------ sun and moon
    Item {
        id: sun
        visible: sky.e > -1.5
        x: sky.sunX
        y: sky.clearOfPill(sky.sunX, sky.skyY(sky.e), 17)
        Behavior on x { NumberAnimation { duration: 1500 } }
        Behavior on y { NumberAnimation { duration: 1500 } }
        // How bright it shines, with solar watts against a clear sky
        readonly property real strength: 1 - 0.8 * sky.cloud
        Image {
            readonly property real sz: 230
            x: -sz / 2; y: -sz / 2
            width: sz; height: sz
            source: sky.art + "sun_glow.png"
            property real level: sun.strength * (Theme.dark ? 0.55 : 0.8) * (1 + 0.25 * sky.smooth(10, 1, sky.e))
            Behavior on level { NumberAnimation { duration: 1500 } }
            opacity: level * (0.85 + 0.15 * breathe.v)
        }
        Image {
            readonly property real sz: sky.e < 10 ? 34 : 30
            x: -sz / 2; y: -sz / 2
            width: sz; height: sz
            source: sky.art + "sun_disc.png"
            opacity: Math.min(1, 0.25 + sun.strength) * (Theme.dark ? 0.9 : 1)
            Behavior on opacity { NumberAnimation { duration: 1500 } }
        }
    }
    Image {
        id: moon
        visible: Solar.located && Solar.moonElevation > 0
        x: sky.azX(Solar.moonAzimuth) - width / 2
        y: sky.clearOfPill(x + width / 2, sky.skyY(Solar.moonElevation), 21) - height / 2
        width: 42; height: 42
        source: sky.art + "moon_" + Solar.moonPhase + "_" + sky.th + ".png"
        // Faint by day, clear at night
        opacity: (0.45 + 0.55 * sky.dark) * (1 - 0.6 * sky.cloud)
        smooth: true; mipmap: true
        // The lit side is on the left south of the equator
        transform: Scale { origin.x: 21; xScale: Solar.southern ? -1 : 1 }
    }

    // ------------------------------------------------------------ clouds
    // Always drifting; how many show, and how solid, follows the cloud judgement
    Repeater {
        model: [{ i: 2, y: 18, w: 128, from: 0.1, secs: 150 }, { i: 1, y: 6, w: 200, from: 0.35, secs: 190 },
                { i: 3, y: 96, w: 165, from: 0.6, secs: 125 }]
        delegate: Image {
            id: cl
            required property var modelData
            required property int index
            y: modelData.y
            width: modelData.w
            height: width * implicitHeight / Math.max(1, implicitWidth)
            source: sky.art + "cloud" + modelData.i + "_" + sky.th + ".png"
            smooth: true; mipmap: true
            // One faint cloud on a clear day; more and denser with cloud
            property real level: index === 0 ? 0.55 + 0.4 * sky.cloud : sky.smooth(0.15 + index * 0.15, 0.45 + index * 0.15, sky.cloud)
            Behavior on level { NumberAnimation { duration: 3000 } }
            opacity: level * (1 - 0.45 * sky.dark)
            visible: opacity > 0.01
            property real t: modelData.from
            x: -width + (t % 1) * (sky.width + width)
            NumberAnimation on t {
                running: sky.shown && cl.visible
                from: cl.modelData.from; to: 1 + cl.modelData.from
                duration: cl.modelData.secs * 1000
                loops: Animation.Infinite
            }
        }
    }

    // ------------------------------------------------------------ land and van
    Repeater {
        model: sky.phases
        delegate: Image {
            required property string modelData
            anchors.fill: parent
            source: sky.art + "land_" + modelData + "_" + sky.th + ".png"
            opacity: sky.weights[modelData]
            visible: opacity > 0.005
            smooth: true
            Behavior on opacity { NumberAnimation { duration: 2000 } }
        }
    }
    Item {
        id: van
        x: 26
        y: sky.height - 8 - height
        width: 222
        height: width * 253 / 480
        Repeater {
            model: sky.phases
            delegate: Image {
                required property string modelData
                anchors.fill: parent
                source: sky.art + "van_" + modelData + "_" + sky.th + ".png"
                opacity: sky.weights[modelData]
                visible: opacity > 0.005
                smooth: true; mipmap: true
                Behavior on opacity { NumberAnimation { duration: 2000 } }
            }
        }
        // The living area lit after dark, brighter with more load
        Image {
            anchors.fill: parent
            source: sky.art + "van_windows.png"
            property real level: sky.dark * Math.max(0.3, Math.min(1, sky.loadW / 250))
            Behavior on level { NumberAnimation { duration: 1200 } }
            opacity: level * (Theme.dark ? 0.85 : 1)
            visible: opacity > 0.01
            smooth: true; mipmap: true
        }
    }

    // Sunrise and sunset on the land
    Text {
        visible: Solar.located && !isNaN(Solar.sunrise.getTime ? Solar.sunrise.getTime() : NaN)
        x: 280
        y: sky.height - 24
        text: {
            void Solar.now;
            var r = sky.e < 0 && Solar.now.getTime() > Solar.sunset.getTime() ? Solar.nextSunrise : Solar.sunrise;
            return "Sunrise " + (r && r.getTime && !isNaN(r.getTime()) ? Hmi.fmtTime(r, true) : "—");
        }
        color: Qt.rgba(1, 1, 1, 0.72)
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }
    Text {
        visible: Solar.located && !isNaN(Solar.sunset.getTime ? Solar.sunset.getTime() : NaN)
        anchors.right: parent.right
        anchors.rightMargin: 14
        y: sky.height - 24
        text: "Sunset " + Hmi.fmtTime(Solar.sunset, true)
        color: Qt.rgba(1, 1, 1, 0.72)
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }

    // ------------------------------------------------------------ what it is
    readonly property string label: {
        void Solar.now;
        var c = Solar.condition, el = e;
        if (!Solar.located && !(en && en.solar)) return "";
        if (el >= 8) {
            var name = c === "sun" ? "Full sun" : c === "partly" ? "Partly cloudy" : c === "cloudy" ? "Cloudy or shaded" : "Sun up";
            return Solar.throttled ? (c ? name + " · charger throttled" : "Charger throttled") : name;
        }
        if (el >= 0) return "Low sun";
        if (el >= -6) {
            var morning = Solar.located ? (Solar.southern ? Solar.sunAzimuth > 180 : Solar.sunAzimuth < 180) : Solar.now.getHours() < 12;
            return morning ? "Dawn" : "Dusk";
        }
        if (!Solar.located) return "Night";
        var rise = Solar.nextMoonrise;
        var up = Solar.moonElevation > 0, when = rise && rise.getTime && !isNaN(rise.getTime());
        return "Night · " + Solar.moonPhaseName + (!up && when ? " · rises " + Hmi.fmtTime(rise, true) : "");
    }
    Rectangle {
        id: pillBox
        visible: sky.label !== ""
        x: 12; y: 12
        width: pill.implicitWidth + 24
        height: 32
        radius: 16
        color: Theme.dark ? Qt.rgba(0, 0, 0, 0.55) : Qt.rgba(1, 1, 1, 0.8)
        Row {
            id: pill
            anchors.centerIn: parent
            spacing: 8
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: sky.label
                color: Theme.tx
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }
            Num {
                anchors.verticalCenter: parent.verticalCenter
                visible: !!(sky.en && sky.en.fresh && sky.en.solar && sky.en.solarW > 0)
                text: (sky.en ? sky.en.solarW : 0) + " W"
                color: Theme.acc
                font.pixelSize: 14
                font.weight: Font.Bold
            }
        }
    }

    // The card's rounded corners, cut in the page colour: cheaper than a mask
    Repeater {
        model: 4
        delegate: Item {
            required property int index
            readonly property int r: 22
            x: index % 2 ? sky.width - r : 0
            y: index > 1 ? sky.height - r : 0
            width: r; height: r
            clip: true
            Rectangle {
                x: (parent.index % 2 ? -parent.r : 0) - 30
                y: (parent.index > 1 ? -parent.r : 0) - 30
                width: parent.r * 2 + 60
                height: parent.r * 2 + 60
                radius: parent.r + 30
                color: "transparent"
                border.width: 30
                border.color: Theme.bg
            }
        }
    }

    // One slow breath for the sun's glow
    QtObject {
        id: breathe
        property real v: 0.75
        property SequentialAnimation anim: SequentialAnimation {
            running: sky.shown
            loops: Animation.Infinite
            NumberAnimation { target: breathe; property: "v"; from: 0.6; to: 1; duration: 2200; easing.type: Easing.InOutSine }
            NumberAnimation { target: breathe; property: "v"; from: 1; to: 0.6; duration: 2200; easing.type: Easing.InOutSine }
        }
    }
}
