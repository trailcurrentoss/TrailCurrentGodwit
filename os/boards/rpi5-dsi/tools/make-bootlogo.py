#!/usr/bin/env python3
"""Make the Raspberry Pi kernel boot logo for the rpi5-dsi board.

    tools/make-bootlogo.py SPLASH.tga [OUT.tga]

SPLASH is the TrailCurrent boot splash, a 1920x1080 landscape TGA (in the
TrailCurrentHeadwaters repository, CM5/image/splash/trailcurrent-splash.tga).
The default OUT is the godwit-bootlogo recipe's copy.

The kernel's fullscreen_logo draws the image on the panel's own framebuffer:
720x1280 portrait. So the splash is scaled to the 1280x720 the panel shows in
landscape, cropped to the logo, and turned 90 degrees clockwise, the same turn
as the HMI's GODWIT_ROTATION=90. The kernel centres it and fills the rest of
the screen with the colour of its first pixel, the splash's background.

The kernel reads only uncompressed 24-bit TGA with at most 224 colours.
Needs Pillow.
"""
import os
import sys

from PIL import Image

PANEL_LANDSCAPE = (1280, 720)
MARGIN = 24
MAX_COLOURS = 224

here = os.path.dirname(os.path.abspath(__file__))
default_out = os.path.join(here, "..", "meta-rpi5-dsi", "recipes-bsp", "godwit-bootlogo", "files", "godwit-logo.tga")

if len(sys.argv) not in (2, 3):
    sys.exit(__doc__)
src = Image.open(sys.argv[1]).convert("RGB")
out = sys.argv[2] if len(sys.argv) == 3 else default_out

img = src.resize(PANEL_LANDSCAPE, Image.LANCZOS)
bg = img.getpixel((0, 0))

# Crop to whatever differs from the background, plus a margin of background
mask = Image.frombytes("L", img.size, bytes(0 if p == bg else 255 for p in img.getdata()))
left, top, right, bottom = mask.getbbox()
box = (max(0, left - MARGIN), max(0, top - MARGIN),
       min(img.width, right + MARGIN), min(img.height, bottom + MARGIN))
img = img.crop(box)

# Clockwise, as the HMI turns its screen
img = img.transpose(Image.Transpose.ROTATE_270)

# At most 224 colours, keeping the background exact so the kernel's fill matches
img = img.quantize(colors=MAX_COLOURS - 1, method=Image.Quantize.MEDIANCUT).convert("RGB")
img.putpixel((0, 0), bg)
colours = len(img.getcolors(1 << 16) or [])
if colours > MAX_COLOURS:
    sys.exit(f"{colours} colours, more than the kernel's {MAX_COLOURS}")

img.save(out, compression=None)
print(f"{out}: {img.width}x{img.height}, {colours} colours, background #{bg[0]:02x}{bg[1]:02x}{bg[2]:02x}")
