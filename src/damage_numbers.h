#pragma once

#include "mesh.h"
#include "vendor/HandmadeMath.h"

// ============================================================
//  Floating damage numbers
// ============================================================

struct DamageNumber {
    HMM_Vec3 position;
    float    velocity_y;   // upward drift
    float    lifetime;     // remaining seconds
    float    max_lifetime;
    int      value;        // damage amount (integer display)
    bool     is_kill;      // show as kill (different color)
    bool     active;
};

struct DamageNumberSystem {
    static constexpr int MAX_NUMBERS = 64;
    DamageNumber numbers[MAX_NUMBERS] = {};

    // Spawn a new floating damage number at world position.
    void spawn(HMM_Vec3 pos, int damage, bool is_kill = false);

    // Update positions + lifetimes. Call once per frame.
    void update(float dt);

    // Append billboard digit quads to the entity mesh.
    // cam_right and cam_up are used for billboarding.
    void build_mesh(Mesh& out, HMM_Vec3 cam_right, HMM_Vec3 cam_up) const;
};
