#!/usr/bin/env python3
"""Turn the Level screen's renders into the HMI's artwork.

    python3 level_post.py RENDERDIR SOURCEDIR ASSETDIR

RENDERDIR holds level_art_blender.py's output (vial, bubble, boards,
blocks, ramps).
SOURCEDIR holds van_rear_full.png, the ComfyUI cutout. Everything is written
at twice the size the HMI shows it.
"""
import math
import os
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter

src, cut, dst = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(dst, exist_ok=True)
THEMES = ("light", "dark")
DARK = 0.8   # dark-theme copies of rendered vehicles, as the Climate van


def solid(im):
    """RemBG leaves glass and pale paint partly see-through: fill the body
    back in, keeping only the soft outer edge."""
    a = im.getchannel("A")
    body = a.point(lambda v: 255 if v > 128 else 0)
    # Holes are what the outside cannot reach: flood the outside, invert
    w, h = body.size
    pad = Image.new("L", (w + 2, h + 2), 0)
    pad.paste(body, (1, 1))
    ImageDraw.floodfill(pad, (0, 0), 128)
    inside = pad.crop((1, 1, w + 1, h + 1)).point(lambda v: 0 if v == 128 else 255)
    inside = inside.filter(ImageFilter.MinFilter(5))       # stay off the edge
    out = im.copy()
    out.putalpha(ImageChops.lighter(a, inside))
    return out


# --- the van from behind: 2x its 140 px display height
van = solid(Image.open(f"{cut}/van_rear_full.png").convert("RGBA"))
H = 280
van = van.resize((round(van.width * H / van.height), H), Image.LANCZOS)
van.save(f"{dst}/van_rear_light.png", optimize=True)
r, g, b, a = van.split()
Image.merge("RGBA", (r.point(lambda v: v * DARK), g.point(lambda v: v * DARK), b.point(lambda v: v * DARK), a)).save(
    f"{dst}/van_rear_dark.png", optimize=True)

# --- the bull's-eye: 2x its 150 px display size; the dark bezel toned down
for t in THEMES:
    v = Image.open(f"{src}/vial_{t}.png").convert("RGBA").resize((300, 300), Image.LANCZOS)
    if t == "dark":
        px = v.load()
        c = (v.width - 1) / 2
        for y in range(v.height):
            for x in range(v.width):
                d = math.hypot(x - c, y - c) / c
                if d > 0.84:
                    p = px[x, y]
                    k = 0.32
                    px[x, y] = (int(p[0] * k), int(p[1] * k), int(p[2] * k), p[3])
    v.save(f"{dst}/vial_{t}.png", optimize=True)
    Image.open(f"{src}/bubble_{t}.png").convert("RGBA").resize((52, 52), Image.LANCZOS).save(f"{dst}/bubble_{t}.png", optimize=True)

# --- boards, blocks and ramps, trimmed to their outline. The ramp's top
# follows height = full * sin(pi/2 * x / length) from its low front edge.
for t in THEMES:
    for kind in ("board", "block", "ramp"):
        for side in ("side", "end"):
            bd = Image.open(f"{src}/{kind}_{side}_{t}.png").convert("RGBA")
            bd = bd.crop(bd.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox())
            if kind != "board" and t == "dark":
                # Charcoal plastic disappears on the dark theme's ground: the
                # dark copy is the light render at 85%, as the vans are
                lt = Image.open(f"{src}/{kind}_{side}_light.png").convert("RGBA")
                bd = lt.crop(lt.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox())
                r, g, b, a = bd.split()
                bd = Image.merge("RGBA", (r.point(lambda v: v * 0.85), g.point(lambda v: v * 0.85), b.point(lambda v: v * 0.85), a))
            bd.save(f"{dst}/{kind}_{side}_{t}.png", optimize=True)

print("wrote", len(os.listdir(dst)), "files to", dst)
