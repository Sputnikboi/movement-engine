#pragma once

#include "vendor/HandmadeMath.h"
#include "collision.h"

struct InputState {
    float forward;      // -1 to 1 (W/S)
    float right;        // -1 to 1 (D/A)
    bool  jump_held;    // true while jump key is down
    bool  crouch_held;  // true while crouch key is down
    float yaw;          // camera yaw (radians)
    float pitch;        // camera pitch (radians, positive = up)
};

struct Player {
    // --- State ---
    HMM_Vec3 position = HMM_V3(0.0f, 2.0f, 15.0f);
    HMM_Vec3 velocity = HMM_V3(0.0f, 0.0f, 0.0f);
    bool     grounded = false;
    HMM_Vec3 ground_normal = HMM_V3(0.0f, 1.0f, 0.0f);

    // --- Dimensions ---
    float radius         = 0.4f;

    // Health
    float health         = 100.0f;
    float max_health     = 100.0f;
    float damage_accum   = 0.0f;  // recent damage for vignette intensity
    float damage_decay   = 3.0f;  // seconds to fully fade vignette
    float height_stand   = 1.8f;
    float height_crouch  = 1.0f;
    float eye_stand      = 1.6f;
    float eye_crouch     = 0.8f;

    // --- Source/Quake movement parameters ---
    float gravity         = 20.0f;
    float max_speed       = 8.0f;       // holstered / base max speed
    float weapon_speed    = 6.5f;       // max speed with weapon out
    float holster_accel_scale = 1.0f;   // accel multiplier when holstered (auto-computed)
    float crouch_speed    = 4.0f;     // ground speed while crouched (not sliding)
    float air_wish_speed  = 0.76f;
    float ground_accel    = 8.0f;
    float air_accel       = 200.0f;
    float friction        = 6.0f;
    float stop_speed      = 2.0f;
    float jump_speed      = 7.2f;
    float step_height     = 0.5f;

    // --- Slide parameters ---
    float slide_friction         = 0.4f;   // much lower than normal friction
    float slide_boost            = 2.1f;   // speed burst on power slide start
    float slide_stop_speed       = 3.0f;   // auto-cancel slide below this speed
    float slide_boost_cooldown   = 1.5f;   // seconds between power slide boosts
    float slide_min_speed        = 6.0f;   // minimum speed to start a slide
    float slide_jump_boost       = 2.8f;   // extra speed when jumping out of power slide
    float slide_min_time_for_jump = 0.24f; // must slide this long for jump boost
    float slide_min_speed_for_jump = 5.0f; // must be this fast for slide-jump boost

    // Soft speed cap (drag scales with excess speed)
    float soft_speed_cap      = 12.0f;  // above this, apply gentle drag
    float soft_cap_drag_min   = 0.5f;   // drag at soft cap (u/s²)
    float soft_cap_drag_max   = 2.0f;   // drag at hard ramp speed (u/s²)
    float soft_cap_drag_full  = 18.0f;  // speed at which drag reaches max
    float hard_speed_cap      = 50.0f;  // absolute max horizontal speed

    // --- Slope landing ---
    float slope_landing_conversion = 0.3f; // fraction of fall speed converted to downhill speed

    // --- Lurch parameters ---
    float lurch_window   = 0.5f;    // seconds after jump where lurch is active
    float lurch_strength = 0.5f;    // 0=no redirect, 1=full snap to input dir
    float lurch_strafe_decay_window = 0.375f;  // accumulator decays over this many seconds
    float lurch_strafe_full_time   = 0.25f;   // seconds of strafing to reach min power
    float lurch_strafe_min_power   = 0.1f;   // lurch power floor (fraction of full strength)
    float lurch_reclaim_per_use    = 0.25f;   // fraction of accum drained per lurch after the first

    // --- Ground check ---
    float ground_check_dist = 0.35f;

    // --- Ladder state ---
    bool     on_ladder         = false;
    int      ladder_volume_idx = -1;    // which volume we're on
    HMM_Vec3 ladder_normal     = {};    // face normal of the ladder surface
    HMM_Vec3 ladder_center     = {};    // center of the ladder volume
    float    ladder_speed_mult  = 1.0f;   // climb speed = max_speed * this
    float    ladder_jump_mult  = 1.2f;  // jump-off speed = max_speed * this
    int      ladder_cooldown_idx = -1;  // volume index to ignore after jump-off
    float    ladder_cooldown     = 0.0f; // seconds remaining before re-grab allowed

    // --- Weapon holster ---
    bool weapon_holstered  = false;
    bool weapon_lightweight = false; // lightweight weapons use holstered speed
    float effective_max_speed() const { return (weapon_holstered || weapon_lightweight) ? max_speed : weapon_speed; }
    float effective_accel() const { return (weapon_holstered || weapon_lightweight) ? ground_accel * (max_speed / weapon_speed) : ground_accel; }

    // --- Crouch/slide state ---
    bool  crouched          = false;
    bool  sliding           = false;
    bool  power_sliding     = false;
    float slide_timer       = 0.0f;
    float slide_boost_timer = 0.0f;

    // --- Jump state ---
    bool  jump_held_last = false;
    bool  just_landed    = false;
    bool  auto_hop       = false;

    // --- Lurch state ---
    float lurch_timer          = 0.0f;
    float lurch_strafe_accum   = 0.0f;  // accumulated air strafe time (0 to lurch_strafe_full_time)
    int   lurch_count          = 0;     // lurches performed in current accumulator window
    float prev_forward         = 0.0f;
    float prev_right           = 0.0f;

    // --- Methods ---
    float current_height() const { return crouched ? height_crouch : height_stand; }
    float current_eye_offset() const { return crouched ? eye_crouch : eye_stand; }

    // Capsule hitbox: line segment from bottom sphere center to top sphere center + radius
    // Bottom = position + (0, radius, 0), Top = position + (0, current_eye_height, 0)
    HMM_Vec3 capsule_bottom() const { return HMM_V3(position.X, position.Y + radius, position.Z); }
    HMM_Vec3 capsule_top() const {
        float h = crouched ? eye_crouch : eye_stand;
        return HMM_V3(position.X, position.Y + h, position.Z);
    }
    float capsule_half_height() const {
        float h = crouched ? eye_crouch : eye_stand;
        return (h - radius) * 0.5f;
    }
    HMM_Vec3 capsule_center() const {
        float h = crouched ? eye_crouch : eye_stand;
        float mid_y = position.Y + (radius + h) * 0.5f;
        return HMM_V3(position.X, mid_y, position.Z);
    }

    HMM_Vec3 eye_position() const {
        return HMM_AddV3(position, HMM_V3(0.0f, current_eye_offset(), 0.0f));
    }

    void update(float dt, const InputState& input, const CollisionWorld& world);

private:
    void check_ground(const CollisionWorld& world);
    void apply_friction(float dt, float fric);
    void ground_move(float dt, const InputState& input, const CollisionWorld& world);
    void air_move(float dt, const InputState& input, const CollisionWorld& world);
    void ladder_move(float dt, const InputState& input, const CollisionWorld& world);
    void apply_soft_speed_cap(float dt);
    void handle_crouch(const InputState& input, const CollisionWorld& world);
    void try_slide(const InputState& input);
    void perform_lurch(const InputState& input);
    HMM_Vec3 build_wish_dir(const InputState& input) const;
    void do_collide_and_move(float dt, const CollisionWorld& world);

    void accelerate(HMM_Vec3 wish_dir, float wish_speed, float accel, float dt);
};
