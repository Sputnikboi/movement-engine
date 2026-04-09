#pragma once

#include "entity.h"
#include "collision.h"
#include "vendor/HandmadeMath.h"

// ============================================================
//  Shielder AI states
//  Medium enemy that projects a shield barrier for nearby allies.
//  Enemies within shield_radius get a shield_hp barrier that
//  absorbs hits. Must be prioritized by player.
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
    float health            = 60.0f;
    float radius            = 0.6f;

    // Shield aura
    float shield_radius     = 10.0f;  // allies within this get protection
    float shield_hp         = 40.0f;  // barrier HP granted to each ally
    float shield_recharge   = 5.0f;   // HP/s recharge rate while in aura
    float shield_apply_cd   = 3.0f;   // seconds before shield can be reapplied after breaking
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

// Apply shield barriers to allies in range (call once per frame after shielder updates).
// Recharges shield_hp on entities within range of an active shielder.
// Decays shield_hp on entities NOT in range.
void shielder_apply_barriers(Entity entities[], int max_entities,
                             const ShielderConfig& config, float dt);

// Absorb damage through shield barrier. Returns actual HP damage after barrier.
// Modifies entity's shield_hp in place.
float shielder_absorb_damage(Entity& target, float raw_damage);

// Check if an entity currently has an active shield barrier
bool shielder_has_barrier(const Entity& e);

void shielder_tick_frame();
