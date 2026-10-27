"""
Adapt MK1GolfSpeedoAdapter.STL to the screw-lock speedo's locking pin (per the
hand sketch on Nesbitt Engineering pad).

LOCKING-PIN GEOMETRY (the speedo spigot the adapter slides over):
    - Cylindrical sleeve   : Ø 9.6 mm
    - Sleeve length        : 16.63 mm (from speedo face to shoulder)
    - Square drive tang    : 8 mm square, projecting from the shoulder
    - Total spigot length  : 23 mm  (so square length = 23 - 16.63 = 6.37 mm)
    - Axial locking slot   : vertical notch cut in the side of the sleeve
                             (visible in close-up photo) — adapter needs a
                             matching INWARD tab in the round bore so the
                             adapter can't rotate on the spigot.

ADAPTER INNER-BORE TARGET (with running-clearance fits):
    - First 16.63 mm: Ø 9.8 mm round + inward locking tab
    - Next  6.37 mm : 8.2 mm square
    - Total stepped-bore depth: 23 mm

PART AXES (per MK1GolfSpeedoAdapter.STL):
    - Bore axis = Y
    - Speedo-facing disc face at Y = 30 (after the C-clip)
    - Existing central bore = Ø 14.3 mm through-hole

STRATEGY:
    1. Fill the existing Ø14.3 mm bore over the first 23 mm (y=30..53).
    2. Cut the new stepped bore from the speedo-facing face into that solid.
    3. Add the locking tab back into the round portion of the bore.
    4. Leave the rest of the existing geometry untouched.
"""
import numpy as np
import trimesh

SRC  = r"E:\My Drive\SolidWorks\SpeedPulser\MK1GolfSpeedoAdapter.STL"
DST  = r"E:\My Drive\SolidWorks\SpeedPulser\MK1GolfSpeedoAdapterScrewLock.STL"

# --- locking-pin / bore parameters (mm) ---
SLEEVE_OD            = 9.6
SLEEVE_LEN           = 16.63
SQUARE_SIZE          = 8.0
TOTAL_LEN            = 23.0
SQUARE_LEN           = TOTAL_LEN - SLEEVE_LEN          # 6.37
SLEEVE_CLEAR         = 0.2          # diametral clearance on round
SQUARE_CLEAR         = 0.2          # side clearance on square

BORE_ROUND_D         = SLEEVE_OD  + SLEEVE_CLEAR        # 9.8
BORE_SQUARE_SIDE     = SQUARE_SIZE + SQUARE_CLEAR       # 8.2

# --- locking tab (the feature you flagged) ---
# Estimated from photo of the slot in the speedo's sleeve.
# MEASURE THE REAL SLOT AND TUNE THESE FOUR NUMBERS.
TAB_RADIAL_HEIGHT    = 1.0     # how far the tab sticks into the bore (mm)
TAB_WIDTH            = 2.5     # tab width tangentially (mm) — leave 0.2 clearance vs slot width
TAB_AXIAL_LEN        = 14.0    # tab length along Y (< SLEEVE_LEN so it doesn't bottom out)
TAB_AXIAL_OFFSET     = 1.0     # gap between speedo face and start of tab (mm)
TAB_ANGLE_DEG        = 0.0     # 0 = tab points in +Z (toward "top"); rotate to align with slot

# --- part-specific landmarks (verified from cross-sections of MK1 STL) ---
AXIS_X, AXIS_Z       = 25.0, 25.0   # bore axis in XZ
SPEEDO_FACE_Y        = 30.0         # disc face
EXISTING_BORE_D      = 14.3
OVERSHOOT            = 0.5

def cyl(d, h, y0, cx=AXIS_X, cz=AXIS_Z):
    c = trimesh.creation.cylinder(radius=d/2.0, height=h, sections=128)
    R = trimesh.transformations.rotation_matrix(np.pi/2.0, [1, 0, 0])
    c.apply_transform(R)
    c.apply_translation([cx, y0 + h/2.0, cz])
    return c

def sq(side, h, y0, cx=AXIS_X, cz=AXIS_Z):
    b = trimesh.creation.box(extents=[side, h, side])
    b.apply_translation([cx, y0 + h/2.0, cz])
    return b

def tab():
    """Rectangular tab projecting radially inward from the round-bore wall.
    Built at angle 0 (pointing +Z) then rotated about Y to TAB_ANGLE_DEG."""
    # Centre of tab in radial direction so its outer face sits at bore wall and
    # it protrudes by TAB_RADIAL_HEIGHT toward the axis.
    r_outer = BORE_ROUND_D / 2.0
    r_inner = r_outer - TAB_RADIAL_HEIGHT
    radial_thickness = r_outer - r_inner    # = TAB_RADIAL_HEIGHT
    # Build the tab as a small box, centred at (cx, y_mid, cz + r_mid_z)
    b = trimesh.creation.box(extents=[TAB_WIDTH, TAB_AXIAL_LEN, radial_thickness])
    y_mid = SPEEDO_FACE_Y + TAB_AXIAL_OFFSET + TAB_AXIAL_LEN / 2.0
    # Position so its outer edge sits at +Z = r_outer (touching bore wall) and
    # inner edge at +Z = r_inner.
    z_offset = (r_outer + r_inner) / 2.0
    b.apply_translation([AXIS_X, y_mid, AXIS_Z + z_offset])
    # Rotate about Y-axis through bore centre to set angular position
    R = trimesh.transformations.rotation_matrix(np.deg2rad(TAB_ANGLE_DEG),
                                                [0, 1, 0],
                                                point=[AXIS_X, 0, AXIS_Z])
    b.apply_transform(R)
    return b

print("Loading...")
mesh = trimesh.load(SRC)
print(f"  in  bounds={mesh.bounds.round(2).tolist()}  watertight={mesh.is_watertight}")

# 1) Plug the existing Ø14.3 bore over the new stepped-bore depth
plug = cyl(EXISTING_BORE_D, TOTAL_LEN + OVERSHOOT, SPEEDO_FACE_Y - OVERSHOOT/2)
filled = trimesh.boolean.union([mesh, plug], engine='manifold')
print(f"  after plug:   watertight={filled.is_watertight}")

# 2) Cut new stepped bore
new_bore_round  = cyl(BORE_ROUND_D, SLEEVE_LEN + OVERSHOOT, SPEEDO_FACE_Y - OVERSHOOT)
new_bore_square = sq (BORE_SQUARE_SIDE, SQUARE_LEN + OVERSHOOT,
                      SPEEDO_FACE_Y + SLEEVE_LEN - OVERSHOOT/2)
new_bore = trimesh.boolean.union([new_bore_round, new_bore_square], engine='manifold')
bored = trimesh.boolean.difference([filled, new_bore], engine='manifold')
print(f"  after bore:   watertight={bored.is_watertight}")

# 3) Add locking tab back into the round-bore region
result = trimesh.boolean.union([bored, tab()], engine='manifold')
print(f"  after tab :   watertight={result.is_watertight}  vol={result.volume:.0f}")

result.export(DST)
print(f"\nSaved: {DST}")
print(f"  bounds  : {result.bounds.round(2).tolist()}")
print(f"  extents : {result.extents.round(2).tolist()}")
