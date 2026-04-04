#pragma once

#include "vendor/HandmadeMath.h"
#include "collision.h"

struct InputState {
    float forward;      // -1 to 1 (W/S)
    float right;        // -1 to 1 (D/A)
    bool  jump_held;    // true while jump key is down
    bool  crouch_held;  // true while crouch key is down
    float yaw;          // camera yaw (radians)
};

struct Player {
    // --- State ---
    HMM_Vec3 position = HMM_V3(0.0f, 2.0f, 15.0f);
    HMM_Vec3 velocity = HMM_V3(0.0f, 0.0f, 0.0f);
    bool     grounded = false;
    HMM_Vec3 ground_normal = HMM_V3(0.0f, 1.0f, 0.0f);

    // --- Dimensions ---
    float radius         = 0.4f;
    float height_stand   = 1.8f;
    float height_crouch  = 1.0f;
    float eye_stand      = 1.6f;
    float eye_crouch     = 0.8f;

    // --- Source/Quake movement parameters ---
    float gravity         = 20.0f;
    float max_speed       = 8.0f;
    float crouch_speed    = 4.0f;     // ground speed while crouched (not sliding)
    float air_wish_speed  = 0.76f;
    float ground_accel    = 10.0f;
    float air_accel       = 70.0f;
    float friction        = 6.0f;
    float stop_speed      = 2.0f;
    float jump_speed      = 7.2f;
    float step_height     = 0.5f;

    // --- Slide parameters ---
    float slide_friction         = 0.8f;   // much lower than normal friction
    float slide_boost            = 3.0f;   // speed burst on power slide start
    float slide_stop_speed       = 3.0f;   // auto-cancel slide below this speed
    float slide_boost_cooldown   = 2.0f;   // seconds between power slide boosts
    float slide_min_speed        = 6.0f;   // minimum speed to start a slide
    float slide_jump_boost       = 4.0f;   // extra speed when jumping out of power slide
    float slide_min_time_for_jump = 0.3f;  // must slide this long for jump boost
    float slide_min_speed_for_jump = 5.0f; // must be this fast for slide-jump boost

    // --- Lurch parameters ---
    float lurch_window   = 0.5f;    // seconds after jump where lurch is active
    float lurch_strength = 0.5f;    // 0=no redirect, 1=full snap to input dir

    // --- Slope handling ---
    float slope_stick_force = 10.0f;

    // --- Ground check ---
    float ground_check_dist = 0.15f;

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
    float lurch_timer    = 0.0f;
    float prev_forward   = 0.0f;
    float prev_right     = 0.0f;

    // --- Methods ---
    float current_height() const { return crouched ? height_crouch : height_stand; }
    float current_eye_offset() const { return crouched ? eye_crouch : eye_stand; }

    HMM_Vec3 eye_position() const {
        return HMM_AddV3(position, HMM_V3(0.0f, current_eye_offset(), 0.0f));
    }

    void update(float dt, const InputState& input, const CollisionWorld& world);

private:
    void check_ground(const CollisionWorld& world);
    void apply_friction(float dt, float fric);
    void ground_move(float dt, const InputState& input, const CollisionWorld& world);
    void air_move(float dt, const InputState& input, const CollisionWorld& world);
    void handle_crouch(const InputState& input, const CollisionWorld& world);
    void try_slide(const InputState& input);
    void perform_lurch(const InputState& input);
    HMM_Vec3 build_wish_dir(const InputState& input) const;
    void do_collide_and_move(float dt, const CollisionWorld& world);

    void accelerate(HMM_Vec3 wish_dir, float wish_speed, float accel, float dt);
};
