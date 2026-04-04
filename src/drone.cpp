#include "drone.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>

// ============================================================
//  Helpers
// ============================================================

float randf(float lo, float hi) {
    return lo + (float)rand() / (float)RAND_MAX * (hi - lo);
}

// ============================================================
//  Spawn
// ============================================================

int drone_spawn(Entity entities[], int max_entities,
                HMM_Vec3 position, const DroneConfig& config) {
    for (int i = 0; i < max_entities; i++) {
        if (!entities[i].alive) {
            Entity& e = entities[i];
            e = Entity{};
            e.type       = EntityType::Drone;
            e.alive      = true;
            e.position   = position;
            e.velocity   = HMM_V3(0, 0, 0);
            e.health     = config.drone_health;
            e.max_health = config.drone_health;
            e.radius     = config.drone_radius;
            e.ai_state   = DRONE_CHASING;
            e.ai_timer   = 0.0f;
            e.ai_timer2  = 0.0f;
            e.ai_dir     = (rand() % 2) ? 1 : -1;
            e.owner      = -1;

            // Per-drone randomization
            e.chase_speed  = randf(config.chase_speed_min,  config.chase_speed_max);
            e.circle_speed = randf(config.circle_speed_min, config.circle_speed_max);
            e.hover_height = randf(config.hover_height_min, config.hover_height_max);
            e.bob_amp      = randf(config.bob_amp_min,      config.bob_amp_max);
            e.bob_freq     = randf(config.bob_freq_min,     config.bob_freq_max);
            e.bob_seed     = randf(0.0f, 100.0f);

            e.death_timer  = 0.0f;
            e.hit_flash    = 0.0f;
            e.angular_vel  = HMM_V3(0, 0, 0);
            e.tumble_x     = 0.0f;
            e.tumble_z     = 0.0f;

            return i;
        }
    }
    return -1;
}

// ============================================================
//  State transitions
// ============================================================

static void switch_state(Entity& d, uint8_t new_state, const DroneConfig& config) {
    d.ai_state = new_state;
    switch (new_state) {
    case DRONE_CIRCLING:
        d.ai_timer = randf(config.circle_dur_min, config.circle_dur_max);
        d.ai_dir   = (rand() % 2) ? 1 : -1;
        break;
    case DRONE_ATTACKING:
        d.ai_timer = config.attack_windup;
        break;
    default:
        break;
    }
}

// ============================================================
//  Shoot projectile at player
// ============================================================

static void shoot_at_player(Entity& drone, Entity entities[], int max_entities,
                            HMM_Vec3 player_pos, const DroneConfig& config, int drone_idx) {
    for (int i = 0; i < max_entities; i++) {
        if (!entities[i].alive) {
            Entity& p = entities[i];
            p = Entity{};
            p.type      = EntityType::Projectile;
            p.alive     = true;
            p.position  = drone.position;
            p.radius    = 0.15f;
            p.damage    = config.projectile_damage;
            p.owner     = drone_idx;
            p.lifetime  = 5.0f;

            HMM_Vec3 dir = HMM_NormV3(HMM_SubV3(player_pos, drone.position));
            p.velocity = HMM_MulV3F(dir, config.projectile_speed);
            return;
        }
    }
}

// ============================================================
//  Hover: raycast down, spring force to maintain height + bob
// ============================================================

static void apply_hover(Entity& d, const CollisionWorld& world,
                        const DroneConfig& config, float dt, float total_time) {
    HMM_Vec3 ray_origin = d.position;
    HMM_Vec3 ray_dir    = HMM_V3(0, -1, 0);
    float    max_dist   = d.hover_height * 3.0f;

    HitResult hit = world.raycast(ray_origin, ray_dir, max_dist);
    if (hit.hit) {
        float height_error = d.hover_height - hit.t;
        // Spring force + damping
        float force = height_error * config.hover_force - d.velocity.Y * 2.0f;
        d.velocity.Y += force * dt;
    } else {
        // No ground below — gentle gravity to find ground
        d.velocity.Y -= 3.0f * dt;
    }

    // Bob
    float bob = sinf(total_time * d.bob_freq * 6.28f + d.bob_seed) * d.bob_amp;
    d.velocity.Y += bob * dt * 2.0f;
}

// ============================================================
//  Drone AI update
// ============================================================

void drone_update(Entity& drone, Entity entities[], int max_entities,
                  HMM_Vec3 player_pos, const CollisionWorld& world,
                  const DroneConfig& config, float dt, float total_time) {
    if (!drone.alive || drone.type != EntityType::Drone) return;

    // Hit flash decay
    if (drone.hit_flash > 0.0f)
        drone.hit_flash -= dt;

    // Figure out drone index for projectile ownership
    int drone_idx = -1;
    for (int i = 0; i < max_entities; i++) {
        if (&entities[i] == &drone) { drone_idx = i; break; }
    }

    // ------ DYING state (ragdoll) ------
    if (drone.ai_state == DRONE_DYING) {
        drone.death_timer += dt;

        // Gravity
        drone.velocity.Y -= config.death_gravity * dt;

        // Drag on horizontal velocity
        float drag = expf(-config.death_drag * dt);
        drone.velocity.X *= drag;
        drone.velocity.Z *= drag;

        // Accumulate tumble rotation
        drone.tumble_x += drone.angular_vel.X * dt;
        drone.tumble_z += drone.angular_vel.Z * dt;

        // Move with world collision
        HMM_Vec3 vel_before = drone.velocity;
        float speed_before = HMM_LenV3(vel_before);
        drone.position = world.slide_move(drone.position, drone.radius,
                                          drone.velocity, dt);
        float speed_after = HMM_LenV3(drone.velocity);

        // Detect ground/slope impact: was falling (negative Y) and
        // slide_move absorbed significant speed (works on slopes too,
        // where Y doesn't go to zero but speed gets redirected/lost)
        bool was_falling = vel_before.Y < -1.0f;
        float speed_lost = speed_before - speed_after;
        if (was_falling && speed_lost > speed_before * 0.4f) {
            drone.ai_state = DRONE_DEAD;
            drone.alive = false;
            return;
        }

        // Timeout
        if (drone.death_timer > config.death_timeout) {
            drone.ai_state = DRONE_DEAD;
            drone.alive = false;
        }
        return;
    }

    // ------ Live AI ------
    HMM_Vec3 to_player = HMM_SubV3(player_pos, drone.position);
    float dist = HMM_LenV3(to_player);
    HMM_Vec3 dir_to_player = (dist > 0.01f)
        ? HMM_MulV3F(to_player, 1.0f / dist)
        : HMM_V3(0, 0, 1);

    // Face player (yaw only)
    drone.yaw = atan2f(dir_to_player.X, dir_to_player.Z);

    // Horizontal desired velocity (set per-state)
    HMM_Vec3 desired_hvel = HMM_V3(0, 0, 0);

    switch (drone.ai_state) {
    case DRONE_CHASING:
        if (dist <= config.attack_range) {
            switch_state(drone, DRONE_CIRCLING, config);
        } else {
            // Normalize horizontal direction
            float hlen = sqrtf(dir_to_player.X * dir_to_player.X +
                               dir_to_player.Z * dir_to_player.Z);
            if (hlen > 0.01f) {
                desired_hvel = HMM_V3(dir_to_player.X / hlen * drone.chase_speed,
                                      0,
                                      dir_to_player.Z / hlen * drone.chase_speed);
            }
        }
        break;

    case DRONE_CIRCLING: {
        if (dist > config.attack_range) {
            switch_state(drone, DRONE_CHASING, config);
            break;
        }
        drone.ai_timer -= dt;
        if (drone.ai_timer <= 0.0f) {
            switch_state(drone, DRONE_ATTACKING, config);
            break;
        }

        // Orbit: cross(to_player, up) gives tangent direction
        HMM_Vec3 orbit_dir = HMM_NormV3(HMM_Cross(to_player, HMM_V3(0, 1, 0)));
        orbit_dir = HMM_MulV3F(orbit_dir, (float)drone.ai_dir);

        // Distance correction to maintain circle_distance
        float dist_err = dist - config.circle_distance;
        HMM_Vec3 correction = HMM_MulV3F(
            HMM_V3(dir_to_player.X, 0, dir_to_player.Z), dist_err * 0.5f);

        desired_hvel = HMM_AddV3(
            HMM_MulV3F(orbit_dir, drone.circle_speed), correction);
        break;
    }

    case DRONE_ATTACKING: {
        // Decelerate during windup
        desired_hvel = HMM_V3(0, 0, 0);
        drone.ai_timer -= dt;
        if (drone.ai_timer <= 0.0f) {
            shoot_at_player(drone, entities, max_entities,
                            player_pos, config, drone_idx);
            if (dist > config.attack_range)
                switch_state(drone, DRONE_CHASING, config);
            else
                switch_state(drone, DRONE_CIRCLING, config);
        }
        break;
    }
    }

    // Apply horizontal acceleration toward desired velocity
    HMM_Vec3 hvel_diff = HMM_SubV3(desired_hvel,
                                     HMM_V3(drone.velocity.X, 0, drone.velocity.Z));
    float accel = fminf(config.acceleration * dt, 1.0f);
    drone.velocity.X += hvel_diff.X * accel;
    drone.velocity.Z += hvel_diff.Z * accel;

    // Hover + bob
    apply_hover(drone, world, config, dt, total_time);

    // Move with world collision (slides along walls/floors)
    drone.position = world.slide_move(drone.position, drone.radius,
                                      drone.velocity, dt);
}

// ============================================================
//  Projectile update — move, collide with world
// ============================================================

void projectiles_update(Entity entities[], int max_entities,
                        const CollisionWorld& world, float dt) {
    for (int i = 0; i < max_entities; i++) {
        Entity& p = entities[i];
        if (!p.alive || p.type != EntityType::Projectile) continue;

        p.lifetime -= dt;
        if (p.lifetime <= 0.0f) {
            p.alive = false;
            continue;
        }

        // Check wall collision via raycast along travel direction
        HMM_Vec3 travel = HMM_MulV3F(p.velocity, dt);
        float travel_len = HMM_LenV3(travel);
        if (travel_len > 0.001f) {
            HMM_Vec3 travel_dir = HMM_MulV3F(travel, 1.0f / travel_len);
            HitResult hit = world.raycast(p.position, travel_dir,
                                          travel_len + p.radius);
            if (hit.hit && hit.t <= travel_len + p.radius) {
                p.alive = false;
                continue;
            }
        }

        p.position = HMM_AddV3(p.position, travel);
    }
}
