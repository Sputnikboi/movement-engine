#pragma once

#include "entity.h"
#include "collision.h"
#include "vendor/HandmadeMath.h"

// ============================================================
//  Rusher AI states (matches Unity RusherDrone)
// ============================================================

enum RusherState : uint8_t {
    RUSHER_IDLE       = 0,  // wandering, not alerted
    RUSHER_CHASING    = 1,
    RUSHER_CHARGING   = 2,  // braking, winding up for dash
    RUSHER_DASHING    = 3,  // lunging at player
    RUSHER_COOLDOWN   = 4,  // recovering after dash
    RUSHER_DYING      = 5,
    RUSHER_DEAD       = 6,
};

// ============================================================
//  Rusher config
// ============================================================

struct RusherConfig {
    // Detection
    float detection_range   = 25.0f;

    // Movement
    float chase_speed       = 10.0f;
    float acceleration      = 8.0f;
    float hover_height      = 2.0f;
    float hover_force       = 10.0f;

    // Combat
    float health            = 15.0f;
    float melee_damage      = 25.0f;
    float radius            = 0.5f;

    // Lunge attack
    float attack_range      = 8.0f;
    float charge_up_time    = 1.25f;  // seconds of windup before dash
    float braking_force     = 15.0f;  // deceleration during charge
    float dash_force        = 30.0f;  // impulse on dash start
    float dash_duration     = 0.5f;   // max dash time
    float dash_cooldown     = 1.5f;   // recovery after dash

    // Death
    float death_gravity     = 15.0f;
    float death_drag        = 2.0f;
    float death_tumble_speed = 8.0f;
    float death_timeout     = 5.0f;

    // Hit feedback
    float hit_flash_time    = 0.15f;

    // Wander
    float wander_radius     = 6.0f;
    float wander_speed      = 2.5f;
    float wander_pause_min  = 1.0f;
    float wander_pause_max  = 3.0f;
    float wall_avoid_dist   = 2.0f;
    float wall_avoid_force  = 8.0f;

    // Bob (subtle)
    float bob_amp           = 0.2f;
    float bob_freq          = 1.5f;
};

// ============================================================
//  Rusher functions
// ============================================================

int rusher_spawn(Entity entities[], int max_entities,
                 HMM_Vec3 position, const RusherConfig& config);

void rusher_update(Entity& rusher, Entity entities[], int max_entities,
                   HMM_Vec3 player_pos, const CollisionWorld& world,
                   const RusherConfig& config, float dt, float total_time);

// Returns true if rusher hit the player during dash this frame
bool rusher_check_player_hit(Entity& rusher, HMM_Vec3 cap_bottom, HMM_Vec3 cap_top,
                             float player_radius, const RusherConfig& config);

// Call once per frame before rusher updates (advances stagger counter)
void rusher_tick_frame();
