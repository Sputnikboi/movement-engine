#include "procgen.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <ctime>

extern float randf(float lo, float hi);

// ============================================================
//  Geometry helpers — build quads into a Mesh
// ============================================================

static void add_quad(Mesh& m, HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c, HMM_Vec3 d,
                     HMM_Vec3 normal, HMM_Vec3 color) {
    uint32_t base = (uint32_t)m.vertices.size();
    auto push = [&](HMM_Vec3 pos) {
        Vertex3D v;
        v.pos[0] = pos.X; v.pos[1] = pos.Y; v.pos[2] = pos.Z;
        v.normal[0] = normal.X; v.normal[1] = normal.Y; v.normal[2] = normal.Z;
        v.color[0] = color.X; v.color[1] = color.Y; v.color[2] = color.Z;
        m.vertices.push_back(v);
    };
    push(a); push(b); push(c); push(d);

    // Auto-correct winding: check if cross(b-a, c-a) matches the normal.
    // If not, reverse triangle winding so the face is visible from the normal side.
    HMM_Vec3 e1 = HMM_SubV3(b, a);
    HMM_Vec3 e2 = HMM_SubV3(c, a);
    HMM_Vec3 cross = HMM_Cross(e1, e2);

    if (HMM_DotV3(cross, normal) >= 0.0f) {
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 1);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 3);
    } else {
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 2);
        m.indices.push_back(base + 1);
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 3);
        m.indices.push_back(base + 2);
    }
}

// Rotate a point around Y axis at a given center
static HMM_Vec3 rotate_y(HMM_Vec3 p, float cx, float cz, float cy_cos, float sy_sin) {
    float lx = p.X - cx, lz = p.Z - cz;
    return {cx + lx * cy_cos + lz * sy_sin, p.Y, cz - lx * sy_sin + lz * cy_cos};
}
static HMM_Vec3 rotate_n(HMM_Vec3 n, float cy_cos, float sy_sin) {
    return {n.X * cy_cos + n.Z * sy_sin, n.Y, -n.X * sy_sin + n.Z * cy_cos};
}

// Add a box with random Y rotation. Bottom face at pos.Y.
static void add_box(Mesh& m, HMM_Vec3 pos, float w, float h, float d,
                    HMM_Vec3 color, float yaw = 0.0f) {
    float x0 = pos.X - w * 0.5f, x1 = pos.X + w * 0.5f;
    float y0 = pos.Y,            y1 = pos.Y + h;
    float z0 = pos.Z - d * 0.5f, z1 = pos.Z + d * 0.5f;
    float c = cosf(yaw), s = sinf(yaw);
    float cx = pos.X, cz = pos.Z;

    auto r = [&](HMM_Vec3 p) { return rotate_y(p, cx, cz, c, s); };
    auto rn = [&](HMM_Vec3 n) { return rotate_n(n, c, s); };

    // Top (+Y)
    add_quad(m, r({x0,y1,z0}), r({x1,y1,z0}), r({x1,y1,z1}), r({x0,y1,z1}),
             {0,1,0}, color);
    // Bottom (-Y)
    add_quad(m, r({x0,y0,z1}), r({x1,y0,z1}), r({x1,y0,z0}), r({x0,y0,z0}),
             {0,-1,0}, color);
    // Front (+Z)
    add_quad(m, r({x0,y0,z1}), r({x0,y1,z1}), r({x1,y1,z1}), r({x1,y0,z1}),
             rn({0,0,1}), color);
    // Back (-Z)
    add_quad(m, r({x1,y0,z0}), r({x1,y1,z0}), r({x0,y1,z0}), r({x0,y0,z0}),
             rn({0,0,-1}), color);
    // Right (+X)
    add_quad(m, r({x1,y0,z1}), r({x1,y1,z1}), r({x1,y1,z0}), r({x1,y0,z0}),
             rn({1,0,0}), color);
    // Left (-X)
    add_quad(m, r({x0,y0,z0}), r({x0,y1,z0}), r({x0,y1,z1}), r({x0,y0,z1}),
             rn({-1,0,0}), color);
}

// Add a ramp from 4 corner points (low0, low1 at bottom, high0, high1 at top).
// Only the top slope face — no underside/back wall/sides, which all fight
// the sphere collision and block the player from walking smoothly.
static void add_ramp_from_corners(Mesh& m,
                                  HMM_Vec3 low0, HMM_Vec3 low1,   // bottom edge (floor_y)
                                  HMM_Vec3 high0, HMM_Vec3 high1, // top edge (top_y)
                                  HMM_Vec3 color) {
    HMM_Vec3 edge1 = HMM_SubV3(high1, low1);
    HMM_Vec3 edge2 = HMM_SubV3(low0, low1);
    HMM_Vec3 normal = HMM_NormV3(HMM_Cross(edge1, edge2));
    if (normal.Y < 0) normal = HMM_MulV3F(normal, -1.0f);

    add_quad(m, low0, high0, high1, low1, normal, color);
}

// ============================================================
//  Overlap check for placed objects
// ============================================================

struct PlacedObj {
    float x, z, radius; // center + bounding circle radius
};

static bool overlaps(float x, float z, float r,
                     const PlacedObj* objs, int count, float margin) {
    for (int i = 0; i < count; i++) {
        const auto& b = objs[i];
        float dx = x - b.x, dz = z - b.z;
        float min_dist = r + b.radius + margin;
        if (dx * dx + dz * dz < min_dist * min_dist)
            return true;
    }
    return false;
}

// ============================================================
//  Generate level
// ============================================================

// Merge a mesh into another with a transform (position + Y rotation)
static void merge_mesh(Mesh& dst, const Mesh& src, HMM_Vec3 pos, float yaw) {
    float cy = cosf(yaw), sy = sinf(yaw);
    uint32_t base = (uint32_t)dst.vertices.size();
    for (const auto& sv : src.vertices) {
        Vertex3D v = sv;
        // Rotate around Y then translate
        float rx = sv.pos[0] * cy + sv.pos[2] * sy;
        float rz = -sv.pos[0] * sy + sv.pos[2] * cy;
        v.pos[0] = rx + pos.X;
        v.pos[1] = sv.pos[1] + pos.Y;
        v.pos[2] = rz + pos.Z;
        // Rotate normal too
        float nx = sv.normal[0] * cy + sv.normal[2] * sy;
        float nz = -sv.normal[0] * sy + sv.normal[2] * cy;
        v.normal[0] = nx;
        v.normal[2] = nz;
        dst.vertices.push_back(v);
    }
    for (auto idx : src.indices)
        dst.indices.push_back(base + idx);
}

LevelData generate_level(const ProcGenConfig& config,
                         const Mesh* door_mesh,
                         std::vector<DoorInfo>* doors_out) {
    if (config.seed != 0)
        srand(config.seed);
    else
        srand((unsigned)time(nullptr));

    LevelData ld;
    Mesh& m = ld.mesh;

    float rw = randf(config.room_width_min, config.room_width_max);
    float rd = randf(config.room_depth_min, config.room_depth_max);
    float rh = config.room_height;
    float wt = config.wall_thickness;
    float hw = rw * 0.5f, hd = rd * 0.5f;

    printf("ProcGen: room %.0f x %.0f x %.0f\n", rw, rd, rh);

    // --- Floor (normal +Y, facing up) ---
    add_quad(m, {-hw,0,-hd}, {-hw,0,hd}, {hw,0,hd}, {hw,0,-hd},
             {0,1,0}, config.floor_color);

    // --- Ceiling (normal -Y, facing down) ---
    add_quad(m, {-hw,rh,-hd}, {hw,rh,-hd}, {hw,rh,hd}, {-hw,rh,hd},
             {0,-1,0}, config.ceiling_color);

    // --- Door dimensions (sized to match Door.glb: ~1.2m wide x 2.1m tall) ---
    float door_w = 1.2f;   // width of door gap (tight to model)
    float door_h = 2.15f;  // height of door gap (tight to model)

    // Entry door on -Z wall, exit door on +Z wall (opposite sides)
    float entry_x = 0.0f;  // centered
    float exit_x  = 0.0f;

    // Build wall helper: a wall quad with a rectangular hole cut out.
    // Splits into 5 quads: left, right, top, bottom-left (unused — door at floor), above.
    auto wall_with_door = [&](float wx0, float wx1, float wy0, float wy1,
                              float wz, HMM_Vec3 normal,
                              float gap_center_x, float gap_w, float gap_h) {
        float gx0 = gap_center_x - gap_w * 0.5f;
        float gx1 = gap_center_x + gap_w * 0.5f;
        // Left of gap
        if (gx0 > wx0)
            add_quad(m, {wx0,wy0,wz}, {wx0,wy1,wz}, {gx0,wy1,wz}, {gx0,wy0,wz}, normal, config.wall_color);
        // Right of gap
        if (gx1 < wx1)
            add_quad(m, {gx1,wy0,wz}, {gx1,wy1,wz}, {wx1,wy1,wz}, {wx1,wy0,wz}, normal, config.wall_color);
        // Above gap
        add_quad(m, {gx0,gap_h,wz}, {gx0,wy1,wz}, {gx1,wy1,wz}, {gx1,gap_h,wz}, normal, config.wall_color);
    };

    // -Z wall with entry door gap
    wall_with_door(-hw, hw, 0, rh, -hd, {0,0,1}, entry_x, door_w, door_h);
    // +Z wall with exit door gap
    wall_with_door(-hw, hw, 0, rh, hd, {0,0,-1}, exit_x, door_w, door_h);
    // +X wall (solid)
    add_quad(m, {hw,0,hd}, {hw,rh,hd}, {hw,rh,-hd}, {hw,0,-hd},
             {-1,0,0}, config.wall_color);
    // -X wall (solid)
    add_quad(m, {-hw,0,-hd}, {-hw,rh,-hd}, {-hw,rh,hd}, {-hw,0,hd},
             {1,0,0}, config.wall_color);

    // --- Place door models ---
    // Scoot door models slightly into the wall so they sit flush
    float door_offset = 0.05f;
    DoorInfo entry_door, exit_door;
    entry_door.position = HMM_V3(entry_x, 0, -hd - door_offset);
    entry_door.yaw = 0;                    // facing +Z (into room)
    entry_door.is_exit = false;
    entry_door.locked = false;

    exit_door.position = HMM_V3(exit_x, 0, hd + door_offset);
    exit_door.yaw = 3.14159265f;            // facing -Z (into room)
    exit_door.is_exit = true;
    exit_door.locked = true;

    if (door_mesh) {
        merge_mesh(m, *door_mesh, entry_door.position, entry_door.yaw);
        merge_mesh(m, *door_mesh, exit_door.position, exit_door.yaw);
    }

    if (doors_out) {
        doors_out->clear();
        doors_out->push_back(entry_door);
        doors_out->push_back(exit_door);
    }

    // --- Track placed objects for overlap avoidance ---
    const int MAX_PLACED = 64;
    PlacedObj placed[MAX_PLACED];
    int placed_count = 0;

    // Reserve spawn area + door areas so nothing blocks them
    placed[placed_count++] = {entry_x, -hd + 2.0f, 2.5f}; // entry/spawn
    placed[placed_count++] = {exit_x,   hd - 2.0f, 2.5f}; // exit door

    // --- Platforms ---
    for (int i = 0; i < config.platform_count && placed_count < MAX_PLACED; i++) {
        float pw = randf(config.platform_size_min, config.platform_size_max);
        float pd = randf(config.platform_size_min, config.platform_size_max);
        float ph = randf(config.platform_height_min, config.platform_height_max);
        float yaw = randf(0, HMM_PI32 * 2.0f); // random rotation

        // Bounding radius: half-diagonal of platform + ramp width for the wrap-around
        float half_diag = sqrtf(pw * pw + pd * pd) * 0.5f;
        float total_radius = half_diag + config.ramp_width + 1.0f;

        // Try to place without overlap
        float px = 0, pz = 0;
        bool ok = false;
        float inset = total_radius + config.box_margin;
        for (int attempt = 0; attempt < 30; attempt++) {
            px = randf(-hw + inset, hw - inset);
            pz = randf(-hd + inset, hd - inset);
            if (!overlaps(px, pz, total_radius, placed, placed_count, 2.0f)) {
                ok = true;
                break;
            }
        }
        if (!ok) continue;

        add_box(m, {px, 0, pz}, pw, ph, pd, config.platform_color, yaw);
        placed[placed_count++] = {px, pz, total_radius};

        // Wrap-around ramp: 3 slope segments along 3 sides with
        // connecting landings at corners, and walls down to floor
        if (config.gen_ramps) {
            float rw = config.ramp_width;
            float phw = pw * 0.5f, phd = pd * 0.5f;
            float c = cosf(yaw), s = sinf(yaw);
            auto rot = [&](float lx, float lz, float ly) -> HMM_Vec3 {
                return {px + lx * c + lz * s, ly, pz - lx * s + lz * c};
            };
            auto rot_dir = [&](float lx, float lz) -> HMM_Vec3 {
                return {lx * c + lz * s, 0, -lx * s + lz * c};
            };

            struct Side {
                float sx, sz, ex, ez;
                float ox, oz;
            };
            Side sides[4] = {
                {-phw, -phd,  phw, -phd,  0, -1},
                { phw, -phd,  phw,  phd,  1,  0},
                { phw,  phd, -phw,  phd,  0,  1},
                {-phw,  phd, -phw, -phd, -1,  0},
            };

            int start_side = rand() % 4;
            int num_segs = 3;
            for (int seg = 0; seg < num_segs; seg++) {
                int si = (start_side + seg) % 4;
                const Side& sd = sides[si];
                float h_lo = ph * (float)seg / (float)num_segs;
                float h_hi = ph * (float)(seg + 1) / (float)num_segs;

                // --- Slope surface ---
                HMM_Vec3 low_out  = rot(sd.sx + sd.ox * rw, sd.sz + sd.oz * rw, h_lo);
                HMM_Vec3 low_in   = rot(sd.sx,              sd.sz,              h_lo);
                HMM_Vec3 high_out = rot(sd.ex + sd.ox * rw, sd.ez + sd.oz * rw, h_hi);
                HMM_Vec3 high_in  = rot(sd.ex,              sd.ez,              h_hi);
                add_ramp_from_corners(m, low_out, low_in, high_out, high_in, config.ramp_color);

                // --- Outer wall: from outer ramp edge down to floor ---
                HMM_Vec3 outward = rot_dir(sd.ox, sd.oz);
                HMM_Vec3 low_out_floor  = low_out;  low_out_floor.Y  = 0;
                HMM_Vec3 high_out_floor = high_out; high_out_floor.Y = 0;
                add_quad(m, low_out_floor, high_out_floor, high_out, low_out,
                         outward, config.ramp_color);

                // --- Start endcap wall (first segment only) ---
                if (seg == 0) {
                    HMM_Vec3 low_in_floor = low_in; low_in_floor.Y = 0;
                    // Normal faces toward the start direction
                    HMM_Vec3 along = HMM_NormV3(HMM_SubV3(low_in, high_in));
                    add_quad(m, low_out_floor, low_out, low_in, low_in_floor,
                             along, config.ramp_color);
                }

                // --- Connecting landing at corners ---
                if (seg < num_segs - 1) {
                    int ni = (start_side + seg + 1) % 4;
                    const Side& nd = sides[ni];
                    float ix = sd.ex, iz = sd.ez;
                    float o0x = sd.ex + sd.ox * rw, o0z = sd.ez + sd.oz * rw;
                    float o1x = nd.sx + nd.ox * rw, o1z = nd.sz + nd.oz * rw;
                    float odx = ix + sd.ox * rw + nd.ox * rw;
                    float odz = iz + sd.oz * rw + nd.oz * rw;

                    // Flat landing surface at h_hi
                    add_quad(m, rot(ix, iz, h_hi), rot(o0x, o0z, h_hi),
                             rot(odx, odz, h_hi), rot(o1x, o1z, h_hi),
                             {0, 1, 0}, config.ramp_color);

                    // Landing outer walls down to floor (two edges)
                    HMM_Vec3 outward0 = rot_dir(sd.ox, sd.oz);
                    add_quad(m, rot(o0x, o0z, 0), rot(odx, odz, 0),
                             rot(odx, odz, h_hi), rot(o0x, o0z, h_hi),
                             outward0, config.ramp_color);
                    HMM_Vec3 outward1 = rot_dir(nd.ox, nd.oz);
                    add_quad(m, rot(odx, odz, 0), rot(o1x, o1z, 0),
                             rot(o1x, o1z, h_hi), rot(odx, odz, h_hi),
                             outward1, config.ramp_color);
                }

                // --- End endcap wall (last segment only) ---
                if (seg == num_segs - 1) {
                    HMM_Vec3 high_in_floor = high_in; high_in_floor.Y = 0;
                    HMM_Vec3 along = HMM_NormV3(HMM_SubV3(high_in, low_in));
                    add_quad(m, high_out, high_out_floor, high_in_floor, high_in,
                             along, config.ramp_color);
                }
            }
        }
    }

    // --- Boxes / cover ---
    int box_count = (int)randf((float)config.box_count_min, (float)config.box_count_max + 0.99f);
    for (int i = 0; i < box_count && placed_count < MAX_PLACED; i++) {
        float bw = randf(config.box_size_min, config.box_size_max);
        float bd = randf(config.box_size_min, config.box_size_max);
        float bh = randf(config.box_height_min, config.box_height_max);
        float yaw = randf(0, HMM_PI32 * 2.0f);
        float br = sqrtf(bw * bw + bd * bd) * 0.5f; // bounding radius

        float bx = 0, bz = 0;
        bool ok = false;
        float inset = br + config.box_margin;
        for (int attempt = 0; attempt < 30; attempt++) {
            bx = randf(-hw + inset, hw - inset);
            bz = randf(-hd + inset, hd - inset);
            if (!overlaps(bx, bz, br, placed, placed_count, 1.5f)) {
                ok = true;
                break;
            }
        }
        if (!ok) continue;

        // Slight color variation per box
        HMM_Vec3 col = config.box_color;
        float var = randf(-0.05f, 0.05f);
        col.X += var; col.Y += var; col.Z += var;

        add_box(m, {bx, 0, bz}, bw, bh, bd, col, yaw);
        placed[placed_count++] = {bx, bz, br};
    }

    // --- Spawn point (near entry door, facing into room) ---
    ld.spawn_pos = HMM_V3(entry_x, 1.0f, -hd + 3.0f);
    ld.has_spawn = true;

    // --- Enemy spawns (avoid placed objects + min distance from player) ---
    float min_enemy_dist = 15.0f; // minimum distance from player spawn
    auto spawn_enemy = [&](EntityType type, float spawn_h) {
        for (int attempt = 0; attempt < 40; attempt++) {
            float ex = randf(-hw * 0.7f, hw * 0.7f);
            float ez = randf(-hd * 0.7f, hd * 0.7f);
            // Check not inside any placed object
            if (overlaps(ex, ez, 0.5f, placed, placed_count, 1.0f))
                continue;
            // Check minimum distance from player spawn
            float dx = ex - ld.spawn_pos.X, dz = ez - ld.spawn_pos.Z;
            if (dx * dx + dz * dz < min_enemy_dist * min_enemy_dist)
                continue;
            EnemySpawn es;
            es.position = HMM_V3(ex, spawn_h, ez);
            es.type = type;
            ld.enemy_spawns.push_back(es);
            return;
        }
        // Fallback: spawn anyway at a random position
        float ex = randf(-hw * 0.5f, hw * 0.5f);
        float ez = randf(-hd * 0.5f, hd * 0.5f);
        EnemySpawn es;
        es.position = HMM_V3(ex, spawn_h, ez);
        es.type = type;
        ld.enemy_spawns.push_back(es);
    };
    for (int i = 0; i < config.drone_count; i++)
        spawn_enemy(EntityType::Drone, config.enemy_height);
    for (int i = 0; i < config.rusher_count; i++)
        spawn_enemy(EntityType::Rusher, 1.0f); // rushers on the ground

    printf("ProcGen: %zu verts, %zu indices, %d boxes, %d platforms, %d enemies\n",
           m.vertices.size(), m.indices.size(), box_count, config.platform_count,
           config.drone_count + config.rusher_count);

    return ld;
}
