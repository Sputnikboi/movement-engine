#pragma once

#include "entity.h"
#include "collision.h"
#include "vendor/HandmadeMath.h"

// ============================================================
//  Turret AI states
//  Stationary enemy with long-range hitscan and visible windup.
// ============================================================

enum TurretState : uint8_t {
    TURRET_IDLE     = 0,  // scanning, not alerted
    TURRET_TRACKING = 1,  // rotating towards player
    TURRET_WINDUP   = 2,  // charging shot (visible laser)
    TURRET_FIRING   = 3,  // hitscan burst
    TURRET_COOLDOWN = 4,  // post-fire recovery
    TURRET_DYING    = 5,
    TURRET_DEAD     = 6,
};

// ============================================================
//  Turret config
// ============================================================

struct TurretConfig {
    // Detection
    float detection_range   = 40.0f;
    float lose_range        = 50.0f;  // deaggro distance

    // Stats
    float health            = 10.0f;
    float radius            = 0.7f;
    float hover_height      = 1.5f;   // sits low to ground
    float hover_force       = 10.0f;

    // Combat
    float beam_dps          = 30.0f;  // damage per second while firing
    float windup_time       = 1.2f;   // visible laser before shot
    float burst_count_f     = 3.0f;   // shots per burst (float for slider)
    float burst_interval    = 0.12f;  // time between burst shots
    float cooldown_time     = 2.5f;   // time between bursts
    float accuracy          = 0.97f;  // 1.0 = perfect, lower = more spread
    float track_speed       = 1.2f;   // radians/s rotation speed

    // Death
    float death_gravity     = 15.0f;
    float death_drag        = 2.0f;
    float death_tumble_speed = 4.0f;  // slow tumble (heavy)
    float death_timeout     = 5.0f;

    // Hit feedback
    float hit_flash_time    = 0.15f;

    // Wander (idle scan)
    float scan_speed        = 0.8f;   // radians/s idle rotation
    float wall_avoid_dist   = 2.0f;
    float wall_avoid_force  = 8.0f;
};

// ============================================================
//  Turret functions
// ============================================================

int turret_spawn(Entity entities[], int max_entities,
                 HMM_Vec3 position, const TurretConfig& config);

void turret_update(Entity& turret, Entity entities[], int max_entities,
                   HMM_Vec3 player_pos, const CollisionWorld& world,
                   const TurretConfig& config, float dt, float total_time);

// Returns true if turret's beam is hitting the player this frame.
// damage_out = beam_dps * dt (caller provides dt).
bool turret_check_player_hit(Entity& turret, HMM_Vec3 cap_bottom, HMM_Vec3 cap_top,
                             float player_radius, const CollisionWorld& world,
                             const TurretConfig& config, float& damage_out);

void turret_tick_frame();

// Get the laser origin and direction for rendering the windup laser
bool turret_get_laser(const Entity& turret, HMM_Vec3& origin, HMM_Vec3& dir);
