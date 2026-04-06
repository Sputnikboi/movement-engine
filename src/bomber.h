#pragma once

#include "entity.h"
#include "collision.h"
#include "vendor/HandmadeMath.h"

// ============================================================
//  Bomber AI states
//  Flies high, circles area, drops projectiles from above.
//  Forces player to keep moving.
// ============================================================

enum BomberState : uint8_t {
    BOMBER_IDLE      = 0,
    BOMBER_APPROACH  = 1,  // flying towards player area
    BOMBER_BOMBING   = 2,  // circling + dropping bombs
    BOMBER_RELOADING = 3,  // out of bombs, retreating briefly
    BOMBER_DYING     = 4,
    BOMBER_DEAD      = 5,
};

// ============================================================
//  Bomber config
// ============================================================

struct BomberConfig {
    // Detection
    float detection_range   = 35.0f;

    // Movement
    float approach_speed    = 8.0f;
    float circle_speed      = 6.0f;
    float circle_radius     = 12.0f;
    float acceleration      = 5.0f;
    float hover_height      = 12.0f;  // flies very high
    float hover_force       = 8.0f;

    // Stats
    float health            = 25.0f;
    float radius            = 0.8f;

    // Bombs
    float bomb_damage       = 15.0f;
    float bomb_interval     = 1.5f;   // seconds between drops
    int   bombs_per_run     = 4;      // bombs before reloading
    float bomb_speed        = 2.0f;   // slight forward velocity on drop
    float bomb_gravity      = 12.0f;  // bombs fall fast
    float bomb_aoe_radius   = 3.0f;   // explosion radius
    float reload_time       = 3.0f;

    // Death
    float death_gravity     = 15.0f;
    float death_drag        = 2.0f;
    float death_tumble_speed = 6.0f;
    float death_timeout     = 5.0f;

    // Hit feedback
    float hit_flash_time    = 0.15f;

    // Wander
    float wander_radius     = 10.0f;
    float wander_speed      = 3.0f;
    float wander_pause_min  = 1.0f;
    float wander_pause_max  = 3.0f;
    float wall_avoid_dist   = 3.0f;
    float wall_avoid_force  = 8.0f;

    // Bob
    float bob_amp           = 0.3f;
    float bob_freq          = 0.7f;

    // Randomization
    float hover_height_min  = 10.0f;
    float hover_height_max  = 14.0f;
};

// ============================================================
//  Bomber functions
// ============================================================

int bomber_spawn(Entity entities[], int max_entities,
                 HMM_Vec3 position, const BomberConfig& config);

void bomber_update(Entity& bomber, Entity entities[], int max_entities,
                   HMM_Vec3 player_pos, const CollisionWorld& world,
                   const BomberConfig& config, float dt, float total_time);

void bomber_tick_frame();
