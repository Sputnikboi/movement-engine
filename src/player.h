#pragma once

#include "vendor/HandmadeMath.h"
#include "collision.h"

struct InputState {
    float forward;      // -1 to 1 (W/S)
    float right;        // -1 to 1 (D/A)
    bool  jump_held;    // true while jump key is down
    float yaw;          // camera yaw (radians)
};

struct Player {
    // --- State ---
    HMM_Vec3 position = HMM_V3(0.0f, 2.0f, 15.0f);
    HMM_Vec3 velocity = HMM_V3(0.0f, 0.0f, 0.0f);
    bool     grounded = false;
    HMM_Vec3 ground_normal = HMM_V3(0.0f, 1.0f, 0.0f);

    // --- Dimensions ---
    // Player is modeled as a sphere (at waist height) for collision.
    // Position is at feet.
    float radius      = 0.4f;
    float height      = 1.8f;     // total height
    float eye_offset  = 1.6f;     // eyes from feet

    // --- Source/Quake movement parameters (scaled to meters) ---
    // These are tuned to replicate the Source engine feel.
    float gravity         = 20.0f;     // m/s² downward
    float max_speed       = 8.0f;      // ground wish speed cap
    float air_wish_speed  = 0.76f;     // air wish speed cap (THE air-strafe magic number)
    float ground_accel    = 10.0f;     // ground acceleration factor
    float air_accel       = 70.0f;     // air acceleration factor (high because air_wish_speed is low)
    float friction        = 6.0f;      // ground friction
    float stop_speed      = 2.0f;      // below this, friction is stronger
    float jump_speed      = 7.2f;      // upward velocity on jump (~1.3m jump height)
    float step_height     = 0.5f;      // auto-step up ledges this high

    // --- Ground check config ---
    float ground_check_dist = 0.15f;   // raycast distance below feet

    // --- Jump state ---
    bool jump_held_last = false;        // was jump held last tick
    bool just_landed    = false;        // landed this tick (skip friction)
    bool auto_hop       = false;        // debug: allow hold-to-hop

    // --- Methods ---
    HMM_Vec3 eye_position() const {
        return HMM_AddV3(position, HMM_V3(0.0f, eye_offset, 0.0f));
    }

    // The main physics tick. Call at fixed timestep.
    void update(float dt, const InputState& input, const CollisionWorld& world);

private:
    void check_ground(const CollisionWorld& world);
    void apply_friction(float dt);
    void ground_move(float dt, const InputState& input, const CollisionWorld& world);
    void air_move(float dt, const InputState& input, const CollisionWorld& world);

    // Quake-style acceleration: the heart of everything
    void accelerate(HMM_Vec3 wish_dir, float wish_speed, float accel, float dt);
};
