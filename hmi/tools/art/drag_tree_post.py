#!/usr/bin/env python3
"""Turn drag_tree_blender.py's renders into the HMI's tree artwork.

    python3 drag_tree_post.py RENDERDIR ASSETDIR

Crops the four renders to one shared frame ending just below the red lamp,
fades the pole out, scales them to 512 px tall (twice the size the HMI
shows) and writes tree_{off,on}_{light,dark}.png and the glow sprites to
ASSETDIR. Prints each lens's box as fractions of the image, which
TrackApp.qml carries in its tree's `lamps` table.
"""
import json
import math
import sys

from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
d = json.load(open(f"{src}/lamps_render.json"))
ims = {f"{s}_{t}": Image.open(f"{src}/tree_{s}_{t}_raw.png").convert("RGBA")
       for s in ("off", "on") for t in ("light", "dark")}

box = None
for im in ims.values():
    b = im.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
    box = b if box is None else (min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
pad = 4
x0, y0, x1 = max(0, box[0] - pad), max(0, box[1] - pad), box[2] + pad
y1 = min(box[3], int(d["lamps"]["red"][3] + 95))
H = 512
W = round((x1 - x0) * H / (y1 - y0))
FADE = 70
for name, im in ims.items():
    c = im.crop((x0, y0, x1, y1))
    a = c.getchannel("A")
    px = a.load()
    w, h = c.size
    for y in range(h - FADE, h):
        f = ((h - y) / FADE) ** 2
        for x in range(w):
            px[x, y] = int(px[x, y] * f)
    c.putalpha(a)
    c.resize((W, H), Image.LANCZOS).save(f"{dst}/tree_{name}.png", optimize=True)

# Soft round glows laid over a lit lens
GLOWS = {"amber": (255, 150, 10), "green": (60, 255, 110), "red": (255, 40, 30), "stage": (255, 236, 190)}
S = 96
for name, rgb in GLOWS.items():
    g = Image.new("RGBA", (S, S))
    px = g.load()
    for y in range(S):
        for x in range(S):
            r = math.hypot(x + 0.5 - S / 2, y + 0.5 - S / 2) / (S / 2)
            px[x, y] = (*rgb, int(255 * 0.75 * max(0.0, 1 - r) ** 2.2))
    g.save(f"{dst}/glow_{name}.png", optimize=True)

lamps = {k: [round((a - x0) / (x1 - x0), 4), round((b - y0) / (y1 - y0), 4),
             round((c - x0) / (x1 - x0), 4), round((e - y0) / (y1 - y0), 4)]
         for k, (a, b, c, e) in sorted(d["lamps"].items())}
print(f"{W}x{H}")
print(json.dumps(lamps))
