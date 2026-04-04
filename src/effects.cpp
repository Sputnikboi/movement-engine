#include "effects.h"
#include <cmath>
#include <cstring>

// ============================================================
//  Init / update
// ============================================================

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
            e.position        = pos;
            e.lifetime        = 0.6f;
            e.age             = 0.0f;
            e.alive           = true;
            e.ball_start_radius = 0.8f;
            e.ring_max_radius   = 3.0f;
            e.ring_tube_radius  = 0.15f;
            return;
        }
    }
}

// ============================================================
//  Geometry helpers
// ============================================================

// Append a low-poly sphere (icosahedron, no subdivision — 20 tris is enough
// for a quick flash effect)
static void append_ball(Mesh& out, HMM_Vec3 center, float radius,
                        float r, float g, float b) {
    if (radius < 0.001f) return;

    const float t = (1.0f + sqrtf(5.0f)) / 2.0f;
    auto norm = [](float x, float y, float z) -> HMM_Vec3 {
        float len = sqrtf(x*x + y*y + z*z);
        return HMM_V3(x/len, y/len, z/len);
    };

    HMM_Vec3 verts[12] = {
        norm(-1, t,0), norm(1, t,0), norm(-1,-t,0), norm(1,-t,0),
        norm(0,-1, t), norm(0, 1, t), norm(0,-1,-t), norm(0, 1,-t),
        norm( t,0,-1), norm( t,0, 1), norm(-t,0,-1), norm(-t,0, 1),
    };
    static const uint32_t tris[] = {
        0,11,5, 0,5,1, 0,1,7, 0,7,10, 0,10,11,
        1,5,9, 5,11,4, 11,10,2, 10,7,6, 7,1,8,
        3,9,4, 3,4,2, 3,2,6, 3,6,8, 3,8,9,
        4,9,5, 2,4,11, 6,2,10, 8,6,7, 9,8,1,
    };

    uint32_t base = static_cast<uint32_t>(out.vertices.size());
    for (int i = 0; i < 12; i++) {
        Vertex3D v;
        v.pos[0] = verts[i].X * radius + center.X;
        v.pos[1] = verts[i].Y * radius + center.Y;
        v.pos[2] = verts[i].Z * radius + center.Z;
        v.normal[0] = verts[i].X;
        v.normal[1] = verts[i].Y;
        v.normal[2] = verts[i].Z;
        v.color[0] = r; v.color[1] = g; v.color[2] = b;
        out.vertices.push_back(v);
    }
    for (int i = 0; i < 60; i++)
        out.indices.push_back(base + tris[i]);
}

// Append a torus (donut ring) lying flat in the XZ plane at center.
//   major_r = distance from center of torus to center of tube
//   minor_r = radius of the tube
//   seg_major = segments around the ring
//   seg_minor = segments around the tube cross-section
static void append_torus(Mesh& out, HMM_Vec3 center,
                         float major_r, float minor_r,
                         float r, float g, float b,
                         int seg_major = 24, int seg_minor = 8) {
    if (major_r < 0.001f || minor_r < 0.001f) return;

    uint32_t base = static_cast<uint32_t>(out.vertices.size());
    const float PI2 = 6.2831853f;

    // Generate vertices
    for (int i = 0; i <= seg_major; i++) {
        float theta = (float)i / seg_major * PI2; // angle around ring
        float ct = cosf(theta), st = sinf(theta);

        for (int j = 0; j <= seg_minor; j++) {
            float phi = (float)j / seg_minor * PI2; // angle around tube
            float cp = cosf(phi), sp = sinf(phi);

            // Point on torus surface
            float px = (major_r + minor_r * cp) * ct;
            float pz = (major_r + minor_r * cp) * st;
            float py = minor_r * sp;

            // Normal: direction from tube center to surface point
            float nx = cp * ct;
            float nz = cp * st;
            float ny = sp;

            Vertex3D v;
            v.pos[0] = px + center.X;
            v.pos[1] = py + center.Y;
            v.pos[2] = pz + center.Z;
            v.normal[0] = nx;
            v.normal[1] = ny;
            v.normal[2] = nz;
            v.color[0] = r; v.color[1] = g; v.color[2] = b;
            out.vertices.push_back(v);
        }
    }

    // Generate indices (quads as 2 tris)
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
//  Build death effect geometry into existing mesh
// ============================================================

void EffectSystem::append_to_mesh(Mesh& out) const {
    for (int i = 0; i < MAX_DEATH_EFFECTS; i++) {
        const DeathEffect& e = death_effects[i];
        if (!e.alive) continue;

        float t = e.age / e.lifetime; // 0..1

        // --- Collapsing yellow ball ---
        // Ease-in collapse (accelerates)
        float ball_t = t * t;
        float ball_radius = e.ball_start_radius * (1.0f - ball_t);

        // Yellow → white-hot as it collapses
        float white_blend = t * 0.6f;
        float br = 1.0f;
        float bg = 0.85f + white_blend * 0.15f;
        float bb = 0.15f + white_blend * 0.85f;

        append_ball(out, e.position, ball_radius, br, bg, bb);

        // --- Expanding solid donut ring ---
        // Ease-out expansion (fast start, slows down)
        float ring_t = 1.0f - (1.0f - t) * (1.0f - t);
        float major_r = e.ring_max_radius * ring_t;

        // Tube shrinks slightly as it expands
        float tube_r = e.ring_tube_radius * (1.0f - t * 0.5f);

        // Yellow-orange, slightly dimmer than ball
        float rr = 1.0f;
        float rg = 0.7f;
        float rb = 0.1f;

        append_torus(out, e.position, major_r, tube_r, rr, rg, rb);
    }
}
