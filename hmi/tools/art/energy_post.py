#!/usr/bin/env python3
"""Turn the Energy screen's renders into the HMI's artwork.

    python3 energy_post.py RENDERDIR CUTDIR ASSETDIR

RENDERDIR holds energy_sky_blender.py's output (sky, land, clouds, moon).
CUTDIR holds the ComfyUI cutouts van_side_full.png, battery_light_full.png
and battery_dark_full.png. Everything is written at twice the size the HMI
shows it.
"""
import math
import os
import sys

from PIL import Image, ImageChops, ImageFilter

src, cut, dst = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(dst, exist_ok=True)
PHASES = ("day", "low", "overcast", "dusk", "night")
THEMES = ("light", "dark")
DARK = 0.8          # dark-theme copies of rendered objects, as the van and tank

# --- sky and land
for p in PHASES:
    for t in THEMES:
        # Opaque, so JPEG: a tenth of the size and no visible loss at this quality
        Image.open(f"{src}/sky_{p}_{t}.png").convert("RGB").save(f"{dst}/sky_{p}_{t}.jpg", quality=93, subsampling=0)
        land = Image.open(f"{src}/land_{p}_{t}.png").convert("RGBA")
        land.save(f"{dst}/land_{p}_{t}.png", optimize=True)

# --- clouds: trimmed to their own outline
for i in (1, 2, 3):
    for t in THEMES:
        c = Image.open(f"{src}/cloud{i}_{t}.png").convert("RGBA")
        c = c.crop(c.getchannel("A").point(lambda a: 255 if a > 6 else 0).getbbox())
        c.save(f"{dst}/cloud{i}_{t}.png", optimize=True)

# --- moon: the unlit side mostly see-through, keeping a faint earthshine
for k in range(8):
    for t in THEMES:
        m = Image.open(f"{src}/moon_{k}_{t}.png").convert("RGBA")
        r, g, b, a = m.split()
        lum = Image.merge("RGB", (r, g, b)).convert("L")
        alpha = ImageChops.multiply(a, lum.point(lambda v: min(255, 40 + v * 3)))
        m.putalpha(alpha)
        m.save(f"{dst}/moon_{k}_{t}.png", optimize=True)


# --- sun: a crisp warm disc and a separate wide glow, both drawn in code
def radial(size, fn):
    im = Image.new("RGBA", (size, size))
    px = im.load()
    c = (size - 1) / 2
    for y in range(size):
        for x in range(size):
            px[x, y] = fn(math.hypot(x - c, y - c) / c)
    return im


def disc(d):
    a = 1.0 if d < 0.86 else max(0.0, (1.0 - d) / 0.14)
    w = 1.0 - 0.25 * d * d
    return (255, int(246 * w + 9), int(215 * w + 20), int(255 * a))


def glow(d):
    a = max(0.0, 1.0 - d) ** 2.2
    return (255, 214, 140, int(255 * a))


radial(96, disc).save(f"{dst}/sun_disc.png", optimize=True)
radial(320, glow).save(f"{dst}/sun_glow.png", optimize=True)

# --- van: one copy per light, scaled to 2x its 240 px display width
VAN_W = 480
TINT = {   # multiply per phase: daylight, warm low sun, cool grey overcast,
           # blue dusk, moonlit night
    "day": (1.0, 1.0, 1.0), "low": (1.0, 0.86, 0.72), "overcast": (0.86, 0.88, 0.91),
    "dusk": (0.52, 0.5, 0.62), "night": (0.2, 0.23, 0.32),
}
van = Image.open(f"{cut}/van_side_full.png").convert("RGBA")
van = van.resize((VAN_W, round(van.height * VAN_W / van.width)), Image.LANCZOS)
for p, tint in TINT.items():
    for t in THEMES:
        k = 1.0 if t == "light" else DARK
        r, g, b, a = van.split()
        out = Image.merge("RGBA", (r.point(lambda v: v * tint[0] * k), g.point(lambda v: v * tint[1] * k),
                                   b.point(lambda v: v * tint[2] * k), a))
        out.save(f"{dst}/van_{p}_{t}.png", optimize=True)

# Lit windows: the dark glass of the living-area window, warm amber, so
# QML can light it with the loads at night
W, H = van.size
box = (int(0.394 * W), int(0.398 * H), int(0.944 * W), int(0.606 * H))
mask = Image.new("L", van.size, 0)
lum = van.convert("L")
mpx, lpx = mask.load(), lum.load()
for y in range(box[1], box[3]):
    for x in range(box[0], box[2]):
        if lpx[x, y] < 70:
            mpx[x, y] = 255
# Warm light behind the glass: brightest low in the window where the lamps
# are, keeping the glass's own reflections, plus a soft bloom past the frame
core = mask.filter(ImageFilter.GaussianBlur(0.8))
bloom = mask.filter(ImageFilter.GaussianBlur(5)).point(lambda v: int(v * 0.55))
alpha = ImageChops.lighter(core.point(lambda v: int(v * 0.88)), bloom)
win = Image.new("RGBA", van.size)
wpx, vpx = win.load(), van.load()
for y in range(H):
    f = 0.0 if not (box[1] - 8 <= y <= box[3] + 8) else min(1.0, max(0.0, (y - box[1]) / (box[3] - box[1])))
    for x in range(W):
        r, g, b, _ = vpx[x, y]
        refl = (r + g + b) / 765        # glass reflections show through
        wpx[x, y] = (255, int(170 + 50 * f + 30 * refl), int(80 + 50 * f + 40 * refl), 0)
win.putalpha(alpha)
win.save(f"{dst}/van_windows.png", optimize=True)

# --- battery: 2x its 170 px display height
for t in THEMES:
    b = Image.open(f"{cut}/battery_{t}_full.png").convert("RGBA")
    b = b.resize((round(b.width * 340 / b.height), 340), Image.LANCZOS)
    b.save(f"{dst}/battery_{t}.png", optimize=True)

print("wrote", len(os.listdir(dst)), "files to", dst)
