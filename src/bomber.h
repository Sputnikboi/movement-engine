#pragma once

#include "entity.h"
#include "collision.h"
#include "vendor/HandmadeMath.h"

// ============================================================
//  Bomber AI states
//  Flies high, then dive-bombs the player kamikaze-style.
//  Explodes on ground impact in a big red fireball.
// ============================================================

enum BomberState : uint8_t {
    BOMBER_IDLE      = 0,
    BOMBER_APPROACH  = 1,  // flying towards player area at altitude
    BOMBER_DIVING    = 2,  // kamikaze dive towards player
    BOMBER_EXPLODING = 3,  // hit ground/player, exploding
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
    float dive_speed        = 18.0f;  // fast dive
    float acceleration      = 5.0f;
    float hover_height      = 12.0f;  // flies very high
    float hover_force       = 8.0f;

    // Stats
    float health            = 30.0f;
    float radius            = 0.8f;

    // Explosion (on ground impact)
    float explosion_damage  = 15.0f;
    float explosion_radius  = 5.0f;
    float explosion_knockback = 40.0f;

    // Dive trigger
    float dive_trigger_dist = 15.0f;  // starts dive when within this horizontal distance

    // Death (when killed before diving)
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

// Check if bomber just exploded near player. Returns true + damage/knockback.
bool bomber_check_explosion(Entity& bomber, HMM_Vec3 cap_bottom, HMM_Vec3 cap_top,
                            float player_radius, const BomberConfig& config,
                            float& damage_out, HMM_Vec3& knockback_out);

void bomber_tick_frame();
