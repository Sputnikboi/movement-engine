#include "weapon.h"
#include <cmath>
#include <cstdio>

// ============================================================
//  Wingman preset
// ============================================================

void Weapon::init_wingman() {
    config.damage          = 70.0f;
    config.fire_rate       = 1.5f;
    config.range           = 100.0f;
    config.mag_size        = 6;
    config.reload_time     = 2.5f;
    config.crit_multiplier = 2.5f;

    config.ads_fov_mult    = 0.8f;
    config.ads_speed       = 8.0f;

    config.hip_offset      = HMM_V3(0.25f, -0.2f, 0.4f);
    config.ads_offset      = HMM_V3(0.0f, -0.15f, 0.3f);

    config.recoil_kick     = 0.03f;
    config.recoil_pitch    = 2.0f;
    config.recoil_recovery = 10.0f;

    config.model_scale     = 1.0f;

    ammo  = config.mag_size;
    state = WeaponState::IDLE;
}

// ============================================================
//  Update — timers, ADS blend, recoil recovery, state machine
// ============================================================

void Weapon::update(float dt, bool fire_pressed, bool reload_pressed, bool ads_input) {
    // Tick fire cooldown
    if (fire_timer > 0.0f) fire_timer -= dt;

    // ADS blend
    ads_held = ads_input;
    float ads_target = ads_held ? 1.0f : 0.0f;
    float blend_speed = config.ads_speed * dt;
    if (ads_blend < ads_target)
        ads_blend = fminf(ads_blend + blend_speed, ads_target);
    else if (ads_blend > ads_target)
        ads_blend = fmaxf(ads_blend - blend_speed, ads_target);

    // Recoil recovery
    if (recoil_offset > 0.0f) {
        recoil_offset -= config.recoil_recovery * dt * recoil_offset;
        if (recoil_offset < 0.001f) recoil_offset = 0.0f;
    }
    if (recoil_pitch > 0.0f) {
        recoil_pitch -= config.recoil_recovery * dt * recoil_pitch;
        if (recoil_pitch < 0.01f) recoil_pitch = 0.0f;
    }

    // State machine
    switch (state) {
    case WeaponState::IDLE:
        if (reload_pressed && ammo < config.mag_size) {
            state = WeaponState::RELOADING;
            reload_timer = config.reload_time;
        }
        break;

    case WeaponState::FIRING:
        // Return to idle once fire cooldown expires
        if (fire_timer <= 0.0f)
            state = WeaponState::IDLE;
        break;

    case WeaponState::RELOADING:
        reload_timer -= dt;
        if (reload_timer <= 0.0f) {
            ammo = config.mag_size;
            state = WeaponState::IDLE;
        }
        break;
    }
}

// ============================================================
//  Try to fire a shot
// ============================================================

bool Weapon::try_fire() {
    if (state == WeaponState::RELOADING) return false;
    if (fire_timer > 0.0f) return false;
    if (ammo <= 0) return false;

    ammo--;
    fire_timer = 1.0f / config.fire_rate;
    state = WeaponState::FIRING;

    // Apply recoil
    recoil_offset += config.recoil_kick;
    recoil_pitch  += config.recoil_pitch;

    // Auto-reload on empty
    if (ammo <= 0) {
        state = WeaponState::RELOADING;
        reload_timer = config.reload_time;
    }

    return true;
}

// ============================================================
//  Viewmodel matrix — positions the gun in camera space
// ============================================================

HMM_Mat4 Weapon::get_viewmodel_matrix(const Camera& cam) const {
    // Lerp between hip and ADS offset
    HMM_Vec3 offset = HMM_LerpV3(config.hip_offset, ads_blend, config.ads_offset);

    // Apply recoil (kick backward along camera forward)
    offset.Z -= recoil_offset;

    // Build camera-relative position:
    //   right * offset.X + up * offset.Y + forward * offset.Z
    HMM_Vec3 fwd   = cam.forward();
    HMM_Vec3 right = cam.right();
    HMM_Vec3 up    = HMM_Cross(right, fwd);

    HMM_Vec3 world_pos = HMM_AddV3(cam.position,
        HMM_AddV3(HMM_MulV3F(right, offset.X),
            HMM_AddV3(HMM_MulV3F(up, offset.Y),
                       HMM_MulV3F(fwd, offset.Z))));

    // Build rotation from camera orientation
    // Column vectors: right, up, forward
    HMM_Mat4 rot = HMM_M4D(1.0f);
    rot.Columns[0] = HMM_V4(right.X, right.Y, right.Z, 0.0f);
    rot.Columns[1] = HMM_V4(up.X,    up.Y,    up.Z,    0.0f);
    rot.Columns[2] = HMM_V4(-fwd.X,  -fwd.Y,  -fwd.Z,  0.0f); // -forward for RH
    rot.Columns[3] = HMM_V4(0.0f,    0.0f,    0.0f,    1.0f);

    // Translation
    HMM_Mat4 trans = HMM_Translate(world_pos);

    // Scale
    HMM_Mat4 scale = HMM_Scale(HMM_V3(config.model_scale, config.model_scale, config.model_scale));

    // Model correction: rotate +90 degrees on Z to fix Blender export orientation
    HMM_Mat4 fix = HMM_Rotate_RH(HMM_AngleDeg(90.0f), HMM_V3(0.0f, 0.0f, 1.0f));

    return HMM_MulM4(trans, HMM_MulM4(rot, HMM_MulM4(scale, fix)));
}

// ============================================================
//  Effective FOV with ADS
// ============================================================

float Weapon::get_effective_fov(float base_fov) const {
    float ads_fov = base_fov * config.ads_fov_mult;
    return base_fov + ads_blend * (ads_fov - base_fov);
}
