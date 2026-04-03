#!/usr/bin/env python3
"""Generate a test arena .glb file for the movement engine.
Run: python3 generate_test_arena.py
Creates: test_arena.glb
"""
import struct
import json
import numpy as np
import os

vertices = []  # list of (x,y,z, nx,ny,nz, r,g,b)
indices = []

def add_quad(p0, p1, p2, p3, color):
    """Add a quad (2 triangles). CCW winding."""
    e1 = np.array(p1) - np.array(p0)
    e2 = np.array(p3) - np.array(p0)
    n = np.cross(e1, e2)
    ln = np.linalg.norm(n)
    if ln > 1e-6:
        n = n / ln
    else:
        n = np.array([0,1,0])

    base = len(vertices)
    for p in [p0, p1, p2, p3]:
        vertices.append((*p, *n, *color))

    indices.extend([base, base+1, base+2, base, base+2, base+3])

def add_box(mn, mx, color):
    """Axis-aligned box from min corner to max corner."""
    x0,y0,z0 = mn
    x1,y1,z1 = mx
    # 8 corners
    v = [
        [x0,y0,z0],[x1,y0,z0],[x1,y1,z0],[x0,y1,z0],
        [x0,y0,z1],[x1,y0,z1],[x1,y1,z1],[x0,y1,z1],
    ]
    # 6 faces (CCW from outside)
    add_quad(v[4],v[5],v[6],v[7], color)  # +Z
    add_quad(v[1],v[0],v[3],v[2], color)  # -Z
    add_quad(v[5],v[1],v[2],v[6], color)  # +X
    add_quad(v[0],v[4],v[7],v[3], color)  # -X
    add_quad(v[3],v[7],v[6],v[2], color)  # +Y
    add_quad(v[4],v[0],v[1],v[5], color)  # -Y

def add_ramp(base_pos, width, length, height, color):
    """A ramp rising from base_pos along -Z."""
    x,y,z = base_pos
    # Bottom edge at (x, y, z), top edge at (x, y+height, z-length)
    p0 = [x,       y,        z]
    p1 = [x+width, y,        z]
    p2 = [x+width, y+height, z-length]
    p3 = [x,       y+height, z-length]
    add_quad(p0, p1, p2, p3, color)  # ramp surface

    # Left side
    add_quad([x,y,z], [x,y+height,z-length], [x,y,z-length], [x,y,z], color)
    # Right side
    add_quad([x+width,y,z-length], [x+width,y+height,z-length], [x+width,y,z], [x+width,y,z-length], color)
    # Back wall
    add_quad([x,y,z-length], [x+width,y,z-length], [x+width,y+height,z-length], [x,y+height,z-length], color)
    # Underside
    add_quad([x+width,y,z], [x,y,z], [x,y,z-length], [x+width,y,z-length], color)


# ===== BUILD THE ARENA =====

floor_dark  = (0.22, 0.22, 0.24)
floor_light = (0.30, 0.30, 0.32)
wall_color  = (0.38, 0.38, 0.42)
ramp_color  = (0.70, 0.45, 0.20)
plat_color  = (0.25, 0.40, 0.65)
box_color   = (0.55, 0.25, 0.25)
trim_color  = (0.80, 0.75, 0.20)
surf_color  = (0.30, 0.60, 0.50)

# --- Large floor (80x80 checkerboard) ---
tile = 5.0
half = 40.0
for gx in range(16):
    for gz in range(16):
        x0 = -half + gx * tile
        z0 = -half + gz * tile
        col = floor_dark if (gx+gz)%2==0 else floor_light
        add_quad([x0,0,z0+tile], [x0+tile,0,z0+tile], [x0+tile,0,z0], [x0,0,z0], col)

# --- Perimeter walls ---
wh = 10.0
wt = 0.5
add_box((-half, 0, -half-wt), (half, wh, -half), wall_color)
add_box((-half, 0, half), (half, wh, half+wt), wall_color)
add_box((-half-wt, 0, -half), (-half, wh, half), wall_color)
add_box((half, 0, -half), (half+wt, wh, half), wall_color)

# --- Central arena structure ---
# Raised platform in the middle
add_box((-6, 3, -6), (6, 3.5, 6), plat_color)
# 4 pillars supporting it
for sx in [-1, 1]:
    for sz in [-1, 1]:
        add_box((sx*5, 0, sz*5), (sx*5+1*sx, 3, sz*5+1*sz), plat_color)

# --- Ramps up to center platform (4 sides) ---
add_ramp((-3, 0, 12), 6, 6, 3, ramp_color)   # from +Z side
add_ramp((-3, 0, -6), 6, 6, 3, ramp_color)    # from -Z side (inverted)

# Side ramps (along X)
# Left ramp (build as rotated boxes since our ramp func goes along Z)
add_box((-12, 0, -3), (-6, 0.3, 3), ramp_color)  # flat approach
# Simple stepped ramp
add_box((-12, 0, -3), (-10, 1.0, 3), ramp_color)
add_box((-10, 0, -3), (-8,  2.0, 3), ramp_color)
add_box((-8,  0, -3), (-6,  3.0, 3), ramp_color)

# Right side steps
add_box((6,  0, -3), (8,  1.0, 3), ramp_color)
add_box((8,  0, -3), (10, 2.0, 3), ramp_color)
add_box((10, 0, -3), (12, 3.0, 3), ramp_color)

# --- Bhop course (south side) ---
for i in range(8):
    x = -15 + i * 4.0
    add_box((x, 0, -20), (x+2, 0.3, -18), trim_color)

# --- Surf ramp (angled wall to practice on) ---
# A 45-degree angled surface
sx, sz = 20, -10
sw, sl, sh = 10, 15, 8
# Left surf ramp face
p0 = [sx, 0, sz]
p1 = [sx, 0, sz+sl]
p2 = [sx+sw/2, sh, sz+sl]
p3 = [sx+sw/2, sh, sz]
add_quad(p0, p1, p2, p3, surf_color)
# Right surf ramp face
p0r = [sx+sw, 0, sz+sl]
p1r = [sx+sw, 0, sz]
p2r = [sx+sw/2, sh, sz]
p3r = [sx+sw/2, sh, sz+sl]
add_quad(p0r, p1r, p2r, p3r, surf_color)

# --- Scattered obstacles ---
add_box((-20, 0, 15), (-16, 1.5, 17), box_color)   # low wall
add_box((-25, 0, -5), (-22, 4, -2), box_color)      # tall block
add_box((15, 0, 20), (17, 2, 22), box_color)         # small block
add_box((20, 0, 20), (22, 5, 22), wall_color)        # pillar

# --- Elevated sniper perch ---
add_box((25, 6, -25), (35, 6.5, -20), plat_color)
# Stairs up to it
for i in range(6):
    add_box((25, i*1.1, -20+i*1.0), (28, (i+1)*1.1, -20+(i+1)*1.0), ramp_color)


# ===== EXPORT AS GLB =====

vert_data = np.array(vertices, dtype=np.float32)
idx_data = np.array(indices, dtype=np.uint32)

pos_data   = vert_data[:, 0:3].tobytes()
norm_data  = vert_data[:, 3:6].tobytes()
color_data = vert_data[:, 6:9].tobytes()
idx_bytes  = idx_data.tobytes()

# Compute bounds
pos_arr = vert_data[:, 0:3]
pos_min = pos_arr.min(axis=0).tolist()
pos_max = pos_arr.max(axis=0).tolist()

# Build binary buffer: indices | positions | normals | colors
# Pad each to 4-byte alignment
def pad4(data):
    r = len(data) % 4
    return data + b'\x00' * ((4-r) if r else 0)

buf = pad4(idx_bytes) + pad4(pos_data) + pad4(norm_data) + pad4(color_data)

idx_offset = 0
idx_length = len(idx_bytes)
pos_offset = len(pad4(idx_bytes))
pos_length = len(pos_data)
norm_offset = pos_offset + len(pad4(pos_data))
norm_length = len(norm_data)
col_offset = norm_offset + len(pad4(norm_data))
col_length = len(color_data)

num_verts = len(vertices)
num_indices = len(indices)

gltf = {
    "asset": {"version": "2.0", "generator": "movement-engine-levelgen"},
    "scene": 0,
    "scenes": [{"nodes": [0]}],
    "nodes": [{"mesh": 0, "name": "arena"}],
    "meshes": [{
        "primitives": [{
            "attributes": {
                "POSITION": 1,
                "NORMAL": 2,
                "COLOR_0": 3
            },
            "indices": 0,
            "mode": 4
        }]
    }],
    "accessors": [
        {"bufferView": 0, "componentType": 5125, "count": num_indices, "type": "SCALAR"},
        {"bufferView": 1, "componentType": 5126, "count": num_verts, "type": "VEC3",
         "min": pos_min, "max": pos_max},
        {"bufferView": 2, "componentType": 5126, "count": num_verts, "type": "VEC3"},
        {"bufferView": 3, "componentType": 5126, "count": num_verts, "type": "VEC3"},
    ],
    "bufferViews": [
        {"buffer": 0, "byteOffset": idx_offset,  "byteLength": idx_length,  "target": 34963},
        {"buffer": 0, "byteOffset": pos_offset,   "byteLength": pos_length,  "target": 34962},
        {"buffer": 0, "byteOffset": norm_offset,  "byteLength": norm_length, "target": 34962},
        {"buffer": 0, "byteOffset": col_offset,   "byteLength": col_length,  "target": 34962},
    ],
    "buffers": [{"byteLength": len(buf)}]
}

gltf_json = json.dumps(gltf, separators=(',',':')).encode('utf-8')
# Pad JSON to 4-byte alignment
while len(gltf_json) % 4 != 0:
    gltf_json += b' '

# GLB structure: header + JSON chunk + BIN chunk
glb_header = struct.pack('<4sII', b'glTF', 2, 12 + 8 + len(gltf_json) + 8 + len(buf))
json_chunk = struct.pack('<I4s', len(gltf_json), b'JSON') + gltf_json
bin_chunk  = struct.pack('<I4s', len(buf), b'BIN\x00') + buf

out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'test_arena.glb')
with open(out_path, 'wb') as f:
    f.write(glb_header + json_chunk + bin_chunk)

print(f"Generated {out_path}")
print(f"  {num_verts} vertices, {num_indices} indices, {num_indices//3} triangles")
print(f"  Buffer: {len(buf)} bytes")
