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

// Add a box (6 faces) at position with given size. Bottom face at pos.Y.
static void add_box(Mesh& m, HMM_Vec3 pos, float w, float h, float d, HMM_Vec3 color) {
    float x0 = pos.X - w * 0.5f, x1 = pos.X + w * 0.5f;
    float y0 = pos.Y,            y1 = pos.Y + h;
    float z0 = pos.Z - d * 0.5f, z1 = pos.Z + d * 0.5f;

    // Top (+Y)
    add_quad(m, {x0,y1,z0}, {x1,y1,z0}, {x1,y1,z1}, {x0,y1,z1},
             {0,1,0}, color);
    // Bottom (-Y)
    add_quad(m, {x0,y0,z1}, {x1,y0,z1}, {x1,y0,z0}, {x0,y0,z0},
             {0,-1,0}, color);
    // Front (+Z)
    add_quad(m, {x0,y0,z1}, {x0,y1,z1}, {x1,y1,z1}, {x1,y0,z1},
             {0,0,1}, color);
    // Back (-Z)
    add_quad(m, {x1,y0,z0}, {x1,y1,z0}, {x0,y1,z0}, {x0,y0,z0},
             {0,0,-1}, color);
    // Right (+X)
    add_quad(m, {x1,y0,z1}, {x1,y1,z1}, {x1,y1,z0}, {x1,y0,z0},
             {1,0,0}, color);
    // Left (-X)
    add_quad(m, {x0,y0,z0}, {x0,y1,z0}, {x0,y1,z1}, {x0,y0,z1},
             {-1,0,0}, color);
}

// Add a ramp from 4 corner points (low0, low1 at bottom, high0, high1 at top).
// Builds top slope face, underside, two side triangles, and a back wall.
static void add_ramp_from_corners(Mesh& m,
                                  HMM_Vec3 low0, HMM_Vec3 low1,   // bottom edge (floor_y)
                                  HMM_Vec3 high0, HMM_Vec3 high1, // top edge (top_y)
                                  HMM_Vec3 color) {
    // Slope surface normal — cross two edges and ensure it points upward
    HMM_Vec3 edge1 = HMM_SubV3(high1, low1);
    HMM_Vec3 edge2 = HMM_SubV3(low0, low1);
    HMM_Vec3 normal = HMM_NormV3(HMM_Cross(edge1, edge2));
    if (normal.Y < 0) normal = HMM_MulV3F(normal, -1.0f);

    // Top slope face
    add_quad(m, low0, high0, high1, low1, normal, color);
    // Underside
    add_quad(m, low0, low1, high1, high0, HMM_MulV3F(normal, -1.0f), color);

    // Back wall (vertical face at the high end)
    HMM_Vec3 high0_floor = high0; high0_floor.Y = low0.Y;
    HMM_Vec3 high1_floor = high1; high1_floor.Y = low1.Y;
    // Normal points away from the ramp slope
    HMM_Vec3 back_dir = HMM_SubV3(high0, low0);
    back_dir.Y = 0;
    HMM_Vec3 back_normal = HMM_NormV3(back_dir);
    add_quad(m, high1_floor, high1, high0, high0_floor, back_normal, color);

    // Side triangles
    auto add_tri = [&](HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c, HMM_Vec3 n) {
        uint32_t base = (uint32_t)m.vertices.size();
        auto push = [&](HMM_Vec3 pos) {
            Vertex3D v;
            v.pos[0] = pos.X; v.pos[1] = pos.Y; v.pos[2] = pos.Z;
            v.normal[0] = n.X; v.normal[1] = n.Y; v.normal[2] = n.Z;
            v.color[0] = color.X; v.color[1] = color.Y; v.color[2] = color.Z;
            m.vertices.push_back(v);
        };
        push(a); push(b); push(c);
        // Auto-correct winding
        HMM_Vec3 e1 = HMM_SubV3(b, a);
        HMM_Vec3 e2 = HMM_SubV3(c, a);
        HMM_Vec3 cr = HMM_Cross(e1, e2);
        if (HMM_DotV3(cr, n) >= 0.0f) {
            m.indices.push_back(base + 0);
            m.indices.push_back(base + 1);
            m.indices.push_back(base + 2);
        } else {
            m.indices.push_back(base + 0);
            m.indices.push_back(base + 2);
            m.indices.push_back(base + 1);
        }
    };

    // Left side: low0, high0_floor, high0 (side normal perpendicular to ramp direction)
    HMM_Vec3 ramp_dir = HMM_SubV3(high0, low0);
    HMM_Vec3 left_normal = HMM_NormV3(HMM_Cross(ramp_dir, {0,1,0}));
    // Make sure left_normal points toward the low0 side
    HMM_Vec3 center_to_low0 = HMM_SubV3(low0, HMM_MulV3F(HMM_AddV3(low0, low1), 0.5f));
    if (HMM_DotV3(left_normal, center_to_low0) < 0) left_normal = HMM_MulV3F(left_normal, -1.0f);

    add_tri(low0, high0_floor, high0, left_normal);

    // Right side
    HMM_Vec3 right_normal = HMM_MulV3F(left_normal, -1.0f);
    add_tri(low1, high1_floor, high1, right_normal);
}

// ============================================================
//  Overlap check for placed objects
// ============================================================

struct PlacedBox {
    float x, z, w, d;
};

static bool overlaps(float x, float z, float w, float d,
                     const PlacedBox* boxes, int count, float margin) {
    for (int i = 0; i < count; i++) {
        const auto& b = boxes[i];
        if (fabsf(x - b.x) < (w + b.w) * 0.5f + margin &&
            fabsf(z - b.z) < (d + b.d) * 0.5f + margin)
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
    PlacedBox placed[MAX_PLACED];
    int placed_count = 0;

    // Reserve spawn area + door areas so nothing blocks them
    placed[placed_count++] = {entry_x, -hd + 2.0f, 3.0f, 4.0f}; // entry/spawn
    placed[placed_count++] = {exit_x,   hd - 2.0f, 3.0f, 4.0f}; // exit door

    // --- Platforms ---
    for (int i = 0; i < config.platform_count && placed_count < MAX_PLACED; i++) {
        float pw = randf(config.platform_size_min, config.platform_size_max);
        float pd = randf(config.platform_size_min, config.platform_size_max);
        float ph = randf(config.platform_height_min, config.platform_height_max);

        // Try to place without overlap
        float px = 0, pz = 0;
        bool ok = false;
        for (int attempt = 0; attempt < 30; attempt++) {
            px = randf(-hw + config.box_margin + pw * 0.5f, hw - config.box_margin - pw * 0.5f);
            pz = randf(-hd + config.box_margin + pd * 0.5f, hd - config.box_margin - pd * 0.5f);
            if (!overlaps(px, pz, pw, pd, placed, placed_count, 3.0f)) {
                ok = true;
                break;
            }
        }
        if (!ok) continue;

        add_box(m, {px, 0, pz}, pw, ph, pd, config.platform_color);
        placed[placed_count++] = {px, pz, pw, pd};

        // Ramp up to this platform
        if (config.gen_ramps) {
            int side = rand() % 4;
            float ramp_len = ph * 4.0f; // gentle slope (~14°)
            float rw2 = config.ramp_width * 0.5f;
            HMM_Vec3 low0, low1, high0, high1;
            switch (side) {
            case 0: // +Z side — ramp extends from platform +Z edge outward
                low0  = {px - rw2, 0,  pz + pd*0.5f + ramp_len};
                low1  = {px + rw2, 0,  pz + pd*0.5f + ramp_len};
                high0 = {px - rw2, ph, pz + pd*0.5f};
                high1 = {px + rw2, ph, pz + pd*0.5f};
                break;
            case 1: // -Z side
                low0  = {px + rw2, 0,  pz - pd*0.5f - ramp_len};
                low1  = {px - rw2, 0,  pz - pd*0.5f - ramp_len};
                high0 = {px + rw2, ph, pz - pd*0.5f};
                high1 = {px - rw2, ph, pz - pd*0.5f};
                break;
            case 2: // +X side
                low0  = {px + pw*0.5f + ramp_len, 0,  pz + rw2};
                low1  = {px + pw*0.5f + ramp_len, 0,  pz - rw2};
                high0 = {px + pw*0.5f, ph, pz + rw2};
                high1 = {px + pw*0.5f, ph, pz - rw2};
                break;
            default: // -X side
                low0  = {px - pw*0.5f - ramp_len, 0,  pz - rw2};
                low1  = {px - pw*0.5f - ramp_len, 0,  pz + rw2};
                high0 = {px - pw*0.5f, ph, pz - rw2};
                high1 = {px - pw*0.5f, ph, pz + rw2};
                break;
            }
            add_ramp_from_corners(m, low0, low1, high0, high1, config.ramp_color);
        }
    }

    // --- Boxes / cover ---
    int box_count = (int)randf((float)config.box_count_min, (float)config.box_count_max + 0.99f);
    for (int i = 0; i < box_count && placed_count < MAX_PLACED; i++) {
        float bw = randf(config.box_size_min, config.box_size_max);
        float bd = randf(config.box_size_min, config.box_size_max);
        float bh = randf(config.box_height_min, config.box_height_max);

        float bx = 0, bz = 0;
        bool ok = false;
        for (int attempt = 0; attempt < 30; attempt++) {
            bx = randf(-hw + config.box_margin + bw * 0.5f, hw - config.box_margin - bw * 0.5f);
            bz = randf(-hd + config.box_margin + bd * 0.5f, hd - config.box_margin - bd * 0.5f);
            if (!overlaps(bx, bz, bw, bd, placed, placed_count, 2.0f)) {
                ok = true;
                break;
            }
        }
        if (!ok) continue;

        // Slight color variation per box
        HMM_Vec3 col = config.box_color;
        float var = randf(-0.05f, 0.05f);
        col.X += var; col.Y += var; col.Z += var;

        add_box(m, {bx, 0, bz}, bw, bh, bd, col);
        placed[placed_count++] = {bx, bz, bw, bd};
    }

    // --- Spawn point (near entry door, facing into room) ---
    ld.spawn_pos = HMM_V3(entry_x, 1.0f, -hd + 3.0f);
    ld.has_spawn = true;

    // --- Enemy spawns (avoid placed objects) ---
    auto spawn_enemy = [&](EntityType type, float spawn_h) {
        for (int attempt = 0; attempt < 40; attempt++) {
            float ex = randf(-hw * 0.7f, hw * 0.7f);
            float ez = randf(-hd * 0.7f, hd * 0.7f);
            // Check not inside any placed box/platform/door zone (1m clearance)
            if (!overlaps(ex, ez, 1.0f, 1.0f, placed, placed_count, 1.0f)) {
                EnemySpawn es;
                es.position = HMM_V3(ex, spawn_h, ez);
                es.type = type;
                ld.enemy_spawns.push_back(es);
                return;
            }
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
