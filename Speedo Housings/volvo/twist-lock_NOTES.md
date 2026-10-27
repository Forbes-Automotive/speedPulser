# SpeedPulser – Volvo VDO twist-lock speedo housing (v0, from photo estimates)

Speedo: VDO (Volvo, round-gauge cluster). Cable spigot reads from the photos as:
- a pressed **boss** on the can,
- a die-cast **twist-lock collar** with two helical ramps / steps 180° apart,
- a **brass nose tube** protruding beyond the collar, with the square drive inside.

## What's in the zip

| File | |
|---|---|
| `SpeedPulser_Volvo_TwistLock_Housing.STL` | Full housing. Lower body (motor cavity, motor screws, coupler bore up to y 44) is FordSpeedoAdapter unchanged. Everything above y 46 is new. |
| `SpeedPulser_Volvo_TwistLock_FitRing.STL` | The top 15.5 mm only (collar socket + nose bore + grub holes). About 15 minutes to print. Test this first. |
| `SpeedPulser_Volvo_TwistLock_sections.png` | Sections with the estimated spigot overlaid |
| `scripts/build_volvo.py` | Parametric. Edit the four spigot numbers at the top, then re-run (~90 s). |

## Retention

- The housing slides over the collar and is held by **2 × M3 grub screws** into Ø2.5 pilots (self-tapping, 9 mm of wall).
- They sit between the motor-screw counterbores. Swap `GRUB_PILOT` to 4.0 for heat-set inserts.
- I didn't model bayonet lugs. The photos show the ramps but not whether there's an undercut, or which way the ramps run. Lugs that are slightly wrong won't engage at all, whereas a clamp works regardless.
- If the measurements show a usable undercut, internal lugs can be added later.

## Estimated dimensions (±15%)

Scale comes from the can (~85 mm, cross-checked against the M4 cheese-head screws). Measure these with calipers and replace them.

| Param | Est. | Measure |
|---|---|---|
| COLLAR_D | 21.0 | Collar OD (largest, across the ramps) |
| COLLAR_H | 11.5 | Boss shoulder → highest point of the ramps |
| TUBE_D | 12.5 | Brass nose OD |
| TUBE_L | 19.5 | Top of ramps → tip of brass nose |
| (boss) | ~23.5 OD | Boss OD. The housing face lands on its shoulder, so it must be bigger than the collar. If not, say so. |
| (clearance) | r ≥ 26 | Nearest bulb holder / bracket to the spigot axis, and how far each stands off the can. The neck is Ø40 for the first 22 mm. |

Also note the square drive size inside the nose (it decides the key between the coupler and the speedo).

## Layout

- Nose tip lands at **y 47**, about 0.7 mm above the top of the coupler (coupler unchanged: Ø12 × 14.3, 2.85 mm square socket).
- Collar top is at y 66.5. Housing face is at **y 78** (Ford body was 64), sitting on the boss shoulder.
- Neck is Ø40 from the face down to y 55.5, then flares to the standard Ø47 body. The Ford chamfer flat is carried through.
- The motor-screw counterbores (Ø8) run up through the neck and open into the collar socket as lobes (same as the MercW124 adapter). Motor screws still go in from the speedo end.

## Fit-ring test

1. Print the ring.
2. It should slide over the collar with slight play, and the brass nose should pass through the centre hole with ~0.4 mm a side.
3. Run the grubs in and check it can't pull off.
4. Then print the housing.
