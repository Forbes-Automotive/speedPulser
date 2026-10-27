"""
Build SpeedoAdapterScrewLock.STL — a screw-lock-style speedo adapter sleeve.

Based on the hand sketch on Nesbitt Engineering notepad:
    - Spigot OD:                  9.6 mm   (cable's screw-lock collar threads onto this; v1 unthreaded)
    - Total spigot length:        23  mm
    - Thread shoulder height:     16.63 mm (marker for where future thread ends; OD is constant for v1)
    - Internal square drive bore: 8 mm   (through the whole length; the spinning tang turns in it)

Note: with OD 9.6 and a square bore of 8 mm, wall thickness is (9.6-8)/2 = 0.8 mm
(thin but printable; bump BASE_OD slightly if you want thicker walls).

Geometry axis: Y, from y=0 (base) up to y=23 (top).
"""
import numpy as np
import trimesh

# --- Parameters (edit and re-run) ---
BASE_OD            = 9.6
TOTAL_LEN          = 23.0
SHOULDER_Y         = 16.63   # informational; used for thread-end marker in future revisions
SQUARE_SIZE        = 8.0
ADD_BASE_FLANGE_OD = None    # e.g. 12.0 to add a base flange for printability; None = none
BASE_FLANGE_H      = 1.5

OUT = r"E:\My Drive\SolidWorks\SpeedPulser\SpeedoAdapterScrewLock.STL"

def cyl(d, h, y0):
    c = trimesh.creation.cylinder(radius=d / 2.0, height=h, sections=96)
    R = trimesh.transformations.rotation_matrix(np.pi / 2.0, [1, 0, 0])
    c.apply_transform(R)
    c.apply_translation([0, y0 + h / 2.0, 0])
    return c

def sq_bore(side, h, y0):
    b = trimesh.creation.box(extents=[side, h, side])
    b.apply_translation([0, y0 + h / 2.0, 0])
    return b

# Main spigot: single Ø9.6 cylinder, 23 mm long
body = cyl(BASE_OD, TOTAL_LEN, 0.0)

if ADD_BASE_FLANGE_OD:
    flange = cyl(ADD_BASE_FLANGE_OD, BASE_FLANGE_H, 0.0)
    body = trimesh.boolean.union([body, flange], engine='manifold')

# Subtract the 8 mm square drive bore (with overshoot for clean cut)
bore = sq_bore(SQUARE_SIZE, TOTAL_LEN + 2.0, -1.0)
result = trimesh.boolean.difference([body, bore], engine='manifold')

result.export(OUT)

print(f"Saved: {OUT}")
print(f"  bounds   : {result.bounds.round(3).tolist()}")
print(f"  extents  : {result.extents.round(3).tolist()}")
print(f"  watertight: {result.is_watertight}")
print(f"  volume   : {result.volume:.2f} mm^3")
print(f"  wall thk : {(BASE_OD - SQUARE_SIZE)/2:.2f} mm (min, at square's flat face)")
