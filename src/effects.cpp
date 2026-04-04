#include "effects.h"
#include <cmath>
#include <cstring>

void EffectSystem::init() {
    memset(death_effects, 0, sizeof(death_effects));
}

void EffectSystem::update(float dt) {
    for (int i = 0; i < MAX_DEATH_EFFECTS; i++) {
        DeathEffect& e = death_effects[i];
        if (!e.alive) continue;
        e.age += dt;
        if (e.age >= e.lifetime)
            e.alive = false;
    }
}

void EffectSystem::spawn_drone_explosion(HMM_Vec3 pos) {
    for (int i = 0; i < MAX_DEATH_EFFECTS; i++) {
        if (!death_effects[i].alive) {
            DeathEffect& e = death_effects[i];
            e.position          = pos;
            e.lifetime          = 0.8f;
            e.age               = 0.0f;
            e.alive             = true;
            e.ball_start_radius = 1.2f;
            e.ring_max_radius   = 4.0f;
            e.ring_tube_radius  = 0.3f;
            return;
        }
    }
}

// ============================================================
//  Emissive icosphere (normals encode: X=alpha, length<0.5)
// ============================================================

static void append_emissive_ball(Mesh& out, HMM_Vec3 center, float radius,
                                 float r, float g, float b, float alpha = 0.0f,
                                 int subdivisions = 0) {
    if (radius < 0.001f) return;

    const float t = (1.0f + sqrtf(5.0f)) / 2.0f;
    auto vnorm = [](float x, float y, float z) -> HMM_Vec3 {
        float len = sqrtf(x*x + y*y + z*z);
        return HMM_V3(x/len, y/len, z/len);
    };

    HMM_Vec3 base_verts[12] = {
        vnorm(-1, t,0), vnorm(1, t,0), vnorm(-1,-t,0), vnorm(1,-t,0),
        vnorm(0,-1, t), vnorm(0, 1, t), vnorm(0,-1,-t), vnorm(0, 1,-t),
        vnorm( t,0,-1), vnorm( t,0, 1), vnorm(-t,0,-1), vnorm(-t,0, 1),
    };
    std::vector<HMM_Vec3> verts(base_verts, base_verts + 12);
    std::vector<uint32_t> tris = {
        0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
        1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
        4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1,
    };

    for (int s = 0; s < subdivisions; s++) {
        std::vector<uint32_t> new_tris;
        new_tris.reserve(tris.size() * 4);
        std::vector<std::pair<uint64_t, uint32_t>> cache;

        auto get_mid = [&](uint32_t a, uint32_t b) -> uint32_t {
            uint64_t key = (a < b) ? ((uint64_t)a << 32 | b) : ((uint64_t)b << 32 | a);
            for (auto& p : cache)
                if (p.first == key) return p.second;
            HMM_Vec3 mid = HMM_MulV3F(HMM_AddV3(verts[a], verts[b]), 0.5f);
            float len = HMM_LenV3(mid);
            if (len > 0.0001f) mid = HMM_MulV3F(mid, 1.0f / len);
            uint32_t idx = (uint32_t)verts.size();
            verts.push_back(mid);
            cache.push_back({key, idx});
            return idx;
        };

        for (size_t i = 0; i < tris.size(); i += 3) {
            uint32_t a = tris[i], b = tris[i+1], c = tris[i+2];
            uint32_t ab = get_mid(a, b), bc = get_mid(b, c), ca = get_mid(c, a);
            new_tris.insert(new_tris.end(), {a, ab, ca});
            new_tris.insert(new_tris.end(), {b, bc, ab});
            new_tris.insert(new_tris.end(), {c, ca, bc});
            new_tris.insert(new_tris.end(), {ab, bc, ca});
        }
        tris = std::move(new_tris);
    }

    uint32_t base_idx = static_cast<uint32_t>(out.vertices.size());
    for (size_t i = 0; i < verts.size(); i++) {
        Vertex3D v;
        v.pos[0] = verts[i].X * radius + center.X;
        v.pos[1] = verts[i].Y * radius + center.Y;
        v.pos[2] = verts[i].Z * radius + center.Z;
        // Emissive: normal.x = alpha (0 = opaque), length < 0.5
        v.normal[0] = alpha;
        v.normal[1] = 0.0f;
        v.normal[2] = 0.0f;
        v.color[0] = r; v.color[1] = g; v.color[2] = b;
        out.vertices.push_back(v);
    }
    for (auto idx : tris)
        out.indices.push_back(base_idx + idx);
}

// ============================================================
//  Emissive torus
// ============================================================

static void append_emissive_torus(Mesh& out, HMM_Vec3 center,
                                   float major_r, float minor_r,
                                   float r, float g, float b, float alpha = 0.0f,
                                   int seg_major = 24, int seg_minor = 10) {
    if (major_r < 0.001f || minor_r < 0.001f) return;

    uint32_t base = static_cast<uint32_t>(out.vertices.size());
    const float PI2 = 6.2831853f;

    for (int i = 0; i <= seg_major; i++) {
        float theta = (float)i / seg_major * PI2;
        float ct = cosf(theta), st = sinf(theta);
        for (int j = 0; j <= seg_minor; j++) {
            float phi = (float)j / seg_minor * PI2;
            float cp = cosf(phi), sp = sinf(phi);

            Vertex3D v;
            v.pos[0] = (major_r + minor_r * cp) * ct + center.X;
            v.pos[1] = minor_r * sp + center.Y;
            v.pos[2] = (major_r + minor_r * cp) * st + center.Z;
            v.normal[0] = alpha;
            v.normal[1] = 0.0f;
            v.normal[2] = 0.0f;
            v.color[0] = r; v.color[1] = g; v.color[2] = b;
            out.vertices.push_back(v);
        }
    }

    int ring_verts = seg_minor + 1;
    for (int i = 0; i < seg_major; i++) {
        for (int j = 0; j < seg_minor; j++) {
            uint32_t a = base + i * ring_verts + j;
            uint32_t b_idx = base + i * ring_verts + j + 1;
            uint32_t c = base + (i + 1) * ring_verts + j;
            uint32_t d = base + (i + 1) * ring_verts + j + 1;
            out.indices.push_back(a);
            out.indices.push_back(c);
            out.indices.push_back(b_idx);
            out.indices.push_back(b_idx);
            out.indices.push_back(c);
            out.indices.push_back(d);
        }
    }
}

// ============================================================
//  Opaque effect geometry: inner core ball + expanding torus ring
// ============================================================

void EffectSystem::append_to_mesh(Mesh& out) const {
    for (int i = 0; i < MAX_DEATH_EFFECTS; i++) {
        const DeathEffect& e = death_effects[i];
        if (!e.alive) continue;
        float t = e.age / e.lifetime;

        // Inner hot core (opaque, emissive)
        {
            float ball_t = t * t;
            float ball_r = e.ball_start_radius * (1.0f - ball_t);
            float white = t * 0.6f;
            append_emissive_ball(out, e.position, ball_r,
                                1.3f, 0.85f + white * 0.45f, 0.15f + white * 0.85f,
                                0.0f, 1); // alpha=0 → shader outputs 1.0 (opaque)
        }

        // Expanding solid torus ring (opaque, emissive)
        {
            float ring_t = 1.0f - (1.0f - t) * (1.0f - t);
            float major_r = e.ring_max_radius * ring_t;
            float tube_r = e.ring_tube_radius * (1.0f - t * 0.3f);
            float fade = 1.0f - t * 0.7f;
            append_emissive_torus(out, e.position, major_r, tube_r,
                                  1.0f * fade, 0.7f * fade, 0.1f * fade,
                                  0.0f); // opaque
        }
    }
}

// ============================================================
//  Transparent effect geometry: outer glow sphere
// ============================================================

void EffectSystem::append_transparent(Mesh& out) const {
    for (int i = 0; i < MAX_DEATH_EFFECTS; i++) {
        const DeathEffect& e = death_effects[i];
        if (!e.alive) continue;
        float t = e.age / e.lifetime;

        // Outer glow layer — larger, transparent, fades out
        {
            float outer_t = t * t;
            float outer_r = e.ball_start_radius * 1.8f * (1.0f - outer_t);
            float alpha = (1.0f - t) * 0.45f; // starts semi-transparent, fades
            // Warm orange-red glow
            append_emissive_ball(out, e.position, outer_r,
                                1.0f, 0.4f, 0.05f,
                                alpha, 1);
        }
    }
}
