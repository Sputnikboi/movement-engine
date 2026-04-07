#include "procgen.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>

#include <ctime>
static float randf(float lo, float hi) {
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

// --- Geometry helpers ---

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

    // Auto-correct winding so face matches normal direction
    HMM_Vec3 e1 = HMM_SubV3(b, a), e2 = HMM_SubV3(c, a);
    HMM_Vec3 cross = HMM_Cross(e1, e2);
    if (HMM_DotV3(cross, normal) >= 0.0f) {
        m.indices.push_back(base); m.indices.push_back(base+1); m.indices.push_back(base+2);
        m.indices.push_back(base); m.indices.push_back(base+2); m.indices.push_back(base+3);
    } else {
        m.indices.push_back(base); m.indices.push_back(base+2); m.indices.push_back(base+1);
        m.indices.push_back(base); m.indices.push_back(base+3); m.indices.push_back(base+2);
    }
}

// Rotate a point around Y axis at a given center
static HMM_Vec3 rotate_y(HMM_Vec3 p, float cx, float cz, float c, float s) {
    float lx = p.X - cx, lz = p.Z - cz;
    return {cx + lx * c + lz * s, p.Y, cz - lx * s + lz * c};
}
static HMM_Vec3 rotate_n(HMM_Vec3 n, float c, float s) {
    return {n.X * c + n.Z * s, n.Y, -n.X * s + n.Z * c};
}

// Full 3-axis rotation: yaw (Y), pitch (X), roll (Z)


// Add a box using 3 basis vectors (right, up, forward) for orientation.
// bottom center at pos.
static void add_box_oriented(Mesh& m, HMM_Vec3 pos, float w, float h, float d,
                             HMM_Vec3 color,
                             HMM_Vec3 right, HMM_Vec3 up, HMM_Vec3 fwd) {
    // Half extents along each axis
    HMM_Vec3 hr = HMM_MulV3F(right, w * 0.5f);
    HMM_Vec3 hu = HMM_MulV3F(up, h);
    HMM_Vec3 hf = HMM_MulV3F(fwd, d * 0.5f);

    // 8 corners: combinations of +-right, 0/+up, +-forward
    auto corner = [&](float sr, float su, float sf) -> HMM_Vec3 {
        return HMM_AddV3(HMM_AddV3(HMM_AddV3(pos,
            HMM_MulV3F(hr, sr)),
            HMM_MulV3F(hu, su)),
            HMM_MulV3F(hf, sf));
    };
    HMM_Vec3 c000 = corner(-1, 0, -1); // bottom-left-back
    HMM_Vec3 c100 = corner( 1, 0, -1);
    HMM_Vec3 c010 = corner(-1, 1, -1);
    HMM_Vec3 c110 = corner( 1, 1, -1);
    HMM_Vec3 c001 = corner(-1, 0,  1);
    HMM_Vec3 c101 = corner( 1, 0,  1);
    HMM_Vec3 c011 = corner(-1, 1,  1);
    HMM_Vec3 c111 = corner( 1, 1,  1);

    // Top (+up)
    add_quad(m, c010, c110, c111, c011, up, color);
    // Bottom (-up)
    add_quad(m, c001, c101, c100, c000, HMM_MulV3F(up, -1), color);
    // Front (+fwd)
    add_quad(m, c001, c011, c111, c101, fwd, color);
    // Back (-fwd)
    add_quad(m, c100, c110, c010, c000, HMM_MulV3F(fwd, -1), color);
    // Right (+right)
    add_quad(m, c101, c111, c110, c100, right, color);
    // Left (-right)
    add_quad(m, c000, c010, c011, c001, HMM_MulV3F(right, -1), color);
}

// Convenience: box with yaw only (no terrain tilt), for pillars/platforms
static void add_box(Mesh& m, HMM_Vec3 pos, float w, float h, float d,
                    HMM_Vec3 color, float yaw) {
    float cy = cosf(yaw), sy = sinf(yaw);
    HMM_Vec3 right = {cy, 0, -sy};
    HMM_Vec3 up    = {0, 1, 0};
    HMM_Vec3 fwd   = {sy, 0, cy};
    add_box_oriented(m, pos, w, h, d, color, right, up, fwd);
}

// Merge external mesh (for door model)
static void merge_mesh(Mesh& dst, const Mesh& src, HMM_Vec3 offset, float yaw) {
    uint32_t base = (uint32_t)dst.vertices.size();
    float cy = cosf(yaw), sy = sinf(yaw);
    for (const auto& sv : src.vertices) {
        Vertex3D v = sv;
        float rx = sv.pos[0] * cy + sv.pos[2] * sy;
        float rz = -sv.pos[0] * sy + sv.pos[2] * cy;
        v.pos[0] = rx + offset.X;
        v.pos[1] = sv.pos[1] + offset.Y;
        v.pos[2] = rz + offset.Z;
        float rnx = sv.normal[0] * cy + sv.normal[2] * sy;
        float rnz = -sv.normal[0] * sy + sv.normal[2] * cy;
        v.normal[0] = rnx; v.normal[2] = rnz;
        dst.vertices.push_back(v);
    }
    for (auto idx : src.indices)
        dst.indices.push_back(base + idx);
}

// --- Overlap tracking ---
struct PlacedObj {
    float x, z, radius;
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

// --- Heightmap for terrain floor ---
struct HeightMap {
    int res;
    float hw, hd; // half-width, half-depth
    std::vector<float> heights; // (res+1) x (res+1)

    float cell_w() const { return (2.0f * hw) / res; }
    float cell_d() const { return (2.0f * hd) / res; }

    float& at(int ix, int iz) { return heights[iz * (res+1) + ix]; }
    float  at(int ix, int iz) const { return heights[iz * (res+1) + ix]; }

    float sample(float x, float z) const {
        float fx = (x + hw) / (2.0f * hw) * res;
        float fz = (z + hd) / (2.0f * hd) * res;
        int ix = std::max(0, std::min(res-1, (int)fx));
        int iz = std::max(0, std::min(res-1, (int)fz));
        float tx = fx - ix, tz = fz - iz;
        float h00 = at(ix, iz),   h10 = at(ix+1, iz);
        float h01 = at(ix, iz+1), h11 = at(ix+1, iz+1);
        return h00*(1-tx)*(1-tz) + h10*tx*(1-tz) + h01*(1-tx)*tz + h11*tx*tz;
    }

    // Surface normal via finite differences
    HMM_Vec3 sample_normal(float x, float z) const {
        float eps = cell_w() * 0.5f;
        float hL = sample(x - eps, z), hR = sample(x + eps, z);
        float hD = sample(x, z - eps), hU = sample(x, z + eps);
        HMM_Vec3 n = {-(hR - hL), 2.0f * eps, -(hU - hD)};
        return HMM_NormV3(n);
    }

    // Build a 3x3 rotation matrix aligning Y-up to the terrain normal,
    // then rotating around that normal by yaw.
    // Returns 3 basis vectors (right, up, forward).
    void terrain_basis(float x, float z, float yaw,
                       HMM_Vec3* out_right, HMM_Vec3* out_up, HMM_Vec3* out_fwd) const {
        HMM_Vec3 up = sample_normal(x, z);
        // Yaw direction in XZ plane
        float cy = cosf(yaw), sy = sinf(yaw);
        HMM_Vec3 wish_fwd = {sy, 0, cy};
        // Right = cross(wish_fwd, up), normalized
        HMM_Vec3 right = HMM_NormV3(HMM_Cross(wish_fwd, up));
        // Recompute forward = cross(up, right) to ensure orthogonal
        HMM_Vec3 fwd = HMM_Cross(up, right);
        *out_right = right;
        *out_up = up;
        *out_fwd = fwd;
    }
};

static void emit_terrain(Mesh& m, const HeightMap& hm, HMM_Vec3 color) {
    float cw = hm.cell_w(), cd = hm.cell_d();
    for (int iz = 0; iz < hm.res; iz++) {
        for (int ix = 0; ix < hm.res; ix++) {
            float x0 = -hm.hw + ix * cw, x1 = x0 + cw;
            float z0 = -hm.hd + iz * cd, z1 = z0 + cd;
            float h00 = hm.at(ix, iz),   h10 = hm.at(ix+1, iz);
            float h01 = hm.at(ix, iz+1), h11 = hm.at(ix+1, iz+1);

            HMM_Vec3 a = {x0, h00, z0};
            HMM_Vec3 b = {x1, h10, z0};
            HMM_Vec3 c = {x1, h11, z1};
            HMM_Vec3 d = {x0, h01, z1};

            // Slight color variation per cell
            HMM_Vec3 col = color;
            float avg_h = (h00 + h10 + h01 + h11) * 0.25f;
            float tint = avg_h * 0.02f;
            col.X += tint; col.Y += tint; col.Z += tint;

            // Compute per-face normal
            HMM_Vec3 n1 = HMM_NormV3(HMM_Cross(HMM_SubV3(b, a), HMM_SubV3(d, a)));
            if (n1.Y < 0) n1 = HMM_MulV3F(n1, -1);
            add_quad(m, a, d, c, b, n1, col);
        }
    }
}

// ====================== MAIN GENERATE ======================

LevelData generate_level(const ProcGenConfig& config,
                         const Mesh* door_mesh,
                         std::vector<DoorInfo>* doors_out) {
    if (config.seed != 0) srand(config.seed);
    else srand((unsigned)time(nullptr));

    LevelData ld;
    Mesh& m = ld.mesh;

    float rw = randf(config.room_width_min, config.room_width_max);
    float rd = randf(config.room_depth_min, config.room_depth_max);
    float rh = config.room_height;
    float hw = rw * 0.5f, hd = rd * 0.5f;

    printf("ProcGen: room %.0f x %.0f x %.0f\n", rw, rd, rh);

    // --- Terrain heightmap ---
    HeightMap hm;
    hm.res = config.floor_grid_res;
    hm.hw = hw; hm.hd = hd;
    hm.heights.resize((hm.res+1) * (hm.res+1), 0.0f);

    // Place hills
    int hill_count = (int)randf((float)config.hill_count_min, (float)config.hill_count_max + 0.99f);
    struct Hill {
        float cx, cz;           // center
        float rx, rz;           // ellipse radii (local frame)
        float angle;            // rotation angle
        float height;           // positive = hill, negative = crater
        float sharpness;        // 1 = smooth, higher = sharper edges
    };
    std::vector<Hill> hills(hill_count);
    for (int i = 0; i < hill_count; i++) {
        Hill& hl = hills[i];
        hl.cx = randf(-hw * 0.7f, hw * 0.7f);
        hl.cz = randf(-hd * 0.7f, hd * 0.7f);
        float base_r = randf(config.hill_radius_min, config.hill_radius_max);
        // 30% chance of ridge (elongated 2-4x), otherwise roughly circular
        float elong = (randf(0,1) < 0.3f) ? randf(2.0f, 4.0f) : randf(0.8f, 1.2f);
        hl.rx = base_r * elong;
        hl.rz = base_r / elong;
        hl.angle = randf(0, HMM_PI32 * 2.0f);
        // 25% chance of crater (negative height)
        float sign = (randf(0,1) < 0.25f) ? -1.0f : 1.0f;
        float h_max = config.hill_height_max;
        // Ridges (elongated shapes) get reduced height
        if (elong > 1.5f) h_max *= 0.4f;
        hl.height = sign * randf(config.hill_height_min, h_max);
        // Sharpness: 1.0 = smooth parabolic, up to 3.0 = mesa/plateau edges
        hl.sharpness = randf(1.0f, 3.0f);
    }

    // Exit platform config
    float exit_plat_h     = 2.5f;
    float exit_plat_depth = 6.0f;
    float exit_plat_width = 14.0f;
    float exit_ramp_len   = 8.0f;

    float entry_x = 0.0f, exit_x = 0.0f;

    float cw = hm.cell_w(), cd = hm.cell_d();
    for (int iz = 0; iz <= hm.res; iz++) {
        for (int ix = 0; ix <= hm.res; ix++) {
            float x = -hw + ix * cw;
            float z = -hd + iz * cd;
            float h = 0.0f;
            for (const auto& hl : hills) {
                // Rotate point into hill's local frame
                float dx = x - hl.cx, dz = z - hl.cz;
                float ca = cosf(hl.angle), sa = sinf(hl.angle);
                float lx = dx * ca + dz * sa;
                float lz = -dx * sa + dz * ca;
                // Elliptical normalized distance
                float ex = lx / hl.rx, ez = lz / hl.rz;
                float d2 = ex * ex + ez * ez;
                if (d2 < 1.0f) {
                    float d = sqrtf(d2);
                    // Trapezoidal profile: flat_top = inner fraction that's flat
                    float flat_top = 1.0f - 1.0f / hl.sharpness; // sharpness 1→0% flat, 3→67% flat
                    float falloff;
                    if (d < flat_top)
                        falloff = 1.0f; // flat top
                    else
                        falloff = 1.0f - (d - flat_top) / (1.0f - flat_top); // linear ramp
                    // Smooth the ramp edges slightly
                    falloff = falloff * falloff * (3.0f - 2.0f * falloff); // smoothstep
                    h += hl.height * falloff;
                }
            }
            // Clamp craters
            if (h < -2.0f) h = -2.0f;

            // Flatten rectangular safe zone at entry (-Z wall)
            {
                float entry_safe_w = 10.0f, entry_safe_d = 10.0f;
                float fade_w = 4.0f; // fade-out margin
                float dz_in = z - (-hd); // distance from -Z wall into room
                float dx = fabsf(x - entry_x);
                if (dz_in < entry_safe_d && dx < entry_safe_w) {
                    h = 0.0f; // fully flat
                } else if (dz_in < entry_safe_d + fade_w && dx < entry_safe_w + fade_w) {
                    float tz = (dz_in > entry_safe_d) ? (dz_in - entry_safe_d) / fade_w : 0.0f;
                    float tx = (dx > entry_safe_w) ? (dx - entry_safe_w) / fade_w : 0.0f;
                    float blend = fmaxf(tz, tx);
                    blend = blend * blend * (3.0f - 2.0f * blend); // smoothstep
                    h *= blend;
                }
            }
            // Flatten rectangular safe zone at exit (+Z wall)
            {
                float exit_safe_d = exit_plat_depth + exit_ramp_len + 4.0f;
                float exit_safe_w = exit_plat_width * 0.5f + 3.0f;
                float fade_w = 4.0f;
                float dz_in = hd - z; // distance from +Z wall into room
                float dx = fabsf(x - exit_x);
                if (dz_in < exit_safe_d && dx < exit_safe_w) {
                    h = 0.0f;
                } else if (dz_in < exit_safe_d + fade_w && dx < exit_safe_w + fade_w) {
                    float tz = (dz_in > exit_safe_d) ? (dz_in - exit_safe_d) / fade_w : 0.0f;
                    float tx = (dx > exit_safe_w) ? (dx - exit_safe_w) / fade_w : 0.0f;
                    float blend = fmaxf(tz, tx);
                    blend = blend * blend * (3.0f - 2.0f * blend);
                    h *= blend;
                }
            }
            // Flatten near room edges so terrain doesn't poke through walls
            {
                float edge_margin = 3.0f;
                float dx_wall = fminf(x - (-hw), hw - x);
                float dz_wall = fminf(z - (-hd), hd - z);
                float d_wall = fminf(dx_wall, dz_wall);
                if (d_wall < edge_margin) {
                    float blend = d_wall / edge_margin;
                    blend = blend * blend * (3.0f - 2.0f * blend);
                    h *= blend;
                }
            }
            hm.at(ix, iz) = h;
        }
    }

    // Find minimum terrain height for extending walls/pillars below
    float min_terrain_y = 0.0f;
    for (int i = 0; i < (int)hm.heights.size(); i++)
        if (hm.heights[i] < min_terrain_y) min_terrain_y = hm.heights[i];
    float wall_floor = min_terrain_y - 1.0f; // extend 1m below lowest point

    emit_terrain(m, hm, config.floor_color);

    // --- Ceiling ---
    add_quad(m, {-hw,rh,-hd}, {hw,rh,-hd}, {hw,rh,hd}, {-hw,rh,hd},
             {0,-1,0}, config.ceiling_color);

    // --- Door dimensions ---
    float door_w = 1.2f;
    float door_h = 2.15f;

    // Build wall with door gap
    auto wall_with_door = [&](float wx0, float wx1, float wy0, float wy1,
                              float wz, HMM_Vec3 normal,
                              float gap_center_x, float gap_w, float gap_h) {
        float gx0 = gap_center_x - gap_w * 0.5f;
        float gx1 = gap_center_x + gap_w * 0.5f;
        if (gx0 > wx0)
            add_quad(m, {wx0,wy0,wz}, {wx0,wy1,wz}, {gx0,wy1,wz}, {gx0,wy0,wz}, normal, config.wall_color);
        if (gx1 < wx1)
            add_quad(m, {gx1,wy0,wz}, {gx1,wy1,wz}, {wx1,wy1,wz}, {wx1,wy0,wz}, normal, config.wall_color);
        add_quad(m, {gx0,gap_h,wz}, {gx0,wy1,wz}, {gx1,wy1,wz}, {gx1,gap_h,wz}, normal, config.wall_color);
    };

    // Entry door gap starts at floor level
    wall_with_door(-hw, hw, wall_floor, rh, -hd, {0,0,1}, entry_x, door_w, door_h);
    // Exit door gap starts at platform height  
    wall_with_door(-hw, hw, exit_plat_h, rh, hd, {0,0,-1}, exit_x, door_w, exit_plat_h + door_h);
    // Full wall strip from floor to platform height (below the upper wall section)
    add_quad(m, {-hw,wall_floor,hd}, {-hw,exit_plat_h,hd}, {hw,exit_plat_h,hd}, {hw,wall_floor,hd},
             {0,0,-1}, config.wall_color);
    add_quad(m, {hw,wall_floor,hd}, {hw,rh,hd}, {hw,rh,-hd}, {hw,wall_floor,-hd}, {-1,0,0}, config.wall_color);
    add_quad(m, {-hw,wall_floor,-hd}, {-hw,rh,-hd}, {-hw,rh,hd}, {-hw,wall_floor,hd}, {1,0,0}, config.wall_color);

    // --- Exit platform (raised area at +Z wall) ---
    {
        float px0 = exit_x - exit_plat_width * 0.5f;
        float px1 = exit_x + exit_plat_width * 0.5f;
        float pz0 = hd - exit_plat_depth;
        float pz1 = hd;
        HMM_Vec3 pcol = {0.32f, 0.30f, 0.35f};
        // Top surface
        add_quad(m, {px0,exit_plat_h,pz0}, {px1,exit_plat_h,pz0},
                 {px1,exit_plat_h,pz1}, {px0,exit_plat_h,pz1},
                 {0,1,0}, pcol);
        // Front wall (facing -Z into room)
        add_quad(m, {px0,wall_floor,pz0}, {px0,exit_plat_h,pz0},
                 {px1,exit_plat_h,pz0}, {px1,wall_floor,pz0},
                 {0,0,-1}, pcol);
        // Left wall
        add_quad(m, {px0,wall_floor,pz1}, {px0,exit_plat_h,pz1},
                 {px0,exit_plat_h,pz0}, {px0,wall_floor,pz0},
                 {-1,0,0}, pcol);
        // Right wall
        add_quad(m, {px1,wall_floor,pz0}, {px1,exit_plat_h,pz0},
                 {px1,exit_plat_h,pz1}, {px1,wall_floor,pz1},
                 {1,0,0}, pcol);

        // Ramp from floor up to platform (slope face only)
        float ramp_z0 = pz0 - exit_ramp_len;
        float rw2 = exit_plat_width * 0.5f;
        HMM_Vec3 low0  = {exit_x - rw2, 0,           ramp_z0};
        HMM_Vec3 low1  = {exit_x + rw2, 0,           ramp_z0};
        HMM_Vec3 high0 = {exit_x - rw2, exit_plat_h, pz0};
        HMM_Vec3 high1 = {exit_x + rw2, exit_plat_h, pz0};
        HMM_Vec3 re1 = HMM_SubV3(high1, low1);
        HMM_Vec3 re2 = HMM_SubV3(low0, low1);
        HMM_Vec3 rn = HMM_NormV3(HMM_Cross(re1, re2));
        if (rn.Y < 0) rn = HMM_MulV3F(rn, -1.0f);
        add_quad(m, low0, high0, high1, low1, rn, config.ramp_color);
        // Ramp side walls down to floor
        add_quad(m, {exit_x - rw2, wall_floor, ramp_z0}, {exit_x - rw2, wall_floor, pz0},
                 {exit_x - rw2, exit_plat_h, pz0}, low0,
                 {-1,0,0}, pcol);
        add_quad(m, {exit_x + rw2, wall_floor, pz0}, {exit_x + rw2, wall_floor, ramp_z0},
                 low1, {exit_x + rw2, exit_plat_h, pz0},
                 {1,0,0}, pcol);
    }

    // --- Doors ---
    float door_offset = 0.05f;
    DoorInfo entry_door, exit_door;
    entry_door.position = HMM_V3(entry_x, 0, -hd - door_offset);
    entry_door.yaw = 0;
    entry_door.is_exit = false;
    entry_door.locked = false;

    exit_door.position = HMM_V3(exit_x, exit_plat_h, hd + door_offset);
    exit_door.yaw = 3.14159265f;
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

    // --- Overlap tracking ---
    const int MAX_PLACED = 128;
    PlacedObj placed[MAX_PLACED];
    int placed_count = 0;

    placed[placed_count++] = {entry_x, -hd + 2.0f, 2.5f};
    placed[placed_count++] = {exit_x, hd - (exit_plat_depth + exit_ramp_len) * 0.5f, (exit_plat_depth + exit_ramp_len) * 0.5f + 2.0f};

    // --- Helper: place a terrain-aligned box ---
    auto place_box_terrain = [&](float bx, float bz, float bw, float bh, float bd,
                                 HMM_Vec3 col, float yaw) {
        float base_y = hm.sample(bx, bz);
        HMM_Vec3 right, up, fwd;
        hm.terrain_basis(bx, bz, yaw, &right, &up, &fwd);
        add_box_oriented(m, {bx, base_y, bz}, bw, bh, bd, col, right, up, fwd);
    };
    // Place a box on top of another, using terrain basis to find actual top position
    auto place_box_stacked = [&](float bx, float bz, float bw, float bh, float bd,
                                 HMM_Vec3 col, float yaw, float base_box_h,
                                 float ground_x, float ground_z) {
        HMM_Vec3 right, up, fwd;
        hm.terrain_basis(ground_x, ground_z, yaw, &right, &up, &fwd);
        // Top center of base box = ground_pos + up * base_box_h
        float ground_y = hm.sample(ground_x, ground_z);
        HMM_Vec3 top_pos = HMM_AddV3(
            HMM_V3(ground_x, ground_y, ground_z),
            HMM_MulV3F(up, base_box_h));
        // Offset stacked box slightly from center
        float sox = bx - ground_x, soz = bz - ground_z;
        HMM_Vec3 stack_pos = HMM_AddV3(top_pos,
            HMM_AddV3(HMM_MulV3F(right, sox), HMM_MulV3F(fwd, soz)));
        add_box_oriented(m, stack_pos, bw, bh, bd, col, right, up, fwd);
    };

    // --- Box clusters + individual boxes ---
    int box_count = (int)randf((float)config.box_count_min, (float)config.box_count_max + 0.99f);
    int boxes_placed = 0;
    while (boxes_placed < box_count && placed_count < MAX_PLACED - 6) {
        bool is_cluster = randf(0, 1) < config.cluster_chance;

        float cx = 0, cz = 0;
        // Pick center position
        float probe_r = is_cluster ? 5.0f : 2.5f;
        float inset = probe_r + config.box_margin;
        bool ok = false;
        for (int attempt = 0; attempt < 30; attempt++) {
            cx = randf(-hw + inset, hw - inset);
            cz = randf(-hd + inset, hd - inset);
            if (!overlaps(cx, cz, probe_r, placed, placed_count, 1.5f)) {
                ok = true;
                break;
            }
        }
        if (!ok) { boxes_placed++; continue; }

        if (is_cluster) {
            // Place a cluster of boxes
            int cluster_n = (int)randf((float)config.cluster_size_min,
                                       (float)config.cluster_size_max + 0.99f);
            float cluster_yaw = randf(0, HMM_PI32 * 2.0f); // overall cluster orientation

            for (int ci = 0; ci < cluster_n && placed_count < MAX_PLACED; ci++) {
                // Offset from cluster center
                float off_x, off_z;
                if (ci == 0) {
                    off_x = 0; off_z = 0;
                } else {
                    float angle = randf(0, HMM_PI32 * 2.0f);
                    float dist = randf(1.5f, 4.0f);
                    off_x = cosf(angle) * dist;
                    off_z = sinf(angle) * dist;
                }
                float bx = cx + off_x, bz = cz + off_z;

                // More square: use a base size and vary slightly
                float base_sz = randf(config.box_size_min, config.box_size_max);
                float bw = base_sz * randf(0.85f, 1.15f);
                float bd = base_sz * randf(0.85f, 1.15f);
                float bh = randf(config.box_height_min, fminf(config.box_height_max, base_sz * 1.2f));
                float yaw = cluster_yaw + randf(-0.5f, 0.5f);
                float base_y = hm.sample(bx, bz);

                HMM_Vec3 col = config.box_color;
                float var = randf(-0.05f, 0.05f);
                col.X += var; col.Y += var; col.Z += var;

                place_box_terrain(bx, bz, bw, bh, bd, col, yaw);

                // Maybe stack
                if (randf(0, 1) < config.box_stack_chance) {
                    float sw = randf(config.box_size_min, bw * 0.8f);
                    float sd = randf(config.box_size_min, bd * 0.8f);
                    float sh = randf(config.box_height_min, bh * 0.7f);
                    float syaw = yaw + randf(-0.1f, 0.1f); // match base rotation closely
                    float sox = randf(-0.3f, 0.3f), soz = randf(-0.3f, 0.3f);
                    HMM_Vec3 scol = col;
                    float sv = randf(-0.04f, 0.04f);
                    scol.X += sv; scol.Y += sv; scol.Z += sv;
                    place_box_stacked(bx + sox, bz + soz, sw, sh, sd, scol, syaw, bh, bx, bz);
                }
            }
            // Register cluster as one placed object
            placed[placed_count++] = {cx, cz, probe_r};
            boxes_placed += cluster_n;
        } else {
            // Single box
            float base_sz = randf(config.box_size_min, config.box_size_max);
            float bw = base_sz * randf(0.85f, 1.15f);
            float bd = base_sz * randf(0.85f, 1.15f);
            float bh = randf(config.box_height_min, fminf(config.box_height_max, base_sz * 1.2f));
            float yaw = randf(0, HMM_PI32 * 2.0f);
            float base_y = hm.sample(cx, cz);
            float br = sqrtf(bw * bw + bd * bd) * 0.5f;

            HMM_Vec3 col = config.box_color;
            float var = randf(-0.05f, 0.05f);
            col.X += var; col.Y += var; col.Z += var;

            place_box_terrain(cx, cz, bw, bh, bd, col, yaw);
            placed[placed_count++] = {cx, cz, br};

            // Maybe stack
            if (randf(0, 1) < config.box_stack_chance) {
                float sw = randf(config.box_size_min, bw * 0.8f);
                float sd = randf(config.box_size_min, bd * 0.8f);
                float sh = randf(config.box_height_min, bh * 0.7f);
                float syaw = yaw + randf(-0.1f, 0.1f); // match base rotation closely
                float sox = randf(-0.3f, 0.3f), soz = randf(-0.3f, 0.3f);
                HMM_Vec3 scol = col;
                float sv = randf(-0.04f, 0.04f);
                scol.X += sv; scol.Y += sv; scol.Z += sv;
                place_box_stacked(cx + sox, cz + soz, sw, sh, sd, scol, syaw, bh, cx, cz);
            }

            boxes_placed++;
        }
    }

    // --- Tall structures ---
    int tall_count = (int)randf((float)config.tall_count_min, (float)config.tall_count_max + 0.99f);
    for (int i = 0; i < tall_count && placed_count < MAX_PLACED; i++) {
        float tw = randf(config.tall_size_min, config.tall_size_max);
        float td = randf(config.tall_size_min, config.tall_size_max);
        float th = randf(config.tall_height_min, config.tall_height_max);
        float yaw = randf(0, HMM_PI32 * 2.0f);
        float tr = sqrtf(tw * tw + td * td) * 0.5f;

        float tx = 0, tz = 0;
        bool tok = false;
        float tinset = tr + config.box_margin;
        for (int attempt = 0; attempt < 30; attempt++) {
            tx = randf(-hw + tinset, hw - tinset);
            tz = randf(-hd + tinset, hd - tinset);
            if (!overlaps(tx, tz, tr, placed, placed_count, 2.0f)) {
                tok = true;
                break;
            }
        }
        if (!tok) continue;

        HMM_Vec3 col = config.tall_color;
        float var = randf(-0.03f, 0.03f);
        col.X += var; col.Y += var; col.Z += var;

        add_box(m, {tx, wall_floor, tz}, tw, th - wall_floor, td, col, yaw);
        placed[placed_count++] = {tx, tz, tr};
    }

    // --- Fill empty areas with large buildings ---
    // Scan a grid and place buildings in cells that have nothing nearby
    {
        float scan_step = 12.0f; // check every 12m
        float min_empty_radius = 10.0f; // area must be this empty
        float bldg_w_min = 6.0f, bldg_w_max = 12.0f;
        float bldg_h_min = 6.0f, bldg_h_max = 14.0f;
        HMM_Vec3 bldg_color = {0.32f, 0.30f, 0.28f};

        for (float sz = -hd + scan_step; sz < hd - scan_step && placed_count < MAX_PLACED; sz += scan_step) {
            for (float sx = -hw + scan_step; sx < hw - scan_step && placed_count < MAX_PLACED; sx += scan_step) {
                // Jitter the probe point
                float px = sx + randf(-3.0f, 3.0f);
                float pz = sz + randf(-3.0f, 3.0f);
                // Check if area is empty
                if (!overlaps(px, pz, min_empty_radius, placed, placed_count, 0.0f)) {
                    float bw = randf(bldg_w_min, bldg_w_max);
                    float bd = randf(bldg_w_min, bldg_w_max);
                    float bh = randf(bldg_h_min, bldg_h_max);
                    float yaw = randf(0, HMM_PI32 * 2.0f);
                    float br = sqrtf(bw*bw + bd*bd) * 0.5f;

                    HMM_Vec3 col = bldg_color;
                    float var = randf(-0.03f, 0.03f);
                    col.X += var; col.Y += var; col.Z += var;

                    add_box(m, {px, wall_floor, pz}, bw, bh - wall_floor, bd, col, yaw);
                    placed[placed_count++] = {px, pz, br + 2.0f};
                }
            }
        }
    }

    // --- Spawn point ---
    ld.spawn_pos = HMM_V3(entry_x, 1.0f, -hd + 3.0f);
    ld.has_spawn = true;

    // --- Enemy spawns (placed last so they avoid all objects) ---
    float min_enemy_dist = 15.0f;
    auto spawn_enemy = [&](EntityType type, float spawn_h) {
        for (int attempt = 0; attempt < 40; attempt++) {
            float ex = randf(-hw * 0.7f, hw * 0.7f);
            float ez = randf(-hd * 0.7f, hd * 0.7f);
            if (overlaps(ex, ez, 0.5f, placed, placed_count, 1.0f))
                continue;
            float dx = ex - ld.spawn_pos.X, dz = ez - ld.spawn_pos.Z;
            if (dx * dx + dz * dz < min_enemy_dist * min_enemy_dist)
                continue;
            EnemySpawn es;
            float terrain_y = hm.sample(ex, ez);
            es.position = HMM_V3(ex, terrain_y + spawn_h, ez);
            es.type = type;
            ld.enemy_spawns.push_back(es);
            return;
        }
        float ex = randf(-hw * 0.5f, hw * 0.5f);
        float ez = randf(-hd * 0.5f, hd * 0.5f);
        EnemySpawn es;
        es.position = HMM_V3(ex, hm.sample(ex, ez) + spawn_h, ez);
        es.type = type;
        ld.enemy_spawns.push_back(es);
    };
    // Check for manual overrides (any fixed count > 0 uses old system)
    bool use_manual = (config.drone_count > 0 || config.rusher_count > 0 ||
                       config.turret_count > 0 || config.tank_count > 0 ||
                       config.bomber_count > 0 || config.shielder_count > 0);

    if (use_manual) {
        for (int i = 0; i < config.drone_count; i++)
            spawn_enemy(EntityType::Drone, config.enemy_height);
        for (int i = 0; i < config.rusher_count; i++)
            spawn_enemy(EntityType::Rusher, 1.0f);
        for (int i = 0; i < config.turret_count; i++)
            spawn_enemy(EntityType::Turret, 1.5f);
        for (int i = 0; i < config.tank_count; i++)
            spawn_enemy(EntityType::Tank, 2.0f);
        for (int i = 0; i < config.bomber_count; i++)
            spawn_enemy(EntityType::Bomber, 12.0f);
        for (int i = 0; i < config.shielder_count; i++)
            spawn_enemy(EntityType::Shielder, 3.0f);
    } else {
        // Budget system: total count scales with room number
        int budget = config.enemy_budget_base +
                     (config.room_number - 1) * config.enemy_budget_per_room;
        if (budget > config.enemy_budget_max) budget = config.enemy_budget_max;

        // Weighted random selection
        // Weights shift with difficulty: tougher types become more common
        float diff = config.difficulty;
        float w[6] = {
            config.weight_drone,                              // always common
            config.weight_rusher,                             // always common
            config.weight_turret  * (0.5f + diff * 0.5f),    // ramps up
            config.weight_tank    * (0.2f + diff * 0.8f),    // rare early, common late
            config.weight_bomber  * (0.3f + diff * 0.7f),    // ramps up
            config.weight_shielder* (0.1f + diff * 0.9f),    // very rare early
        };
        float total_w = w[0] + w[1] + w[2] + w[3] + w[4] + w[5];

        // Guarantee at least 1 shielder after room 3
        bool shielder_guaranteed = (config.room_number >= 3);
        bool shielder_placed = false;

        EntityType types[6] = {
            EntityType::Drone, EntityType::Rusher, EntityType::Turret,
            EntityType::Tank, EntityType::Bomber, EntityType::Shielder
        };
        float heights[6] = {
            config.enemy_height, 1.0f, 1.5f, 2.0f, 12.0f, 3.0f
        };
        int costs[6] = {
            1, 1, 2, 5, 2, 3  // drone, rusher, turret, tank, bomber, shielder
        };

        int spent = 0;
        while (spent < budget) {
            // Force shielder if guaranteed and not yet placed, near end of budget
            if (shielder_guaranteed && !shielder_placed && budget - spent <= 4) {
                spawn_enemy(EntityType::Shielder, 3.0f);
                shielder_placed = true;
                spent += 3;
                continue;
            }

            float roll = randf(0.0f, total_w);
            float cumulative = 0.0f;
            int picked = 0;
            for (int j = 0; j < 6; j++) {
                cumulative += w[j];
                if (roll <= cumulative) { picked = j; break; }
            }

            // Skip if this enemy would exceed budget — pick a cheaper one
            if (spent + costs[picked] > budget) {
                // Fall back to cheapest (drone or rusher)
                picked = (rand() % 2 == 0) ? 0 : 1;
                if (spent + costs[picked] > budget) break;
            }

            if (types[picked] == EntityType::Shielder) shielder_placed = true;
            spawn_enemy(types[picked], heights[picked]);
            spent += costs[picked];
        }

        printf("ProcGen: Room %d, budget %d, difficulty %.2f\n",
               config.room_number, budget, diff);
    }

    printf("ProcGen: %zu verts, %zu indices, %d boxes, %d tall, %d hills\n",
           m.vertices.size(), m.indices.size(), boxes_placed, tall_count, hill_count);

    return ld;
}

// ============================================================
//  Shop room generation
// ============================================================

// Helper: add a hexagonal pedestal (approximated as 6-sided prism)
static void add_pedestal(Mesh& m, HMM_Vec3 base_pos, float radius, float height,
                         HMM_Vec3 body_color, HMM_Vec3 top_color) {
    const int SIDES = 6;
    float angle_step = 2.0f * HMM_PI32 / SIDES;

    // Compute corner positions at bottom and top
    HMM_Vec3 bot[SIDES], top_v[SIDES];
    for (int i = 0; i < SIDES; i++) {
        float a = angle_step * i;
        float cx = cosf(a) * radius;
        float cz = sinf(a) * radius;
        bot[i]   = HMM_V3(base_pos.X + cx, base_pos.Y,          base_pos.Z + cz);
        top_v[i] = HMM_V3(base_pos.X + cx, base_pos.Y + height, base_pos.Z + cz);
    }

    // Side faces (quads)
    for (int i = 0; i < SIDES; i++) {
        int j = (i + 1) % SIDES;
        HMM_Vec3 e1 = HMM_SubV3(bot[j], bot[i]);
        HMM_Vec3 e2 = HMM_V3(0, 1, 0);
        HMM_Vec3 normal = HMM_NormV3(HMM_Cross(e2, e1));
        add_quad(m, bot[i], top_v[i], top_v[j], bot[j], normal, body_color);
    }

    // Top face (fan of triangles, CCW when viewed from +Y)
    {
        HMM_Vec3 center_top = HMM_V3(base_pos.X, base_pos.Y + height, base_pos.Z);
        uint32_t center_idx = (uint32_t)m.vertices.size();
        Vertex3D cv;
        cv.pos[0] = center_top.X; cv.pos[1] = center_top.Y; cv.pos[2] = center_top.Z;
        cv.normal[0] = 0; cv.normal[1] = 1; cv.normal[2] = 0;
        cv.color[0] = top_color.X; cv.color[1] = top_color.Y; cv.color[2] = top_color.Z;
        m.vertices.push_back(cv);

        for (int i = 0; i < SIDES; i++) {
            Vertex3D v;
            v.pos[0] = top_v[i].X; v.pos[1] = top_v[i].Y; v.pos[2] = top_v[i].Z;
            v.normal[0] = 0; v.normal[1] = 1; v.normal[2] = 0;
            v.color[0] = top_color.X; v.color[1] = top_color.Y; v.color[2] = top_color.Z;
            m.vertices.push_back(v);
        }
        for (int i = 0; i < SIDES; i++) {
            int j = (i + 1) % SIDES;
            m.indices.push_back(center_idx);
            m.indices.push_back(center_idx + 1 + j);
            m.indices.push_back(center_idx + 1 + i);
        }
    }

    // Bottom face (fan, facing -Y)
    {
        HMM_Vec3 center_bot = base_pos;
        uint32_t center_idx = (uint32_t)m.vertices.size();
        Vertex3D cv;
        cv.pos[0] = center_bot.X; cv.pos[1] = center_bot.Y; cv.pos[2] = center_bot.Z;
        cv.normal[0] = 0; cv.normal[1] = -1; cv.normal[2] = 0;
        cv.color[0] = body_color.X; cv.color[1] = body_color.Y; cv.color[2] = body_color.Z;
        m.vertices.push_back(cv);

        for (int i = 0; i < SIDES; i++) {
            Vertex3D v;
            v.pos[0] = bot[i].X; v.pos[1] = bot[i].Y; v.pos[2] = bot[i].Z;
            v.normal[0] = 0; v.normal[1] = -1; v.normal[2] = 0;
            v.color[0] = body_color.X; v.color[1] = body_color.Y; v.color[2] = body_color.Z;
            m.vertices.push_back(v);
        }
        for (int i = 0; i < SIDES; i++) {
            int j = (i + 1) % SIDES;
            m.indices.push_back(center_idx);
            m.indices.push_back(center_idx + 1 + i);
            m.indices.push_back(center_idx + 1 + j);
        }
    }
}

ShopRoomData generate_shop_room(const Mesh* door_mesh,
                                std::vector<DoorInfo>* doors_out) {
    ShopRoomData shop;
    LevelData& ld = shop.level;
    Mesh& m = ld.mesh;

    // Small room dimensions
    float rw = 16.0f;  // width (X)
    float rd = 20.0f;  // depth (Z)
    float rh = 6.0f;   // height
    float hw = rw * 0.5f, hd = rd * 0.5f;

    // Colors
    HMM_Vec3 floor_col   = {0.20f, 0.18f, 0.22f};  // dark purple-grey
    HMM_Vec3 wall_col    = {0.15f, 0.14f, 0.18f};
    HMM_Vec3 ceiling_col = {0.12f, 0.11f, 0.14f};
    HMM_Vec3 trim_col    = {0.55f, 0.45f, 0.25f};  // gold trim

    // --- Floor ---
    add_quad(m, {-hw,0,-hd}, {hw,0,-hd}, {hw,0,hd}, {-hw,0,hd},
             {0,1,0}, floor_col);

    // --- Ceiling ---
    add_quad(m, {-hw,rh,-hd}, {-hw,rh,hd}, {hw,rh,hd}, {hw,rh,-hd},
             {0,-1,0}, ceiling_col);

    // --- Walls ---
    // Entry wall (-Z) with door gap
    float door_w = 1.2f, door_h = 2.15f;
    float entry_x = 0.0f;
    {
        float gx0 = entry_x - door_w * 0.5f;
        float gx1 = entry_x + door_w * 0.5f;
        // Left of door
        if (gx0 > -hw)
            add_quad(m, {-hw,0,-hd}, {-hw,rh,-hd}, {gx0,rh,-hd}, {gx0,0,-hd}, {0,0,1}, wall_col);
        // Right of door
        if (gx1 < hw)
            add_quad(m, {gx1,0,-hd}, {gx1,rh,-hd}, {hw,rh,-hd}, {hw,0,-hd}, {0,0,1}, wall_col);
        // Above door
        add_quad(m, {gx0,door_h,-hd}, {gx0,rh,-hd}, {gx1,rh,-hd}, {gx1,door_h,-hd}, {0,0,1}, wall_col);
    }

    // Exit wall (+Z) with door gap
    float exit_x = 0.0f;
    {
        float gx0 = exit_x - door_w * 0.5f;
        float gx1 = exit_x + door_w * 0.5f;
        if (gx0 > -hw)
            add_quad(m, {gx0,0,hd}, {gx0,rh,hd}, {-hw,rh,hd}, {-hw,0,hd}, {0,0,-1}, wall_col);
        if (gx1 < hw)
            add_quad(m, {hw,0,hd}, {hw,rh,hd}, {gx1,rh,hd}, {gx1,0,hd}, {0,0,-1}, wall_col);
        add_quad(m, {gx1,door_h,hd}, {gx1,rh,hd}, {gx0,rh,hd}, {gx0,door_h,hd}, {0,0,-1}, wall_col);
    }

    // Side walls (solid)
    add_quad(m, {hw,0,-hd}, {hw,rh,-hd}, {hw,rh,hd}, {hw,0,hd}, {-1,0,0}, wall_col);
    add_quad(m, {-hw,0,hd}, {-hw,rh,hd}, {-hw,rh,-hd}, {-hw,0,-hd}, {1,0,0}, wall_col);

    // --- Gold trim strips along floor-wall edges ---
    float trim_h = 0.08f;
    float trim_d = 0.02f;
    // Left wall trim
    add_box(m, {-hw + trim_d, 0, 0}, trim_d * 2, trim_h, rd, trim_col, 0);
    // Right wall trim
    add_box(m, { hw - trim_d, 0, 0}, trim_d * 2, trim_h, rd, trim_col, 0);
    // Back wall trim
    add_box(m, {0, 0, hd - trim_d}, rw, trim_h, trim_d * 2, trim_col, 0);
    // Front wall trim
    add_box(m, {0, 0, -hd + trim_d}, rw, trim_h, trim_d * 2, trim_col, 0);

    // --- Doors ---
    float door_offset = 0.05f;
    DoorInfo entry_door, exit_door;
    entry_door.position = HMM_V3(entry_x, 0, -hd - door_offset);
    entry_door.yaw = 0;
    entry_door.is_exit = false;
    entry_door.locked = false;

    exit_door.position = HMM_V3(exit_x, 0, hd + door_offset);
    exit_door.yaw = HMM_PI32;
    exit_door.is_exit = true;
    exit_door.locked = false;  // shop exit is always unlocked

    shop.exit_door_pos = exit_door.position;

    if (door_mesh) {
        merge_mesh(m, *door_mesh, entry_door.position, entry_door.yaw);
        merge_mesh(m, *door_mesh, exit_door.position, exit_door.yaw);
    }
    if (doors_out) {
        doors_out->clear();
        doors_out->push_back(entry_door);
        doors_out->push_back(exit_door);
    }

    // --- Pedestals / stands ---
    // Layout: stands arranged in a semicircle around the room center
    // Stand 0: Weapon (left)
    // Stand 1: Healthpack (right)
    // Stand 2: Empty / future (far left)
    // Stand 3: Empty / future (far right)

    float pedestal_radius = 0.45f;
    float pedestal_height = 0.9f;
    HMM_Vec3 ped_body = {0.25f, 0.22f, 0.28f};  // dark stone
    HMM_Vec3 ped_top_weapon = {0.6f, 0.5f, 0.2f};    // gold top for weapon
    HMM_Vec3 ped_top_health = {0.2f, 0.6f, 0.3f};    // green top for health
    HMM_Vec3 ped_top_empty  = {0.3f, 0.3f, 0.35f};   // grey for empty

    struct StandPlacement {
        float x, z;
        ShopStandType type;
        const char* label;
        HMM_Vec3 top_color;
    };

    // Positions: stands along the sides, leaving center aisle open
    StandPlacement placements[] = {
        { -3.5f,  2.0f, ShopStandType::Weapon,     "Weapon",       ped_top_weapon },
        {  3.5f,  2.0f, ShopStandType::Healthpack,  "Healthpack",   ped_top_health },
        { -3.5f,  6.0f, ShopStandType::Empty,       "Coming Soon",  ped_top_empty  },
        {  3.5f,  6.0f, ShopStandType::Empty,       "Coming Soon",  ped_top_empty  },
    };

    for (const auto& sp : placements) {
        HMM_Vec3 pos = HMM_V3(sp.x, 0, sp.z);
        add_pedestal(m, pos, pedestal_radius, pedestal_height, ped_body, sp.top_color);

        ShopStand stand;
        stand.position = HMM_V3(sp.x, pedestal_height, sp.z);  // top of pedestal
        stand.type = sp.type;
        stand.weapon_index = -1;  // set by caller
        stand.cost = 10;          // default, overridden by caller
        stand.purchased = false;
        stand.label = sp.label;
        shop.stands.push_back(stand);
    }

    // --- Small floating item markers above stands ---
    // Weapon stand: display model is added dynamically each frame (spinning)
    // Health stand: green cross
    {
        HMM_Vec3 hp_pos = HMM_V3(3.5f, pedestal_height + 0.15f, 2.0f);
        HMM_Vec3 hp_col = {0.3f, 0.8f, 0.4f};
        // Horizontal bar
        add_box(m, hp_pos, 0.5f, 0.12f, 0.15f, hp_col, 0);
        // Vertical bar
        add_box(m, hp_pos, 0.15f, 0.12f, 0.5f, hp_col, 0);
    }

    // --- Spawn point (just inside entry door) ---
    ld.spawn_pos = HMM_V3(entry_x, 1.0f, -hd + 2.0f);
    ld.has_spawn = true;

    printf("ShopRoom: %zu verts, %zu indices, %zu stands\n",
           m.vertices.size(), m.indices.size(), shop.stands.size());

    return shop;
}
