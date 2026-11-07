import QtQuick
import MapLibre 4.0
import Godwit
import "../DemoData.js" as D

// Offline vector map: OpenMapTiles PMTiles from the installed map bundle.
// Kept in its own file so the HMI still runs where MapLibre is not built.
MapLibre {
    id: map

    // Read once, when the map is made; NavApp makes a new map for a new theme
    style: Theme.dark ? Config.mapStyleDark : Config.mapStyleLight
    // Where the map this one replaces was looking ({ center, zoom }), if any
    property var camera: null
    // Set once, not bound: a changed coordinate or zoomLevel makes the next
    // frame jump the camera, which cancels any ease in flight (the 2D/3D pitch
    // among them) and would pull a panned map back to the vehicle on every
    // fix. Afterwards the camera moves only through Hmi.mapEase: following
    // the vehicle (Hmi.follow, off once the map is dragged), Recenter, the
    // knobs and Start.
    Component.onCompleted: {
        // A trail asked for before the first map was made
        var c = camera || Hmi.mapTarget;
        coordinate = c ? c.center : Hmi.pos();
        zoomLevel = c ? c.zoom : Hmi.nav.zoom;
    }

    // A style can carry its own camera; the knobs own it here, so reapply
    // the HMI's view whenever a style (light or dark) finishes loading
    onStyleLoadedChanged: if (styleLoaded) easeTo({ center: coordinate, zoom: zoomLevel, pitch: Hmi.nav.view === "3D" ? 55 : 0,
                                                                bearing: Hmi.nav.up === "heading" && Gnss.fix ? Gnss.course : 0 }, { duration: 0 })

    Connections {
        target: Hmi
        function onMapEase(camera, duration) { map.easeTo(camera, { duration: duration }); }
    }

    // The route being previewed in search, else the active one
    readonly property var routeShape: Hmi.navSearch && Router.route.shape ? Router.route.shape
                                      : Hmi.routeActive ? Hmi.routeShape : []

    Style {
        // The trails that are on, beneath the route so guidance stays on top.
        // Thinner than the route, and cased in the map's own background so any
        // color separates from the roads beneath it.
        SourceParameter {
            styleId: "godwit-trails"
            type: "geojson"
            property var data: Trails.overlay
            onDataChanged: updateNotify()
        }
        LayerParameter {
            styleId: "godwit-trails-casing"
            type: "line"
            property string source: "godwit-trails"
            layout: { "line-cap": "round", "line-join": "round" }
            paint: { "line-color": Theme.dark ? "#000000" : "#ffffff", "line-opacity": 0.85,
                     "line-width": ["interpolate", ["linear"], ["zoom"], 4, 5, 10, 8] }
        }
        LayerParameter {
            styleId: "godwit-trails-line"
            type: "line"
            property string source: "godwit-trails"
            layout: { "line-cap": "round", "line-join": "round" }
            paint: { "line-color": ["get", "color"], "line-width": ["interpolate", ["linear"], ["zoom"], 4, 3, 10, 5] }
        }
        // The route being recorded, so far
        SourceParameter {
            styleId: "godwit-recording"
            type: "geojson"
            property var data: Trails.recordedLine
            onDataChanged: updateNotify()
        }
        LayerParameter {
            styleId: "godwit-recording-line"
            type: "line"
            property string source: "godwit-recording"
            layout: { "line-cap": "butt", "line-join": "round" }
            paint: { "line-color": "#E53935", "line-width": 5, "line-dasharray": [1.5, 1] }
        }
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
        // The vehicle, where the receiver puts it
        SourceParameter {
            styleId: "godwit-vehicle"
            type: "geojson"
            property var data: { var p = Hmi.pos(); return { type: "Feature", properties: {},
                                                             geometry: { type: "Point", coordinates: [p[1], p[0]] } }; }
            onDataChanged: updateNotify()
        }
        LayerParameter {
            styleId: "godwit-vehicle-halo"
            type: "circle"
            property string source: "godwit-vehicle"
            paint: { "circle-radius": 20, "circle-color": "#52A441", "circle-opacity": 0.22 }
        }
        LayerParameter {
            styleId: "godwit-vehicle-dot"
            type: "circle"
            property string source: "godwit-vehicle"
            paint: { "circle-radius": 10, "circle-color": "#52A441", "circle-stroke-width": 4, "circle-stroke-color": "#ffffff" }
        }
    }

    PinchHandler {
        id: pinch
        target: null
        grabPermissions: PointerHandler.TakeOverForbidden
        onScaleChanged: (delta) => {
            map.scale(delta, pinch.centroid.position);
            // Only a pinch gives a true zoomLevel: during an ease it lags. Held to
            // the knob's range, or the knob's next click could go the wrong way.
            var z = Math.min(Math.max(map.zoomLevel, Hmi.navZoomMin), Hmi.navZoomMax);
            if (z !== map.zoomLevel) map.scale(Math.pow(2, z - map.zoomLevel), pinch.centroid.position);
            Hmi.navPinched(z);
        }
    }
    DragHandler {
        target: null
        // Looking around stops the map following the vehicle until Recenter
        onActiveChanged: if (active) Hmi.follow = false
        onTranslationChanged: (delta) => map.pan(delta)
    }
}
