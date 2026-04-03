#include "effects.h"
#include "drone.h"   // for randf
#include <cmath>
#include <cstring>

void EffectSystem::init() {
    memset(particles, 0, sizeof(particles));
}

int EffectSystem::find_free() const {
    for (int i = 0; i < MAX_PARTICLES; i++)
        if (!particles[i].alive) return i;
    return -1;
}

void EffectSystem::spawn(HMM_Vec3 pos, HMM_Vec3 vel, HMM_Vec3 color,
                         float size_start, float size_end,
                         float lifetime, float type)
{
    int idx = find_free();
    if (idx < 0) return;

    Particle& p = particles[idx];
    p.position   = pos;
    p.velocity   = vel;
    p.color      = color;
    p.alpha      = 1.0f;
    p.size       = size_start;
    p.size_start = size_start;
    p.size_end   = size_end;
    p.lifetime   = lifetime;
    p.age        = 0.0f;
    p.type       = type;
    p.alive      = true;
}

// ============================================================
//  Drone explosion: ring + scattered particles
// ============================================================

void EffectSystem::spawn_drone_explosion(HMM_Vec3 pos) {
    // --- Expanding ring ---
    spawn(pos, HMM_V3(0, 0, 0),
          HMM_V3(1.0f, 0.5f, 0.1f),   // orange tint
          0.3f, 3.0f,                   // start small, expand to 3m
          0.8f,                         // lifetime
          0.0f);                        // type = ring

    // --- Core flash ---
    spawn(pos, HMM_V3(0, 0, 0),
          HMM_V3(1.0f, 0.9f, 0.6f),   // bright warm white
          0.5f, 0.1f,                   // start big, shrink
          0.3f,                         // short lifetime
          1.0f);                        // type = particle

    // --- Scattered debris particles (8-12) ---
    int count = 8 + static_cast<int>(randf(0, 4));
    for (int i = 0; i < count; i++) {
        // Random direction
        float theta = randf(0, 6.283f);
        float phi   = randf(-0.5f, 1.0f);
        float speed = randf(3.0f, 8.0f);

        HMM_Vec3 vel = HMM_V3(
            cosf(theta) * cosf(phi) * speed,
            sinf(phi) * speed * 0.5f + 2.0f,  // bias upward
            sinf(theta) * cosf(phi) * speed
        );

        // Color variation: orange to red
        HMM_Vec3 col = HMM_V3(
            randf(0.8f, 1.0f),
            randf(0.2f, 0.6f),
            randf(0.0f, 0.15f)
        );

        float sz = randf(0.15f, 0.4f);
        float life = randf(0.5f, 1.2f);

        spawn(pos, vel, col, sz, sz * 0.3f, life, 1.0f);
    }

    // --- Smoke-ish particles (slower, darker, longer lived) ---
    for (int i = 0; i < 4; i++) {
        float theta = randf(0, 6.283f);
        float speed = randf(0.5f, 2.0f);

        HMM_Vec3 vel = HMM_V3(
            cosf(theta) * speed,
            randf(1.0f, 3.0f),
            sinf(theta) * speed
        );

        HMM_Vec3 col = HMM_V3(0.4f, 0.25f, 0.1f);  // dark orange/brown
        float sz = randf(0.3f, 0.6f);

        spawn(pos, vel, col, sz, sz * 1.5f, randf(0.8f, 1.5f), 1.0f);
    }
}

// ============================================================
//  Update all particles
// ============================================================

void EffectSystem::update(float dt) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle& p = particles[i];
        if (!p.alive) continue;

        p.age += dt;
        if (p.age >= p.lifetime) {
            p.alive = false;
            continue;
        }

        float t = p.age / p.lifetime;  // 0 to 1

        // Move
        p.position = HMM_AddV3(p.position, HMM_MulV3F(p.velocity, dt));

        // Gravity on debris particles (not rings)
        if (p.type > 0.5f) {
            p.velocity.Y -= 8.0f * dt;
        }

        // Drag
        p.velocity = HMM_MulV3F(p.velocity, 1.0f - 1.5f * dt);

        // Interpolate size
        p.size = p.size_start + (p.size_end - p.size_start) * t;

        // Fade out (quick in last 30%)
        if (t > 0.7f) {
            p.alpha = 1.0f - (t - 0.7f) / 0.3f;
        } else {
            p.alpha = 1.0f;
        }
    }
}

// ============================================================
//  Build vertex data for GPU upload
//  Each particle = 1 quad = 4 vertices + 6 indices
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
    for (int i = 0; i < MAX_PARTICLES; i++) {
        const Particle& p = particles[i];
        if (!p.alive) continue;

        uint32_t base = static_cast<uint32_t>(out_verts.size());

        for (int c = 0; c < 4; c++) {
            ParticleVertex v;
            v.center[0] = p.position.X;
            v.center[1] = p.position.Y;
            v.center[2] = p.position.Z;
            v.corner[0] = corners[c][0];
            v.corner[1] = corners[c][1];
            v.color[0]  = p.color.X;
            v.color[1]  = p.color.Y;
            v.color[2]  = p.color.Z;
            v.color[3]  = p.alpha;
            v.params[0] = p.size;
            v.params[1] = p.type;
            out_verts.push_back(v);
        }

        // Two triangles per quad
        out_indices.push_back(base + 0);
        out_indices.push_back(base + 1);
        out_indices.push_back(base + 2);
        out_indices.push_back(base + 0);
        out_indices.push_back(base + 2);
        out_indices.push_back(base + 3);

        count++;
    }

    return count;
}
