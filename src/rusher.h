#pragma once

#include "entity.h"
#include "collision.h"
#include "vendor/HandmadeMath.h"

// ============================================================
//  Rusher AI states (matches Unity RusherDrone)
// ============================================================

enum RusherState : uint8_t {
    RUSHER_CHASING    = 0,
    RUSHER_CHARGING   = 1,  // braking, winding up for dash
    RUSHER_DASHING    = 2,  // lunging at player
    RUSHER_COOLDOWN   = 3,  // recovering after dash
    RUSHER_DYING      = 4,
    RUSHER_DEAD       = 5,
};

// ============================================================
//  Rusher config
// ============================================================

struct RusherConfig {
    // Movement
    float chase_speed       = 15.0f;
    float acceleration      = 8.0f;
    float hover_height      = 2.0f;
    float hover_force       = 10.0f;

    // Combat
    float health            = 15.0f;
    float melee_damage      = 25.0f;
    float radius            = 0.5f;

    // Lunge attack
    float attack_range      = 5.0f;
    float charge_up_time    = 0.75f;  // seconds of windup before dash
    float braking_force     = 15.0f;  // deceleration during charge
    float dash_force        = 50.0f;  // impulse on dash start
    float dash_duration     = 0.5f;   // max dash time
    float dash_cooldown     = 1.5f;   // recovery after dash

    // Death
    float death_gravity     = 15.0f;
    float death_drag        = 2.0f;
    float death_tumble_speed = 8.0f;
    float death_timeout     = 5.0f;

    // Hit feedback
    float hit_flash_time    = 0.15f;

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
bool rusher_check_player_hit(Entity& rusher, HMM_Vec3 player_pos,
                             float player_radius, const RusherConfig& config);
