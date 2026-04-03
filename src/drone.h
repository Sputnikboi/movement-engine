#pragma once

#include "entity.h"
#include "collision.h"
#include "vendor/HandmadeMath.h"

// ============================================================
//  Drone AI states (matches your Unity DroneEnemy)
// ============================================================

enum DroneState : uint8_t {
    DRONE_CHASING   = 0,
    DRONE_CIRCLING  = 1,
    DRONE_ATTACKING = 2,
    DRONE_DEAD      = 3,
};

// ============================================================
//  Drone config (shared by all drones)
// ============================================================

struct DroneConfig {
    float attack_range       = 10.0f;
    float circle_distance    = 8.0f;
    float acceleration       = 5.0f;
    float hover_force        = 5.0f;
    float attack_windup      = 1.0f;
    float circle_dur_min     = 2.0f;
    float circle_dur_max     = 4.0f;
    float projectile_speed   = 20.0f;
    float projectile_damage  = 5.0f;
    float drone_health       = 20.0f;
    float drone_radius       = 0.6f;

    // Randomization ranges
    float chase_speed_min    = 9.0f;
    float chase_speed_max    = 11.0f;
    float circle_speed_min   = 4.0f;
    float circle_speed_max   = 6.0f;
    float hover_height_min   = 1.8f;
    float hover_height_max   = 2.2f;
    float bob_amp_min        = 0.4f;
    float bob_amp_max        = 0.6f;
    float bob_freq_min       = 0.9f;
    float bob_freq_max       = 1.1f;
};

// ============================================================
//  Drone functions
// ============================================================

// Spawn a drone at a position. Returns the entity index, or -1 on failure.
int drone_spawn(Entity entities[], int max_entities,
                HMM_Vec3 position, const DroneConfig& config);

// Update a single drone entity. May spawn a projectile.
void drone_update(Entity& drone, Entity entities[], int max_entities,
                  HMM_Vec3 player_pos, const CollisionWorld& world,
                  const DroneConfig& config, float dt, float total_time);

// Update all projectiles.
void projectiles_update(Entity entities[], int max_entities,
                        const CollisionWorld& world, float dt);

// Simple random float in [lo, hi]
float randf(float lo, float hi);
