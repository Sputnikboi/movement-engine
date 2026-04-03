#include "player.h"
#include "collision.h"
#include <cmath>
#include <cstdio>

// ============================================================
//  Quake-style Accelerate
// ============================================================

void Player::accelerate(HMM_Vec3 wish_dir, float wish_speed, float accel, float dt) {
    float current_speed = HMM_DotV3(velocity, wish_dir);
    float add_speed = wish_speed - current_speed;
    if (add_speed <= 0.0f) return;

    float accel_speed = accel * wish_speed * dt;
    if (accel_speed > add_speed)
        accel_speed = add_speed;

    velocity.X += accel_speed * wish_dir.X;
    velocity.Y += accel_speed * wish_dir.Y;
    velocity.Z += accel_speed * wish_dir.Z;
}

// ============================================================
//  Build wish direction from input + yaw
// ============================================================

HMM_Vec3 Player::build_wish_dir(const InputState& input) const {
    float forward_x =  cosf(input.yaw);
    float forward_z =  sinf(input.yaw);
    float right_x   =  forward_z;
    float right_z   = -forward_x;

    return HMM_V3(
        forward_x * input.forward + right_x * input.right,
        0.0f,
        forward_z * input.forward + right_z * input.right
    );
}

// ============================================================
//  Ground check: raycast down from feet
// ============================================================

void Player::check_ground(const CollisionWorld& world) {
    HMM_Vec3 ray_origin = HMM_AddV3(position, HMM_V3(0.0f, 0.1f, 0.0f));
    HMM_Vec3 ray_dir    = HMM_V3(0.0f, -1.0f, 0.0f);
    float    ray_dist   = 0.1f + ground_check_dist;

    HitResult hit = world.raycast(ray_origin, ray_dir, ray_dist);

    if (g_collision_log) {
        printf("check_ground: pos=(%.2f,%.2f,%.2f) hit=%d", position.X, position.Y, position.Z, hit.hit);
        if (hit.hit) printf(" normal=(%.2f,%.2f,%.2f)", hit.normal.X, hit.normal.Y, hit.normal.Z);
        printf("\n");
    }

    if (hit.hit && hit.normal.Y > 0.7f && velocity.Y <= 0.1f) {
        // Only count as grounded if not actively jumping upward.
        // velocity.Y > 0.1 means we just jumped — don't re-ground.
        grounded = true;
        ground_normal = hit.normal;

        float ground_y = hit.point.Y;
        if (position.Y < ground_y)
            position.Y = ground_y;
        else if (position.Y - ground_y < ground_check_dist && velocity.Y <= 0.0f)
            position.Y = ground_y;
    } else {
        grounded = false;
        ground_normal = HMM_V3(0.0f, 1.0f, 0.0f);
    }
}

// ============================================================
//  Friction
// ============================================================

void Player::apply_friction(float dt, float fric) {
    float speed = sqrtf(velocity.X * velocity.X + velocity.Z * velocity.Z);
    if (speed < 0.001f) {
        velocity.X = 0.0f;
        velocity.Z = 0.0f;
        return;
    }

    float control = (speed < stop_speed) ? stop_speed : speed;
    float drop = control * fric * dt;

    float new_speed = speed - drop;
    if (new_speed < 0.0f) new_speed = 0.0f;

    float scale = new_speed / speed;
    velocity.X *= scale;
    velocity.Z *= scale;
}

// ============================================================
//  Collision move (shared by ground and air)
// ============================================================

void Player::do_collide_and_move(float dt, const CollisionWorld& world) {
    HMM_Vec3 sphere_center = HMM_AddV3(position, HMM_V3(0.0f, radius, 0.0f));
    sphere_center = world.slide_move(sphere_center, radius, velocity, dt);
    position = HMM_SubV3(sphere_center, HMM_V3(0.0f, radius, 0.0f));
}

// ============================================================
//  Crouch handling
// ============================================================

void Player::handle_crouch(const InputState& input, const CollisionWorld& world) {
    if (input.crouch_held) {
        if (!crouched) {
            crouched = true;
            // If grounded and moving fast enough, initiate a slide
            if (grounded) {
                try_slide(input);
            }
        }
    } else {
        if (crouched) {
            // Check if there's room to stand up: raycast upward from head
            HMM_Vec3 ray_origin = HMM_AddV3(position, HMM_V3(0.0f, height_crouch, 0.0f));
            HMM_Vec3 ray_dir = HMM_V3(0.0f, 1.0f, 0.0f);
            float check_dist = height_stand - height_crouch;

            HitResult hit = world.raycast(ray_origin, ray_dir, check_dist);
            if (!hit.hit) {
                crouched = false;
                sliding = false;
                power_sliding = false;
            }
            // else: can't stand up, stay crouched
        }
    }
}

// ============================================================
//  Slide initiation
// ============================================================

void Player::try_slide(const InputState& input) {
    float speed = sqrtf(velocity.X * velocity.X + velocity.Z * velocity.Z);

    if (speed < slide_min_speed) {
        sliding = false;
        power_sliding = false;
        return;
    }

    sliding = true;
    slide_timer = 0.0f;

    // Power slide: boost if cooldown is ready
    if (slide_boost_timer <= 0.0f) {
        power_sliding = true;
        slide_boost_timer = slide_boost_cooldown;

        // Boost in current movement direction
        HMM_Vec3 hvel = HMM_V3(velocity.X, 0.0f, velocity.Z);
        float hspeed = HMM_LenV3(hvel);
        if (hspeed > 0.1f) {
            HMM_Vec3 dir = HMM_MulV3F(hvel, 1.0f / hspeed);
            velocity.X += dir.X * slide_boost;
            velocity.Z += dir.Z * slide_boost;
        }
    } else {
        power_sliding = false;
    }
}

// ============================================================
//  Lurch: redirect momentum on input change after jumping
//  (Titanfall-style — slerp velocity toward input direction)
// ============================================================

void Player::perform_lurch(const InputState& input) {
    if (lurch_timer <= 0.0f) return;
    if (input.forward == 0.0f && input.right == 0.0f) return;

    // Only lurch on input CHANGE (not every tick)
    bool input_changed = (input.forward != prev_forward || input.right != prev_right);
    if (!input_changed) return;

    HMM_Vec3 hvel = HMM_V3(velocity.X, 0.0f, velocity.Z);
    float hspeed = HMM_LenV3(hvel);
    if (hspeed < 0.5f) return;

    HMM_Vec3 lurch_dir = build_wish_dir(input);
    float lurch_len = HMM_LenV3(lurch_dir);
    if (lurch_len < 0.001f) return;
    lurch_dir = HMM_MulV3F(lurch_dir, 1.0f / lurch_len);

    HMM_Vec3 cur_dir = HMM_MulV3F(hvel, 1.0f / hspeed);

    HMM_Vec3 new_dir = HMM_AddV3(
        HMM_MulV3F(cur_dir, 1.0f - lurch_strength),
        HMM_MulV3F(lurch_dir, lurch_strength)
    );
    float new_len = HMM_LenV3(new_dir);
    if (new_len > 0.001f)
        new_dir = HMM_MulV3F(new_dir, 1.0f / new_len);

    velocity.X = new_dir.X * hspeed;
    velocity.Z = new_dir.Z * hspeed;
}

// ============================================================
//  Ground movement
// ============================================================

void Player::ground_move(float dt, const InputState& input, const CollisionWorld& world) {
    // Determine if this is a valid jump
    bool wants_jump = false;
    if (auto_hop)
        wants_jump = input.jump_held;
    else
        wants_jump = input.jump_held && !jump_held_last;

    // --- Slide-jump: jumping out of a power slide ---
    if (wants_jump && sliding && power_sliding &&
        slide_timer >= slide_min_time_for_jump)
    {
        float hspeed = sqrtf(velocity.X * velocity.X + velocity.Z * velocity.Z);
        if (hspeed >= slide_min_speed_for_jump) {
            // Boost in current horizontal direction
            HMM_Vec3 hdir = HMM_V3(velocity.X, 0.0f, velocity.Z);
            hdir = HMM_MulV3F(hdir, 1.0f / hspeed);
            velocity.X += hdir.X * slide_jump_boost;
            velocity.Z += hdir.Z * slide_jump_boost;
        }
    }

    // --- Jump ---
    if (wants_jump) {
        velocity.Y = jump_speed;
        grounded = false;
        sliding = false;

        // Start lurch window
        lurch_timer = lurch_window;

        do_collide_and_move(dt, world);
        return;
    }

    // --- Sliding ---
    if (sliding) {
        // Low friction, no acceleration (you're sliding on momentum)
        if (!just_landed)
            apply_friction(dt, slide_friction);

        // Allow slight steering while sliding
        HMM_Vec3 wish_dir_raw = build_wish_dir(input);
        float wish_len = HMM_LenV3(wish_dir_raw);
        if (wish_len > 0.001f) {
            HMM_Vec3 wish_dir = HMM_MulV3F(wish_dir_raw, 1.0f / wish_len);
            // Very limited acceleration while sliding (just for steering)
            accelerate(wish_dir, crouch_speed * 0.5f, ground_accel * 0.3f, dt);
        }

        // Auto-cancel when too slow
        float hspeed = sqrtf(velocity.X * velocity.X + velocity.Z * velocity.Z);
        if (hspeed < slide_stop_speed) {
            sliding = false;
            power_sliding = false;
        }

        // On slopes: project along surface
        if (ground_normal.Y < 0.999f && ground_normal.Y > 0.7f) {
            float hspeed = sqrtf(velocity.X * velocity.X + velocity.Z * velocity.Z);
            if (hspeed > 0.001f) {
                HMM_Vec3 hdir = HMM_V3(velocity.X / hspeed, 0.0f, velocity.Z / hspeed);
                float d = HMM_DotV3(hdir, ground_normal);
                HMM_Vec3 slope_dir = HMM_SubV3(hdir, HMM_MulV3F(ground_normal, d));
                float slope_len = HMM_LenV3(slope_dir);
                if (slope_len > 0.001f) {
                    slope_dir = HMM_MulV3F(slope_dir, 1.0f / slope_len);
                    velocity.X = slope_dir.X * hspeed;
                    velocity.Y = slope_dir.Y * hspeed;
                    velocity.Z = slope_dir.Z * hspeed;
                }
            }
        } else {
            if (velocity.Y < 0.0f) velocity.Y = 0.0f;
        }

        do_collide_and_move(dt, world);
        return;
    }

    // --- Normal ground movement ---
    if (!just_landed) {
        apply_friction(dt, friction);
    }

    HMM_Vec3 wish_dir_raw = build_wish_dir(input);
    float wish_len = HMM_LenV3(wish_dir_raw);
    float wish_speed = 0.0f;

    HMM_Vec3 wish_dir = {};
    if (wish_len > 0.001f) {
        wish_dir = HMM_MulV3F(wish_dir_raw, 1.0f / wish_len);
        float speed_cap = crouched ? crouch_speed : max_speed;
        wish_speed = wish_len * speed_cap;
        if (wish_speed > speed_cap) wish_speed = speed_cap;
    }

    accelerate(wish_dir, wish_speed, ground_accel, dt);

    // On slopes: project horizontal velocity along the slope surface
    // so we move smoothly along ramps instead of bouncing.
    // Keep Y handling separate so jumps aren't affected.
    if (ground_normal.Y < 0.999f && ground_normal.Y > 0.7f) {
        // Get horizontal speed and direction
        float hspeed = sqrtf(velocity.X * velocity.X + velocity.Z * velocity.Z);
        if (hspeed > 0.001f) {
            HMM_Vec3 hdir = HMM_V3(velocity.X / hspeed, 0.0f, velocity.Z / hspeed);
            // Project horizontal direction onto slope: remove normal component, renormalize
            float d = HMM_DotV3(hdir, ground_normal);
            HMM_Vec3 slope_dir = HMM_SubV3(hdir, HMM_MulV3F(ground_normal, d));
            float slope_len = HMM_LenV3(slope_dir);
            if (slope_len > 0.001f) {
                slope_dir = HMM_MulV3F(slope_dir, 1.0f / slope_len);
                // Apply: same horizontal speed, directed along slope
                velocity.X = slope_dir.X * hspeed;
                velocity.Y = slope_dir.Y * hspeed;
                velocity.Z = slope_dir.Z * hspeed;
            }
        }
    } else {
        // Flat ground: just clamp negative Y
        if (velocity.Y < 0.0f) velocity.Y = 0.0f;
    }

    do_collide_and_move(dt, world);
}

// ============================================================
//  Air movement
// ============================================================

void Player::air_move(float dt, const InputState& input, const CollisionWorld& world) {
    // Perform lurch (Titanfall-style momentum redirect)
    perform_lurch(input);

    // Normal air acceleration
    HMM_Vec3 wish_dir_raw = build_wish_dir(input);
    float wish_len = HMM_LenV3(wish_dir_raw);
    float wish_speed = 0.0f;

    HMM_Vec3 wish_dir = {};
    if (wish_len > 0.001f) {
        wish_dir = HMM_MulV3F(wish_dir_raw, 1.0f / wish_len);
        wish_speed = wish_len * max_speed;
        if (wish_speed > air_wish_speed)
            wish_speed = air_wish_speed;
    }

    accelerate(wish_dir, wish_speed, air_accel, dt);

    velocity.Y -= gravity * dt;

    do_collide_and_move(dt, world);
}

// ============================================================
//  Main update
// ============================================================

void Player::update(float dt, const InputState& input, const CollisionWorld& world) {
    // Tick timers
    if (slide_boost_timer > 0.0f) slide_boost_timer -= dt;
    if (lurch_timer > 0.0f) lurch_timer -= dt;
    if (sliding) slide_timer += dt;

    bool was_grounded = grounded;
    check_ground(world);
    just_landed = (grounded && !was_grounded);

    // Handle crouch state changes
    handle_crouch(input, world);

    // If we just landed while crouching and moving fast, start a slide
    if (just_landed && crouched && !sliding) {
        try_slide(input);
    }

    if (grounded) {
        ground_move(dt, input, world);
    } else {
        air_move(dt, input, world);
    }

    // Track for edge detection
    jump_held_last = input.jump_held;
    prev_forward = input.forward;
    prev_right = input.right;

    // Void respawn
    if (position.Y < -50.0f) {
        position = HMM_V3(0.0f, 5.0f, 15.0f);
        velocity = HMM_V3(0.0f, 0.0f, 0.0f);
    }
}
