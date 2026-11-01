#!/usr/bin/env python3
"""Turn a Headwaters web map style into one MapLibre Native reads from disk.

    godwit-style.py <in style.json> <out style.json> <share dir> <tiles path> [--night]

Headwaters serves tiles, glyphs and sprites over HTTP from its frontend. On
Godwit they are local files: the tiles come from the installed map bundle, the
glyphs and sprites from this package. The style's own camera is cleared
because the HMI sets pitch and bearing from the knobs.

--night repaints the Headwaters light style with Godwit's night palette. The
web app darkens its map with a CSS filter, which MapLibre Native does not
have, so the head unit needs every colour chosen for the dark cabin. Layers,
filters and zoom rules stay Headwaters', so they follow upstream; a layer the
palette does not cover stops the build rather than ship bright.
"""
import json
import re
import sys

# Godwit night palette. Land sits just above the HMI's black so the map's edge
# reads against the chrome; nothing is white, roads rank by brightness with
# motorways a muted amber, and labels are the brightest thing left. The route
# and vehicle (drawn by the HMI) are the only saturated colours.
BG = "#05070a"
HALO = BG
CASING_MOTORWAY = "#2b2115"
CASING_MAJOR = "#1f1d1a"
CASING_MINOR = "#15181c"
RAIL = "#2f3338"
ICONS = 0.6  # sprite icons are drawn for a light map

PLACE = {"text-halo-color": HALO, "text-halo-width": 1.4}
ROAD_NAME = {"text-halo-color": HALO, "text-halo-width": 1.2}

# (layer id, paint) in order; the first match wins
NIGHT = [
    ("background", {"background-color": BG}),
    # Land
    ("landcover-(glacier|ice-shelf)", {"fill-color": "#0f161c"}),
    ("landuse-(residential|railway)", {"fill-color": "#0c1014"}),
    ("landuse-commercial", {"fill-color": "#100d12"}),
    ("landuse-industrial", {"fill-color": "#100f0c"}),
    ("landuse-cemetery", {"fill-color": "#0b120d"}),
    ("landuse-hospital", {"fill-color": "#130c10"}),
    ("landuse-school", {"fill-color": "#0e0c13"}),
    ("landcover-wood", {"fill-color": "#0d1a10", "fill-opacity": 1, "fill-outline-color": "rgba(0, 0, 0, 0)"}),
    ("landcover-grass(-park)?", {"fill-color": "#0b140d"}),
    ("landcover-sand", {"fill-color": "#14130c"}),
    # Water
    ("water(-offset|-intermittent)?", {"fill-color": "#0a1a28"}),
    ("water-pattern", None),
    ("waterway(_tunnel|-(other|stream-canal|river)(-intermittent)?)", {"line-color": "#123049"}),
    ("ferry", {"line-color": "#1b4058"}),
    # Buildings
    ("building-3d-extrusion", {"fill-extrusion-color": "#161b22", "fill-extrusion-opacity": 0.9}),
    ("building", {"fill-color": "#11151b"}),
    ("building-top", {"fill-color": "#141920", "fill-outline-color": "#0d1015"}),
    # Roads: casings, then fills from motorway down; tunnels dimmer
    ("(tunnel|highway|bridge)-motorway(-link)?-casing", {"line-color": CASING_MOTORWAY}),
    ("(tunnel|highway|bridge)-(link|secondary-tertiary|trunk-primary|primary|trunk)-casing", {"line-color": CASING_MAJOR}),
    ("(tunnel|highway|bridge)-(minor|service-track|path-steps|path)-casing", {"line-color": CASING_MINOR}),
    ("tunnel-motorway(-link)?", {"line-color": "#56432a"}),
    ("(highway|bridge)-motorway(-link)?", {"line-color": "#7d6038"}),
    ("tunnel-(link|secondary-tertiary|trunk-primary)", {"line-color": "#36342f"}),
    ("(highway|bridge)-(link|trunk-primary|primary|trunk)", {"line-color": "#4f4c45"}),
    ("(highway|bridge)-secondary-tertiary", {"line-color": "#4a4843"}),
    ("tunnel-(minor|service-track)", {"line-color": "#202328"}),
    ("(highway|bridge)-minor", {"line-color": "#373c43"}),
    ("(tunnel|highway|bridge)-path-steps", {"line-color": "#36322d"}),
    ("(tunnel|highway|bridge)-path", {"line-color": "#3b3630"}),
    ("highway-area", {"fill-color": "#13161a", "fill-outline-color": "#1a1d21"}),
    ("road_area_pier", {"fill-color": "#13161a"}),
    ("road_pier", {"line-color": "#13161a"}),
    ("road_oneway(_opposite)?", {"icon-opacity": 0.35}),
    # Rail and air
    ("(tunnel-|bridge-)?railway(-transit|-service)?(-hatching)?", {"line-color": RAIL}),
    ("cablecar(-dash)?", {"line-color": RAIL}),
    ("aeroway-area", {"fill-color": "#101318"}),
    ("aeroway-(runway|taxiway)-casing", {"line-color": "#181b20"}),
    ("aeroway-(runway|taxiway)", {"line-color": "#272b31"}),
    # Boundaries
    ("boundary-land-level-4", {"line-color": "#3a3746"}),
    ("boundary-land-(level-2|disputed)", {"line-color": "#4a4658"}),
    ("boundary-water", {"line-color": "#1f3a52"}),
    # Labels
    ("waterway-name|water-name-.*", {"text-color": "#4b7ba2", "text-halo-color": HALO, "text-halo-width": 1.5}),
    ("poi-.*|airport-label-major", {"text-color": "#80858c", "text-halo-color": HALO, "icon-opacity": ICONS}),
    ("highway-name-path", {"text-color": "#6e6a64", "text-halo-color": HALO, "text-halo-width": 1}),
    ("highway-name-(minor|major)", dict(ROAD_NAME, **{"text-color": "#8a8f96"})),
    ("highway-shield.*", {"icon-opacity": ICONS}),
    ("place-(city|city-capital|town)", dict(PLACE, **{"text-color": "#b4bac2", "icon-opacity": ICONS})),
    ("place-village", dict(PLACE, **{"text-color": "#9aa0a8"})),
    ("place-(other|state)", dict(PLACE, **{"text-color": "#7e7889"})),
    ("place-(country-.*|continent)", {"text-color": "#8f949b", "text-halo-color": HALO}),
]


def night(style):
    layers = []
    unpainted = []
    for layer in style.get("layers", []):
        paint = next((p for pattern, p in NIGHT if re.fullmatch(pattern, layer["id"])), False)
        if paint is False:
            unpainted.append(layer["id"])
        elif paint is not None:
            layer.setdefault("paint", {}).update(paint)
            layers.append(layer)
    if unpainted:
        sys.exit("no night colours for layers: %s" % ", ".join(unpainted))
    style["layers"] = layers
    style["name"] = "Godwit Night"


def main(argv):
    if len(argv) < 5:
        sys.exit(__doc__)
    src, dst, share, tiles = argv[1:5]

    with open(src, encoding="utf-8") as f:
        style = json.load(f)

    for source in style.get("sources", {}).values():
        if source.get("url", "").startswith("pmtiles://"):
            source["url"] = "pmtiles://file://" + tiles
    style["glyphs"] = "file://" + share + "/fonts/{fontstack}/{range}.pbf"
    style["sprite"] = "file://" + share + "/sprites/sprite"

    for key in ("center", "zoom", "pitch", "bearing"):
        style.pop(key, None)

    if "--night" in argv[5:]:
        night(style)

    leftover = [s for s in json.dumps(style).split('"') if s.startswith(("http://", "https://", "/maps"))]
    if leftover:
        sys.exit("style still references the network or web paths: %s" % ", ".join(sorted(set(leftover))))

    with open(dst, "w", encoding="utf-8") as f:
        json.dump(style, f, separators=(",", ":"))


if __name__ == "__main__":
    main(sys.argv)
