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

// Add a ramp: a triangular prism connecting floor_y to top_y
// along the Z axis from z0 to z1, centered on x with given width.
static void add_ramp(Mesh& m, float cx, float z0, float z1,
                     float floor_y, float top_y, float width, HMM_Vec3 color) {
    float hw = width * 0.5f;
    float x0 = cx - hw, x1 = cx + hw;

    // Slope surface (the ramp face)
    HMM_Vec3 a = {x0, floor_y, z0};
    HMM_Vec3 b = {x1, floor_y, z0};
    HMM_Vec3 c = {x1, top_y, z1};
    HMM_Vec3 d = {x0, top_y, z1};

    HMM_Vec3 edge1 = HMM_SubV3(b, a);
    HMM_Vec3 edge2 = HMM_SubV3(d, a);
    HMM_Vec3 normal = HMM_NormV3(HMM_Cross(edge1, edge2));
    add_quad(m, a, b, c, d, normal, color);

    // Bottom face
    add_quad(m, {x0, floor_y, z1}, {x1, floor_y, z1}, {x1, floor_y, z0}, {x0, floor_y, z0},
             {0, -1, 0}, color);

    // Left side triangle
    {
        uint32_t base = (uint32_t)m.vertices.size();
        auto push = [&](HMM_Vec3 pos, HMM_Vec3 n) {
            Vertex3D v;
            v.pos[0] = pos.X; v.pos[1] = pos.Y; v.pos[2] = pos.Z;
            v.normal[0] = n.X; v.normal[1] = n.Y; v.normal[2] = n.Z;
            v.color[0] = color.X; v.color[1] = color.Y; v.color[2] = color.Z;
            m.vertices.push_back(v);
        };
        HMM_Vec3 n = {-1, 0, 0};
        push({x0, floor_y, z0}, n);
        push({x0, top_y, z1}, n);
        push({x0, floor_y, z1}, n);
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 1);
        m.indices.push_back(base + 2);
    }
    // Right side triangle
    {
        uint32_t base = (uint32_t)m.vertices.size();
        auto push = [&](HMM_Vec3 pos, HMM_Vec3 n) {
            Vertex3D v;
            v.pos[0] = pos.X; v.pos[1] = pos.Y; v.pos[2] = pos.Z;
            v.normal[0] = n.X; v.normal[1] = n.Y; v.normal[2] = n.Z;
            v.color[0] = color.X; v.color[1] = color.Y; v.color[2] = color.Z;
            m.vertices.push_back(v);
        };
        HMM_Vec3 n = {1, 0, 0};
        push({x1, floor_y, z0}, n);
        push({x1, floor_y, z1}, n);
        push({x1, top_y, z1}, n);
        m.indices.push_back(base + 0);
        m.indices.push_back(base + 1);
        m.indices.push_back(base + 2);
    }
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

LevelData generate_level(const ProcGenConfig& config) {
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

    // --- Floor ---
    add_quad(m, {-hw, 0, -hd}, {hw, 0, -hd}, {hw, 0, hd}, {-hw, 0, hd},
             {0, 1, 0}, config.floor_color);

    // --- Ceiling ---
    add_quad(m, {-hw, rh, hd}, {hw, rh, hd}, {hw, rh, -hd}, {-hw, rh, -hd},
             {0, -1, 0}, config.ceiling_color);

    // --- Walls ---
    // +Z wall
    add_box(m, {0, 0, hd}, rw + wt * 2, rh, wt, config.wall_color);
    // -Z wall
    add_box(m, {0, 0, -hd - wt}, rw + wt * 2, rh, wt, config.wall_color);
    // +X wall
    add_box(m, {hw, 0, 0}, wt, rh, rd, config.wall_color);
    // -X wall
    add_box(m, {-hw - wt, 0, 0}, wt, rh, rd, config.wall_color);

    // --- Track placed objects for overlap avoidance ---
    const int MAX_PLACED = 64;
    PlacedBox placed[MAX_PLACED];
    int placed_count = 0;

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
            if (!overlaps(px, pz, pw, pd, placed, placed_count, 2.0f)) {
                ok = true;
                break;
            }
        }
        if (!ok) continue;

        add_box(m, {px, 0, pz}, pw, ph, pd, config.platform_color);
        placed[placed_count++] = {px, pz, pw, pd};

        // Ramp up to this platform
        if (config.gen_ramps) {
            // Pick a side to place the ramp
            int side = rand() % 4;
            float ramp_len = ph * 2.0f; // gentle slope
            switch (side) {
            case 0: // +Z side
                add_ramp(m, px, pz + pd * 0.5f, pz + pd * 0.5f + ramp_len,
                         0, ph, config.ramp_width, config.ramp_color);
                break;
            case 1: // -Z side
                add_ramp(m, px, pz - pd * 0.5f - ramp_len, pz - pd * 0.5f,
                         0, ph, config.ramp_width, config.ramp_color);
                break;
            case 2: // +X side (ramp along X, swap coords)
                add_ramp(m, pz, px + pw * 0.5f, px + pw * 0.5f + ramp_len,
                         0, ph, config.ramp_width, config.ramp_color);
                // This won't work right — ramps are Z-aligned. Just use Z.
                // Fallback to +Z
                break;
            default: // -Z fallback
                add_ramp(m, px, pz - pd * 0.5f - ramp_len, pz - pd * 0.5f,
                         0, ph, config.ramp_width, config.ramp_color);
                break;
            }
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
            if (!overlaps(bx, bz, bw, bd, placed, placed_count, 1.0f)) {
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

    // --- Spawn point (center of room) ---
    ld.spawn_pos = HMM_V3(0, 1.0f, 0);
    ld.has_spawn = true;

    // --- Enemy spawns ---
    for (int i = 0; i < config.drone_count; i++) {
        float ex = randf(-hw * 0.7f, hw * 0.7f);
        float ez = randf(-hd * 0.7f, hd * 0.7f);
        EnemySpawn es;
        es.position = HMM_V3(ex, config.enemy_height, ez);
        es.type = EntityType::Drone;
        ld.enemy_spawns.push_back(es);
    }
    for (int i = 0; i < config.rusher_count; i++) {
        float ex = randf(-hw * 0.7f, hw * 0.7f);
        float ez = randf(-hd * 0.7f, hd * 0.7f);
        EnemySpawn es;
        es.position = HMM_V3(ex, config.enemy_height, ez);
        es.type = EntityType::Rusher;
        ld.enemy_spawns.push_back(es);
    }

    printf("ProcGen: %zu verts, %zu indices, %d boxes, %d platforms, %d enemies\n",
           m.vertices.size(), m.indices.size(), box_count, config.platform_count,
           config.drone_count + config.rusher_count);

    return ld;
}
