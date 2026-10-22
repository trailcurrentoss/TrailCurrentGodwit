# Water tank renders

A translucent RV holding tank, seen straight on, used for each tank on the
Water screen. The level is drawn over it in QML, so one image serves every
tank and every level. Generated locally with ComfyUI.

| File | What it is |
|---|---|
| `water_tank_light.png` | Frosted white tank, for the light theme |
| `water_tank_dark.png` | Smoked graphite tank, for the dark theme |

## How they were made

1. **Render:** Z-Image Turbo (`z_image_turbo_bf16`, ComfyUI template
   `image_z_image_turbo`), 896×1152, 8 steps, batch of 4, fourth image of
   each. Light: seed 5101. Prompt:

   > Straight-on front orthographic view of a single empty rectangular RV
   > water holding tank made of translucent frosted milky white polyethylene,
   > softly rounded corners and edges, taller than wide, a small round
   > threaded fill cap on top, subtle molded horizontal ribs across the front
   > face, glossy edge highlights, the tank is perfectly centered and fills
   > most of the frame, premium minimalist 3D product render, soft even
   > studio lighting, very soft shadow beneath, isolated on a seamless flat
   > very light grey background, Apple product photography style, crisp,
   > high detail. No text, no words, no letters, no numbers, no logo, no
   > label, no watermark.

   Dark: seed 5201, the same prompt with "translucent smoked dark graphite
   polyethylene", "thin crisp white rim light along the edges", "dramatic
   low-key studio lighting" and "a seamless flat pure black background".

2. **Cutout:** ComfyUI essentials `RemBG Session` (`isnet-general-use`) and
   `Image Remove Background`, saving the mask, combined as for the vehicle
   renders (`../vehicle/README.md`). Each was then cropped to its solid
   outline (alpha above 50%) plus 6 px, scaled to 560 px tall, and alpha
   below 12 cleared.

## Where the tank body is

`WaterApp.qml` fills the body between these fractions of the image (left,
right, top, bottom), below the cap:

| Image | Left | Right | Top | Bottom |
|---|---|---|---|---|
| light (347 × 560) | 0.020 | 0.974 | 0.064 | 0.955 |
| dark (373 × 560) | 0.021 | 0.976 | 0.066 | 0.970 |

Regenerating a render means measuring these again.
