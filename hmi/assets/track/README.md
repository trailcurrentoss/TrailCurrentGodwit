# Track app artwork

Rendered locally, in light and dark versions: the trophy with ComfyUI and cut
out, the same way as the van on the Climate screen (`../vehicle/README.md`);
the drag-racing tree modelled and rendered in Blender from a script. The
masters each one was made from are in `source/`, which is not built into the
HMI.

| File | What it is |
|---|---|
| `trophy_light.png` | The best-lap trophy, cut out onto a transparent background |
| `tree_off_light.png`, `tree_off_dark.png` | The drag-racing tree, every lamp dark |
| `tree_on_light.png`, `tree_on_dark.png` | The same tree with every lamp lit. The 1/4 Mile screen shows each lit lamp by revealing its own patch of this image over the dark one |
| `glow_amber.png`, `glow_green.png`, `glow_red.png`, `glow_stage.png` | Soft round glows laid over a lit lamp |
| `source/trophy_render.png` | The trophy as ComfyUI rendered it, before the cutout |
| `source/trophy_mask.png` | The RemBG mask the cutout used |
| `source/trophy_light_full.png`, `source/trophy_dark_full.png` | The cutouts at full size, before scaling |
| `source/trophy_workflow.json`, `source/trophy_cutout_workflow.json` | The exact ComfyUI workflows for the render and the cutout |
| `trophy_dark.png` | The same trophy for the dark theme: the gold at 85% brightness, and the black plinth lifted to charcoal so it does not vanish into the dark card |

## Trophy

1. **Render:** Z-Image Turbo (`z_image_turbo_bf16`, ComfyUI template
   `image_z_image_turbo`), 1024×1024, 8 steps, seed 7301, first image of a
   batch of 2. Prompt:

   > A single small motorsport winner's trophy: a polished gold cup with two
   > slim curved handles on a short stem, standing on a stepped matte black
   > square plinth with a thin brushed aluminium band. Front three-quarter
   > view from slightly above, perfectly centered, upright, the whole trophy
   > in frame with generous margin. Premium minimalist 3D product render,
   > soft even studio lighting with gentle highlights on the gold, very soft
   > shadow directly beneath only, isolated on a seamless flat very light grey
   > background, Apple product photography style, crisp, high detail. No
   > text, no words, no letters, no numbers, no engraving, no logo, no
   > watermark.

2. **Cutout:** ComfyUI essentials `RemBG Session` (`isnet-general-use`, CUDA)
   and `Image Remove Background`, saving the mask, then:

   ```sh
   convert render.png mask.png -alpha off -compose CopyOpacity -composite -trim +repage PNG32:trophy_light.png
   ```

3. **Size:** both scaled to 240 px tall (Lanczos), sharp at twice the largest size the HMI shows.

4. **Dark copy:** every pixel's RGB × 0.85, except the plinth (the bottom
   34% of the image), which is mapped to 58 + 0.78 × value so black becomes
   charcoal and its shading stays.

## Drag-racing tree

Built and rendered by `hmi/tools/art/drag_tree_blender.py`, then cropped and
scaled by `hmi/tools/art/drag_tree_post.py`. The script is the tree's source:
it models the whole scene from nothing, so there is no `.blend` to keep.

```sh
blender -b -P "$PWD/hmi/tools/art/drag_tree_blender.py" -- /path/to/renders
python3 hmi/tools/art/drag_tree_post.py /path/to/renders hmi/assets/track
```

- **Model:** a single-lane tree, top to bottom: pre-stage and stage (pairs of
  small bulbs), three ambers, green, red. Black powder-coated housings, a hood
  over each lens, lenses as LED arrays behind tinted glass. Real proportions:
  8-inch-class main lenses.
- **Render:** Cycles, 192 samples, denoised, Standard view transform (Filmic
  washes the lit amber out to yellow), orthographic camera a little left of
  and above the tree so the hoods read, transparent film, 260×1000.
- **Light and dark:** the same render with the rim light at 220 W for light
  cards and 520 W for dark ones, so the black housing does not sink into a
  dark card.
- **Post:** the four renders share one crop ending just below the red lamp,
  the pole fades out over the bottom 70 px, and they are scaled to 512 px tall
  (Lanczos), twice the size the HMI shows.
- **Lamp positions:** `drag_tree_post.py` prints each lens's box as fractions
  of the image; they are the `lamps` table in `TrackApp.qml`. Re-run it and
  copy them across if the model changes.
