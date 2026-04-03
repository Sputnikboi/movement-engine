#include "player.h"
#include "collision.h"
#include <cmath>
#include <cstdio>

// ============================================================
//  Quake-style Accelerate
//  This single function creates ALL of the movement feel:
//  - Ground strafing
//  - Air strafing / bunny hop speed gain
//  - Surf acceleration
//
//  It works by only accelerating up to wish_speed in the
//  wish_direction. If you're already moving fast but NOT in
//  the wish direction, you can still accelerate — which is
//  exactly how air strafing gains speed.
// ============================================================

void Player::accelerate(HMM_Vec3 wish_dir, float wish_speed, float accel, float dt) {
    // Current speed in the wish direction
    float current_speed = HMM_DotV3(velocity, wish_dir);

    // How much speed we need to add to reach wish_speed in this direction
    float add_speed = wish_speed - current_speed;
    if (add_speed <= 0.0f)
        return;  // already at or above wish speed in this direction

    // How much we CAN accelerate this tick
    float accel_speed = accel * wish_speed * dt;

    // Don't overshoot
    if (accel_speed > add_speed)
        accel_speed = add_speed;

    // Apply acceleration
    velocity.X += accel_speed * wish_dir.X;
    velocity.Y += accel_speed * wish_dir.Y;
    velocity.Z += accel_speed * wish_dir.Z;
}

// ============================================================
//  Ground check: raycast down from feet
// ============================================================

void Player::check_ground(const CollisionWorld& world) {
    // Cast from slightly above feet downward
    HMM_Vec3 ray_origin = HMM_AddV3(position, HMM_V3(0.0f, 0.1f, 0.0f));
    HMM_Vec3 ray_dir    = HMM_V3(0.0f, -1.0f, 0.0f);
    float    ray_dist   = 0.1f + ground_check_dist;

    HitResult hit = world.raycast(ray_origin, ray_dir, ray_dist);

    if (g_collision_log) {
        printf("check_ground: pos=(%.2f,%.2f,%.2f) ray_from=(%.2f,%.2f,%.2f) dist=%.3f hit=%d",
               position.X, position.Y, position.Z,
               ray_origin.X, ray_origin.Y, ray_origin.Z,
               ray_dist, hit.hit);
        if (hit.hit) {
            printf(" t=%.4f point=(%.2f,%.2f,%.2f) normal=(%.2f,%.2f,%.2f)",
                   hit.t, hit.point.X, hit.point.Y, hit.point.Z,
                   hit.normal.X, hit.normal.Y, hit.normal.Z);
        }
        printf("\n");
    }

    if (hit.hit && hit.normal.Y > 0.7f) {  // not too steep (< ~45 degrees)
        grounded = true;
        ground_normal = hit.normal;

        // Snap to ground surface
        float ground_y = hit.point.Y;
        if (position.Y < ground_y)
            position.Y = ground_y;
        // Only snap down if we're close (don't pull player into ground while jumping)
        else if (position.Y - ground_y < ground_check_dist && velocity.Y <= 0.0f)
            position.Y = ground_y;

        if (g_collision_log) {
            printf("  -> GROUNDED, snapped pos.Y=%.4f (ground_y=%.4f)\n", position.Y, ground_y);
        }
    } else {
        grounded = false;
        ground_normal = HMM_V3(0.0f, 1.0f, 0.0f);
        if (g_collision_log) {
            printf("  -> AIRBORNE%s\n", hit.hit ? " (too steep)" : " (no hit)");
        }
    }
}

// ============================================================
//  Friction: applied only on ground, decelerates the player
// ============================================================

void Player::apply_friction(float dt) {
    float speed = HMM_LenV3(velocity);
    if (speed < 0.001f) {
        velocity.X = 0.0f;
        velocity.Z = 0.0f;
        return;
    }

    // Quake friction: stronger when below stop_speed
    float control = (speed < stop_speed) ? stop_speed : speed;
    float drop = control * friction * dt;

    float new_speed = speed - drop;
    if (new_speed < 0.0f)
        new_speed = 0.0f;

    float scale = new_speed / speed;
    velocity.X *= scale;
    velocity.Z *= scale;
    // Don't friction the Y component (gravity handles that)
}

// ============================================================
//  Ground movement: friction + accelerate + jump
// ============================================================

void Player::ground_move(float dt, const InputState& input, const CollisionWorld& world) {
    // Determine if this is a valid jump input:
    // - auto_hop: jump whenever held
    // - normal: jump only on fresh press (not held last tick)
    bool wants_jump = false;
    if (auto_hop) {
        wants_jump = input.jump_held;
    } else {
        wants_jump = input.jump_held && !jump_held_last;
    }

    // If jumping this tick, skip friction entirely.
    // This is the key to bhop speed preservation: on the landing tick,
    // you go grounded -> jump immediately with zero friction applied.
    if (wants_jump) {
        velocity.Y = jump_speed;
        grounded = false;

        // Move as airborne this tick (no friction, no ground accel)
        HMM_Vec3 sphere_center = HMM_AddV3(position, HMM_V3(0.0f, height * 0.5f, 0.0f));
        sphere_center = world.slide_move(sphere_center, radius, velocity, dt);
        position = HMM_SubV3(sphere_center, HMM_V3(0.0f, height * 0.5f, 0.0f));
        return;
    }

    // No jump — apply friction (but skip on the landing tick to give
    // a 1-tick window even without jumping, matching Source behavior)
    if (!just_landed) {
        apply_friction(dt);
    }

    // Build wish direction from input + camera yaw
    float forward_x =  cosf(input.yaw);
    float forward_z =  sinf(input.yaw);
    float right_x   =  forward_z;   // perpendicular
    float right_z   = -forward_x;

    HMM_Vec3 wish_dir = HMM_V3(
        forward_x * input.forward + right_x * input.right,
        0.0f,
        forward_z * input.forward + right_z * input.right
    );

    float wish_speed = HMM_LenV3(wish_dir);
    if (wish_speed > 0.001f) {
        wish_dir = HMM_MulV3F(wish_dir, 1.0f / wish_speed);  // normalize
        wish_speed *= max_speed;  // scale to actual speed
        if (wish_speed > max_speed) wish_speed = max_speed;
    } else {
        wish_speed = 0.0f;
    }

    // Ground accelerate
    accelerate(wish_dir, wish_speed, ground_accel, dt);

    // Zero out downward velocity while grounded
    if (velocity.Y < 0.0f)
        velocity.Y = 0.0f;

    // Move through world with collision
    HMM_Vec3 sphere_center = HMM_AddV3(position, HMM_V3(0.0f, height * 0.5f, 0.0f));
    sphere_center = world.slide_move(sphere_center, radius, velocity, dt);
    position = HMM_SubV3(sphere_center, HMM_V3(0.0f, height * 0.5f, 0.0f));
}

// ============================================================
//  Air movement: no friction, restricted wish speed (air strafe magic)
// ============================================================

void Player::air_move(float dt, const InputState& input, const CollisionWorld& world) {
    // No friction in air

    // Build wish direction (same as ground)
    float forward_x =  cosf(input.yaw);
    float forward_z =  sinf(input.yaw);
    float right_x   =  forward_z;
    float right_z   = -forward_x;

    HMM_Vec3 wish_dir = HMM_V3(
        forward_x * input.forward + right_x * input.right,
        0.0f,
        forward_z * input.forward + right_z * input.right
    );

    float wish_speed = HMM_LenV3(wish_dir);
    if (wish_speed > 0.001f) {
        wish_dir = HMM_MulV3F(wish_dir, 1.0f / wish_speed);  // normalize
        wish_speed *= max_speed;

        // THE KEY: cap air wish speed to a tiny value
        // This is what makes air strafing work. Because wish_speed is small,
        // you can almost always accelerate in your strafe direction, which
        // lets you curve and gain speed by coordinating mouse + strafe keys.
        if (wish_speed > air_wish_speed)
            wish_speed = air_wish_speed;
    } else {
        wish_speed = 0.0f;
    }

    // Air accelerate (high factor * low wish speed = the Source feel)
    accelerate(wish_dir, wish_speed, air_accel, dt);

    // Apply gravity
    velocity.Y -= gravity * dt;

    // Move through world with collision
    HMM_Vec3 sphere_center = HMM_AddV3(position, HMM_V3(0.0f, height * 0.5f, 0.0f));
    sphere_center = world.slide_move(sphere_center, radius, velocity, dt);
    position = HMM_SubV3(sphere_center, HMM_V3(0.0f, height * 0.5f, 0.0f));
}

// ============================================================
//  Main update — called at fixed timestep (e.g. 128Hz)
// ============================================================

void Player::update(float dt, const InputState& input, const CollisionWorld& world) {
    bool was_grounded = grounded;
    check_ground(world);

    // Detect the exact tick we transitioned from air -> ground
    just_landed = (grounded && !was_grounded);

    if (grounded) {
        ground_move(dt, input, world);
    } else {
        air_move(dt, input, world);
    }

    // Track jump held state for edge detection (must be after move)
    jump_held_last = input.jump_held;

    // Safety: clamp to level bounds (prevent falling into void)
    if (position.Y < -50.0f) {
        position = HMM_V3(0.0f, 5.0f, 15.0f);
        velocity = HMM_V3(0.0f, 0.0f, 0.0f);
    }
}
