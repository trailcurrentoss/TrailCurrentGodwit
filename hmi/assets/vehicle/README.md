# Vehicle renders

Top-down view of a long-wheelbase, high-roof camper van with the roof
removed, used on the Climate screen. Generated locally with ComfyUI.

| File | What it is |
|---|---|
| `van_top_light.png` | The render, cut out onto a transparent background |
| `van_top_dark.png` | The same image at 80% brightness, for the dark theme |

## How they were made

1. **Render:** Z-Image Turbo (`z_image_turbo_bf16`, ComfyUI template
   `image_z_image_turbo`), 768×1344, 8 steps, seed 4401, first image of a
   batch of 2. Prompt:

   > Top-down orthographic view looking straight down onto a white
   > long-wheelbase high-roof cargo van camper conversion, Sprinter-style,
   > with the roof removed to reveal the whole interior: driver and passenger
   > cab seats side by side at the front behind the dashboard and steering
   > wheel, then a kitchen galley counter with a sink and cooktop along one
   > side wall, a small bench seat opposite, light oak cabinetry and floor,
   > and a transverse bed platform with neat white bedding across the rear.
   > The van points straight up the frame, front at the top, perfectly
   > centered and symmetric, long rectangular body. Premium minimalist 3D
   > product render, soft even overhead studio lighting, very soft shadow
   > directly beneath only, isolated on a seamless flat very light grey
   > background, Apple product photography style, crisp, high detail. No
   > text, no words, no letters, no logo, no badge, no license plate, no
   > watermark.

2. **Cutout:** ComfyUI essentials `RemBG Session` (`isnet-general-use`) and
   `Image Remove Background`, saving the mask. The render and mask were then
   combined and trimmed:

   ```sh
   convert render.png mask.png -alpha off -compose CopyOpacity -composite -trim +repage PNG32:van_top_light.png
   ```

3. **Dark copy:**

   ```sh
   convert van_top_light.png -channel RGB -evaluate multiply 0.8 +channel PNG32:van_top_dark.png
   ```

## Where things are in the image

`ClimateApp.qml` places its effects in the image's own pixel coordinates
(412 × 1103): cab seat centres at x 132 and 278, y 352; dash vents along
y 232; footwells at y 285; windshield from y 150 to 206. Regenerating the
render means measuring these again.
