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
    // Set once, not bound: a changed zoomLevel makes the next frame jump the
    // camera, which cancels any ease in flight (the 2D/3D pitch among them).
    // The knobs move the camera through Hmi.mapEase instead.
    Component.onCompleted: zoomLevel = Hmi.nav.zoom

    // A style can carry its own camera; the knobs own it here, so reapply
    // the HMI's view whenever a style (light or dark) finishes loading
    onStyleLoadedChanged: if (styleLoaded) easeTo({ center: coordinate, zoom: zoomLevel, pitch: Hmi.nav.view === "3D" ? 55 : 0, bearing: 0 }, { duration: 0 })

    Connections {
        target: Hmi
        function onMapEase(camera, duration) { map.easeTo(camera, { duration: duration }); }
    }

    // The route being previewed in search, else the active one
    readonly property var routeShape: Hmi.navSearch && Router.route.shape ? Router.route.shape
                                      : Hmi.routeActive ? Hmi.routeShape : []

    Style {
        SourceParameter {
            styleId: "godwit-route"
            type: "geojson"
            property var data: map.routeShape.length > 1
                ? { type: "Feature", properties: {}, geometry: { type: "LineString", coordinates: map.routeShape } }
                : { type: "FeatureCollection", features: [] }
            onDataChanged: updateNotify()
        }
        LayerParameter {
            styleId: "godwit-route-line"
            type: "line"
            property string source: "godwit-route"
            layout: { "line-cap": "round", "line-join": "round" }
            paint: { "line-color": "#52A441", "line-width": 7, "line-opacity": 0.9 }
        }
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
