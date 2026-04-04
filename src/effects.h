#pragma once

#include "vendor/HandmadeMath.h"
#include "mesh.h"
#include <vector>
#include <cstdint>

// ============================================================
//  Particle vertex (kept for the particle pipeline in renderer)
// ============================================================

struct ParticleVertex {
    float center[3];
    float corner[2];
    float color[4];
    float params[2];
};

// ============================================================
//  Death effect: collapsing yellow ball + expanding solid torus ring
// ============================================================

struct DeathEffect {
    HMM_Vec3 position;
    float    lifetime;       // total duration
    float    age;            // current age
    bool     alive;

    // Ball
    float    ball_start_radius;

    // Ring (torus)
    float    ring_max_radius;    // major radius at end
    float    ring_tube_radius;   // minor radius (tube thickness)
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

    // Append opaque death effect geometry (inner core + ring) into entity mesh.
    void append_to_mesh(Mesh& out) const;

    // Append transparent death effect geometry (outer glow) into separate mesh.
    // Rendered through the transparent pipeline (alpha blend, no depth write).
    void append_transparent(Mesh& out) const;
};
