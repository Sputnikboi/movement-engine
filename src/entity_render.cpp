#include "entity_render.h"
#include <cmath>
#include <map>

// ============================================================
//  Frustum extraction from view-projection matrix
//  (Gribb-Hartmann method)
// ============================================================

void Frustum::extract(const HMM_Mat4& vp) {
    // Access elements as m[row][col]
    // HMM stores column-major: Elements[col][row]
    auto m = [&](int row, int col) -> float { return vp.Elements[col][row]; };

    // Left:   row3 + row0
    planes[0] = HMM_V4(m(3,0)+m(0,0), m(3,1)+m(0,1), m(3,2)+m(0,2), m(3,3)+m(0,3));
    // Right:  row3 - row0
    planes[1] = HMM_V4(m(3,0)-m(0,0), m(3,1)-m(0,1), m(3,2)-m(0,2), m(3,3)-m(0,3));
    // Bottom: row3 + row1
    planes[2] = HMM_V4(m(3,0)+m(1,0), m(3,1)+m(1,1), m(3,2)+m(1,2), m(3,3)+m(1,3));
    // Top:    row3 - row1
    planes[3] = HMM_V4(m(3,0)-m(1,0), m(3,1)-m(1,1), m(3,2)-m(1,2), m(3,3)-m(1,3));
    // Near:   row3 + row2
    planes[4] = HMM_V4(m(3,0)+m(2,0), m(3,1)+m(2,1), m(3,2)+m(2,2), m(3,3)+m(2,3));
    // Far:    row3 - row2
    planes[5] = HMM_V4(m(3,0)-m(2,0), m(3,1)-m(2,1), m(3,2)-m(2,2), m(3,3)-m(2,3));

    // Normalize each plane
    for (int i = 0; i < 6; i++) {
        float len = sqrtf(planes[i].X*planes[i].X + planes[i].Y*planes[i].Y + planes[i].Z*planes[i].Z);
        if (len > 0.0001f) {
            planes[i].X /= len;
            planes[i].Y /= len;
            planes[i].Z /= len;
            planes[i].W /= len;
        }
    }
}

bool Frustum::sphere_visible(HMM_Vec3 center, float radius) const {
    for (int i = 0; i < 6; i++) {
        float dist = planes[i].X * center.X + planes[i].Y * center.Y +
                     planes[i].Z * center.Z + planes[i].W;
        if (dist < -radius) return false; // entirely outside this plane
    }
    return true;
}

// ============================================================
//  Icosphere generation
// ============================================================

using EdgeKey = uint64_t;
static EdgeKey make_edge_key(uint32_t a, uint32_t b) {
    if (a > b) { uint32_t t = a; a = b; b = t; }
    return (static_cast<uint64_t>(a) << 32) | b;
}

static uint32_t get_midpoint(std::vector<HMM_Vec3>& verts,
                             std::map<EdgeKey, uint32_t>& cache,
                             uint32_t a, uint32_t b)
{
    EdgeKey key = make_edge_key(a, b);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;

    HMM_Vec3 mid = HMM_MulV3F(HMM_AddV3(verts[a], verts[b]), 0.5f);
    // Project onto unit sphere
    float len = HMM_LenV3(mid);
    if (len > 0.0001f) mid = HMM_MulV3F(mid, 1.0f / len);

    uint32_t idx = static_cast<uint32_t>(verts.size());
    verts.push_back(mid);
    cache[key] = idx;
    return idx;
}

Mesh create_icosphere(int subdivisions) {
    // Start with icosahedron
    const float t = (1.0f + sqrtf(5.0f)) / 2.0f;
    auto norm = [](float x, float y, float z) -> HMM_Vec3 {
        float len = sqrtf(x*x + y*y + z*z);
        return HMM_V3(x/len, y/len, z/len);
    };

    std::vector<HMM_Vec3> verts = {
        norm(-1,  t, 0), norm( 1,  t, 0), norm(-1, -t, 0), norm( 1, -t, 0),
        norm( 0, -1,  t), norm( 0,  1,  t), norm( 0, -1, -t), norm( 0,  1, -t),
        norm( t, 0, -1), norm( t, 0,  1), norm(-t, 0, -1), norm(-t, 0,  1),
    };

    std::vector<uint32_t> tris = {
        0,11,5,  0,5,1,  0,1,7,  0,7,10, 0,10,11,
        1,5,9,   5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4,   3,4,2,  3,2,6,  3,6,8,  3,8,9,
        4,9,5,   2,4,11, 6,2,10, 8,6,7,  9,8,1,
    };

    // Subdivide
    for (int s = 0; s < subdivisions; s++) {
        std::map<EdgeKey, uint32_t> cache;
        std::vector<uint32_t> new_tris;
        new_tris.reserve(tris.size() * 4);

        for (size_t i = 0; i < tris.size(); i += 3) {
            uint32_t a = tris[i], b = tris[i+1], c = tris[i+2];
            uint32_t ab = get_midpoint(verts, cache, a, b);
            uint32_t bc = get_midpoint(verts, cache, b, c);
            uint32_t ca = get_midpoint(verts, cache, c, a);

            new_tris.insert(new_tris.end(), {a, ab, ca});
            new_tris.insert(new_tris.end(), {b, bc, ab});
            new_tris.insert(new_tris.end(), {c, ca, bc});
            new_tris.insert(new_tris.end(), {ab, bc, ca});
        }
        tris = std::move(new_tris);
    }

    // Build Mesh with normals = positions (unit sphere)
    Mesh mesh;
    mesh.vertices.resize(verts.size());
    for (size_t i = 0; i < verts.size(); i++) {
        mesh.vertices[i].pos[0] = verts[i].X;
        mesh.vertices[i].pos[1] = verts[i].Y;
        mesh.vertices[i].pos[2] = verts[i].Z;
        mesh.vertices[i].normal[0] = verts[i].X;
        mesh.vertices[i].normal[1] = verts[i].Y;
        mesh.vertices[i].normal[2] = verts[i].Z;
        mesh.vertices[i].color[0] = 1.0f;
        mesh.vertices[i].color[1] = 1.0f;
        mesh.vertices[i].color[2] = 1.0f;
    }
    mesh.indices = tris;
    return mesh;
}

// ============================================================
//  Low-poly kunai shape for world projectiles (~12 verts)
// ============================================================
static Mesh create_kunai_lod() {
    Mesh m;
    // Elongated diamond: tip at +Z, pommel at -Z, 4-sided cross section
    // Blade: long front section, handle: shorter back
    const float blade_len = 7.0f;
    const float handle_len = 3.0f;
    const float blade_w = 0.35f;
    const float handle_w = 0.15f;

    // 6 key points: tip, 4 mid-ring, pommel
    HMM_Vec3 tip    = {0, 0, blade_len};
    HMM_Vec3 pommel = {0, 0, -handle_len};
    HMM_Vec3 mid[4] = {
        { blade_w, 0, 0},
        {0,  blade_w, 0},
        {-blade_w, 0, 0},
        {0, -blade_w, 0},
    };
    HMM_Vec3 grip[4] = {
        { handle_w, 0, -0.3f},
        {0,  handle_w, -0.3f},
        {-handle_w, 0, -0.3f},
        {0, -handle_w, -0.3f},
    };

    float col[3] = {0.55f, 0.55f, 0.5f};

    auto add_tri = [&](HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c) {
        uint32_t base = (uint32_t)m.vertices.size();
        HMM_Vec3 e1 = HMM_SubV3(b, a), e2 = HMM_SubV3(c, a);
        HMM_Vec3 n = HMM_NormV3(HMM_Cross(e1, e2));
        Vertex3D v;
        v.color[0] = col[0]; v.color[1] = col[1]; v.color[2] = col[2];
        v.normal[0] = n.X; v.normal[1] = n.Y; v.normal[2] = n.Z;
        v.pos[0] = a.X; v.pos[1] = a.Y; v.pos[2] = a.Z; m.vertices.push_back(v);
        v.pos[0] = b.X; v.pos[1] = b.Y; v.pos[2] = b.Z; m.vertices.push_back(v);
        v.pos[0] = c.X; v.pos[1] = c.Y; v.pos[2] = c.Z; m.vertices.push_back(v);
        m.indices.push_back(base); m.indices.push_back(base+1); m.indices.push_back(base+2);
    };

    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        // Blade (front cone)
        add_tri(tip, mid[j], mid[i]);
        // Mid band
        add_tri(mid[i], mid[j], grip[j]);
        add_tri(mid[i], grip[j], grip[i]);
        // Handle (back cone)
        add_tri(grip[i], grip[j], pommel);
    }
    return m;
}

// ============================================================
//  Append a transformed mesh (for knife projectiles, etc.)
// ============================================================
static void append_mesh_transformed(Mesh& out, const Mesh& src,
                                    HMM_Vec3 pos, float yaw, float pitch,
                                    float scale, float color_mult = 1.0f) {
    uint32_t base = static_cast<uint32_t>(out.vertices.size());

    // Build rotation: first pitch (around X), then yaw (around Y)
    HMM_Mat4 rot_yaw   = HMM_Rotate_RH(yaw,   HMM_V3(0, 1, 0));
    HMM_Mat4 rot_pitch = HMM_Rotate_RH(pitch,  HMM_V3(1, 0, 0));
    HMM_Mat4 rot = HMM_MulM4(rot_yaw, rot_pitch);

    for (const auto& sv : src.vertices) {
        Vertex3D v = sv;
        HMM_Vec3 p = HMM_V3(sv.pos[0] * scale, sv.pos[1] * scale, sv.pos[2] * scale);
        HMM_Vec4 rp = HMM_MulM4V4(rot, HMM_V4(p.X, p.Y, p.Z, 1.0f));
        v.pos[0] = rp.X + pos.X;
        v.pos[1] = rp.Y + pos.Y;
        v.pos[2] = rp.Z + pos.Z;

        HMM_Vec4 rn = HMM_MulM4V4(rot, HMM_V4(sv.normal[0], sv.normal[1], sv.normal[2], 0.0f));
        v.normal[0] = rn.X;
        v.normal[1] = rn.Y;
        v.normal[2] = rn.Z;

        if (color_mult != 1.0f) {
            v.color[0] *= color_mult;
            v.color[1] *= color_mult;
            v.color[2] *= color_mult;
        }

        out.vertices.push_back(v);
    }
    for (uint32_t idx : src.indices) {
        out.indices.push_back(base + idx);
    }
}

// ============================================================
//  Build entity mesh for rendering
// ============================================================

// Append a sphere with optional rotation (for tumbling ragdolls)
static void append_sphere(Mesh& out, const Mesh& sphere,
                          HMM_Vec3 pos, float scale,
                          float r, float g, float b,
                          float rot_x = 0.0f, float rot_z = 0.0f)
{
    uint32_t base = static_cast<uint32_t>(out.vertices.size());

    // Build rotation matrix if needed
    bool has_rot = (fabsf(rot_x) > 0.001f || fabsf(rot_z) > 0.001f);
    HMM_Mat4 rot_mat = HMM_M4D(1.0f);
    if (has_rot) {
        rot_mat = HMM_MulM4(
            HMM_Rotate_RH(HMM_AngleDeg(rot_x), HMM_V3(1, 0, 0)),
            HMM_Rotate_RH(HMM_AngleDeg(rot_z), HMM_V3(0, 0, 1))
        );
    }

    for (auto& sv : sphere.vertices) {
        Vertex3D v;
        HMM_Vec3 lp = HMM_V3(sv.pos[0] * scale, sv.pos[1] * scale, sv.pos[2] * scale);
        HMM_Vec3 ln = HMM_V3(sv.normal[0], sv.normal[1], sv.normal[2]);
        if (has_rot) {
            HMM_Vec4 rp = HMM_MulM4V4(rot_mat, HMM_V4(lp.X, lp.Y, lp.Z, 1.0f));
            lp = HMM_V3(rp.X, rp.Y, rp.Z);
            HMM_Vec4 rn = HMM_MulM4V4(rot_mat, HMM_V4(ln.X, ln.Y, ln.Z, 0.0f));
            ln = HMM_V3(rn.X, rn.Y, rn.Z);
        }
        v.pos[0] = lp.X + pos.X;
        v.pos[1] = lp.Y + pos.Y;
        v.pos[2] = lp.Z + pos.Z;
        v.normal[0] = ln.X;
        v.normal[1] = ln.Y;
        v.normal[2] = ln.Z;
        v.color[0] = r;
        v.color[1] = g;
        v.color[2] = b;
        out.vertices.push_back(v);
    }

    for (auto idx : sphere.indices)
        out.indices.push_back(base + idx);
}

Mesh build_entity_mesh(const Entity entities[], int max_entities,
                       const Frustum& frustum) {
    static Mesh sphere = create_icosphere(0);

    // Count alive entities for reserve
    int alive = 0;
    for (int i = 0; i < max_entities; i++)
        if (entities[i].alive) alive++;

    Mesh out;
    out.vertices.reserve(alive * sphere.vertices.size());
    out.indices.reserve(alive * sphere.indices.size());

    for (int i = 0; i < max_entities; i++) {
        const Entity& e = entities[i];
        if (!e.alive) continue;

        // Frustum cull
        if (!frustum.sphere_visible(e.position, e.radius)) continue;

        switch (e.type) {
        case EntityType::Drone: {
            // Health-based color: green -> red as health drops
            float hp_frac = (e.max_health > 0) ? e.health / e.max_health : 0.0f;
            float r = 1.0f - hp_frac;
            float g = hp_frac * 0.5f + 0.2f;
            float b = 0.3f;

            // Hit flash: lerp towards white
            if (e.hit_flash > 0.0f) {
                float flash = fminf(e.hit_flash * 6.0f, 1.0f); // quick in, fade out
                r = r + (1.0f - r) * flash;
                g = g + (1.0f - g) * flash;
                b = b + (1.0f - b) * flash;
            }

            append_sphere(out, sphere, e.position, e.radius, r, g, b,
                          e.tumble_x, e.tumble_z);
        } break;

        case EntityType::Rusher: {
            // Purple-red base, brightens as health drops
            float hp_frac = (e.max_health > 0) ? e.health / e.max_health : 0.0f;
            float r = 0.8f + (1.0f - hp_frac) * 0.2f;
            float g = 0.1f;
            float b = 0.4f + hp_frac * 0.2f;

            // Charging/dashing: glow brighter
            if (e.ai_state == 2 || e.ai_state == 3) { // RUSHER_CHARGING or DASHING
                r = 1.0f; g = 0.3f; b = 0.1f; // angry orange
            }

            // Hit flash
            if (e.hit_flash > 0.0f) {
                float flash = fminf(e.hit_flash * 6.0f, 1.0f);
                r = r + (1.0f - r) * flash;
                g = g + (1.0f - g) * flash;
                b = b + (1.0f - b) * flash;
            }

            append_sphere(out, sphere, e.position, e.radius, r, g, b,
                          e.tumble_x, e.tumble_z);
        } break;

        case EntityType::Turret: {
            // Steel blue-gray, reddens when firing
            float hp_frac = (e.max_health > 0) ? e.health / e.max_health : 0.0f;
            float r = 0.4f + (1.0f - hp_frac) * 0.3f;
            float g = 0.4f * hp_frac + 0.2f;
            float b = 0.5f + hp_frac * 0.2f;

            // Windup/firing glow: red
            if (e.ai_state == 2 || e.ai_state == 3) { // WINDUP or FIRING
                float pulse = (e.ai_state == 3) ? 1.0f : 0.5f;
                r = r + (1.0f - r) * pulse;
                g *= (1.0f - pulse * 0.5f);
                b *= (1.0f - pulse * 0.5f);
            }

            if (e.hit_flash > 0.0f) {
                float flash = fminf(e.hit_flash * 6.0f, 1.0f);
                r = r + (1.0f - r) * flash;
                g = g + (1.0f - g) * flash;
                b = b + (1.0f - b) * flash;
            }

            append_sphere(out, sphere, e.position, e.radius, r, g, b,
                          e.tumble_x, e.tumble_z);
        } break;

        case EntityType::Tank: {
            // Dark brown/gray, large
            float hp_frac = (e.max_health > 0) ? e.health / e.max_health : 0.0f;
            float r = 0.5f + (1.0f - hp_frac) * 0.3f;
            float g = 0.35f * hp_frac + 0.15f;
            float b = 0.2f;

            // Windup: glows orange-red before stomp
            if (e.ai_state == 2) { // TANK_WINDUP
                r = 1.0f; g = 0.5f; b = 0.1f;
            }
            // Stomp impact: bright flash
            if (e.ai_state == 3) { // TANK_STOMP
                r = 1.0f; g = 0.8f; b = 0.3f;
            }

            if (e.hit_flash > 0.0f) {
                float flash = fminf(e.hit_flash * 6.0f, 1.0f);
                r = r + (1.0f - r) * flash;
                g = g + (1.0f - g) * flash;
                b = b + (1.0f - b) * flash;
            }

            append_sphere(out, sphere, e.position, e.radius, r, g, b,
                          e.tumble_x, e.tumble_z);
        } break;

        case EntityType::Bomber: {
            // Dark green, glows when diving
            float hp_frac = (e.max_health > 0) ? e.health / e.max_health : 0.0f;
            float r = 0.2f + (1.0f - hp_frac) * 0.3f;
            float g = 0.5f + hp_frac * 0.2f;
            float b = 0.2f;

            // Diving: angry red-orange
            if (e.ai_state == 2) { // BOMBER_DIVING
                r = 1.0f; g = 0.3f; b = 0.05f;
            }
            // Exploding: bright white-red
            if (e.ai_state == 3) { // BOMBER_EXPLODING
                r = 1.0f; g = 0.9f; b = 0.5f;
            }

            if (e.hit_flash > 0.0f) {
                float flash = fminf(e.hit_flash * 6.0f, 1.0f);
                r = r + (1.0f - r) * flash;
                g = g + (1.0f - g) * flash;
                b = b + (1.0f - b) * flash;
            }

            append_sphere(out, sphere, e.position, e.radius, r, g, b,
                          e.tumble_x, e.tumble_z);
        } break;

        case EntityType::Shielder: {
            // Cyan/blue, pulses when shielding
            float hp_frac = (e.max_health > 0) ? e.health / e.max_health : 0.0f;
            float r = 0.1f;
            float g = 0.4f + hp_frac * 0.3f;
            float b = 0.8f + hp_frac * 0.2f;

            // Shielding: bright cyan pulse
            if (e.ai_state == 2) { // SHIELDER_SHIELDING
                float pulse = sinf(e.bob_seed + e.position.Y * 3.0f) * 0.3f + 0.7f;
                r = 0.2f * pulse;
                g = 0.8f * pulse;
                b = 1.0f * pulse;
            }

            if (e.hit_flash > 0.0f) {
                float flash = fminf(e.hit_flash * 6.0f, 1.0f);
                r = r + (1.0f - r) * flash;
                g = g + (1.0f - g) * flash;
                b = b + (1.0f - b) * flash;
            }

            append_sphere(out, sphere, e.position, e.radius, r, g, b,
                          e.tumble_x, e.tumble_z);
        } break;

        case EntityType::Projectile: {
            // Skip rendering real knife during grace period (avoids appearing inside camera)
            if (e.owner == -3 && e.ai_timer > 0.0f) break;

            if (e.owner == -3 || e.owner == -4) {
                // Player knife projectile — use low-poly LOD mesh
                static Mesh kunai_lod = create_kunai_lod();
                append_mesh_transformed(out, kunai_lod, e.position, e.yaw, e.pitch, 0.08f);
            } else if (e.owner == -2) {
                // Bomb (from bomber) — red-orange
                append_sphere(out, sphere, e.position, e.radius, 1.0f, 0.3f, 0.1f);
            } else {
                // Drone projectile — orange-yellow
                append_sphere(out, sphere, e.position, e.radius, 1.0f, 0.7f, 0.1f);
            }
        } break;

        default: break;
        }
    }

    return out;
}

// ============================================================
//  Shield bubble rendering (transparent)
// ============================================================

// Helper: append a shield link beam quad between two points
static void append_shield_beam(Mesh& out, HMM_Vec3 start, HMM_Vec3 end,
                               float half_width, float alpha) {
    // Use a fixed up vector, cross with beam dir for width
    HMM_Vec3 beam = HMM_SubV3(end, start);
    float beam_len = HMM_LenV3(beam);
    if (beam_len < 0.1f) return;

    HMM_Vec3 beam_dir = HMM_MulV3F(beam, 1.0f / beam_len);
    HMM_Vec3 up = HMM_V3(0, 1, 0);
    HMM_Vec3 right = HMM_Cross(beam_dir, up);
    float rlen = HMM_LenV3(right);
    if (rlen < 0.01f) {
        up = HMM_V3(1, 0, 0);
        right = HMM_Cross(beam_dir, up);
        rlen = HMM_LenV3(right);
    }
    right = HMM_MulV3F(right, half_width / rlen);
    up = HMM_NormV3(HMM_Cross(right, beam_dir));
    up = HMM_MulV3F(up, half_width);

    // Two crossed quads for visibility from any angle
    uint32_t base = (uint32_t)out.vertices.size();
    HMM_Vec3 corners[8] = {
        HMM_AddV3(start, right), HMM_SubV3(start, right),
        HMM_SubV3(end, right),   HMM_AddV3(end, right),
        HMM_AddV3(start, up),    HMM_SubV3(start, up),
        HMM_SubV3(end, up),      HMM_AddV3(end, up),
    };
    for (int i = 0; i < 8; i++) {
        Vertex3D v;
        v.pos[0] = corners[i].X;
        v.pos[1] = corners[i].Y;
        v.pos[2] = corners[i].Z;
        v.normal[0] = alpha;
        v.normal[1] = 0.0f;
        v.normal[2] = 0.0f;
        v.color[0] = 0.1f;
        v.color[1] = 0.4f;
        v.color[2] = 0.9f;
        out.vertices.push_back(v);
    }
    // Quad 1
    out.indices.push_back(base + 0); out.indices.push_back(base + 1); out.indices.push_back(base + 2);
    out.indices.push_back(base + 0); out.indices.push_back(base + 2); out.indices.push_back(base + 3);
    // Quad 2
    out.indices.push_back(base + 4); out.indices.push_back(base + 5); out.indices.push_back(base + 6);
    out.indices.push_back(base + 4); out.indices.push_back(base + 6); out.indices.push_back(base + 7);
}

void build_shield_bubbles(Mesh& out,
                          const Entity entities[], int max_entities,
                          const Frustum& frustum) {
    static Mesh bubble_sphere = create_icosphere(1); // subdivision 1 for smoother bubble

    // --- Shield bubbles on shielded entities ---
    for (int i = 0; i < max_entities; i++) {
        const Entity& e = entities[i];
        if (!e.alive) continue;
        if (e.shield_hp < 0.5f) continue;
        if (e.type == EntityType::Projectile || e.type == EntityType::Shielder) continue;

        if (!frustum.sphere_visible(e.position, e.radius * 1.8f)) continue;

        float bubble_radius = e.radius * 1.6f;
        float alpha = 0.25f + (e.shield_hp / 20.0f) * 0.15f;
        if (alpha > 0.4f) alpha = 0.4f;

        uint32_t base = static_cast<uint32_t>(out.vertices.size());

        for (auto& sv : bubble_sphere.vertices) {
            Vertex3D v;
            v.pos[0] = sv.pos[0] * bubble_radius + e.position.X;
            v.pos[1] = sv.pos[1] * bubble_radius + e.position.Y;
            v.pos[2] = sv.pos[2] * bubble_radius + e.position.Z;
            v.normal[0] = alpha;
            v.normal[1] = 0.0f;
            v.normal[2] = 0.0f;
            v.color[0] = 0.15f;
            v.color[1] = 0.5f;
            v.color[2] = 0.9f;
            out.vertices.push_back(v);
        }

        for (auto idx : bubble_sphere.indices)
            out.indices.push_back(base + idx);
    }

    // --- Shield link beams from shielders to shielded allies ---
    for (int i = 0; i < max_entities; i++) {
        const Entity& s = entities[i];
        if (!s.alive || s.type != EntityType::Shielder) continue;
        if (s.ai_state != 2) continue; // SHIELDER_SHIELDING = 2

        if (!frustum.sphere_visible(s.position, 15.0f)) continue;

        // Draw beam to each shielded ally in range
        for (int j = 0; j < max_entities; j++) {
            if (j == i) continue;
            const Entity& e = entities[j];
            if (!e.alive) continue;
            if (e.shield_hp < 0.5f) continue;
            if (e.type == EntityType::Projectile || e.type == EntityType::Shielder) continue;

            float dist = HMM_LenV3(HMM_SubV3(e.position, s.position));
            if (dist > 12.0f) continue; // slightly beyond shield_radius

            // Beam width and alpha scale with distance (thinner when further)
            float t = dist / 12.0f;
            float beam_width = 0.06f * (1.0f - t * 0.5f);
            float beam_alpha = 0.2f * (1.0f - t * 0.4f);

            append_shield_beam(out, s.position, e.position, beam_width, beam_alpha);
        }
    }
}

// ============================================================
//  Turret laser/beam/particle effects
// ============================================================
#include "collision.h"
#include "turret.h"

// Helper: append a quad (2 triangles) with emissive material
static void append_emissive_quad(Mesh& out,
                                 HMM_Vec3 a, HMM_Vec3 b, HMM_Vec3 c, HMM_Vec3 d,
                                 float r, float g, float bl, float alpha) {
    uint32_t base = (uint32_t)out.vertices.size();
    HMM_Vec3 corners[4] = {a, b, c, d};
    for (int i = 0; i < 4; i++) {
        Vertex3D v;
        v.pos[0] = corners[i].X;
        v.pos[1] = corners[i].Y;
        v.pos[2] = corners[i].Z;
        v.normal[0] = alpha; // emissive: normal.x = alpha
        v.normal[1] = 0.0f;
        v.normal[2] = 0.0f;
        v.color[0] = r;
        v.color[1] = g;
        v.color[2] = bl;
        out.vertices.push_back(v);
    }
    out.indices.push_back(base + 0);
    out.indices.push_back(base + 1);
    out.indices.push_back(base + 2);
    out.indices.push_back(base + 0);
    out.indices.push_back(base + 2);
    out.indices.push_back(base + 3);
    // Back face (double-sided)
    out.indices.push_back(base + 2);
    out.indices.push_back(base + 1);
    out.indices.push_back(base + 0);
    out.indices.push_back(base + 3);
    out.indices.push_back(base + 2);
    out.indices.push_back(base + 0);
}

// Build a camera-facing beam from `start` to `end` with given half-width
static void append_beam(Mesh& out, HMM_Vec3 start, HMM_Vec3 end,
                        float half_width, HMM_Vec3 cam_pos,
                        float r, float g, float b, float alpha) {
    HMM_Vec3 beam_dir = HMM_SubV3(end, start);
    HMM_Vec3 mid = HMM_MulV3F(HMM_AddV3(start, end), 0.5f);
    HMM_Vec3 view_dir = HMM_NormV3(HMM_SubV3(cam_pos, mid));

    // Perpendicular to both beam direction and view direction
    HMM_Vec3 right = HMM_NormV3(HMM_Cross(beam_dir, view_dir));
    HMM_Vec3 offset = HMM_MulV3F(right, half_width);

    HMM_Vec3 a = HMM_AddV3(start, offset);
    HMM_Vec3 bb = HMM_SubV3(start, offset);
    HMM_Vec3 c = HMM_SubV3(end, offset);
    HMM_Vec3 d = HMM_AddV3(end, offset);

    append_emissive_quad(out, a, bb, c, d, r, g, b, alpha);
}

void build_turret_effects(Mesh& opaque_out, Mesh& transparent_out,
                          const Entity entities[], int max_entities,
                          const CollisionWorld& world,
                          const Frustum& frustum, float total_time) {
    for (int i = 0; i < max_entities; i++) {
        const Entity& e = entities[i];
        if (!e.alive || e.type != EntityType::Turret) continue;
        if (e.ai_state == TURRET_IDLE || e.ai_state == TURRET_DYING || e.ai_state == TURRET_DEAD)
            continue;

        if (!frustum.sphere_visible(e.position, 50.0f)) continue;

        float cp = cosf(e.pitch);
        HMM_Vec3 fwd = HMM_V3(sinf(e.yaw) * cp, sinf(e.pitch), cosf(e.yaw) * cp);
        HMM_Vec3 origin = e.position;

        // Raycast to find where laser hits geometry
        float max_dist = 80.0f;
        HitResult hit = world.raycast(origin, fwd, max_dist);
        float laser_len = hit.hit ? hit.t : max_dist;
        HMM_Vec3 laser_end = HMM_AddV3(origin, HMM_MulV3F(fwd, laser_len));

        // Camera position for billboard beams (approximate from frustum)
        // We don't have direct cam_pos here, so extract from frustum plane normals
        // Actually let's just pass it through... we'll compute a reasonable up vector
        // For billboard we need cam_pos. Let's use a fixed up = (0,1,0) cross fwd
        HMM_Vec3 up = HMM_V3(0, 1, 0);
        HMM_Vec3 right = HMM_NormV3(HMM_Cross(fwd, up));
        if (HMM_LenV3(right) < 0.01f) {
            up = HMM_V3(1, 0, 0);
            right = HMM_NormV3(HMM_Cross(fwd, up));
        }
        up = HMM_NormV3(HMM_Cross(right, fwd));

        // === THIN RED AIMING LASER (always when tracking/windup/firing/cooldown) ===
        {
            float width = 0.015f;  // very thin
            float alpha_val = 0.0f;  // opaque emissive (alpha=0 → shader uses 1.0)

            // Pulsing brightness during windup
            float intensity = 0.6f;
            if (e.ai_state == TURRET_WINDUP) {
                float pulse = sinf(total_time * 12.0f) * 0.3f + 0.7f;
                intensity = 0.6f + pulse * 0.4f;
            }
            if (e.ai_state == TURRET_FIRING) intensity = 0.3f; // dim during beam

            // Build as 2 crossed quads for visibility from any angle
            HMM_Vec3 off_r = HMM_MulV3F(right, width);
            HMM_Vec3 off_u = HMM_MulV3F(up, width);

            // Horizontal quad
            append_emissive_quad(opaque_out,
                HMM_AddV3(origin, off_r), HMM_SubV3(origin, off_r),
                HMM_SubV3(laser_end, off_r), HMM_AddV3(laser_end, off_r),
                intensity, 0.05f, 0.05f, alpha_val);

            // Vertical quad
            append_emissive_quad(opaque_out,
                HMM_AddV3(origin, off_u), HMM_SubV3(origin, off_u),
                HMM_SubV3(laser_end, off_u), HMM_AddV3(laser_end, off_u),
                intensity, 0.05f, 0.05f, alpha_val);
        }

        // === CHARGE-UP PARTICLES (during windup) ===
        if (e.ai_state == TURRET_WINDUP) {
            // Particles orbit turret, slow spin, converge to center as charge completes
            float charge_t = 1.0f - (e.ai_timer / 1.2f); // 0→1 as charge completes
            if (charge_t < 0) charge_t = 0;
            if (charge_t > 1) charge_t = 1;

            int num_particles = 6;
            // Start wide, converge fully to center
            float orbit_radius = e.radius * 2.0f * (1.0f - charge_t * charge_t);
            float spin_speed = 1.5f + charge_t * 1.0f; // gentle spin, slight speedup

            for (int p = 0; p < num_particles; p++) {
                float angle = total_time * spin_speed + (float)p * (6.2831853f / num_particles);
                float y_off = sinf(total_time * 1.5f + (float)p * 1.5f) * 0.2f * (1.0f - charge_t);

                HMM_Vec3 ppos = HMM_V3(
                    origin.X + cosf(angle) * orbit_radius,
                    origin.Y + y_off,
                    origin.Z + sinf(angle) * orbit_radius
                );

                float psize = 0.04f + charge_t * 0.03f;

                // Two tiny crossed quads per particle
                HMM_Vec3 pr = HMM_MulV3F(right, psize);
                HMM_Vec3 pu = HMM_MulV3F(up, psize);

                // Color: orange → bright red as converging
                float pr_val = 0.8f + charge_t * 0.5f;
                float pg_val = 0.3f * (1.0f - charge_t);

                append_emissive_quad(opaque_out,
                    HMM_AddV3(ppos, pr), HMM_SubV3(ppos, pr),
                    HMM_SubV3(ppos, HMM_AddV3(pr, pu)), HMM_AddV3(ppos, HMM_SubV3(pu, pr)),
                    pr_val, pg_val, 0.05f, 0.0f);
            }
        }

        // === BIG RAILGUN BEAM (during firing) ===
        if (e.ai_state == TURRET_FIRING) {
            // Thick bright beam, fades over burst
            float burst_progress = e.ai_timer2 / 3.0f; // 0→1 over burst
            float beam_alpha = 0.7f * (1.0f - burst_progress * 0.4f);
            float beam_width = 0.15f + (1.0f - burst_progress) * 0.1f;

            // Core beam (bright white-red, transparent)
            HMM_Vec3 off_r = HMM_MulV3F(right, beam_width);
            HMM_Vec3 off_u = HMM_MulV3F(up, beam_width);

            // Main beam - 2 crossed quads
            append_emissive_quad(transparent_out,
                HMM_AddV3(origin, off_r), HMM_SubV3(origin, off_r),
                HMM_SubV3(laser_end, off_r), HMM_AddV3(laser_end, off_r),
                1.0f, 0.3f, 0.2f, beam_alpha);

            append_emissive_quad(transparent_out,
                HMM_AddV3(origin, off_u), HMM_SubV3(origin, off_u),
                HMM_SubV3(laser_end, off_u), HMM_AddV3(laser_end, off_u),
                1.0f, 0.3f, 0.2f, beam_alpha);

            // Outer glow beam (wider, more transparent)
            float glow_width = beam_width * 2.5f;
            HMM_Vec3 goff_r = HMM_MulV3F(right, glow_width);
            HMM_Vec3 goff_u = HMM_MulV3F(up, glow_width);

            append_emissive_quad(transparent_out,
                HMM_AddV3(origin, goff_r), HMM_SubV3(origin, goff_r),
                HMM_SubV3(laser_end, goff_r), HMM_AddV3(laser_end, goff_r),
                1.0f, 0.1f, 0.05f, beam_alpha * 0.3f);

            append_emissive_quad(transparent_out,
                HMM_AddV3(origin, goff_u), HMM_SubV3(origin, goff_u),
                HMM_SubV3(laser_end, goff_u), HMM_AddV3(laser_end, goff_u),
                1.0f, 0.1f, 0.05f, beam_alpha * 0.3f);

            // Particles along beam length
            int beam_particles = 8;
            for (int p = 0; p < beam_particles; p++) {
                float t = (float)p / (float)beam_particles;
                // Jitter offset so they shimmer
                float jitter_r = sinf(total_time * 15.0f + t * 20.0f) * beam_width * 1.5f;
                float jitter_u = cosf(total_time * 13.0f + t * 17.0f) * beam_width * 1.5f;

                HMM_Vec3 bp = HMM_AddV3(origin, HMM_MulV3F(fwd, laser_len * t));
                bp = HMM_AddV3(bp, HMM_MulV3F(right, jitter_r));
                bp = HMM_AddV3(bp, HMM_MulV3F(up, jitter_u));

                float psize = 0.06f;
                HMM_Vec3 pr2 = HMM_MulV3F(right, psize);
                HMM_Vec3 pu2 = HMM_MulV3F(up, psize);

                append_emissive_quad(opaque_out,
                    HMM_AddV3(bp, pr2), HMM_SubV3(bp, pr2),
                    HMM_SubV3(bp, HMM_AddV3(pr2, pu2)), HMM_AddV3(bp, HMM_SubV3(pu2, pr2)),
                    1.0f, 0.6f, 0.2f, 0.0f);
            }

            // Impact point flash
            if (hit.hit) {
                float flash_size = 0.2f + sinf(total_time * 20.0f) * 0.05f;
                HMM_Vec3 ir = HMM_MulV3F(right, flash_size);
                HMM_Vec3 iu = HMM_MulV3F(up, flash_size);

                append_emissive_quad(transparent_out,
                    HMM_AddV3(laser_end, ir), HMM_SubV3(laser_end, ir),
                    HMM_SubV3(laser_end, HMM_AddV3(ir, iu)),
                    HMM_AddV3(laser_end, HMM_SubV3(iu, ir)),
                    1.0f, 0.8f, 0.3f, 0.6f);

                append_emissive_quad(transparent_out,
                    HMM_AddV3(laser_end, iu), HMM_SubV3(laser_end, iu),
                    HMM_SubV3(laser_end, HMM_AddV3(ir, iu)),
                    HMM_AddV3(laser_end, HMM_SubV3(ir, iu)),
                    1.0f, 0.8f, 0.3f, 0.6f);
            }
        }
    }
}
