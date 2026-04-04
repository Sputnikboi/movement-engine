#pragma once

#include "vendor/HandmadeMath.h"
#include <cstdint>

// ============================================================
//  Simple entity types — no ECS, just tagged unions.
//  Good enough for a movement shooter.
// ============================================================

enum class EntityType : uint8_t {
    None,
    Drone,
    Projectile,
};

struct Entity {
    EntityType type = EntityType::None;
    bool       alive = false;

    HMM_Vec3 position = {};
    HMM_Vec3 velocity = {};
    float    yaw      = 0.0f;   // facing direction (radians)

    float    health    = 0.0f;
    float    max_health = 0.0f;
    float    radius    = 0.5f;   // collision sphere radius

    // AI state
    uint8_t  ai_state  = 0;
    float    ai_timer   = 0.0f;
    float    ai_timer2  = 0.0f;
    int      ai_dir     = 1;        // circle direction

    // Projectile owner (index of entity that fired it, -1 = player)
    int      owner = -1;
    float    damage = 0.0f;
    float    lifetime = 0.0f;

    // Randomized per-drone
    float    chase_speed  = 10.0f;
    float    circle_speed = 5.0f;
    float    hover_height = 2.0f;
    float    bob_amp      = 0.5f;
    float    bob_freq     = 1.0f;
    float    bob_seed     = 0.0f;

    // Death ragdoll
    float    death_timer  = 0.0f;
};

static constexpr int MAX_ENTITIES = 256;
