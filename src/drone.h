#pragma once

#include "entity.h"
#include "collision.h"
#include "vendor/HandmadeMath.h"

// ============================================================
//  Drone AI states (matches Unity DroneEnemy)
// ============================================================

enum DroneState : uint8_t {
    DRONE_IDLE      = 0,  // wandering, not alerted
    DRONE_CHASING   = 1,
    DRONE_CIRCLING  = 2,
    DRONE_ATTACKING = 3,
    DRONE_DYING     = 4,
    DRONE_DEAD      = 5,
};

// ============================================================
//  Drone config (shared by all drones)
// ============================================================

struct DroneConfig {
    float detection_range    = 30.0f;  // player must be within this to alert
    float attack_range       = 17.0f;
    float circle_distance    = 8.0f;
    float acceleration       = 5.0f;
    float hover_force        = 5.0f;
    float attack_windup      = 1.0f;
    float circle_dur_min     = 2.0f;
    float circle_dur_max     = 4.0f;
    float projectile_speed   = 20.0f;
    float projectile_damage  = 5.0f;
    float drone_health       = 40.0f;
    float drone_radius       = 0.6f;

    // Wander (idle state)
    float wander_radius      = 8.0f;   // max distance from spawn to pick wander targets
    float wander_speed       = 3.0f;
    float wander_pause_min   = 1.0f;   // seconds to wait at each point
    float wander_pause_max   = 3.0f;
    float wall_avoid_dist    = 2.0f;   // raycast distance for wall avoidance
    float wall_avoid_force   = 8.0f;   // steering force away from walls

    // Randomization ranges
    float chase_speed_min    = 9.0f;
    float chase_speed_max    = 11.0f;
    float circle_speed_min   = 4.0f;
    float circle_speed_max   = 6.0f;
    float hover_height_min   = 4.0f;
    float hover_height_max   = 5.0f;
    float bob_amp_min        = 0.4f;
    float bob_amp_max        = 0.6f;
    float bob_freq_min       = 0.9f;
    float bob_freq_max       = 1.1f;

    // Death ragdoll
    float death_gravity      = 15.0f;
    float death_drag         = 2.0f;
    float death_tumble_speed = 8.0f;
    float death_timeout      = 5.0f;  // max time before forced despawn

    // Hit feedback
    float hit_flash_time     = 0.15f; // seconds the drone flashes white on hit
};

// ============================================================
//  Drone functions
// ============================================================

int drone_spawn(Entity entities[], int max_entities,
                HMM_Vec3 position, const DroneConfig& config);

// Update a single drone entity. Uses CollisionWorld for world collision.
void drone_update(Entity& drone, Entity entities[], int max_entities,
                  HMM_Vec3 player_pos, const CollisionWorld& world,
                  const DroneConfig& config, float dt, float total_time);

void projectiles_update(Entity entities[], int max_entities,
                        const CollisionWorld& world, float dt);

// Call once per frame before drone updates (advances stagger counter)
void drone_tick_frame();

float randf(float lo, float hi);
