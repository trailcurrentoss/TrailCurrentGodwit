import QtQuick
import MapLibre 4.0
import Godwit
import "../DemoData.js" as D

// Offline vector map: OpenMapTiles PMTiles from the installed map bundle.
// Kept in its own file so the HMI still runs where MapLibre is not built.
MapLibre {
    id: map

    style: Theme.dark ? Config.mapStyleDark : Config.mapStyleLight
    coordinate: D.MAPC
    zoomLevel: Hmi.nav.zoom

    // A style can carry its own camera; the knobs own it here, so reapply
    // the HMI's view whenever a style (light or dark) finishes loading
    onStyleLoadedChanged: if (styleLoaded) easeTo({ center: coordinate, zoom: zoomLevel, pitch: Hmi.nav.view === "3D" ? 55 : 0, bearing: 0 }, { duration: 0 })

    Connections {
        target: Hmi
        function onMapEase(camera, duration) { map.easeTo(camera, { duration: duration }); }
    }

    PinchHandler {
        id: pinch
        target: null
        grabPermissions: PointerHandler.TakeOverForbidden
        onScaleChanged: (delta) => map.scale(delta, pinch.centroid.position)
    }
    DragHandler {
        target: null
        onTranslationChanged: (delta) => map.pan(delta)
    }
}
