#!/usr/bin/env python3
"""Turn a Headwaters web map style into one MapLibre Native reads from disk.

    godwit-style.py <in style.json> <out style.json> <share dir> <tiles path> [--dark]

Headwaters serves tiles, glyphs and sprites over HTTP from its frontend. On
Godwit they are local files: the tiles come from the installed map bundle, the
glyphs and sprites from this package. The style's own camera is cleared
because the HMI sets pitch and bearing from the knobs.
"""
import json
import sys


def main(argv):
    if len(argv) < 5:
        sys.exit(__doc__)
    src, dst, share, tiles = argv[1:5]
    dark = "--dark" in argv[5:]

    with open(src, encoding="utf-8") as f:
        style = json.load(f)

    for source in style.get("sources", {}).values():
        if source.get("url", "").startswith("pmtiles://"):
            source["url"] = "pmtiles://file://" + tiles
    style["glyphs"] = "file://" + share + "/fonts/{fontstack}/{range}.pbf"
    style["sprite"] = "file://" + share + "/sprites/sprite"

    for key in ("center", "zoom", "pitch", "bearing"):
        style.pop(key, None)

    if dark:
        for layer in style.get("layers", []):
            if layer.get("type") == "fill-extrusion":
                layer.setdefault("paint", {})["fill-extrusion-color"] = "#2a2a2a"

    leftover = [s for s in json.dumps(style).split('"') if s.startswith(("http://", "https://", "/maps"))]
    if leftover:
        sys.exit("style still references the network or web paths: %s" % ", ".join(sorted(set(leftover))))

    with open(dst, "w", encoding="utf-8") as f:
        json.dump(style, f, separators=(",", ":"))


if __name__ == "__main__":
    main(sys.argv)
