#include "tank.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>

extern float randf(float lo, float hi);

// ============================================================
//  Helpers
// ============================================================

static void pick_tank_wander(Entity& e, float radius) {
    float angle = randf(0.0f, 6.2831853f);
    float dist  = randf(1.0f, radius);
    e.wander_target = HMM_V3(
        e.spawn_pos.X + cosf(angle) * dist,
        e.spawn_pos.Y,
        e.spawn_pos.Z + sinf(angle) * dist
    );
}

static HMM_Vec3 tank_wall_avoid(const Entity& e, HMM_Vec3 desired_dir,
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

int tank_spawn(Entity entities[], int max_entities,
               HMM_Vec3 position, const TankConfig& config) {
    for (int i = 0; i < max_entities; i++) {
        if (!entities[i].alive) {
            Entity& e = entities[i];
            e = Entity{};
            e.type       = EntityType::Tank;
            e.alive      = true;
            e.position   = position;
            e.velocity   = HMM_V3(0, 0, 0);
            e.health     = config.health;
            e.max_health = config.health;
            e.radius     = config.radius;
            e.ai_state   = TANK_IDLE;
            e.ai_timer   = 0.0f;
            e.ai_timer2  = 0.0f;
            e.ai_dir     = 1;
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
            e.wander_timer = randf(1.0f, 3.0f);
            pick_tank_wander(e, config.wander_radius);

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

static void apply_tank_hover(Entity& t, const CollisionWorld& world,
                             const TankConfig& config, float dt, float total_time,
                             bool do_raycast) {
    if (do_raycast || !t.hover_cache_valid) {
        HitResult hit = world.raycast(t.position, HMM_V3(0, -1, 0),
                                      t.hover_height * 3.0f);
        if (hit.hit) {
            t.hover_cache_t = hit.t;
            t.hover_cache_valid = true;
        } else {
            t.hover_cache_valid = false;
        }
    }

    if (t.hover_cache_valid) {
        float target_h = t.hover_height;
        // During windup, rise up slightly for visual effect
        if (t.ai_state == TANK_WINDUP) {
            float progress = 1.0f - (t.ai_timer / config.windup_time);
            target_h += progress * 1.5f;  // rise up
        }
        float height_error = target_h - t.hover_cache_t;
        float force = height_error * config.hover_force - t.velocity.Y * 3.0f;
        t.velocity.Y += force * dt;
    } else {
        t.velocity.Y -= 5.0f * dt;
    }

    // Subtle bob
    float bob = sinf(total_time * t.bob_freq * 6.28f + t.bob_seed) * t.bob_amp;
    t.velocity.Y += bob * dt * 2.0f;
}

// ============================================================
//  AI update
// ============================================================

static uint32_t s_tank_frame = 0;

void tank_tick_frame() { s_tank_frame++; }

void tank_update(Entity& tank, Entity entities[], int max_entities,
                 HMM_Vec3 player_pos, const CollisionWorld& world,
                 const TankConfig& config, float dt, float total_time) {
    if (!tank.alive || tank.type != EntityType::Tank) return;

    if (tank.hit_flash > 0.0f)
        tank.hit_flash -= dt;

    bool do_expensive = ((s_tank_frame + tank.ai_frame_id) % 4 == 0);

    // ------ DYING ------
    if (tank.ai_state == TANK_DYING) {
        tank.death_timer += dt;
        tank.velocity.Y -= config.death_gravity * dt;

        float drag = expf(-config.death_drag * dt);
        tank.velocity.X *= drag;
        tank.velocity.Z *= drag;

        tank.tumble_x += tank.angular_vel.X * dt;
        tank.tumble_z += tank.angular_vel.Z * dt;

        HMM_Vec3 vel_before = tank.velocity;
        float speed_before = HMM_LenV3(vel_before);
        tank.position = world.slide_move(tank.position, tank.radius,
                                         tank.velocity, dt);
        float speed_after = HMM_LenV3(tank.velocity);

        bool was_falling = vel_before.Y < -1.0f;
        float speed_lost = speed_before - speed_after;
        if (was_falling && speed_lost > speed_before * 0.4f) {
            tank.ai_state = TANK_DEAD;
            tank.alive = false;
            return;
        }

        if (tank.death_timer > config.death_timeout) {
            tank.ai_state = TANK_DEAD;
            tank.alive = false;
        }
        return;
    }

    // ------ Live AI ------
    HMM_Vec3 to_player = HMM_SubV3(player_pos, tank.position);
    float dist = HMM_LenV3(to_player);
    HMM_Vec3 dir_to_player = (dist > 0.01f)
        ? HMM_MulV3F(to_player, 1.0f / dist) : HMM_V3(0, 0, 1);

    // Update yaw to face movement/player
    if (tank.ai_state != TANK_IDLE) {
        tank.yaw = atan2f(dir_to_player.X, dir_to_player.Z);
    }

    // Hover
    apply_tank_hover(tank, world, config, dt, total_time, do_expensive);

    HMM_Vec3 desired_hvel = HMM_V3(0, 0, 0);

    switch (tank.ai_state) {
    case TANK_IDLE: {
        // Wander
        HMM_Vec3 to_target = HMM_SubV3(tank.wander_target, tank.position);
        float target_dist = sqrtf(to_target.X * to_target.X + to_target.Z * to_target.Z);

        if (target_dist < 1.5f) {
            tank.wander_timer -= dt;
            if (tank.wander_timer <= 0.0f) {
                pick_tank_wander(tank, config.wander_radius);
                tank.wander_timer = randf(config.wander_pause_min, config.wander_pause_max);
            }
        } else {
            HMM_Vec3 dir = HMM_V3(to_target.X / target_dist, 0, to_target.Z / target_dist);
            desired_hvel = HMM_MulV3F(dir, config.wander_speed);
            tank.yaw = atan2f(dir.X, dir.Z);
        }

        // Detection
        if (dist < config.detection_range) {
            tank.ai_state = TANK_CHASING;
        }
    } break;

    case TANK_CHASING: {
        // Move towards player
        HMM_Vec3 hdir = HMM_V3(dir_to_player.X, 0, dir_to_player.Z);
        float hlen = HMM_LenV3(hdir);
        if (hlen > 0.01f) hdir = HMM_MulV3F(hdir, 1.0f / hlen);

        desired_hvel = HMM_MulV3F(hdir, config.chase_speed);

        // In stomp range? Start windup
        if (dist < config.stomp_range) {
            tank.ai_state = TANK_WINDUP;
            tank.ai_timer = config.windup_time;
        }
    } break;

    case TANK_WINDUP: {
        // Slow to a stop during windup
        desired_hvel = HMM_V3(0, 0, 0);

        tank.ai_timer -= dt;
        if (tank.ai_timer <= 0.0f) {
            tank.ai_state = TANK_STOMP;
            tank.ai_timer = 0.15f;  // brief stomp impact window
        }
    } break;

    case TANK_STOMP: {
        // Impact frame — damage check happens in tank_check_player_hit
        desired_hvel = HMM_V3(0, 0, 0);

        tank.ai_timer -= dt;
        if (tank.ai_timer <= 0.0f) {
            tank.ai_state = TANK_COOLDOWN;
            tank.ai_timer = config.stomp_cooldown;
        }
    } break;

    case TANK_COOLDOWN: {
        // Slowly resume chasing
        HMM_Vec3 hdir = HMM_V3(dir_to_player.X, 0, dir_to_player.Z);
        float hlen = HMM_LenV3(hdir);
        if (hlen > 0.01f) hdir = HMM_MulV3F(hdir, 1.0f / hlen);

        float speed_frac = 1.0f - (tank.ai_timer / config.stomp_cooldown);
        desired_hvel = HMM_MulV3F(hdir, config.chase_speed * speed_frac * 0.5f);

        tank.ai_timer -= dt;
        if (tank.ai_timer <= 0.0f) {
            tank.ai_state = TANK_CHASING;
        }
    } break;

    default: break;
    }

    // Wall avoidance
    if (do_expensive) {
        tank.cached_avoid = tank_wall_avoid(tank, desired_hvel, world,
                                            config.wall_avoid_dist, config.wall_avoid_force);
    }
    desired_hvel = HMM_AddV3(desired_hvel, tank.cached_avoid);

    // Accelerate towards desired velocity
    HMM_Vec3 hvel = HMM_V3(tank.velocity.X, 0, tank.velocity.Z);
    HMM_Vec3 diff = HMM_SubV3(desired_hvel, hvel);
    float diff_len = HMM_LenV3(diff);
    if (diff_len > 0.01f) {
        float step = config.acceleration * dt;
        if (step > diff_len) step = diff_len;
        HMM_Vec3 accel = HMM_MulV3F(diff, step / diff_len);
        tank.velocity.X += accel.X;
        tank.velocity.Z += accel.Z;
    }

    // Move with collision
    tank.position = world.slide_move(tank.position, tank.radius,
                                     tank.velocity, dt);
}

// ============================================================
//  Stomp hit check
// ============================================================

bool tank_check_player_hit(Entity& tank, HMM_Vec3 player_pos,
                           float player_radius, const TankConfig& config,
                           float& damage_out, HMM_Vec3& knockback_out) {
    // Only on the stomp frame (TANK_STOMP just entered)
    if (tank.ai_state != TANK_STOMP) return false;
    if (tank.ai_timer < 0.10f) return false;  // only first frame

    HMM_Vec3 delta = HMM_SubV3(player_pos, tank.position);
    float dist = HMM_LenV3(delta);

    if (dist > config.stomp_aoe_radius) return false;

    // Damage falloff: full at center, linear to zero at edge
    float falloff = 1.0f - (dist / config.stomp_aoe_radius);
    if (falloff < 0.0f) falloff = 0.0f;

    damage_out = config.stomp_damage * falloff;

    // Knockback: upward + outward
    HMM_Vec3 outward = (dist > 0.1f)
        ? HMM_MulV3F(delta, 1.0f / dist)
        : HMM_V3(0, 0, 1);
    knockback_out = HMM_AddV3(
        HMM_MulV3F(outward, config.stomp_knockback * falloff),
        HMM_V3(0, config.stomp_knockback * falloff * 0.7f, 0)
    );

    return true;
}
