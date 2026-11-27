# Energy screen art

The Energy screen's sky card and battery. The card is a stack of layers that
`EnergyApp.qml` blends and moves, so one set of images covers every hour,
every amount of cloud, and every phase of the moon. Every image comes in a
light and a dark version.

| Files | What they are | Made with |
|---|---|---|
| `sky_<phase>_<theme>.jpg` | The sky alone, no sun disc and no land. 1096 × 440, twice the card's 548 × 220 | Blender |
| `land_<phase>_<theme>.png` | A distant mountain range, nearer hills and the ground, transparent above, lit like the sky of the same name | Blender |
| `cloud1-3_<theme>.png` | Single volumetric cumulus clouds, transparent | Blender |
| `moon_<0-7>_<theme>.png` | The moon at eight phases: 0 new, 2 first quarter, 4 full, 6 last quarter. The unlit side is mostly see-through with a faint earthshine. Drawn mirrored south of the equator | Blender |
| `sun_disc.png`, `sun_glow.png` | The sun, and the wide glow around it | Pillow, in `energy_post.py` |
| `van_<phase>_<theme>.png` | The camper van in side view with roof panels, tinted for each light | ComfyUI, tinted in `energy_post.py` |
| `van_windows.png` | Warm light in the van's living-area window, laid over the van after dark as loads draw power | `energy_post.py`, from the van |
| `battery_<theme>.png` | The house battery. Its charge is drawn in QML inside the front panel | ComfyUI |

`phase` is `day` (sun 55° up), `low` (1.5°), `overcast` (day light under a
cloud deck), `dusk` (−3°) or `night` (−14°). `theme` is `light` or `dark`:
the dark versions are the same scenes 2.6 stops darker and less saturated, so
the card sits on the black page without glare.

## Sky, land, clouds and moon (Blender)

`hmi/tools/art/energy_sky_blender.py` builds every scene from nothing and
renders it on the CPU with Cycles:

```sh
blender -b -P hmi/tools/art/energy_sky_blender.py -- RENDERDIR [layers] [clouds] [moon]
```

- **Sky:** Blender's Sky Texture, Multiple Scattering, 1500 m up, so each
  phase's colour comes from the sun's real height. The overcast sky is the
  day sky at a quarter of the saturation and a little over half the light.
- **Land:** two ridgelines made of sine waves (the same every run), a far
  range in pale blue-grey and darker nearer hills, over flat ground. The land
  has no specular reflection, so a blue sky does not turn it into a lake.
- **Clouds:** spheres merged into a volume (Mesh to Volume), broken up with a
  Clouds noise displacement, Principled Volume at density 3, eight volume
  bounces, a high sun behind the viewer and blue sky fill.
- **Moon:** a sphere with soft noise maria and Voronoi crater bump, lit by a
  sun lamp turned 45° per phase, with a very faint blue world for earthshine.

## Van and battery (ComfyUI)

Z-Image Turbo (`z_image_turbo_bf16`, template `image_z_image_turbo`), 8
steps, batch of 4. The exact workflows are in `source/`.

| Image | Size | Seed | Picked |
|---|---|---|---|
| Van | 1344 × 768 | 6101 | 2nd |
| Battery, light | 896 × 1152 | 6201 | 2nd |
| Battery, dark | 896 × 1152 | 6301 | 1st |

Van prompt:

> Side profile view from slightly above of a single white long-wheelbase
> high-roof cargo van camper conversion, Sprinter-style, facing left, the whole
> van in frame with generous margin. Two large black monocrystalline solar
> panels mounted flat on a low aluminium roof rack cover most of the roof and
> are clearly visible from this slightly elevated angle. A sliding side door
> window and a rear side window with dark tinted glass, black wheels with
> all-terrain tyres, black bumpers. Premium minimalist 3D product render, soft
> even studio lighting, very soft shadow directly beneath only, isolated on a
> seamless flat very light grey background, Apple product photography style,
> crisp, high detail. No text, no words, no letters, no numbers, no logo, no
> label, no badge, no license plate, no decals, no watermark.

Battery prompt (light; the dark one says "matte dark graphite plastic case",
"smoked translucent front window panel", and ends with "dramatic low-key
studio lighting with a thin crisp white rim light along the edges, isolated on
a seamless flat pure black background"):

> Straight-on front orthographic view of a single modern lithium iron
> phosphate RV house battery, a rectangular box taller than wide, matte warm
> white plastic case with softly rounded corners, a large flat smooth recessed
> frosted translucent front window panel covering most of the front face, two
> short terminal posts on top with a red cap and a black cap, a slim carry
> handle recessed into the lid, perfectly centered, fills most of the frame.
> Premium minimalist 3D product render, soft even studio lighting, very soft
> shadow directly beneath only, isolated on a seamless flat very light grey
> background, Apple product photography style, crisp, high detail. No text, no
> words, no letters, no numbers, no logo, no label, no badge, no license plate,
> no decals, no watermark.

**Cutout:** ComfyUI essentials `RemBG Session` (`isnet-general-use`) and
`Image Remove Background`, saving the mask (`source/*_cutout_workflow.json`),
then:

```sh
convert render.png mask.png -alpha off -compose CopyOpacity -composite -trim +repage PNG32:full.png
convert full.png -channel A -fx "a<0.05?0:a" +channel -trim +repage PNG32:full.png
```

The results are `source/*_full.png`.

## Building the shipped files

```sh
python3 hmi/tools/art/energy_post.py RENDERDIR hmi/assets/energy/source hmi/assets/energy
```

scales everything to twice its size on screen, tints the van for each phase
(and to 80% for the dark theme, as the Climate van), cuts the lit-window
layer from the van's dark glass, turns the moon's unlit side see-through and
draws the sun.

## Where things are in the images

`EnergyApp.qml` places its effects in these fractions of each image (left,
right, top, bottom). Regenerating an image means measuring them again.

| Image | Region | Left | Right | Top | Bottom |
|---|---|---|---|---|---|
| `battery_light.png` (321 × 340) | Front panel, where the charge is drawn | 0.106 | 0.894 | 0.250 | 0.880 |
| `battery_dark.png` (303 × 340) | Front panel | 0.090 | 0.910 | 0.236 | 0.884 |
| `van_*.png` (480 × 253) | Living-area window | 0.394 | 0.944 | 0.398 | 0.606 |

In the sky card the horizon is about 82% of the way down.

## Sources

| File | What it is |
|---|---|
| `source/*_render.png` | The renders as ComfyUI made them |
| `source/*_mask.png` | The RemBG masks the cutouts used |
| `source/*_full.png` | The cutouts at full size, before scaling |
| `source/*_workflow.json`, `source/*_cutout_workflow.json` | The exact ComfyUI workflows |
