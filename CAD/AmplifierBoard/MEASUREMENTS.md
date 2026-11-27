# Amplifier board reference model

`AmplifierBoard.FCStd` is a reference model of the 4-channel class-D amplifier
board, built to fit the Godwit enclosure around it. `build_amplifier_board.py`
generates it, and `CAD/exports/Godwit_AmplifierBoard.step` is the export. It
models envelopes and positions, not electrical detail. The amplifier ICs under
the heatsink, the heatsink screws and the small SMD parts are left out.

All dimensions live in the `Dims` spreadsheet. Change a cell, recompute, and
the model follows. To regenerate from scratch, run `build_all()` from the
script inside FreeCAD.

## Coordinates

- Origin at the bottom-left corner of the PCB underside. The PCB top is at
  Z = 1.6 mm.
- X runs across the 86 mm width. The 8-way speaker terminal block is on the
  X = 0 edge. The 3-way power block, the inputs and the DIP switch are on the
  X = 86 edge.
- Y runs along the 128.6 mm length. The toroid end is Y = 0 and the input end
  is Y = 128.6.

## How it was measured

The board was filmed in 1080p slo-mo on an iPhone 16e, which uses the 26 mm
equivalent main camera, with a steel rule beside it. The video was split into
frames, and each component was measured by triangulating between views:

1. **Scale.** The rule's millimetre ticks were measured by frequency analysis
   of the tick pattern, in four top-down frames taken at 170–340 mm.
2. **Board plane.** The four mounting-hole centres give a homography for each
   frame. That corrects the phone's tilt (about 4° on the top-down shots) and
   puts every frame into board coordinates. Projected into the board plane, the
   rule reads 0.8–1.1 % short in every frame, which fits the board top sitting
   about 2 mm above the rule face. That confirms the 80 × 122.6 mm hole
   spacing.
3. **Focal length.** About 1600 px. The vanishing points of the square floor
   tiles in eight oblique frames give 1476–1695 px (mean 1582). That is
   consistent with the 26 mm equivalent lens plus a stabilisation crop.
4. **Heights.** Each feature was read in rectified far, near and oblique views.
   A raised point lands at a different board-plane position in each view. With
   the camera positions recovered from the holes, a least-squares solve gives
   its true X, Y and height. Residuals were 0.1–0.7 mm.

The lens's own distortion is corrected in the phone's video pipeline. What
remained was perspective and tilt, which the per-frame homographies remove.

## Values and confidence

| Feature | Value | Confidence |
|---|---|---|
| PCB | 86.0 × 128.6 × 1.6 mm | ±0.3 mm (thickness assumed) |
| Mounting holes | Ø3.2, 3.0 mm from each edge (80 × 122.6 centres) | ±0.3 mm |
| Heatsink footprint | X 15.3–63.9, Y 15.3–113.0 | ±0.5 mm |
| Heatsink top | 26.6 mm above PCB top (28.2 mm above origin) | ±1.5 mm |
| Bulk caps (8 × 2200 µF 35 V) | Ø12.5, rows at Y 7.0 / 120.9, pitch 13.2 from X 19.7 | XY ±0.5 mm |
| Bulk cap tops | 25.5 mm above PCB top | ±2 mm (the two rows measured 23.4 and 27.5) |
| Small electrolytics | column at X 67.6, tops 18.4 mm (8 off) and 23.5 mm (2 off) | ±1.5 mm |
| Barrier terminal blocks | 7.62 mm pitch (measured 7.70), centred at Y 64.7 | pitch ±0.1 mm |
| Terminal screw heads | 8 mm above PCB top | ±1 mm |
| Terminal fence tops | 13 mm above PCB top | **estimate**, not measured |
| Toroid | Ø24.7 × 9.3 mm on edge, axis along X, centre Y 20.45 | ±1 mm |
| JST XH inputs | B3B-XH-A datasheet body 5.75 × 9.9 × 7.0 at X 83.1, Y 86.45 / 114.5 | position ±0.5 mm |
| DIP switch | 9.9 × 6.5 × 4.5 at X 80.1, Y 100.5 | ±0.5 mm |
| Diodes (DO-201AD) | Ø5.3 × 9.5 lying along Y at X 77.1 / 82.3 | ±0.5 mm |
| Lead tails under PCB | about 1.5 mm, not modelled | estimate |

The 3-way power block overhangs the X = 86 edge by about 0.3 mm. The overall
envelope is 86.3 × 128.6 mm, and 28.2 mm tall from the PCB underside.

## Check with calipers before cutting material

- Heatsink top height and the bulk capacitor height. These set the lid
  clearance, and the heights carry the focal-length uncertainty.
- Terminal block fence height and the wire-entry openings, so screwdrivers and
  wires can reach the terminals.
- PCB thickness and lead-tail length, which set the standoff height.
