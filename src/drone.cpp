#include "drone.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>

// ============================================================
//  Utility
// ============================================================

float randf(float lo, float hi) {
    float t = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    return lo + t * (hi - lo);
}

// Simple perlin-ish noise (good enough for bob)
static float noise1d(float x) {
    int xi = static_cast<int>(floorf(x));
    float xf = x - floorf(x);
    // Smooth step
    float t = xf * xf * (3.0f - 2.0f * xf);
    // Hash-based pseudo random at integer points
    auto hash = [](int n) -> float {
        n = (n << 13) ^ n;
        return 1.0f - static_cast<float>((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f;
    };
    return hash(xi) * (1.0f - t) + hash(xi + 1) * t;
}

// ============================================================
//  Find a free entity slot
// ============================================================

static int find_free(Entity entities[], int max_entities) {
    for (int i = 0; i < max_entities; i++)
        if (!entities[i].alive)
            return i;
    return -1;
}

// ============================================================
//  Spawn drone
// ============================================================

int drone_spawn(Entity entities[], int max_entities,
                HMM_Vec3 position, const DroneConfig& cfg)
{
    int idx = find_free(entities, max_entities);
    if (idx < 0) return -1;

    Entity& e = entities[idx];
    e = {};  // reset
    e.type       = EntityType::Drone;
    e.alive      = true;
    e.position   = position;
    e.velocity   = HMM_V3(0, 0, 0);
    e.health     = cfg.drone_health;
    e.max_health = cfg.drone_health;
    e.radius     = cfg.drone_radius;
    e.ai_state   = DRONE_CHASING;
    e.ai_timer   = 0.0f;
    e.ai_dir     = 1;

    // Randomize per-drone
    e.chase_speed  = randf(cfg.chase_speed_min, cfg.chase_speed_max);
    e.circle_speed = randf(cfg.circle_speed_min, cfg.circle_speed_max);
    e.hover_height = randf(cfg.hover_height_min, cfg.hover_height_max);
    e.bob_amp      = randf(cfg.bob_amp_min, cfg.bob_amp_max);
    e.bob_freq     = randf(cfg.bob_freq_min, cfg.bob_freq_max);
    e.bob_seed     = randf(0.0f, 100.0f);

    printf("Drone spawned at (%.1f, %.1f, %.1f) [slot %d]\n",
           position.X, position.Y, position.Z, idx);
    return idx;
}

// ============================================================
//  Spawn projectile (fired by drone toward player)
// ============================================================

static void spawn_projectile(Entity entities[], int max_entities,
                             HMM_Vec3 origin, HMM_Vec3 target,
                             int owner_idx, const DroneConfig& cfg)
{
    int idx = find_free(entities, max_entities);
    if (idx < 0) return;

    HMM_Vec3 dir = HMM_SubV3(target, origin);
    float len = HMM_LenV3(dir);
    if (len < 0.01f) return;
    dir = HMM_MulV3F(dir, 1.0f / len);

    Entity& e = entities[idx];
    e = {};
    e.type     = EntityType::Projectile;
    e.alive    = true;
    e.position = origin;
    e.velocity = HMM_MulV3F(dir, cfg.projectile_speed);
    e.radius   = 0.15f;
    e.owner    = owner_idx;
    e.damage   = cfg.projectile_damage;
    e.lifetime = 5.0f;
}

// ============================================================
//  Hover + bob (applied every tick)
// ============================================================

static void apply_hover_bob(Entity& drone, const CollisionWorld& world,
                            const DroneConfig& cfg, float total_time)
{
    // Raycast down to find ground height
    HitResult hit = world.raycast(drone.position, HMM_V3(0, -1, 0), 20.0f);

    if (hit.hit) {
        float ground_dist = hit.t;
        float height_error = drone.hover_height - ground_dist;
        drone.velocity.Y += height_error * cfg.hover_force * (1.0f / 128.0f);
    }

    // Bob using noise
    float bob = noise1d(total_time * drone.bob_freq + drone.bob_seed);
    drone.velocity.Y += bob * drone.bob_amp * (1.0f / 128.0f);

    // Dampen vertical velocity to prevent oscillation
    drone.velocity.Y *= 0.95f;
}

// ============================================================
//  Look-at: compute yaw toward target
// ============================================================

static float look_at_yaw(HMM_Vec3 from, HMM_Vec3 to) {
    float dx = to.X - from.X;
    float dz = to.Z - from.Z;
    return atan2f(dz, dx);
}

// ============================================================
//  Drone AI update
// ============================================================

void drone_update(Entity& drone, Entity entities[], int max_entities,
                  HMM_Vec3 player_pos, const CollisionWorld& world,
                  const DroneConfig& cfg, float dt, float total_time)
{
    if (!drone.alive || drone.type != EntityType::Drone) return;
    if (drone.ai_state == DRONE_DEAD) return;

    HMM_Vec3 to_player = HMM_SubV3(player_pos, drone.position);
    float dist = HMM_LenV3(to_player);
    HMM_Vec3 dir_to_player = (dist > 0.01f) ? HMM_MulV3F(to_player, 1.0f / dist) : HMM_V3(0, 0, 1);

    // Face the player
    drone.yaw = look_at_yaw(drone.position, player_pos);

    switch (static_cast<DroneState>(drone.ai_state)) {
    case DRONE_CHASING: {
        if (dist <= cfg.attack_range) {
            // Switch to circling
            drone.ai_state = DRONE_CIRCLING;
            drone.ai_timer = randf(cfg.circle_dur_min, cfg.circle_dur_max);
            drone.ai_dir = (randf(0, 1) > 0.5f) ? 1 : -1;
            break;
        }

        // Accelerate toward player (horizontal only)
        HMM_Vec3 desired = HMM_MulV3F(dir_to_player, drone.chase_speed);
        HMM_Vec3 hvel = HMM_V3(drone.velocity.X, 0, drone.velocity.Z);
        HMM_Vec3 force = HMM_MulV3F(HMM_SubV3(desired, hvel), cfg.acceleration * dt);
        drone.velocity.X += force.X;
        drone.velocity.Z += force.Z;
    } break;

    case DRONE_CIRCLING: {
        if (dist > cfg.attack_range * 1.5f) {
            drone.ai_state = DRONE_CHASING;
            break;
        }

        drone.ai_timer -= dt;
        if (drone.ai_timer <= 0.0f) {
            drone.ai_state = DRONE_ATTACKING;
            drone.ai_timer = cfg.attack_windup;
            break;
        }

        // Orbit perpendicular to player direction
        HMM_Vec3 flat_to_player = HMM_V3(to_player.X, 0, to_player.Z);
        float flat_dist = HMM_LenV3(flat_to_player);
        if (flat_dist < 0.1f) break;
        flat_to_player = HMM_MulV3F(flat_to_player, 1.0f / flat_dist);

        // Cross product with up = orbit direction
        HMM_Vec3 orbit_dir = HMM_V3(
            -flat_to_player.Z * static_cast<float>(drone.ai_dir),
            0.0f,
             flat_to_player.X * static_cast<float>(drone.ai_dir)
        );

        // Distance correction: try to maintain circle_distance
        float dist_correction = (flat_dist - cfg.circle_distance) * 0.5f;
        HMM_Vec3 correction = HMM_MulV3F(flat_to_player, dist_correction);

        HMM_Vec3 desired = HMM_AddV3(
            HMM_MulV3F(orbit_dir, drone.circle_speed),
            correction
        );

        HMM_Vec3 hvel = HMM_V3(drone.velocity.X, 0, drone.velocity.Z);
        HMM_Vec3 force = HMM_MulV3F(HMM_SubV3(desired, hvel), cfg.acceleration * dt);
        drone.velocity.X += force.X;
        drone.velocity.Z += force.Z;
    } break;

    case DRONE_ATTACKING: {
        // Decelerate
        drone.velocity.X *= (1.0f - cfg.acceleration * dt * 0.5f);
        drone.velocity.Z *= (1.0f - cfg.acceleration * dt * 0.5f);

        drone.ai_timer -= dt;
        if (drone.ai_timer <= 0.0f) {
            // Fire!
            spawn_projectile(entities, max_entities,
                             drone.position, player_pos,
                             -1, cfg);

            // Transition
            if (dist > cfg.attack_range)
                drone.ai_state = DRONE_CHASING;
            else {
                drone.ai_state = DRONE_CIRCLING;
                drone.ai_timer = randf(cfg.circle_dur_min, cfg.circle_dur_max);
                drone.ai_dir = (randf(0, 1) > 0.5f) ? 1 : -1;
            }
        }
    } break;

    case DRONE_DYING: {
        // Ragdoll: gravity + some tumble, no AI
        drone.velocity.Y -= 15.0f * dt;  // gravity
        drone.velocity.X *= (1.0f - 2.0f * dt);  // drag
        drone.velocity.Z *= (1.0f - 2.0f * dt);
        drone.yaw += 8.0f * dt;  // spin

        drone.position = HMM_AddV3(drone.position, HMM_MulV3F(drone.velocity, dt));

        // Check ground hit
        HitResult ground_hit = world.raycast(drone.position, HMM_V3(0, -1, 0), 0.5f);
        drone.death_timer -= dt;

        if (ground_hit.hit || drone.death_timer <= 0.0f) {
            drone.ai_state = DRONE_DEAD;
            drone.alive = false;
            // Return -1 sentinel that main.cpp checks for explosion spawn
            // (handled externally — we just mark it dead at this position)
        }
        return;  // skip hover/bob
    } break;

    case DRONE_DEAD:
        break;
    }

    // Apply hover and bob (only for alive states)
    apply_hover_bob(drone, world, cfg, total_time);

    // Move
    drone.position = HMM_AddV3(drone.position, HMM_MulV3F(drone.velocity, dt));
}

// ============================================================
//  Projectile update
// ============================================================

void projectiles_update(Entity entities[], int max_entities,
                        const CollisionWorld& world, float dt)
{
    for (int i = 0; i < max_entities; i++) {
        Entity& e = entities[i];
        if (!e.alive || e.type != EntityType::Projectile) continue;

        e.lifetime -= dt;
        if (e.lifetime <= 0.0f) {
            e.alive = false;
            continue;
        }

        // Move
        HMM_Vec3 new_pos = HMM_AddV3(e.position, HMM_MulV3F(e.velocity, dt));

        // Raycast for wall collision
        HMM_Vec3 move_dir = HMM_SubV3(new_pos, e.position);
        float move_len = HMM_LenV3(move_dir);
        if (move_len > 0.001f) {
            HMM_Vec3 dir = HMM_MulV3F(move_dir, 1.0f / move_len);
            HitResult hit = world.raycast(e.position, dir, move_len);
            if (hit.hit) {
                e.alive = false;
                continue;
            }
        }

        e.position = new_pos;
    }
}
