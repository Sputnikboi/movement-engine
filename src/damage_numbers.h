#pragma once

#include "vendor/HandmadeMath.h"

// ============================================================
//  Floating damage numbers (screen-space UI)
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

    // Draw damage numbers as screen-space UI text.
    // Call during ImGui frame (after NewFrame, before Render).
    void draw_ui(HMM_Mat4 view_proj, float screen_w, float screen_h) const;
};
