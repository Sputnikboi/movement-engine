#include "turret.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>

extern float randf(float lo, float hi);

// ============================================================
//  Spawn
// ============================================================

int turret_spawn(Entity entities[], int max_entities,
                 HMM_Vec3 position, const TurretConfig& config) {
    for (int i = 0; i < max_entities; i++) {
        if (!entities[i].alive) {
            Entity& e = entities[i];
            e = Entity{};
            e.type       = EntityType::Turret;
            e.alive      = true;
            e.position   = position;
            e.velocity   = HMM_V3(0, 0, 0);
            e.health     = config.health;
            e.max_health = config.health;
            e.radius     = config.radius;
            e.ai_state   = TURRET_IDLE;
            e.ai_timer   = 0.0f;
            e.ai_timer2  = 0.0f;  // burst shot counter
            e.ai_dir     = 1;     // scan direction
            e.owner      = -1;
            e.yaw        = randf(0.0f, 6.2831853f);

            e.hover_height = config.hover_height;
            e.bob_amp      = 0.0f;  // turrets don't bob
            e.bob_freq     = 0.0f;
            e.bob_seed     = 0.0f;
            e.chase_speed  = 0.0f;  // stationary

            e.death_timer  = 0.0f;
            e.hit_flash    = 0.0f;
            e.angular_vel  = HMM_V3(0, 0, 0);
            e.tumble_x     = 0.0f;
            e.tumble_z     = 0.0f;

            e.spawn_pos    = position;
            e.wander_timer = 0.0f;

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
//  Hover (simple ground-sit)
// ============================================================

static void apply_turret_hover(Entity& t, const CollisionWorld& world,
                               const TurretConfig& config, float dt,
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
        float height_error = t.hover_height - t.hover_cache_t;
        float force = height_error * config.hover_force - t.velocity.Y * 2.0f;
        t.velocity.Y += force * dt;
    } else {
        t.velocity.Y -= 3.0f * dt;
    }
}

// ============================================================
//  State machine
// ============================================================

static uint32_t s_turret_frame = 0;

void turret_tick_frame() { s_turret_frame++; }

void turret_update(Entity& turret, Entity entities[], int max_entities,
                   HMM_Vec3 player_pos, const CollisionWorld& world,
                   const TurretConfig& config, float dt, float total_time) {
    if (!turret.alive || turret.type != EntityType::Turret) return;

    if (turret.hit_flash > 0.0f)
        turret.hit_flash -= dt;

    bool do_expensive = ((s_turret_frame + turret.ai_frame_id) % 4 == 0);

    // ------ DYING ------
    if (turret.ai_state == TURRET_DYING) {
        turret.death_timer += dt;
        turret.velocity.Y -= config.death_gravity * dt;

        float drag = expf(-config.death_drag * dt);
        turret.velocity.X *= drag;
        turret.velocity.Z *= drag;

        turret.tumble_x += turret.angular_vel.X * dt;
        turret.tumble_z += turret.angular_vel.Z * dt;

        HMM_Vec3 vel_before = turret.velocity;
        float speed_before = HMM_LenV3(vel_before);
        turret.position = world.slide_move(turret.position, turret.radius,
                                           turret.velocity, dt);
        float speed_after = HMM_LenV3(turret.velocity);

        bool was_falling = vel_before.Y < -1.0f;
        float speed_lost = speed_before - speed_after;
        if (was_falling && speed_lost > speed_before * 0.4f) {
            turret.ai_state = TURRET_DEAD;
            turret.alive = false;
            return;
        }

        if (turret.death_timer > config.death_timeout) {
            turret.ai_state = TURRET_DEAD;
            turret.alive = false;
        }
        return;
    }

    // ------ Live AI ------
    HMM_Vec3 to_player = HMM_SubV3(player_pos, turret.position);
    float dist = HMM_LenV3(to_player);
    HMM_Vec3 dir_to_player = (dist > 0.01f)
        ? HMM_MulV3F(to_player, 1.0f / dist) : HMM_V3(0, 0, 1);

    // Desired yaw towards player
    float desired_yaw = atan2f(dir_to_player.X, dir_to_player.Z);

    // Hover
    apply_turret_hover(turret, world, config, dt, do_expensive);

    // Stationary: only vertical movement
    turret.velocity.X = 0.0f;
    turret.velocity.Z = 0.0f;
    turret.position.Y += turret.velocity.Y * dt;

    switch (turret.ai_state) {
    case TURRET_IDLE: {
        // Slow scan rotation
        turret.yaw += config.scan_speed * turret.ai_dir * dt;
        if (turret.yaw > 6.2831853f) turret.yaw -= 6.2831853f;
        if (turret.yaw < 0.0f)       turret.yaw += 6.2831853f;

        // Randomly reverse scan direction
        turret.ai_timer -= dt;
        if (turret.ai_timer <= 0.0f) {
            turret.ai_dir = -turret.ai_dir;
            turret.ai_timer = randf(2.0f, 5.0f);
        }

        // Check detection: player in range AND line of sight
        if (dist < config.detection_range) {
            HitResult los = world.raycast(turret.position, dir_to_player, dist);
            if (!los.hit) {
                turret.ai_state = TURRET_TRACKING;
            }
        }
    } break;

    case TURRET_TRACKING: {
        // Rotate towards player
        float yaw_diff = desired_yaw - turret.yaw;
        // Normalize to [-PI, PI]
        while (yaw_diff >  3.14159f) yaw_diff -= 6.28318f;
        while (yaw_diff < -3.14159f) yaw_diff += 6.28318f;

        float max_rot = config.track_speed * dt;
        if (fabsf(yaw_diff) < max_rot) {
            turret.yaw = desired_yaw;
            // Locked on — start windup
            turret.ai_state = TURRET_WINDUP;
            turret.ai_timer = config.windup_time;
        } else {
            turret.yaw += (yaw_diff > 0 ? max_rot : -max_rot);
        }

        // Lost target?
        if (dist > config.lose_range) {
            turret.ai_state = TURRET_IDLE;
            turret.ai_timer = randf(2.0f, 5.0f);
        }
    } break;

    case TURRET_WINDUP: {
        // Keep tracking during windup
        float yaw_diff = desired_yaw - turret.yaw;
        while (yaw_diff >  3.14159f) yaw_diff -= 6.28318f;
        while (yaw_diff < -3.14159f) yaw_diff += 6.28318f;
        float max_rot = config.track_speed * 0.5f * dt;  // slower during windup
        if (fabsf(yaw_diff) > max_rot)
            turret.yaw += (yaw_diff > 0 ? max_rot : -max_rot);
        else
            turret.yaw = desired_yaw;

        turret.ai_timer -= dt;
        if (turret.ai_timer <= 0.0f) {
            turret.ai_state = TURRET_FIRING;
            turret.ai_timer = 0.0f;
            turret.ai_timer2 = 0.0f;  // shots fired so far
        }

        // Lost LOS? Cancel windup
        if (do_expensive && dist < config.lose_range) {
            HitResult los = world.raycast(turret.position, dir_to_player, dist);
            if (los.hit) {
                turret.ai_state = TURRET_TRACKING;
            }
        }
        if (dist > config.lose_range) {
            turret.ai_state = TURRET_IDLE;
            turret.ai_timer = randf(2.0f, 5.0f);
        }
    } break;

    case TURRET_FIRING: {
        // Burst fire: ai_timer2 = shots fired, ai_timer = time until next shot
        turret.ai_timer -= dt;
        if (turret.ai_timer <= 0.0f && turret.ai_timer2 < config.burst_count_f) {
            // Fire one hitscan shot
            turret.ai_timer2 += 1.0f;
            turret.ai_timer = config.burst_interval;

            // The actual hit check happens in turret_check_player_hit()
            // We just set state to indicate "shot fired this frame"
        }

        if (turret.ai_timer2 >= config.burst_count_f) {
            turret.ai_state = TURRET_COOLDOWN;
            turret.ai_timer = config.cooldown_time;
        }
    } break;

    case TURRET_COOLDOWN: {
        // Still track slowly during cooldown
        float yaw_diff = desired_yaw - turret.yaw;
        while (yaw_diff >  3.14159f) yaw_diff -= 6.28318f;
        while (yaw_diff < -3.14159f) yaw_diff += 6.28318f;
        float max_rot = config.track_speed * 0.3f * dt;
        if (fabsf(yaw_diff) > max_rot)
            turret.yaw += (yaw_diff > 0 ? max_rot : -max_rot);
        else
            turret.yaw = desired_yaw;

        turret.ai_timer -= dt;
        if (turret.ai_timer <= 0.0f) {
            // Re-check LOS
            if (dist < config.detection_range) {
                turret.ai_state = TURRET_TRACKING;
            } else {
                turret.ai_state = TURRET_IDLE;
                turret.ai_timer = randf(2.0f, 5.0f);
            }
        }
    } break;

    default: break;
    }
}

// ============================================================
//  Hitscan check — call from main loop during FIRING state
// ============================================================

bool turret_check_player_hit(Entity& turret, HMM_Vec3 player_pos,
                             float player_radius, const CollisionWorld& world,
                             const TurretConfig& config, float& damage_out) {
    if (turret.ai_state != TURRET_FIRING) return false;
    // Only hit on the frame a shot was fired (ai_timer just reset)
    if (turret.ai_timer > config.burst_interval * 0.9f) return false;
    if (turret.ai_timer2 < 1.0f) return false;

    // Turret facing direction
    HMM_Vec3 fwd = HMM_V3(sinf(turret.yaw), 0.0f, cosf(turret.yaw));

    // Add accuracy spread
    float spread = (1.0f - config.accuracy) * 0.5f;
    fwd.X += randf(-spread, spread);
    fwd.Y += randf(-spread * 0.5f, spread * 0.5f);
    fwd.Z += randf(-spread, spread);
    fwd = HMM_NormV3(fwd);

    // Hitscan: ray-sphere vs player
    HMM_Vec3 oc = HMM_SubV3(turret.position, player_pos);
    float b = HMM_DotV3(oc, fwd);
    float c = HMM_DotV3(oc, oc) - player_radius * player_radius;
    float disc = b * b - c;
    if (disc < 0) return false;

    float t = -b - sqrtf(disc);
    if (t < 0) t = -b + sqrtf(disc);
    if (t < 0 || t > config.detection_range) return false;

    // Check wall in the way
    HitResult wall = world.raycast(turret.position, fwd, t);
    if (wall.hit) return false;

    damage_out = config.hitscan_damage;
    return true;
}

// ============================================================
//  Laser visualization helper
// ============================================================

bool turret_get_laser(const Entity& turret, HMM_Vec3& origin, HMM_Vec3& dir) {
    if (turret.ai_state != TURRET_WINDUP && turret.ai_state != TURRET_FIRING)
        return false;

    origin = turret.position;
    dir = HMM_V3(sinf(turret.yaw), 0.0f, cosf(turret.yaw));
    return true;
}
