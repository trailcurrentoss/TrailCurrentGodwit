# Level screen art

The Level screen's pictures. The side view of the van is the Energy screen's
(`../energy/van_day_*.png`) and the top-down van is the Climate screen's
(`../vehicle/van_top_*.png`). Every image here comes in a light and a dark
version.

| Files | What they are | Made with |
|---|---|---|
| `van_rear_<theme>.png` | The van from behind, for the roll view | ComfyUI |
| `vial_<theme>.png` | A glass bull's-eye level seen from above: bezel, tinted spirit, printed rings, dome. No bubble | Blender |
| `bubble_<theme>.png` | The bubble, which QML moves over the vial | Blender |
| `board_side_<theme>.png`, `board_end_<theme>.png` | A 2x leveling board, from the side and end on | Blender |
| `block_side_<theme>.png`, `block_end_<theme>.png` | A square plastic leveling block, 8.5 × 8.5 × 1 in, ribbed on top | Blender |
| `ramp_side_<theme>.png`, `ramp_end_<theme>.png` | A curved leveling ramp that lifts a wheel as it is driven up, 18 in long and 4 in high | Blender |

## Vial, bubble, boards, blocks and ramps (Blender)

`hmi/tools/art/level_art_blender.py` builds every scene from nothing and
renders it on the CPU with Cycles:

```sh
blender -b -P hmi/tools/art/level_art_blender.py -- RENDERDIR [vial] [boards] [block] [ramp]
```

- **Vial:** an aluminium (light) or black (dark) bezel, a pale or deep green
  spirit under a glass dome, three printed rings and cross hairs, lit by two
  area lights, camera straight down.
- **Bubble:** a flattened, coated sphere rendered on its own.
- **Board:** a 16 in length of 2x lumber (1.5 × 7.25 in), wood from a Wave
  texture in rings mode stretched along the grain.
- **Block** and **ramp:** charcoal plastic. The ramp's top rises as
  `height = 4 in × sin(π/2 × x / 18 in)` from its low front edge, with treads
  across it. `Leveling::rampAt()` uses the same curve to say where along the
  ramp a wheel reaches a given raise, and the screen marks that spot.

## Van from behind (ComfyUI)

Z-Image Turbo (`z_image_turbo_bf16`, template `image_z_image_turbo`),
896 × 1152, 8 steps, batch of 4, seed 7101, fourth image. The exact workflows
are in `source/`. Prompt:

> Straight-on rear orthographic view of a single white long-wheelbase
> high-roof cargo van camper conversion, Sprinter-style, seen exactly from
> behind at eye level, perfectly centered and perfectly symmetric, the whole
> van in frame with generous margin. Two closed rear barn doors with dark
> tinted windows, black rear bumper, two black all-terrain tyres visible at the
> bottom, the dark edges of flat solar panels just visible on the low roof rack
> along the top of the roof. Premium minimalist 3D product render, soft even
> studio lighting, very soft shadow directly beneath only, isolated on a
> seamless flat very light grey background, Apple product photography style,
> crisp, high detail. No text, no words, no letters, no numbers, no logo, no
> label, no badge, no license plate, no decals, no watermark.

The model still lettered a small made-up badge on the left door. It was
covered with a patch of plain door from just above it
(`source/van_rear_retouched.png`):

```sh
convert render.png \( +clone -crop 64x22+262+650 +repage -blur 0x0.6 \) -geometry +262+672 -composite retouched.png
```

**Cutout:** ComfyUI essentials `RemBG Session` (`isnet-general-use`) and
`Image Remove Background`, saving the mask, then combined and trimmed as for
the Climate van (`../vehicle/README.md`), giving `source/van_rear_full.png`.
RemBG leaves the glass and pale paint partly see-through, so
`level_post.py` fills the body back in and keeps only the soft outer edge.

## Building the shipped files

```sh
python3 hmi/tools/art/level_post.py RENDERDIR hmi/assets/level/source hmi/assets/level
```

scales the vial, bubble and van to twice their size on screen, makes the dark
van at 80% brightness, tones down the dark vial's bezel, and trims the
boards, blocks and ramps. The dark block and ramp are the light renders at
85%: charcoal plastic disappears against the dark theme's ground.

## Where things are in the images

Regenerating an image means measuring these again.

| Image | What | Where |
|---|---|---|
| `van_rear_*.png` (220 × 280) | Tyres touch the ground | x 0.12 and 0.89, y 1.0 |
| `../energy/van_day_*.png` (480 × 253) | Tyres touch the ground | x 0.15 (front) and 0.79 (rear), y 0.985 |
| `../vehicle/van_top_*.png` (412 × 1103) | Axles | y 0.165 (front) and 0.79 (rear) |
| `vial_*.png` (300 × 300) | The outer ring the bubble reaches | about 3.5° from the centre at 18 px a degree on screen |

## Sources

| File | What it is |
|---|---|
| `source/van_rear_render.png` | The render as ComfyUI made it |
| `source/van_rear_retouched.png` | The same with the badge painted out |
| `source/van_rear_mask.png` | The RemBG mask |
| `source/van_rear_full.png` | The cutout at full size, before scaling |
| `source/van_rear_workflow.json`, `source/van_rear_cutout_workflow.json` | The exact ComfyUI workflows |
