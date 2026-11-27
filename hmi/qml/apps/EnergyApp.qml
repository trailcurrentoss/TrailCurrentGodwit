import QtQuick
import Godwit

// The real sky over the camper and the van under it, then the house battery
// with its level, and when it will be full or run out. On the head unit the
// numbers come from Solstice on the TrailCurrent bus; the sky from Solar.
AppPage {
    id: page
    readonly property var en: Hmi.energy
    readonly property bool live: !!en.live
    readonly property bool known: en.soc >= 0
    readonly property color socCol: Hmi.lowBattery ? Theme.heat : Theme.acc
    // Net battery power: + charging, - discharging
    readonly property real net: en.fresh && en.watts !== undefined ? en.watts : 0
    readonly property int dir: !en.fresh ? 0 : net > 5 ? 1 : net < -5 ? -1 : 0
    function dash(ok, text) { return ok ? text : "—"; }
    function signedW(w) { return (w > 0 ? "+" : w < 0 ? "−" : "") + Math.abs(Math.round(w)) + " W"; }
    function dur(mins) {
        mins = Math.max(1, Math.round(mins));
        var h = Math.floor(mins / 60), m = mins % 60;
        if (h >= 2) return Math.round(mins / 60) + " h";
        return h ? h + " h" + (m ? " " + m + " min" : "") : m + " min";
    }

    // The line under the level: when it will be full, or how long it lasts,
    // measured against the next sunrise after dark. {t, c}
    readonly property var status: {
        void Solar.now;
        if (live && !en.fresh) return { t: "Solstice is not reporting", c: Theme.tx2 };
        if (!known) return { t: "", c: Theme.tx2 };
        if (en.charge === "Float" || (en.soc >= 99 && dir >= 0)) return { t: "Battery full", c: Theme.acc };
        if (dir > 0) {
            var amps = en.amps || net / 12.8;
            if (Solar.batteryAh > 0 && amps > 0.5) {
                var h = (100 - en.soc) / 100 * Solar.batteryAh / amps;
                if (h < 24) return { t: "Charging · full by " + Hmi.fmtTime(new Date(Solar.now.getTime() + h * 3600000), true), c: Theme.tx2 };
            }
            return { t: "Charging", c: Theme.tx2 };
        }
        if (dir < 0) {
            var mins = en.ttg >= 0 ? en.ttg : Solar.batteryAh > 0 ? en.soc / 100 * Solar.batteryAh * 12.8 / -net * 60 : -1;
            if (mins < 0) return { t: "Discharging", c: Hmi.lowBattery ? Theme.heat : Theme.tx2 };
            if (mins > 24 * 60)
                return { t: "Lasts " + Math.floor(mins / 1440) + " d " + Math.floor(mins % 1440 / 60) + " h at this rate", c: Theme.tx2 };
            var end = new Date(Solar.now.getTime() + mins * 60000);
            var rise = Solar.nextSunrise, dark = Solar.located && Solar.sunElevation < 0 && rise && !isNaN(rise.getTime());
            if (Hmi.lowBattery)
                return { t: "Runs out " + Hmi.fmtTime(end, true), c: Theme.heat };
            if (dark && end.getTime() >= rise.getTime())
                return { t: "Lasts until " + Hmi.fmtTime(end, true) + " · past sunrise", c: Theme.acc };
            if (dark)
                return { t: "Runs out " + Hmi.fmtTime(end, true) + " · " + dur((rise.getTime() - end.getTime()) / 60000) + " before sunrise", c: Theme.warn };
            return { t: "Lasts until " + Hmi.fmtTime(end, true), c: Theme.tx2 };
        }
        return { t: "Holding steady", c: Theme.tx2 };
    }

    EnergySky {
        id: sky
        en: page.en
        x: 0
        y: 0
        width: 548
        height: 220
    }

    // ------------------------------------------------------------ the battery
    Item {
        id: batt
        x: 6
        y: sky.height + 18
        height: parent.height - y - 12
        width: height * (Theme.dark ? 303 / 340 : 321 / 340)
        // The front panel, as fractions of the image (assets/energy/README.md)
        readonly property var box: Theme.dark ? [0.090, 0.910, 0.236, 0.884] : [0.106, 0.894, 0.250, 0.880]
        Image {
            anchors.fill: parent
            source: "../../assets/energy/battery_" + (Theme.dark ? "dark" : "light") + ".png"
            smooth: true
            mipmap: true
        }
        Item {
            x: batt.box[0] * batt.width + 6
            y: batt.box[2] * batt.height + 6
            width: (batt.box[1] - batt.box[0]) * batt.width - 12
            height: (batt.box[3] - batt.box[2]) * batt.height - 12
            Rectangle {
                id: fill
                anchors.bottom: parent.bottom
                width: parent.width
                height: parent.height * Math.max(0, page.en.soc) / 100
                radius: 8
                color: page.socCol
                opacity: 0.92
                clip: true
                Behavior on height { NumberAnimation { duration: 600; easing.type: Easing.OutCubic } }
                Behavior on color { ColorAnimation { duration: 400 } }
                // Charge on the move: light bands rise while charging and sink
                // while discharging, quicker the more current flows
                Repeater {
                    model: 3
                    delegate: Rectangle {
                        id: band
                        required property int index
                        property real t: index / 3
                        visible: page.dir !== 0
                        width: fill.width
                        height: 3
                        radius: 1.5
                        color: "white"
                        readonly property real p: (t + index / 3) % 1
                        y: page.dir > 0 ? fill.height * (1 - p) : fill.height * p
                        opacity: 0.4 * Math.sin(p * Math.PI)
                        NumberAnimation on t {
                            running: band.visible && page.visible
                            from: 0; to: 1
                            loops: Animation.Infinite
                            duration: Math.max(1400, 4200 - Math.abs(page.en.amps || 0) * 60)
                            onDurationChanged: if (running) restart()
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------ the numbers
    Column {
        x: batt.x + batt.width + 22
        y: sky.height + 16
        width: list.x - 28 - x
        spacing: 2
        Text { text: "Battery Level"; color: Theme.tx2; font.pixelSize: 15; font.weight: Font.DemiBold }
        Num {
            text: page.known ? page.en.soc + "%" : "—"
            color: page.known ? page.socCol : Theme.tx2
            font.pixelSize: 84
            font.letterSpacing: -3
            lineHeight: 0.92
            Behavior on color { ColorAnimation { duration: 400 } }
        }
        Text {
            width: parent.width
            text: page.status.t
            color: page.status.c
            font.pixelSize: 17
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Item { width: 1; height: 12 }
        Row {
            width: parent.width
            Repeater {
                model: [
                    { l: "Solar", v: page.dash(page.en.fresh && page.en.solar, page.en.solarW + " W"), warn: false },
                    { l: "Battery", v: page.dash(page.en.fresh && page.en.watts !== undefined, page.signedW(page.net)), warn: false },
                    { l: "Charger", v: page.dash(page.en.fresh && !!page.en.charge, page.en.charge), warn: page.en.charge === "Fault" }
                ]
                delegate: Column {
                    required property var modelData
                    width: parent.width / 3
                    Text { text: modelData.l; color: Theme.tx2; font.pixelSize: 13; font.weight: Font.DemiBold }
                    Num { text: modelData.v; color: modelData.warn ? Theme.warn : Theme.tx; font.pixelSize: 24 }
                }
            }
        }
    }

    ListRows {
        id: list
        app: "energy"
        anchors.right: parent.right
        width: 400
        height: parent.height
    }
}
