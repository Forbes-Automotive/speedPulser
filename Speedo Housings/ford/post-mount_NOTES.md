# SpeedPulser – Ford housing, post-mount version (two-part)

Rowan's speedo has a Ø17 × 25 mm post and two OEM screws at 38 mm centres.
The screws land *inside* the existing 50 mm housing (on the Ø40 motor-cavity wall), so external wings aren't possible.
Instead, the base is split in two:

1. **Base plate** screws onto the speedo with 2 × M4 countersunk screws (heads recessed on the housing side).
2. **Housing + motor** (motor already fitted) then screws onto the plate with 3 × M3 from below the flange.

## Files

| File | What it is |
|---|---|
| `SpeedPulser_Ford_PostMount_Housing.STL` | FordSpeedoAdapter.STL with the bore opened up and 3 flange holes added |
| `SpeedPulser_Ford_PostMount_BasePlate.STL` | New 8 mm base plate (same 50 × 50 outline and chamfered corner as the housing face) |
| `SpeedPulser_Ford_PostMount_section.png` | Assembly section + plate plan view |
| `scripts/build.py` | Parametric rebuild (numpy + scipy only); edit the parameters at the top and re-run |

Both STLs share the original model's coordinates (housing axis = Y, face at y = 64; plate occupies y = 64–72), so they import into SolidWorks already aligned.

## Changes to the housing (from FordSpeedoAdapter.STL)

- Bore Ø15.94 → **Ø18.0** from the face (y 64) down to the existing step at **y 44**. Below the step, the Ø14.29 coupler bore and Ø15.57 motor-boss recess are unchanged.
- **3 × Ø3.4 (M3 clearance)** through the square flange (y 53–64), concentric with the R5 corner fillets at the three un-chamfered corners. Heads sit on the flange underside, outside the Ø47 body, so a driver reaches them straight down the outside.
- Side effects:
  - The Ø18 bore breaks about 0.5 mm into the three Ø8 motor-screw counterbores, leaving narrow slots between them. This is cosmetic, and the screw heads sit at y 38, below the bore floor.
  - Most of the old Ø2.6 cable-clip cross-holes (y ≈ 49.5) are cut away. Small partial grooves remain and do no harm.

## Base plate

- 8 mm thick, outline copied from the housing face.
- Ø18 through-bore for the post.
- 2 × M4 at **38 mm c-c** on the housing's X axis: Ø4.5 clearance, 90° countersink Ø9.6 on the housing side. An ISO 10642 head sits about 0.1 mm below flush.
- 3 × **Ø4.0 × 6 deep** for M3 heat-set inserts (from the housing side) under the flange holes.
- Screws: **M3 × 16** for housing to plate (11 mm flange + 5 mm into the insert). M4 CSK length = 5.5 mm grip + whatever thread depth the speedo has.

## Why 8 mm (coupler check)

The coupler (SpeedoAdapter.STL, Ø12 × 14.3, 6 mm D-socket on the motor side, 2.85 mm square on the speedo side) sits in the Ø14.29 bore above the motor face at y 30.
Its top ends up at y ≈ 44.3 (if it touches the motor face) to ≈ 46.3 (if it sits on the 2 mm motor boss).
The old cable spigot bottomed on the step at y 44.

- Without a plate, a 25 mm post would reach y 39 and hit the coupler by up to ~7 mm.
- With the 8 mm plate, the speedo face is at y 72 and the post face at **y 47**. That clears the coupler by 0.7–2.7 mm, about 1–3 mm further from the coupler than the old spigot sat.

If the drive needs to engage deeper, reduce `PLATE_T`. Each 1 mm off the plate brings the post 1 mm closer to the coupler.

## Assembly order

1. Fit the motor to the housing (motor screws through the face counterbores, as before) and push the coupler on.
2. Press the 3 M3 inserts into the plate.
3. Screw the plate onto the speedo with the 2 × M4 CSK screws.
4. Offer the housing over the post and fix it with 3 × M3 × 16 from underneath the flange.

The cable slot ends up roughly 40° off the screw line, toward the chamfered corner. To change that, rotate the screw line in the plate (it's a parameter); the corner screws fix the housing-to-plate orientation.

## Mesh notes

The STLs were built with a custom BSP CSG (no SolidWorks or trimesh available), followed by T-junction repair. Both meshes are closed (0 open edges), and the volumes match the expected removals.
The housing has a few zero-length sliver edges at the old clip-hole remnants. Slicers ignore them, but if SolidWorks complains on import, re-cut the Ø18 bore on FordSpeedoAdapter.SLDPRT instead.
