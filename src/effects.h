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
//  Single particle
// ============================================================

struct Particle {
    HMM_Vec3 position;
    HMM_Vec3 velocity;
    HMM_Vec3 color;         // RGB
    float    alpha;
    float    size;
    float    size_start;
    float    size_end;
    float    lifetime;       // total lifetime
    float    age;            // current age
    float    type;           // 0 = ring, 1 = particle blob
    bool     alive;
};

// ============================================================
//  Effect system
// ============================================================

static constexpr int MAX_PARTICLES = 1024;

struct EffectSystem {
    Particle particles[MAX_PARTICLES];

    void init();
    void update(float dt);

    // Spawn a drone explosion at position
    void spawn_drone_explosion(HMM_Vec3 pos);

    // Build vertex data for rendering
    // Returns number of vertices (always multiple of 4, each particle = 4 verts)
    int build_vertices(std::vector<ParticleVertex>& out_verts,
                       std::vector<uint32_t>& out_indices) const;

private:
    int find_free() const;
    void spawn(HMM_Vec3 pos, HMM_Vec3 vel, HMM_Vec3 color,
               float size_start, float size_end,
               float lifetime, float type);
};
