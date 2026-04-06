#pragma once

#include "entity.h"
#include "collision.h"
#include "vendor/HandmadeMath.h"

// ============================================================
//  Tank AI states
//  Big, slow, high HP. Chases player, does ground stomp AoE.
// ============================================================

enum TankState : uint8_t {
    TANK_IDLE     = 0,
    TANK_CHASING  = 1,
    TANK_WINDUP   = 2,  // rearing up for ground stomp
    TANK_STOMP    = 3,  // stomp impact frame
    TANK_COOLDOWN = 4,
    TANK_DYING    = 5,
    TANK_DEAD     = 6,
};

// ============================================================
//  Tank config
// ============================================================

struct TankConfig {
    // Detection
    float detection_range   = 30.0f;

    // Movement
    float chase_speed       = 5.0f;   // slow but relentless
    float acceleration      = 4.0f;
    float hover_height      = 2.0f;
    float hover_force       = 15.0f;

    // Stats
    float health            = 80.0f;
    float radius            = 1.2f;   // big boy

    // Ground stomp
    float stomp_range       = 6.0f;   // triggers stomp when within this
    float stomp_aoe_radius  = 8.0f;   // damage falloff radius
    float stomp_damage      = 20.0f;  // max damage at center
    float stomp_knockback   = 1.5f;   // upward + outward force on player
    float windup_time       = 0.8f;   // wind up before stomp
    float stomp_cooldown    = 3.0f;

    // Death
    float death_gravity     = 15.0f;
    float death_drag        = 1.5f;
    float death_tumble_speed = 3.0f;  // slow tumble (very heavy)
    float death_timeout     = 5.0f;

    // Hit feedback
    float hit_flash_time    = 0.15f;

    // Wander
    float wander_radius     = 8.0f;
    float wander_speed      = 2.0f;
    float wander_pause_min  = 2.0f;
    float wander_pause_max  = 4.0f;
    float wall_avoid_dist   = 3.0f;
    float wall_avoid_force  = 10.0f;

    // Bob (heavy, subtle)
    float bob_amp           = 0.15f;
    float bob_freq          = 0.8f;
};

// ============================================================
//  Tank functions
// ============================================================

int tank_spawn(Entity entities[], int max_entities,
               HMM_Vec3 position, const TankConfig& config);

void tank_update(Entity& tank, Entity entities[], int max_entities,
                 HMM_Vec3 player_pos, const CollisionWorld& world,
                 const TankConfig& config, float dt, float total_time);

// Returns true if tank's ground stomp hit the player this frame.
// knockback_out is the force to apply to player.
bool tank_check_player_hit(Entity& tank, HMM_Vec3 cap_bottom, HMM_Vec3 cap_top,
                           float player_radius, const TankConfig& config,
                           float& damage_out, HMM_Vec3& knockback_out);

void tank_tick_frame();
