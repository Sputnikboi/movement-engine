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
        if (e.age >= e.lifetime) {
            e.alive = false;
        }
    }
}

void EffectSystem::spawn_drone_explosion(HMM_Vec3 pos) {
    for (int i = 0; i < MAX_DEATH_EFFECTS; i++) {
        if (!death_effects[i].alive) {
            DeathEffect& e = death_effects[i];
            e.position       = pos;
            e.lifetime       = 0.6f;
            e.age            = 0.0f;
            e.alive          = true;
            e.ball_start_size = 1.2f;
            e.ball_end_size   = 0.0f;
            e.ring_start_size = 0.3f;
            e.ring_max_size   = 5.0f;
            e.ring_thickness  = 0.4f;
            return;
        }
    }
}

// ============================================================
//  Build vertex data: each effect = 2 quads (ball + ring)
// ============================================================

int EffectSystem::build_vertices(std::vector<ParticleVertex>& out_verts,
                                  std::vector<uint32_t>& out_indices) const
{
    out_verts.clear();
    out_indices.clear();

    static const float corners[4][2] = {
        {-1, -1}, {1, -1}, {1, 1}, {-1, 1}
    };

    int count = 0;
    for (int i = 0; i < MAX_DEATH_EFFECTS; i++) {
        const DeathEffect& e = death_effects[i];
        if (!e.alive) continue;

        float t = e.age / e.lifetime; // 0..1

        // --- Yellow ball: collapses from ball_start_size to 0 ---
        // Quick collapse with ease-in (accelerates into nothing)
        float ball_t = t * t;  // ease-in
        float ball_size = e.ball_start_size * (1.0f - ball_t);
        // Fade to white-hot then vanish
        float ball_alpha = 1.0f - t * t;

        if (ball_size > 0.01f) {
            uint32_t base = static_cast<uint32_t>(out_verts.size());
            for (int c = 0; c < 4; c++) {
                ParticleVertex v;
                v.center[0] = e.position.X;
                v.center[1] = e.position.Y;
                v.center[2] = e.position.Z;
                v.corner[0] = corners[c][0];
                v.corner[1] = corners[c][1];
                // Yellow → white as it collapses
                float white = t * 0.5f;
                v.color[0]  = 1.0f;
                v.color[1]  = 0.85f + white * 0.15f;
                v.color[2]  = 0.15f + white * 0.85f;
                v.color[3]  = ball_alpha;
                v.params[0] = ball_size;
                v.params[1] = 1.0f;  // type = particle/blob
                out_verts.push_back(v);
            }
            out_indices.push_back(base + 0);
            out_indices.push_back(base + 1);
            out_indices.push_back(base + 2);
            out_indices.push_back(base + 0);
            out_indices.push_back(base + 2);
            out_indices.push_back(base + 3);
            count++;
        }

        // --- Expanding donut ring ---
        // Expands outward, fades out toward end
        float ring_t = sqrtf(t);  // ease-out (fast start, slow end)
        float ring_size = e.ring_start_size + (e.ring_max_size - e.ring_start_size) * ring_t;
        float ring_alpha = 1.0f - t; // linear fade

        if (ring_alpha > 0.01f) {
            uint32_t base = static_cast<uint32_t>(out_verts.size());
            for (int c = 0; c < 4; c++) {
                ParticleVertex v;
                v.center[0] = e.position.X;
                v.center[1] = e.position.Y;
                v.center[2] = e.position.Z;
                v.corner[0] = corners[c][0];
                v.corner[1] = corners[c][1];
                // Solid yellow-orange ring
                v.color[0]  = 1.0f;
                v.color[1]  = 0.75f;
                v.color[2]  = 0.1f;
                v.color[3]  = ring_alpha;
                v.params[0] = ring_size;
                v.params[1] = 0.0f;  // type = ring
                out_verts.push_back(v);
            }
            out_indices.push_back(base + 0);
            out_indices.push_back(base + 1);
            out_indices.push_back(base + 2);
            out_indices.push_back(base + 0);
            out_indices.push_back(base + 2);
            out_indices.push_back(base + 3);
            count++;
        }
    }

    return count;
}
