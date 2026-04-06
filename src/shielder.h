#pragma once

#include "entity.h"
#include "collision.h"
#include "vendor/HandmadeMath.h"

// ============================================================
//  Shielder AI states
//  Medium enemy that projects a shield aura for nearby allies.
//  Enemies within shield_radius take reduced damage.
//  Must be prioritized by player.
// ============================================================

enum ShielderState : uint8_t {
    SHIELDER_IDLE       = 0,
    SHIELDER_CHASING    = 1,
    SHIELDER_SHIELDING  = 2,  // near allies, projecting shield
    SHIELDER_FLEEING    = 3,  // player too close, backing off
    SHIELDER_DYING      = 4,
    SHIELDER_DEAD       = 5,
};

// ============================================================
//  Shielder config
// ============================================================

struct ShielderConfig {
    // Detection
    float detection_range   = 30.0f;

    // Movement
    float chase_speed       = 6.0f;
    float flee_speed        = 7.0f;
    float acceleration      = 6.0f;
    float hover_height      = 3.0f;
    float hover_force       = 8.0f;

    // Stats
    float health            = 30.0f;
    float radius            = 0.6f;

    // Shield aura
    float shield_radius     = 10.0f;  // allies within this get protection
    float damage_reduction  = 0.5f;   // 0.5 = allies take 50% damage
    float flee_range        = 6.0f;   // backs off if player gets this close
    float preferred_dist    = 12.0f;  // tries to stay this far from player

    // Death
    float death_gravity     = 15.0f;
    float death_drag        = 2.0f;
    float death_tumble_speed = 6.0f;
    float death_timeout     = 5.0f;

    // Hit feedback
    float hit_flash_time    = 0.15f;

    // Wander
    float wander_radius     = 8.0f;
    float wander_speed      = 3.0f;
    float wander_pause_min  = 1.0f;
    float wander_pause_max  = 3.0f;
    float wall_avoid_dist   = 2.0f;
    float wall_avoid_force  = 8.0f;

    // Bob
    float bob_amp           = 0.4f;
    float bob_freq          = 1.0f;
};

// ============================================================
//  Shielder functions
// ============================================================

int shielder_spawn(Entity entities[], int max_entities,
                   HMM_Vec3 position, const ShielderConfig& config);

void shielder_update(Entity& shielder, Entity entities[], int max_entities,
                     HMM_Vec3 player_pos, const CollisionWorld& world,
                     const ShielderConfig& config, float dt, float total_time);

// Check if an entity at `pos` is within any alive shielder's aura.
// Returns the damage multiplier (1.0 = no shield, lower = shielded).
float shielder_get_damage_mult(const Entity entities[], int max_entities,
                               HMM_Vec3 target_pos, const ShielderConfig& config);

void shielder_tick_frame();
