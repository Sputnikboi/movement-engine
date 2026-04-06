#include "bomber.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>

extern float randf(float lo, float hi);

// ============================================================
//  Helpers
// ============================================================

static void pick_bomber_wander(Entity& e, float radius) {
    float angle = randf(0.0f, 6.2831853f);
    float dist  = randf(1.0f, radius);
    e.wander_target = HMM_V3(
        e.spawn_pos.X + cosf(angle) * dist,
        e.spawn_pos.Y,
        e.spawn_pos.Z + sinf(angle) * dist
    );
}

static HMM_Vec3 bomber_wall_avoid(const Entity& e, HMM_Vec3 desired_dir,
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

// ============================================================
//  Spawn
// ============================================================

int bomber_spawn(Entity entities[], int max_entities,
                 HMM_Vec3 position, const BomberConfig& config) {
    for (int i = 0; i < max_entities; i++) {
        if (!entities[i].alive) {
            Entity& e = entities[i];
            e = Entity{};
            e.type       = EntityType::Bomber;
            e.alive      = true;
            e.position   = position;
            e.velocity   = HMM_V3(0, 0, 0);
            e.health     = config.health;
            e.max_health = config.health;
            e.radius     = config.radius;
            e.ai_state   = BOMBER_IDLE;
            e.ai_timer   = 0.0f;
            e.ai_timer2  = 0.0f;
            e.ai_dir     = (rand() % 2) ? 1 : -1;
            e.owner      = -1;

            e.hover_height = randf(config.hover_height_min, config.hover_height_max);
            e.bob_amp      = config.bob_amp;
            e.bob_freq     = config.bob_freq;
            e.bob_seed     = randf(0.0f, 100.0f);
            e.chase_speed  = config.approach_speed;
            e.circle_speed = 0.0f;

            e.death_timer  = 0.0f;
            e.hit_flash    = 0.0f;
            e.angular_vel  = HMM_V3(0, 0, 0);
            e.tumble_x     = 0.0f;
            e.tumble_z     = 0.0f;

            e.spawn_pos    = position;
            e.wander_timer = randf(0.5f, 2.0f);
            pick_bomber_wander(e, config.wander_radius);

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
//  Hover (high altitude, only used while not diving)
// ============================================================

static void apply_bomber_hover(Entity& b, const CollisionWorld& world,
                               const BomberConfig& config, float dt,
                               float total_time, bool do_raycast) {
    if (do_raycast || !b.hover_cache_valid) {
        HitResult hit = world.raycast(b.position, HMM_V3(0, -1, 0),
                                      b.hover_height * 3.0f);
        if (hit.hit) {
            b.hover_cache_t = hit.t;
            b.hover_cache_valid = true;
        } else {
            b.hover_cache_valid = false;
        }
    }

    if (b.hover_cache_valid) {
        float height_error = b.hover_height - b.hover_cache_t;
        float force = height_error * config.hover_force - b.velocity.Y * 2.0f;
        b.velocity.Y += force * dt;
    } else {
        b.velocity.Y -= 3.0f * dt;
    }

    // Bob
    float bob = sinf(total_time * b.bob_freq * 6.28f + b.bob_seed) * b.bob_amp;
    b.velocity.Y += bob * dt * 2.0f;
}

// ============================================================
//  AI update
// ============================================================

static uint32_t s_bomber_frame = 0;

void bomber_tick_frame() { s_bomber_frame++; }

void bomber_update(Entity& bomber, Entity entities[], int max_entities,
                   HMM_Vec3 player_pos, const CollisionWorld& world,
                   const BomberConfig& config, float dt, float total_time) {
    if (!bomber.alive || bomber.type != EntityType::Bomber) return;

    if (bomber.hit_flash > 0.0f)
        bomber.hit_flash -= dt;

    bool do_expensive = ((s_bomber_frame + bomber.ai_frame_id) % 4 == 0);

    // ------ DYING (killed by player before explosion) ------
    if (bomber.ai_state == BOMBER_DYING) {
        bomber.death_timer += dt;
        bomber.velocity.Y -= config.death_gravity * dt;

        float drag = expf(-config.death_drag * dt);
        bomber.velocity.X *= drag;
        bomber.velocity.Z *= drag;

        bomber.tumble_x += bomber.angular_vel.X * dt;
        bomber.tumble_z += bomber.angular_vel.Z * dt;

        HMM_Vec3 vel_before = bomber.velocity;
        float speed_before = HMM_LenV3(vel_before);
        bomber.position = world.slide_move(bomber.position, bomber.radius,
                                           bomber.velocity, dt);
        float speed_after = HMM_LenV3(bomber.velocity);

        bool was_falling = vel_before.Y < -1.0f;
        float speed_lost = speed_before - speed_after;
        bool impact = was_falling && speed_lost > speed_before * 0.4f;
        // Near-ground fast kill: if barely moving after 0.3s, just explode
        if (!impact && bomber.death_timer > 0.3f) {
            HitResult gnd = world.raycast(bomber.position, HMM_V3(0,-1,0), bomber.radius + 0.5f);
            if (gnd.hit) impact = true;
        }
        if (impact) {
            bomber.ai_state = BOMBER_DEAD;
            bomber.alive = false;
            return;
        }

        if (bomber.death_timer > config.death_timeout) {
            bomber.ai_state = BOMBER_DEAD;
            bomber.alive = false;
        }
        return;
    }

    // ------ EXPLODING (hit ground during dive) ------
    if (bomber.ai_state == BOMBER_EXPLODING) {
        // Brief timer then die (explosion effect spawned by main loop)
        bomber.ai_timer -= dt;
        if (bomber.ai_timer <= 0.0f) {
            bomber.ai_state = BOMBER_DEAD;
            bomber.alive = false;
        }
        return;
    }

    // ------ Live AI ------
    HMM_Vec3 to_player = HMM_SubV3(player_pos, bomber.position);
    float dist = HMM_LenV3(to_player);
    HMM_Vec3 dir_to_player = (dist > 0.01f)
        ? HMM_MulV3F(to_player, 1.0f / dist) : HMM_V3(0, 0, 1);

    HMM_Vec3 desired_hvel = HMM_V3(0, 0, 0);

    switch (bomber.ai_state) {
    case BOMBER_IDLE: {
        // Hover + wander
        apply_bomber_hover(bomber, world, config, dt, total_time, do_expensive);

        HMM_Vec3 to_target = HMM_SubV3(bomber.wander_target, bomber.position);
        float target_dist = sqrtf(to_target.X * to_target.X + to_target.Z * to_target.Z);

        if (target_dist < 2.0f) {
            bomber.wander_timer -= dt;
            if (bomber.wander_timer <= 0.0f) {
                pick_bomber_wander(bomber, config.wander_radius);
                bomber.wander_timer = randf(config.wander_pause_min, config.wander_pause_max);
            }
        } else {
            HMM_Vec3 dir = HMM_V3(to_target.X / target_dist, 0, to_target.Z / target_dist);
            desired_hvel = HMM_MulV3F(dir, config.wander_speed);
        }

        // Detection
        if (dist < config.detection_range) {
            bomber.ai_state = BOMBER_APPROACH;
        }
    } break;

    case BOMBER_APPROACH: {
        // Fly towards player at altitude
        apply_bomber_hover(bomber, world, config, dt, total_time, do_expensive);

        HMM_Vec3 hdir = HMM_V3(dir_to_player.X, 0, dir_to_player.Z);
        float hlen = HMM_LenV3(hdir);
        if (hlen > 0.01f) hdir = HMM_MulV3F(hdir, 1.0f / hlen);

        desired_hvel = HMM_MulV3F(hdir, config.approach_speed);

        // Close enough horizontally? Start dive
        float hdist = sqrtf(to_player.X * to_player.X + to_player.Z * to_player.Z);
        if (hdist < config.dive_trigger_dist) {
            bomber.ai_state = BOMBER_DIVING;
            // Invalidate hover so it doesn't fight the dive
            bomber.hover_cache_valid = false;
        }
    } break;

    case BOMBER_DIVING: {
        // Kamikaze dive — aim directly at player, no hover
        HMM_Vec3 dive_dir = HMM_NormV3(to_player);

        // Accelerate towards player in 3D
        HMM_Vec3 desired_vel = HMM_MulV3F(dive_dir, config.dive_speed);
        HMM_Vec3 diff = HMM_SubV3(desired_vel, bomber.velocity);
        float diff_len = HMM_LenV3(diff);
        if (diff_len > 0.01f) {
            float step = config.acceleration * 3.0f * dt; // fast accel during dive
            if (step > diff_len) step = diff_len;
            bomber.velocity = HMM_AddV3(bomber.velocity,
                                        HMM_MulV3F(diff, step / diff_len));
        }

        // Move with collision
        HMM_Vec3 old_pos = bomber.position;
        bomber.position = world.slide_move(bomber.position, bomber.radius,
                                           bomber.velocity, dt);

        // Check if we hit ground (velocity changed drastically = collision)
        HMM_Vec3 actual_move = HMM_SubV3(bomber.position, old_pos);
        float expected_move = HMM_LenV3(bomber.velocity) * dt;
        float actual_move_len = HMM_LenV3(actual_move);

        // Also check ground ray
        HitResult ground = world.raycast(bomber.position, HMM_V3(0, -1, 0),
                                         bomber.radius + 0.5f);

        if ((expected_move > 0.1f && actual_move_len < expected_move * 0.3f) ||
            (ground.hit && ground.t < bomber.radius + 0.3f)) {
            // Hit something — explode!
            bomber.ai_state = BOMBER_EXPLODING;
            bomber.ai_timer = 0.1f; // brief delay before death
            bomber.velocity = HMM_V3(0, 0, 0);
        }

        // Update yaw
        float hspeed = sqrtf(bomber.velocity.X * bomber.velocity.X +
                             bomber.velocity.Z * bomber.velocity.Z);
        if (hspeed > 0.5f)
            bomber.yaw = atan2f(bomber.velocity.X, bomber.velocity.Z);

        return; // Skip normal movement below
    } break;

    default: break;
    }

    // Wall avoidance (only for non-diving states)
    if (do_expensive) {
        bomber.cached_avoid = bomber_wall_avoid(bomber, desired_hvel, world,
                                                config.wall_avoid_dist, config.wall_avoid_force);
    }
    desired_hvel = HMM_AddV3(desired_hvel, bomber.cached_avoid);

    // Accelerate horizontal
    HMM_Vec3 hvel = HMM_V3(bomber.velocity.X, 0, bomber.velocity.Z);
    HMM_Vec3 diff = HMM_SubV3(desired_hvel, hvel);
    float diff_len = HMM_LenV3(diff);
    if (diff_len > 0.01f) {
        float step = config.acceleration * dt;
        if (step > diff_len) step = diff_len;
        HMM_Vec3 accel = HMM_MulV3F(diff, step / diff_len);
        bomber.velocity.X += accel.X;
        bomber.velocity.Z += accel.Z;
    }

    // Move with collision
    bomber.position = world.slide_move(bomber.position, bomber.radius,
                                       bomber.velocity, dt);

    // Update yaw
    float hspeed = sqrtf(bomber.velocity.X * bomber.velocity.X +
                         bomber.velocity.Z * bomber.velocity.Z);
    if (hspeed > 0.5f) {
        bomber.yaw = atan2f(bomber.velocity.X, bomber.velocity.Z);
    }
}

// ============================================================
//  Explosion check — call from main loop
// ============================================================

bool bomber_check_explosion(Entity& bomber, HMM_Vec3 cap_bottom, HMM_Vec3 cap_top,
                            float player_radius, const BomberConfig& config,
                            float& damage_out, HMM_Vec3& knockback_out) {
    if (bomber.ai_state != BOMBER_EXPLODING) return false;
    // Only trigger once (ai_dir used as exploded flag)
    if (bomber.ai_dir == 0) return false;
    bomber.ai_dir = 0;

    // Closest point on player capsule to bomber
    HMM_Vec3 closest = closest_point_on_segment(bomber.position, cap_bottom, cap_top);
    HMM_Vec3 delta = HMM_SubV3(closest, bomber.position);
    float dist = HMM_LenV3(delta);

    if (dist > config.explosion_radius) return false;

    float falloff = 1.0f - (dist / config.explosion_radius);
    if (falloff < 0.0f) falloff = 0.0f;

    damage_out = config.explosion_damage * falloff;

    HMM_Vec3 outward = (dist > 0.1f)
        ? HMM_MulV3F(delta, 1.0f / dist)
        : HMM_V3(0, 1, 0);
    knockback_out = HMM_AddV3(
        HMM_MulV3F(outward, config.explosion_knockback * falloff),
        HMM_V3(0, config.explosion_knockback * falloff * 0.5f, 0)
    );

    return true;
}
