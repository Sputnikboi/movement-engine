#include "rusher.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>

// randf is defined in drone.cpp
extern float randf(float lo, float hi);

// ============================================================
//  Spawn
// ============================================================

static void pick_rusher_wander(Entity& e, float radius) {
    float angle = randf(0.0f, 6.2831853f);
    float dist  = randf(1.0f, radius);
    e.wander_target = HMM_V3(
        e.spawn_pos.X + cosf(angle) * dist,
        e.spawn_pos.Y,
        e.spawn_pos.Z + sinf(angle) * dist
    );
}

static HMM_Vec3 rusher_wall_avoid(const Entity& e, HMM_Vec3 desired_dir,
                                   const CollisionWorld& world,
                                   float avoid_dist, float avoid_force) {
    HMM_Vec3 steer = HMM_V3(0, 0, 0);
    float hlen = sqrtf(desired_dir.X * desired_dir.X + desired_dir.Z * desired_dir.Z);
    if (hlen < 0.01f) return steer;
    HMM_Vec3 fwd = HMM_V3(desired_dir.X / hlen, 0, desired_dir.Z / hlen);
    HMM_Vec3 right = HMM_V3(fwd.Z, 0, -fwd.X);

    HMM_Vec3 dirs[3] = {
        fwd,
        HMM_NormV3(HMM_AddV3(fwd, HMM_MulV3F(right, 0.7f))),
        HMM_NormV3(HMM_SubV3(fwd, HMM_MulV3F(right, 0.7f))),
    };
    for (int i = 0; i < 3; i++) {
        HitResult hit = world.raycast(e.position, dirs[i], avoid_dist);
        if (hit.hit) {
            float strength = (1.0f - hit.t / avoid_dist) * avoid_force;
            steer = HMM_SubV3(steer, HMM_MulV3F(dirs[i], strength));
        }
    }
    return steer;
}

int rusher_spawn(Entity entities[], int max_entities,
                 HMM_Vec3 position, const RusherConfig& config) {
    for (int i = 0; i < max_entities; i++) {
        if (!entities[i].alive) {
            Entity& e = entities[i];
            e = Entity{};
            e.type       = EntityType::Rusher;
            e.alive      = true;
            e.position   = position;
            e.velocity   = HMM_V3(0, 0, 0);
            e.health     = config.health;
            e.max_health = config.health;
            e.radius     = config.radius;
            e.ai_state   = RUSHER_IDLE;
            e.ai_timer   = 0.0f;
            e.ai_timer2  = 0.0f;  // used as "has dealt damage this dash" flag
            e.owner      = -1;

            e.hover_height = config.hover_height;
            e.bob_amp      = config.bob_amp;
            e.bob_freq     = config.bob_freq;
            e.bob_seed     = randf(0.0f, 100.0f);
            e.chase_speed  = config.chase_speed;

            e.death_timer  = 0.0f;
            e.hit_flash    = 0.0f;
            e.angular_vel  = HMM_V3(0, 0, 0);
            e.tumble_x     = 0.0f;
            e.tumble_z     = 0.0f;

            e.spawn_pos    = position;
            e.wander_timer = randf(0.5f, 2.0f);
            pick_rusher_wander(e, config.wander_radius);

            return i;
        }
    }
    return -1;
}

// ============================================================
//  Hover (shared with drone, but simpler — no bob randomization)
// ============================================================

static void apply_rusher_hover(Entity& r, const CollisionWorld& world,
                               const RusherConfig& config, float dt, float total_time) {
    HMM_Vec3 ray_origin = r.position;
    HMM_Vec3 ray_dir    = HMM_V3(0, -1, 0);
    float    max_dist   = r.hover_height * 3.0f;

    HitResult hit = world.raycast(ray_origin, ray_dir, max_dist);
    if (hit.hit) {
        float height_error = r.hover_height - hit.t;
        float force = height_error * config.hover_force - r.velocity.Y * 2.0f;
        r.velocity.Y += force * dt;
    } else {
        r.velocity.Y -= 3.0f * dt;
    }

    // Subtle bob
    float bob = sinf(total_time * r.bob_freq * 6.28f + r.bob_seed) * r.bob_amp;
    r.velocity.Y += bob * dt * 2.0f;
}

// ============================================================
//  Rusher AI update
// ============================================================

void rusher_update(Entity& rusher, Entity entities[], int max_entities,
                   HMM_Vec3 player_pos, const CollisionWorld& world,
                   const RusherConfig& config, float dt, float total_time) {
    if (!rusher.alive || rusher.type != EntityType::Rusher) return;

    // Hit flash decay
    if (rusher.hit_flash > 0.0f)
        rusher.hit_flash -= dt;

    // ------ DYING state (ragdoll — same as drone) ------
    if (rusher.ai_state == RUSHER_DYING) {
        rusher.death_timer += dt;

        rusher.velocity.Y -= config.death_gravity * dt;
        float drag = expf(-config.death_drag * dt);
        rusher.velocity.X *= drag;
        rusher.velocity.Z *= drag;

        rusher.tumble_x += rusher.angular_vel.X * dt;
        rusher.tumble_z += rusher.angular_vel.Z * dt;

        HMM_Vec3 vel_before = rusher.velocity;
        float speed_before = HMM_LenV3(vel_before);
        rusher.position = world.slide_move(rusher.position, rusher.radius,
                                           rusher.velocity, dt);
        float speed_after = HMM_LenV3(rusher.velocity);

        bool was_falling = vel_before.Y < -1.0f;
        float speed_lost = speed_before - speed_after;
        if (was_falling && speed_lost > speed_before * 0.4f) {
            rusher.ai_state = RUSHER_DEAD;
            rusher.alive = false;
            return;
        }

        if (rusher.death_timer > config.death_timeout) {
            rusher.ai_state = RUSHER_DEAD;
            rusher.alive = false;
        }
        return;
    }

    // ------ Live AI ------
    HMM_Vec3 to_player = HMM_SubV3(player_pos, rusher.position);
    float dist = HMM_LenV3(to_player);
    HMM_Vec3 dir_to_player = (dist > 0.01f)
        ? HMM_MulV3F(to_player, 1.0f / dist)
        : HMM_V3(0, 0, 1);

    HMM_Vec3 desired_hvel = HMM_V3(0, 0, 0);

    switch (rusher.ai_state) {
    case RUSHER_IDLE: {
        apply_rusher_hover(rusher, world, config, dt, total_time);

        HMM_Vec3 to_target = HMM_SubV3(rusher.wander_target, rusher.position);
        float tdist = sqrtf(to_target.X * to_target.X + to_target.Z * to_target.Z);

        if (tdist < 1.0f) {
            rusher.wander_timer -= dt;
            if (rusher.wander_timer <= 0.0f) {
                pick_rusher_wander(rusher, config.wander_radius);
                rusher.wander_timer = randf(config.wander_pause_min, config.wander_pause_max);
            }
        } else {
            float hlen = sqrtf(to_target.X * to_target.X + to_target.Z * to_target.Z);
            if (hlen > 0.01f) {
                desired_hvel = HMM_V3(to_target.X / hlen * config.wander_speed,
                                      0,
                                      to_target.Z / hlen * config.wander_speed);
            }
            rusher.yaw = atan2f(to_target.X, to_target.Z);
        }

        if (dist <= config.detection_range) {
            rusher.ai_state = RUSHER_CHASING;
        }
        break;
    }

    case RUSHER_CHASING: {
        rusher.yaw = atan2f(dir_to_player.X, dir_to_player.Z);
        apply_rusher_hover(rusher, world, config, dt, total_time);

        if (dist <= config.attack_range) {
            // Start charging
            rusher.ai_state = RUSHER_CHARGING;
            rusher.ai_timer = config.charge_up_time;
            break;
        }

        {
            float hlen = sqrtf(dir_to_player.X * dir_to_player.X +
                               dir_to_player.Z * dir_to_player.Z);
            if (hlen > 0.01f) {
                desired_hvel = HMM_V3(dir_to_player.X / hlen * config.chase_speed,
                                       0,
                                       dir_to_player.Z / hlen * config.chase_speed);
            }
        }
        break;
    }

    case RUSHER_CHARGING: {
        rusher.yaw = atan2f(dir_to_player.X, dir_to_player.Z);
        // Hover + brake to near-stop during windup
        apply_rusher_hover(rusher, world, config, dt, total_time);

        // Braking force: decelerate horizontal velocity
        float hspeed = sqrtf(rusher.velocity.X * rusher.velocity.X +
                             rusher.velocity.Z * rusher.velocity.Z);
        if (hspeed > 0.1f) {
            float brake = fminf(config.braking_force * dt, hspeed);
            float scale = (hspeed - brake) / hspeed;
            rusher.velocity.X *= scale;
            rusher.velocity.Z *= scale;
        }

        rusher.ai_timer -= dt;
        if (rusher.ai_timer <= 0.0f) {
            // DASH! Apply impulse toward player
            rusher.ai_state = RUSHER_DASHING;
            rusher.ai_timer = config.dash_duration;
            rusher.ai_timer2 = 0.0f; // has_dealt_damage = false

            HMM_Vec3 dash_dir = HMM_NormV3(HMM_SubV3(player_pos, rusher.position));
            rusher.velocity = HMM_MulV3F(dash_dir, config.dash_force);
        }
        break;
    }

    case RUSHER_DASHING: {
        // No hover during dash — let it fly
        rusher.ai_timer -= dt;

        // Check if dash expired or hit a wall (speed dropped significantly)
        float speed = HMM_LenV3(rusher.velocity);
        if (rusher.ai_timer <= 0.0f || speed < config.dash_force * 0.1f) {
            rusher.ai_state = RUSHER_COOLDOWN;
            rusher.ai_timer = config.dash_cooldown;
        }
        break;
    }

    case RUSHER_COOLDOWN: {
        // Hover + gentle braking
        apply_rusher_hover(rusher, world, config, dt, total_time);

        float hspeed = sqrtf(rusher.velocity.X * rusher.velocity.X +
                             rusher.velocity.Z * rusher.velocity.Z);
        if (hspeed > 0.1f) {
            float brake = fminf(config.braking_force * 0.5f * dt, hspeed);
            float scale = (hspeed - brake) / hspeed;
            rusher.velocity.X *= scale;
            rusher.velocity.Z *= scale;
        }

        rusher.ai_timer -= dt;
        if (rusher.ai_timer <= 0.0f) {
            rusher.ai_state = RUSHER_CHASING;
        }
        break;
    }
    }

    // Wall avoidance (not during dash)
    if (rusher.ai_state != RUSHER_DASHING && HMM_LenV3(desired_hvel) > 0.1f) {
        HMM_Vec3 avoid = rusher_wall_avoid(rusher, desired_hvel, world,
                                            config.wall_avoid_dist, config.wall_avoid_force);
        desired_hvel = HMM_AddV3(desired_hvel, HMM_MulV3F(avoid, dt));
    }

    // Apply acceleration (not during dash/charge which handle velocity directly)
    if (rusher.ai_state == RUSHER_IDLE || rusher.ai_state == RUSHER_CHASING) {
        HMM_Vec3 diff = HMM_SubV3(desired_hvel,
                                    HMM_V3(rusher.velocity.X, 0, rusher.velocity.Z));
        float accel = fminf(config.acceleration * dt, 1.0f);
        rusher.velocity.X += diff.X * accel;
        rusher.velocity.Z += diff.Z * accel;
    }

    // Move with world collision
    rusher.position = world.slide_move(rusher.position, rusher.radius,
                                       rusher.velocity, dt);
}

// ============================================================
//  Check if dashing rusher hit the player
// ============================================================

bool rusher_check_player_hit(Entity& rusher, HMM_Vec3 player_pos,
                             float player_radius, const RusherConfig& config) {
    if (rusher.ai_state != RUSHER_DASHING) return false;
    if (rusher.ai_timer2 > 0.5f) return false; // already dealt damage this dash

    HMM_Vec3 delta = HMM_SubV3(player_pos, rusher.position);
    float dist = HMM_LenV3(delta);
    float min_dist = player_radius + rusher.radius;
    if (dist < min_dist) {
        rusher.ai_timer2 = 1.0f; // flag: dealt damage
        // Transition to cooldown on hit
        rusher.ai_state = RUSHER_COOLDOWN;
        rusher.ai_timer = config.dash_cooldown;
        return true;
    }
    return false;
}
