import QtQuick

// A screen. Fades and rises 8 px when it appears, as the mockup's "kin" keyframe.
Item {
    id: page
    property int enterMs: 250

    onVisibleChanged: if (visible) enter.restart()
    Component.onCompleted: if (visible) enter.restart()

    ParallelAnimation {
        id: enter
        NumberAnimation { target: page; property: "opacity"; from: 0; to: 1; duration: page.enterMs; easing.type: Easing.InOutQuad }
        NumberAnimation { target: page; property: "translateY"; from: 8; to: 0; duration: page.enterMs; easing.type: Easing.InOutQuad }
    }
    property real translateY: 0
    transform: Translate { y: page.translateY }
}
