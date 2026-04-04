#pragma once

#include "vendor/HandmadeMath.h"
#include "mesh.h"
#include "camera.h"
#include <cstdint>

// ============================================================
//  Weapon state machine
// ============================================================

enum class WeaponState : uint8_t {
    IDLE,
    FIRING,
    RELOADING,
};

// ============================================================
//  Static config for a weapon type
// ============================================================

struct WeaponConfig {
    float damage          = 70.0f;
    float fire_rate       = 1.5f;     // shots per second
    float range           = 100.0f;
    int   mag_size        = 6;
    float reload_time     = 2.5f;     // seconds
    float crit_multiplier = 2.5f;

    // ADS
    float ads_fov_mult    = 0.8f;     // FOV multiplied by this when ADS
    float ads_speed       = 8.0f;     // blend speed (1/s)

    // Viewmodel offsets (relative to camera)
    HMM_Vec3 hip_offset   = {0.25f, -0.2f, 0.4f};  // right, down, forward
    HMM_Vec3 ads_offset   = {0.0f, -0.15f, 0.3f};  // centered for ADS

    // Recoil
    float recoil_kick     = 0.03f;    // backward displacement
    float recoil_pitch    = 2.0f;     // degrees upward per shot
    float recoil_recovery = 10.0f;    // recovery speed (1/s)

    // Viewmodel scale + rotation correction (degrees)
    float model_scale     = 1.0f;
    HMM_Vec3 model_rotation = {0.0f, 90.0f, 0.0f};  // X, Y, Z degrees
};

// ============================================================
//  Weapon runtime state
// ============================================================

struct Weapon {
    WeaponConfig config;
    WeaponState  state      = WeaponState::IDLE;
    int          ammo       = 6;
    float        fire_timer = 0.0f;   // counts down to 0
    float        reload_timer = 0.0f;
    float        ads_blend  = 0.0f;   // 0 = hip, 1 = ADS
    bool         ads_held   = false;

    // Recoil state
    float        recoil_offset = 0.0f;  // current backward kick
    float        recoil_pitch  = 0.0f;  // current pitch offset (degrees)

    // Viewmodel mesh
    Mesh         viewmodel_mesh;
    bool         mesh_loaded = false;

    // --- Methods ---
    void init_wingman();
    void update(float dt, bool fire_pressed, bool reload_pressed, bool ads_held);

    // Attempt to fire. Returns true if a shot was produced.
    bool try_fire();

    // Build the model matrix for rendering the viewmodel.
    HMM_Mat4 get_viewmodel_matrix(const Camera& cam) const;

    // Current effective FOV (accounts for ADS blend).
    float get_effective_fov(float base_fov) const;
};
