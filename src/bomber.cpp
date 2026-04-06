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
//  Bomb drop — spawns a projectile that falls
// ============================================================

static void drop_bomb(Entity& bomber, Entity entities[], int max_entities,
                      HMM_Vec3 player_pos, const BomberConfig& config) {
    for (int i = 0; i < max_entities; i++) {
        if (!entities[i].alive) {
            Entity& p = entities[i];
            p = Entity{};
            p.type      = EntityType::Projectile;
            p.alive     = true;
            p.position  = bomber.position;
            p.position.Y -= bomber.radius;  // drop from bottom
            p.radius    = 0.25f;            // slightly bigger than drone projectiles
            p.damage    = config.bomb_damage;
            p.owner     = -2;               // -2 = bomb (for AoE check)
            p.lifetime  = 8.0f;

            // Slight forward velocity + gravity handled in projectile update
            HMM_Vec3 hdir = HMM_SubV3(player_pos, bomber.position);
            hdir.Y = 0;
            float hlen = HMM_LenV3(hdir);
            if (hlen > 0.1f) hdir = HMM_MulV3F(hdir, 1.0f / hlen);
            else hdir = HMM_V3(0, 0, 0);

            p.velocity = HMM_V3(
                hdir.X * config.bomb_speed,
                -2.0f,  // initial downward velocity
                hdir.Z * config.bomb_speed
            );

            // Store bomb_gravity in chase_speed field for projectile update to use
            p.chase_speed = config.bomb_gravity;
            // Store AoE radius in circle_speed field
            p.circle_speed = config.bomb_aoe_radius;
            return;
        }
    }
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
            e.ai_timer2  = 0.0f;  // bombs dropped this run
            e.ai_dir     = (rand() % 2) ? 1 : -1;
            e.owner      = -1;

            e.hover_height = randf(config.hover_height_min, config.hover_height_max);
            e.bob_amp      = config.bob_amp;
            e.bob_freq     = config.bob_freq;
            e.bob_seed     = randf(0.0f, 100.0f);
            e.chase_speed  = config.approach_speed;
            e.circle_speed = config.circle_speed;

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
//  Hover (high altitude)
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

    // Find bomber index for bomb ownership
    int bomber_idx = -1;
    for (int i = 0; i < max_entities; i++) {
        if (&entities[i] == &bomber) { bomber_idx = i; break; }
    }

    // ------ DYING ------
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
        if (was_falling && speed_lost > speed_before * 0.4f) {
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

    // ------ Live AI ------
    HMM_Vec3 to_player = HMM_SubV3(player_pos, bomber.position);
    float dist = HMM_LenV3(to_player);
    HMM_Vec3 dir_to_player = (dist > 0.01f)
        ? HMM_MulV3F(to_player, 1.0f / dist) : HMM_V3(0, 0, 1);

    // Hover high
    apply_bomber_hover(bomber, world, config, dt, total_time, do_expensive);

    HMM_Vec3 desired_hvel = HMM_V3(0, 0, 0);

    switch (bomber.ai_state) {
    case BOMBER_IDLE: {
        // Wander at altitude
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
        // Fly towards player (horizontal only, hover handles altitude)
        HMM_Vec3 hdir = HMM_V3(dir_to_player.X, 0, dir_to_player.Z);
        float hlen = HMM_LenV3(hdir);
        if (hlen > 0.01f) hdir = HMM_MulV3F(hdir, 1.0f / hlen);

        desired_hvel = HMM_MulV3F(hdir, config.approach_speed);

        // Close enough horizontally? Start bombing run
        float hdist = sqrtf(to_player.X * to_player.X + to_player.Z * to_player.Z);
        if (hdist < config.circle_radius * 1.2f) {
            bomber.ai_state = BOMBER_BOMBING;
            bomber.ai_timer = config.bomb_interval;
            bomber.ai_timer2 = 0.0f;  // bombs dropped
        }
    } break;

    case BOMBER_BOMBING: {
        // Circle above player while dropping bombs
        HMM_Vec3 h_to_player = HMM_V3(to_player.X, 0, to_player.Z);
        float hdist = HMM_LenV3(h_to_player);
        if (hdist < 0.1f) hdist = 0.1f;
        HMM_Vec3 radial = HMM_MulV3F(h_to_player, 1.0f / hdist);

        // Tangent (perpendicular in XZ)
        HMM_Vec3 tangent = HMM_V3(-radial.Z * bomber.ai_dir, 0,
                                    radial.X * bomber.ai_dir);

        // Maintain circle distance
        float dist_error = hdist - config.circle_radius;
        desired_hvel = HMM_AddV3(
            HMM_MulV3F(tangent, config.circle_speed),
            HMM_MulV3F(radial, dist_error * 2.0f)
        );

        // Drop bombs on interval
        bomber.ai_timer -= dt;
        if (bomber.ai_timer <= 0.0f) {
            drop_bomb(bomber, entities, max_entities, player_pos, config);
            bomber.ai_timer2 += 1.0f;
            bomber.ai_timer = config.bomb_interval;

            if ((int)bomber.ai_timer2 >= config.bombs_per_run) {
                bomber.ai_state = BOMBER_RELOADING;
                bomber.ai_timer = config.reload_time;
            }
        }
    } break;

    case BOMBER_RELOADING: {
        // Pull back from player
        HMM_Vec3 hdir = HMM_V3(dir_to_player.X, 0, dir_to_player.Z);
        float hlen = HMM_LenV3(hdir);
        if (hlen > 0.01f) hdir = HMM_MulV3F(hdir, 1.0f / hlen);

        desired_hvel = HMM_MulV3F(hdir, -config.approach_speed * 0.5f);

        bomber.ai_timer -= dt;
        if (bomber.ai_timer <= 0.0f) {
            bomber.ai_state = BOMBER_APPROACH;
            bomber.ai_timer2 = 0.0f;
        }
    } break;

    default: break;
    }

    // Wall avoidance
    if (do_expensive) {
        bomber.cached_avoid = bomber_wall_avoid(bomber, desired_hvel, world,
                                                config.wall_avoid_dist, config.wall_avoid_force);
    }
    desired_hvel = HMM_AddV3(desired_hvel, bomber.cached_avoid);

    // Accelerate
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
