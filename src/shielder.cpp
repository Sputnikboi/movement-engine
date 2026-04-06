#include "shielder.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>

extern float randf(float lo, float hi);

// ============================================================
//  Helpers
// ============================================================

static void pick_shielder_wander(Entity& e, float radius) {
    float angle = randf(0.0f, 6.2831853f);
    float dist  = randf(1.0f, radius);
    e.wander_target = HMM_V3(
        e.spawn_pos.X + cosf(angle) * dist,
        e.spawn_pos.Y,
        e.spawn_pos.Z + sinf(angle) * dist
    );
}

static HMM_Vec3 shielder_wall_avoid(const Entity& e, HMM_Vec3 desired_dir,
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

// Count nearby allies (non-shielder enemies) for positioning decisions
static int count_nearby_allies(const Entity& shielder, const Entity entities[],
                               int max_entities, float radius) {
    int count = 0;
    for (int i = 0; i < max_entities; i++) {
        const Entity& e = entities[i];
        if (!e.alive) continue;
        if (&e == &shielder) continue;
        if (e.type == EntityType::Projectile) continue;
        if (e.type == EntityType::Shielder) continue;  // don't count other shielders

        float dist = HMM_LenV3(HMM_SubV3(e.position, shielder.position));
        if (dist < radius) count++;
    }
    return count;
}

// Find center of mass of nearby allies
static HMM_Vec3 ally_center(const Entity& shielder, const Entity entities[],
                            int max_entities, float radius) {
    HMM_Vec3 sum = HMM_V3(0, 0, 0);
    int count = 0;
    for (int i = 0; i < max_entities; i++) {
        const Entity& e = entities[i];
        if (!e.alive) continue;
        if (&e == &shielder) continue;
        if (e.type == EntityType::Projectile) continue;
        if (e.type == EntityType::Shielder) continue;

        float dist = HMM_LenV3(HMM_SubV3(e.position, shielder.position));
        if (dist < radius * 2.0f) {
            sum = HMM_AddV3(sum, e.position);
            count++;
        }
    }
    if (count > 0) return HMM_MulV3F(sum, 1.0f / (float)count);
    return shielder.position;
}

// ============================================================
//  Spawn
// ============================================================

int shielder_spawn(Entity entities[], int max_entities,
                   HMM_Vec3 position, const ShielderConfig& config) {
    for (int i = 0; i < max_entities; i++) {
        if (!entities[i].alive) {
            Entity& e = entities[i];
            e = Entity{};
            e.type       = EntityType::Shielder;
            e.alive      = true;
            e.position   = position;
            e.velocity   = HMM_V3(0, 0, 0);
            e.health     = config.health;
            e.max_health = config.health;
            e.radius     = config.radius;
            e.ai_state   = SHIELDER_IDLE;
            e.ai_timer   = 0.0f;
            e.ai_timer2  = 0.0f;
            e.ai_dir     = (rand() % 2) ? 1 : -1;
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
            pick_shielder_wander(e, config.wander_radius);

            static uint8_t next_id = 0;
            e.ai_frame_id = next_id++;
            e.cached_avoid = HMM_V3(0, 0, 0);
            e.hover_cache_valid = false;

            return i;
        }
    }
    return -1;
}

// ============================================================
//  Hover
// ============================================================

static void apply_shielder_hover(Entity& s, const CollisionWorld& world,
                                 const ShielderConfig& config, float dt,
                                 float total_time, bool do_raycast) {
    if (do_raycast || !s.hover_cache_valid) {
        HitResult hit = world.raycast(s.position, HMM_V3(0, -1, 0),
                                      s.hover_height * 3.0f);
        if (hit.hit) {
            s.hover_cache_t = hit.t;
            s.hover_cache_valid = true;
        } else {
            s.hover_cache_valid = false;
        }
    }

    if (s.hover_cache_valid) {
        float height_error = s.hover_height - s.hover_cache_t;
        float force = height_error * config.hover_force - s.velocity.Y * 2.0f;
        s.velocity.Y += force * dt;
    } else {
        s.velocity.Y -= 3.0f * dt;
    }

    // Bob
    float bob = sinf(total_time * s.bob_freq * 6.28f + s.bob_seed) * s.bob_amp;
    s.velocity.Y += bob * dt * 2.0f;
}

// ============================================================
//  AI update
// ============================================================

static uint32_t s_shielder_frame = 0;

void shielder_tick_frame() { s_shielder_frame++; }

void shielder_update(Entity& shielder, Entity entities[], int max_entities,
                     HMM_Vec3 player_pos, const CollisionWorld& world,
                     const ShielderConfig& config, float dt, float total_time) {
    if (!shielder.alive || shielder.type != EntityType::Shielder) return;

    if (shielder.hit_flash > 0.0f)
        shielder.hit_flash -= dt;

    bool do_expensive = ((s_shielder_frame + shielder.ai_frame_id) % 4 == 0);

    // ------ DYING ------
    if (shielder.ai_state == SHIELDER_DYING) {
        shielder.death_timer += dt;
        shielder.velocity.Y -= config.death_gravity * dt;

        float drag = expf(-config.death_drag * dt);
        shielder.velocity.X *= drag;
        shielder.velocity.Z *= drag;

        shielder.tumble_x += shielder.angular_vel.X * dt;
        shielder.tumble_z += shielder.angular_vel.Z * dt;

        HMM_Vec3 vel_before = shielder.velocity;
        float speed_before = HMM_LenV3(vel_before);
        shielder.position = world.slide_move(shielder.position, shielder.radius,
                                             shielder.velocity, dt);
        float speed_after = HMM_LenV3(shielder.velocity);

        bool was_falling = vel_before.Y < -1.0f;
        float speed_lost = speed_before - speed_after;
        if (was_falling && speed_lost > speed_before * 0.4f) {
            shielder.ai_state = SHIELDER_DEAD;
            shielder.alive = false;
            return;
        }

        if (shielder.death_timer > config.death_timeout) {
            shielder.ai_state = SHIELDER_DEAD;
            shielder.alive = false;
        }
        return;
    }

    // ------ Live AI ------
    HMM_Vec3 to_player = HMM_SubV3(player_pos, shielder.position);
    float dist = HMM_LenV3(to_player);
    HMM_Vec3 dir_to_player = (dist > 0.01f)
        ? HMM_MulV3F(to_player, 1.0f / dist) : HMM_V3(0, 0, 1);

    // Hover
    apply_shielder_hover(shielder, world, config, dt, total_time, do_expensive);

    HMM_Vec3 desired_hvel = HMM_V3(0, 0, 0);

    switch (shielder.ai_state) {
    case SHIELDER_IDLE: {
        // Wander
        HMM_Vec3 to_target = HMM_SubV3(shielder.wander_target, shielder.position);
        float target_dist = sqrtf(to_target.X * to_target.X + to_target.Z * to_target.Z);

        if (target_dist < 1.0f) {
            shielder.wander_timer -= dt;
            if (shielder.wander_timer <= 0.0f) {
                pick_shielder_wander(shielder, config.wander_radius);
                shielder.wander_timer = randf(config.wander_pause_min, config.wander_pause_max);
            }
        } else {
            HMM_Vec3 dir = HMM_V3(to_target.X / target_dist, 0, to_target.Z / target_dist);
            desired_hvel = HMM_MulV3F(dir, config.wander_speed);
        }

        if (dist < config.detection_range) {
            shielder.ai_state = SHIELDER_CHASING;
        }
    } break;

    case SHIELDER_CHASING: {
        // Move towards allies (or between player and allies)
        int nearby = count_nearby_allies(shielder, entities, max_entities,
                                         config.shield_radius);

        if (nearby > 0) {
            // Allies nearby — switch to shielding
            shielder.ai_state = SHIELDER_SHIELDING;
            break;
        }

        // No allies close — find nearest ally and move towards it
        float best_dist = 999.0f;
        HMM_Vec3 best_pos = shielder.spawn_pos;
        for (int i = 0; i < max_entities; i++) {
            const Entity& e = entities[i];
            if (!e.alive || &e == &shielder) continue;
            if (e.type == EntityType::Projectile || e.type == EntityType::Shielder) continue;

            float d = HMM_LenV3(HMM_SubV3(e.position, shielder.position));
            if (d < best_dist) { best_dist = d; best_pos = e.position; }
        }

        HMM_Vec3 to_ally = HMM_SubV3(best_pos, shielder.position);
        float ally_dist = sqrtf(to_ally.X * to_ally.X + to_ally.Z * to_ally.Z);
        if (ally_dist > 0.5f) {
            HMM_Vec3 dir = HMM_V3(to_ally.X / ally_dist, 0, to_ally.Z / ally_dist);
            desired_hvel = HMM_MulV3F(dir, config.chase_speed);
        }
    } break;

    case SHIELDER_SHIELDING: {
        // Stay between player and ally center, at preferred distance from player
        HMM_Vec3 center = ally_center(shielder, entities, max_entities,
                                      config.shield_radius);

        // Desired position: near allies but keeping distance from player
        HMM_Vec3 player_to_center = HMM_SubV3(center, player_pos);
        float pc_dist = HMM_LenV3(player_to_center);
        HMM_Vec3 pc_dir = (pc_dist > 0.1f)
            ? HMM_MulV3F(player_to_center, 1.0f / pc_dist)
            : HMM_V3(0, 0, 1);

        // Target: preferred_dist from player, towards ally center
        HMM_Vec3 target = HMM_AddV3(player_pos,
                                     HMM_MulV3F(pc_dir, config.preferred_dist));

        HMM_Vec3 to_target = HMM_SubV3(target, shielder.position);
        float td = sqrtf(to_target.X * to_target.X + to_target.Z * to_target.Z);
        if (td > 1.0f) {
            HMM_Vec3 dir = HMM_V3(to_target.X / td, 0, to_target.Z / td);
            desired_hvel = HMM_MulV3F(dir, config.chase_speed);
        }

        // Flee if player gets too close
        if (dist < config.flee_range) {
            shielder.ai_state = SHIELDER_FLEEING;
            shielder.ai_timer = 1.5f;
        }

        // Check if allies are still nearby
        int nearby = count_nearby_allies(shielder, entities, max_entities,
                                         config.shield_radius * 1.5f);
        if (nearby == 0) {
            shielder.ai_state = SHIELDER_CHASING;
        }
    } break;

    case SHIELDER_FLEEING: {
        // Run away from player
        HMM_Vec3 flee_dir = HMM_V3(-dir_to_player.X, 0, -dir_to_player.Z);
        float flen = HMM_LenV3(flee_dir);
        if (flen > 0.01f) flee_dir = HMM_MulV3F(flee_dir, 1.0f / flen);

        desired_hvel = HMM_MulV3F(flee_dir, config.flee_speed);

        shielder.ai_timer -= dt;
        if (shielder.ai_timer <= 0.0f || dist > config.preferred_dist) {
            shielder.ai_state = SHIELDER_CHASING;
        }
    } break;

    default: break;
    }

    // Wall avoidance
    if (do_expensive) {
        shielder.cached_avoid = shielder_wall_avoid(shielder, desired_hvel, world,
                                                    config.wall_avoid_dist,
                                                    config.wall_avoid_force);
    }
    desired_hvel = HMM_AddV3(desired_hvel, shielder.cached_avoid);

    // Accelerate
    HMM_Vec3 hvel = HMM_V3(shielder.velocity.X, 0, shielder.velocity.Z);
    HMM_Vec3 diff = HMM_SubV3(desired_hvel, hvel);
    float diff_len = HMM_LenV3(diff);
    if (diff_len > 0.01f) {
        float step = config.acceleration * dt;
        if (step > diff_len) step = diff_len;
        HMM_Vec3 accel = HMM_MulV3F(diff, step / diff_len);
        shielder.velocity.X += accel.X;
        shielder.velocity.Z += accel.Z;
    }

    // Move
    shielder.position = world.slide_move(shielder.position, shielder.radius,
                                         shielder.velocity, dt);

    // Update yaw
    float hspeed = sqrtf(shielder.velocity.X * shielder.velocity.X +
                         shielder.velocity.Z * shielder.velocity.Z);
    if (hspeed > 0.5f) {
        shielder.yaw = atan2f(shielder.velocity.X, shielder.velocity.Z);
    }
}

// ============================================================
//  Shield aura query — called when dealing damage to enemies
// ============================================================

float shielder_get_damage_mult(const Entity entities[], int max_entities,
                               HMM_Vec3 target_pos, const ShielderConfig& config) {
    float best = 1.0f;  // no shield by default

    for (int i = 0; i < max_entities; i++) {
        const Entity& e = entities[i];
        if (!e.alive || e.type != EntityType::Shielder) continue;
        // Only active shielders project aura
        if (e.ai_state != SHIELDER_SHIELDING) continue;

        float dist = HMM_LenV3(HMM_SubV3(target_pos, e.position));
        if (dist < config.shield_radius) {
            float mult = config.damage_reduction;
            if (mult < best) best = mult;
        }
    }

    return best;
}
