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
    SWAPPING,   // lowering current weapon or raising new one
};

// Reload sub-phases
enum class ReloadPhase : uint8_t {
    NONE,
    MAG_OUT,      // gun lowers, mag detaches and drops
    MAG_SWAP,     // gun stays low, new mag slides in
    GUN_UP,       // gun returns to idle position
};

// ============================================================
//  Static config for a weapon type
// ============================================================

enum class FireMode : uint8_t {
    HITSCAN,
    PROJECTILE,  // spawns a projectile entity
};

struct WeaponConfig {
    const char* name      = "Unknown";
    FireMode fire_mode    = FireMode::HITSCAN;
    float damage          = 70.0f;
    float fire_rate       = 1.5f;     // shots per second
    float range           = 100.0f;
    int   mag_size        = 6;
    float reload_time     = 2.5f;     // seconds
    float crit_multiplier = 2.5f;

    // Projectile mode
    float proj_speed      = 70.0f;  // units/sec
    float proj_radius     = 0.3f;   // hitbox radius
    float proj_lifetime   = 3.0f;   // seconds
    bool  infinite_ammo   = false;  // no reload needed

    // ADS
    float ads_fov_mult    = 0.7f;     // FOV multiplied by this when ADS
    float ads_sens_mult   = 0.67f;    // sensitivity multiplied by this when ADS
    float ads_speed       = 12.0f;     // blend speed (1/s)

    // Viewmodel offsets (relative to camera)
    HMM_Vec3 hip_offset   = {0.25f, -0.2f, 0.4f};  // right, down, forward
    HMM_Vec3 ads_offset   = {0.0f, -0.125f, 0.3f};  // centered for ADS

    // Recoil
    float recoil_kick       = 0.06f;    // backward displacement per shot
    float recoil_pitch      = -20.0f;   // degrees per shot (negative = down in view)
    float recoil_roll       = 8.0f;     // degrees tilt per shot
    float recoil_side       = 0.045f;    // sideways displacement per shot
    float recoil_recovery   = 10.0f;    // recovery speed (1/s)
    float recoil_tilt_dir   = 1.0f;     // +1 = right tilt, -1 = left tilt

    // Reload buffer — how soon after firing you can queue a reload
    float reload_buffer_delay = 0.3f;   // seconds after shot before reload accepted

    // Viewmodel scale + rotation correction (degrees)
    float model_scale     = 1.0f;
    HMM_Vec3 model_rotation = {0.0f, 90.0f, 0.0f};  // X, Y, Z degrees
    const char* model_path = nullptr;  // if set, loads separate model

    // Reload animation — 3 phases (fractions of reload_time)
    float reload_phase1 = 0.25f;   // mag_out end (0..0.25)
    float reload_phase2 = 0.80f;   // mag_swap end (0.25..0.55)
    // phase3 is remainder:          gun_up (0.55..1.0)

    float reload_drop_dist  = 0.140f;   // how far gun drops during reload
    float reload_tilt       = 25.0f;   // degrees of tilt during reload

    // Mag animation
    float mag_drop_dist     = 0.850f;    // how far mag falls after detach
    float mag_insert_dist   = 0.6f;   // how far new mag slides up from below
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
    bool         reload_buffered = false;

    // Recoil state
    float        recoil_offset = 0.0f;  // current backward kick
    float        recoil_pitch  = 0.0f;  // current pitch offset (degrees)
    float        recoil_roll   = 0.0f;  // current roll tilt (degrees)
    float        recoil_side   = 0.0f;  // current sideways offset

    // Viewmodel mesh
    Mesh         viewmodel_mesh;
    bool         mesh_loaded = false;

    // Mag sub-mesh (index range within viewmodel_mesh)
    uint32_t     mag_index_start = 0;
    uint32_t     mag_index_count = 0;
    bool         has_mag_submesh = false;

    // Reload phase tracking
    ReloadPhase  reload_phase = ReloadPhase::NONE;
    float        reload_progress = 0.0f; // 0..1

    // Swap animation
    float        swap_timer   = 0.0f;
    float        swap_duration = 0.35f;  // seconds per half (lower + raise)
    bool         swap_raising  = false;  // false = lowering, true = raising

    // --- Methods ---
    void init_wingman();
    void init_glock();
    void init_knife();
    void update(float dt, bool fire_pressed, bool reload_pressed, bool ads_held);
    bool try_fire();
    void begin_swap();   // start lowering weapon for swap

    // Viewmodel matrices
    HMM_Mat4 get_viewmodel_matrix(const Camera& cam) const;
    HMM_Mat4 get_mag_matrix(const Camera& cam) const;

    float get_effective_fov(float base_fov) const;

private:
    // Build the common base transform. local_offset is in model space
    // (after scale*fix) so it moves geometry relative to its attachment
    // point on the gun, not relative to the camera/gun origin.
    HMM_Mat4 build_base_transform(const Camera& cam, HMM_Vec3 local_offset,
                                   float extra_tilt_deg) const;
};
