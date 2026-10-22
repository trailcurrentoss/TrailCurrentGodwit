pragma Singleton
import QtQuick

// Design tokens from the mockup's TH table (--k-* variables), light and dark.
QtObject {
    property bool dark: false

    readonly property color bg: dark ? "#000000" : "#f5f5f5"
    readonly property color screen: dark ? "#000000" : "#ffffff"
    readonly property color card: dark ? "#1a1a1a" : "#ffffff"
    readonly property color track: dark ? "#2a2a2a" : "#ebebeb"
    readonly property color raise: dark ? "#3a3a3a" : "#ffffff"
    readonly property color line: dark ? "#3a3a3a" : "#cacaca"
    readonly property color tx: dark ? "#ffffff" : "#1a1a1a"
    readonly property color tx2: dark ? "#aaaaaa" : "#4a4a4a"
    readonly property color acc: dark ? "#7BC96A" : "#3D7B31"
    readonly property color onAcc: dark ? "#000000" : "#ffffff"
    readonly property color accSoft: dark ? Qt.rgba(82 / 255, 164 / 255, 65 / 255, 0.24) : "#DCEDD9"
    readonly property color heat: dark ? "#FF5453" : "#C62828"
    readonly property color cool: dark ? "#48E6FE" : "#00708A"
    readonly property color warn: dark ? "#FFC107" : "#8A5A00"
    readonly property color onAlert: dark ? "#000000" : "#ffffff"
    // The mockup blurs what is behind the status and bottom bars. They sit over
    // mostly static content, so a near-opaque tint looks the same for no GPU cost.
    readonly property color chrome: dark ? Qt.rgba(10 / 255, 10 / 255, 10 / 255, 0.9) : Qt.rgba(245 / 255, 245 / 255, 245 / 255, 0.92)
    // Card shadows only show on the light theme
    readonly property bool shadows: !dark

    // Encoder display text colours are named so Hmi can stay theme-free
    function named(n) {
        switch (n) {
        case "acc": return acc;
        case "heat": return heat;
        case "cool": return cool;
        case "warn": return warn;
        default: return tx;
        }
    }

    readonly property string font: "Inter"
}
