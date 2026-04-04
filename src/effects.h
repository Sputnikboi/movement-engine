#pragma once

#include "vendor/HandmadeMath.h"
#include <vector>
#include <cstdint>

// ============================================================
//  Particle vertex (matches particle.vert layout)
// ============================================================

struct ParticleVertex {
    float center[3];   // location 0: world-space center
    float corner[2];   // location 1: billboard corner (-1 or 1)
    float color[4];    // location 2: RGBA
    float params[2];   // location 3: size, type (0=ring, 1=particle)
};

// ============================================================
//  Death effect: collapsing yellow ball + expanding solid donut ring
// ============================================================

struct DeathEffect {
    HMM_Vec3 position;
    float    lifetime;       // total duration
    float    age;            // current age
    bool     alive;

    // Ball params
    float    ball_start_size;
    float    ball_end_size;   // 0 = fully collapsed

    // Ring params
    float    ring_start_size;
    float    ring_max_size;
    float    ring_thickness;  // visual thickness of the donut
};

// ============================================================
//  Effect system
// ============================================================

static constexpr int MAX_DEATH_EFFECTS = 32;

struct EffectSystem {
    DeathEffect death_effects[MAX_DEATH_EFFECTS];

    void init();
    void update(float dt);

    // Spawn death effect at position
    void spawn_drone_explosion(HMM_Vec3 pos);

    // Build vertex data for rendering
    int build_vertices(std::vector<ParticleVertex>& out_verts,
                       std::vector<uint32_t>& out_indices) const;
};
