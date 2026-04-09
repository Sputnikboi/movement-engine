#pragma once

#include "vendor/HandmadeMath.h"

// ============================================================
//  Floating damage numbers (screen-space UI, stacking)
// ============================================================

struct DamageNumber {
    HMM_Vec3 position;
    float    velocity_y;   // upward drift
    float    lifetime;     // remaining seconds
    float    max_lifetime;
    int      value;        // accumulated damage (integer display)
    int      entity_id;    // entity index this is tracking (-1 = none)
    bool     is_kill;      // show as kill (different color)
    bool     is_poison;    // show as green poison damage
    bool     active;
};

struct DamageNumberSystem {
    static constexpr int MAX_NUMBERS = 64;
    static constexpr float STACK_WINDOW = 1.0f; // seconds to accumulate hits
    DamageNumber numbers[MAX_NUMBERS] = {};

    // Spawn or stack a damage number at world position for a given entity.
    // If entity_id >= 0 and a recent number exists for that entity, stacks.
    void spawn(HMM_Vec3 pos, int damage, int entity_id = -1, bool is_kill = false, bool is_poison = false);

    // Update positions + lifetimes. Call once per frame.
    void update(float dt);

    // Draw damage numbers as screen-space UI text.
    // Call during ImGui frame (after NewFrame, before Render).
    void draw_ui(HMM_Mat4 view_proj, float screen_w, float screen_h) const;
};
