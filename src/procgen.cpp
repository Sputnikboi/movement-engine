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
static HMM_Vec3 rotate_ypz(HMM_Vec3 p, HMM_Vec3 center, float yaw, float pitch, float roll) {
    float lx = p.X - center.X, ly = p.Y - center.Y, lz = p.Z - center.Z;
    // Yaw (around Y)
    float cy = cosf(yaw), sy = sinf(yaw);
    float x1 = lx * cy + lz * sy;
    float z1 = -lx * sy + lz * cy;
    float y1 = ly;
    // Pitch (around X)
    float cp = cosf(pitch), sp = sinf(pitch);
    float y2 = y1 * cp - z1 * sp;
    float z2 = y1 * sp + z1 * cp;
    float x2 = x1;
    // Roll (around Z)
    float cr = cosf(roll), sr = sinf(roll);
    float x3 = x2 * cr - y2 * sr;
    float y3 = x2 * sr + y2 * cr;
    float z3 = z2;
    return {center.X + x3, center.Y + y3, center.Z + z3};
}
static HMM_Vec3 rotate_n_ypz(HMM_Vec3 n, float yaw, float pitch, float roll) {
    HMM_Vec3 zero = {0,0,0};
    return rotate_ypz(HMM_AddV3(zero, n), zero, yaw, pitch, roll);
}

// Add a box with yaw + optional pitch/roll tilt. Bottom center at pos.
static void add_box(Mesh& m, HMM_Vec3 pos, float w, float h, float d,
                    HMM_Vec3 color, float yaw, float pitch = 0, float roll = 0) {
    float x0 = pos.X - w * 0.5f, x1 = pos.X + w * 0.5f;
    float y0 = pos.Y,            y1 = pos.Y + h;
    float z0 = pos.Z - d * 0.5f, z1 = pos.Z + d * 0.5f;
    HMM_Vec3 ctr = {pos.X, pos.Y + h * 0.5f, pos.Z}; // rotate around center of box

    auto r  = [&](HMM_Vec3 p) { return rotate_ypz(p, ctr, yaw, pitch, roll); };
    auto rn = [&](HMM_Vec3 n) { return rotate_n_ypz(n, yaw, pitch, roll); };

    add_quad(m, r({x0,y1,z0}), r({x1,y1,z0}), r({x1,y1,z1}), r({x0,y1,z1}),
             rn({0,1,0}), color);
    add_quad(m, r({x0,y0,z1}), r({x1,y0,z1}), r({x1,y0,z0}), r({x0,y0,z0}),
             rn({0,-1,0}), color);
    add_quad(m, r({x0,y0,z1}), r({x0,y1,z1}), r({x1,y1,z1}), r({x1,y0,z1}),
             rn({0,0,1}), color);
    add_quad(m, r({x1,y0,z0}), r({x1,y1,z0}), r({x0,y1,z0}), r({x0,y0,z0}),
             rn({0,0,-1}), color);
    add_quad(m, r({x1,y0,z1}), r({x1,y1,z1}), r({x1,y1,z0}), r({x1,y0,z0}),
             rn({1,0,0}), color);
    add_quad(m, r({x0,y0,z0}), r({x0,y1,z0}), r({x0,y1,z1}), r({x0,y0,z1}),
             rn({-1,0,0}), color);
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

    // Derive pitch and roll to align a yaw-rotated box with terrain
    void normal_to_tilt(float x, float z, float yaw, float* pitch, float* roll) const {
        HMM_Vec3 n = sample_normal(x, z);
        float cy = cosf(yaw), sy = sinf(yaw);
        float n_fwd  =  n.X * sy + n.Z * cy;
        float n_right =  n.X * cy - n.Z * sy;
        *pitch = atan2f(n_fwd, n.Y);
        *roll  = -atan2f(n_right, n.Y);
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
    struct Hill { float cx, cz, radius, height; };
    std::vector<Hill> hills(hill_count);
    for (int i = 0; i < hill_count; i++) {
        hills[i].cx = randf(-hw * 0.7f, hw * 0.7f);
        hills[i].cz = randf(-hd * 0.7f, hd * 0.7f);
        hills[i].radius = randf(config.hill_radius_min, config.hill_radius_max);
        hills[i].height = randf(config.hill_height_min, config.hill_height_max);
    }

    // Flatten zones around doors
    float door_flat_radius = 5.0f;
    float entry_x = 0.0f, exit_x = 0.0f;

    float cw = hm.cell_w(), cd = hm.cell_d();
    for (int iz = 0; iz <= hm.res; iz++) {
        for (int ix = 0; ix <= hm.res; ix++) {
            float x = -hw + ix * cw;
            float z = -hd + iz * cd;
            float h = 0.0f;
            for (const auto& hill : hills) {
                float dx = x - hill.cx, dz = z - hill.cz;
                float d2 = dx*dx + dz*dz;
                float r2 = hill.radius * hill.radius;
                if (d2 < r2) {
                    float t = 1.0f - d2 / r2;
                    h += hill.height * t * t; // smooth falloff
                }
            }
            // Flatten near doors
            {
                float dz_entry = z - (-hd);
                float dx_entry = x - entry_x;
                float dist_entry = sqrtf(dx_entry*dx_entry + dz_entry*dz_entry);
                if (dist_entry < door_flat_radius)
                    h *= dist_entry / door_flat_radius;

                float dz_exit = z - hd;
                float dx_exit = x - exit_x;
                float dist_exit = sqrtf(dx_exit*dx_exit + dz_exit*dz_exit);
                if (dist_exit < door_flat_radius)
                    h *= dist_exit / door_flat_radius;
            }
            hm.at(ix, iz) = h;
        }
    }

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

    wall_with_door(-hw, hw, 0, rh, -hd, {0,0,1}, entry_x, door_w, door_h);
    wall_with_door(-hw, hw, 0, rh, hd, {0,0,-1}, exit_x, door_w, door_h);
    add_quad(m, {hw,0,hd}, {hw,rh,hd}, {hw,rh,-hd}, {hw,0,-hd}, {-1,0,0}, config.wall_color);
    add_quad(m, {-hw,0,-hd}, {-hw,rh,-hd}, {-hw,rh,hd}, {-hw,0,hd}, {1,0,0}, config.wall_color);

    // --- Doors ---
    float door_offset = 0.05f;
    DoorInfo entry_door, exit_door;
    entry_door.position = HMM_V3(entry_x, 0, -hd - door_offset);
    entry_door.yaw = 0;
    entry_door.is_exit = false;
    entry_door.locked = false;

    exit_door.position = HMM_V3(exit_x, 0, hd + door_offset);
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
    placed[placed_count++] = {exit_x,   hd - 2.0f, 2.5f};

    // --- Helper: place a single box with terrain-aware Y ---
    auto place_box = [&](float bx, float bz, float bw, float bh, float bd,
                         HMM_Vec3 col, float yaw, float pitch, float roll,
                         float base_y) {
        add_box(m, {bx, base_y, bz}, bw, bh, bd, col, yaw, pitch, roll);
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
                float bh = randf(config.box_height_min, config.box_height_max);
                float yaw = cluster_yaw + randf(-0.5f, 0.5f);
                float pitch, roll;
                hm.normal_to_tilt(bx, bz, yaw, &pitch, &roll);


                float base_y = hm.sample(bx, bz);

                HMM_Vec3 col = config.box_color;
                float var = randf(-0.05f, 0.05f);
                col.X += var; col.Y += var; col.Z += var;

                place_box(bx, bz, bw, bh, bd, col, yaw, pitch, roll, base_y);

                // Maybe stack
                if (randf(0, 1) < config.box_stack_chance) {
                    float sw = randf(config.box_size_min, bw * 0.8f);
                    float sd = randf(config.box_size_min, bd * 0.8f);
                    float sh = randf(config.box_height_min, bh * 0.7f);
                    float syaw = yaw + randf(-0.4f, 0.4f);
                    float sox = randf(-0.3f, 0.3f), soz = randf(-0.3f, 0.3f);
                    HMM_Vec3 scol = col;
                    float sv = randf(-0.04f, 0.04f);
                    scol.X += sv; scol.Y += sv; scol.Z += sv;
                    place_box(bx + sox, bz + soz, sw, sh, sd, scol, syaw, 0, 0, base_y + bh);
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
            float bh = randf(config.box_height_min, config.box_height_max);
            float yaw = randf(0, HMM_PI32 * 2.0f);
            float pitch, roll;
            hm.normal_to_tilt(cx, cz, yaw, &pitch, &roll);
            float base_y = hm.sample(cx, cz);
            float br = sqrtf(bw * bw + bd * bd) * 0.5f;

            HMM_Vec3 col = config.box_color;
            float var = randf(-0.05f, 0.05f);
            col.X += var; col.Y += var; col.Z += var;

            place_box(cx, cz, bw, bh, bd, col, yaw, pitch, roll, base_y);
            placed[placed_count++] = {cx, cz, br};

            // Maybe stack
            if (randf(0, 1) < config.box_stack_chance) {
                float sw = randf(config.box_size_min, bw * 0.8f);
                float sd = randf(config.box_size_min, bd * 0.8f);
                float sh = randf(config.box_height_min, bh * 0.7f);
                float syaw = yaw + randf(-0.4f, 0.4f);
                float sox = randf(-0.3f, 0.3f), soz = randf(-0.3f, 0.3f);
                HMM_Vec3 scol = col;
                float sv = randf(-0.04f, 0.04f);
                scol.X += sv; scol.Y += sv; scol.Z += sv;
                place_box(cx + sox, cz + soz, sw, sh, sd, scol, syaw, 0, 0, base_y + bh);
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

        float base_y = hm.sample(tx, tz);
        add_box(m, {tx, base_y, tz}, tw, th, td, col, yaw);
        placed[placed_count++] = {tx, tz, tr};
    }

    // --- Spawn point ---
    ld.spawn_pos = HMM_V3(entry_x, 1.0f, -hd + 3.0f);
    ld.has_spawn = true;

    // --- Enemy spawns ---
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
    for (int i = 0; i < config.drone_count; i++)
        spawn_enemy(EntityType::Drone, config.enemy_height);
    for (int i = 0; i < config.rusher_count; i++)
        spawn_enemy(EntityType::Rusher, 1.0f);

    printf("ProcGen: %zu verts, %zu indices, %d boxes, %d tall, %d hills\n",
           m.vertices.size(), m.indices.size(), boxes_placed, tall_count, hill_count);

    return ld;
}
