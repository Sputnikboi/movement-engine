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
    config.ads_offset      = HMM_V3(0.0f, -0.125f, 0.3f);

    config.recoil_kick     = 0.03f;
    config.recoil_pitch    = 30.0f;
    config.recoil_roll     = 5.0f;
    config.recoil_side     = 0.01f;
    config.recoil_recovery = 10.0f;

    config.model_scale     = 1.0f;
    config.model_rotation  = HMM_V3(0.0f, 90.0f, 0.0f);

    config.reload_drop_dist = 0.15f;
    config.reload_tilt      = 25.0f;

    ammo  = config.mag_size;
    state = WeaponState::IDLE;
    reload_buffered = false;
}

// ============================================================
//  Update — timers, ADS blend, recoil recovery, state machine
// ============================================================

void Weapon::update(float dt, bool fire_pressed, bool reload_pressed, bool ads_input) {
    // Tick fire cooldown
    if (fire_timer > 0.0f) fire_timer -= dt;

    // Buffer reload input during fire cooldown
    if (reload_pressed && state == WeaponState::FIRING && ammo < config.mag_size)
        reload_buffered = true;

    // ADS blend
    ads_held = ads_input;
    float ads_target = (ads_held && state != WeaponState::RELOADING) ? 1.0f : 0.0f;
    float blend_speed = config.ads_speed * dt;
    if (ads_blend < ads_target)
        ads_blend = fminf(ads_blend + blend_speed, ads_target);
    else if (ads_blend > ads_target)
        ads_blend = fmaxf(ads_blend - blend_speed, ads_target);

    // Recoil recovery (exponential decay)
    float rec = config.recoil_recovery * dt;
    if (recoil_offset > 0.0f) {
        recoil_offset -= rec * recoil_offset;
        if (recoil_offset < 0.001f) recoil_offset = 0.0f;
    }
    if (recoil_pitch > 0.0f) {
        recoil_pitch -= rec * recoil_pitch;
        if (recoil_pitch < 0.01f) recoil_pitch = 0.0f;
    }
    if (fabsf(recoil_roll) > 0.01f) {
        recoil_roll -= rec * recoil_roll;
        if (fabsf(recoil_roll) < 0.01f) recoil_roll = 0.0f;
    }
    if (fabsf(recoil_side) > 0.0001f) {
        recoil_side -= rec * recoil_side;
        if (fabsf(recoil_side) < 0.0001f) recoil_side = 0.0f;
    }

    // State machine
    switch (state) {
    case WeaponState::IDLE:
        if ((reload_pressed || reload_buffered) && ammo < config.mag_size) {
            state = WeaponState::RELOADING;
            reload_timer = config.reload_time;
            reload_buffered = false;
        }
        break;

    case WeaponState::FIRING:
        if (fire_timer <= 0.0f) {
            state = WeaponState::IDLE;
            // Consume buffered reload immediately
            if (reload_buffered && ammo < config.mag_size) {
                state = WeaponState::RELOADING;
                reload_timer = config.reload_time;
                reload_buffered = false;
            }
        }
        break;

    case WeaponState::RELOADING:
        reload_timer -= dt;
        if (reload_timer <= 0.0f) {
            ammo = config.mag_size;
            state = WeaponState::IDLE;
        }
        reload_buffered = false;
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

    // Apply recoil — alternating side tilt direction
    recoil_offset += config.recoil_kick;
    recoil_pitch  += config.recoil_pitch;
    float side_sign = (ammo % 2 == 0) ? 1.0f : -1.0f;  // alternate per shot
    recoil_roll   += config.recoil_roll * side_sign;
    recoil_side   += config.recoil_side * side_sign;

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

    // Apply recoil displacement
    offset.Z -= recoil_offset;        // kick backward
    offset.X += recoil_side;          // sideways shift

    // Reload animation: drop gun down and tilt it
    float reload_blend = 0.0f;
    if (state == WeaponState::RELOADING && config.reload_time > 0.0f) {
        // Progress 0→1 over the reload
        float t = 1.0f - (reload_timer / config.reload_time);
        // Bell curve: peaks at t=0.5 so gun drops down mid-reload and comes back up
        reload_blend = sinf(t * 3.14159f);
    }
    offset.Y -= config.reload_drop_dist * reload_blend;

    // Build camera-relative position
    HMM_Vec3 fwd   = cam.forward();
    HMM_Vec3 right = cam.right();
    HMM_Vec3 up    = HMM_Cross(right, fwd);

    HMM_Vec3 world_pos = HMM_AddV3(cam.position,
        HMM_AddV3(HMM_MulV3F(right, offset.X),
            HMM_AddV3(HMM_MulV3F(up, offset.Y),
                       HMM_MulV3F(fwd, offset.Z))));

    // Build rotation from camera orientation
    HMM_Mat4 rot = HMM_M4D(1.0f);
    rot.Columns[0] = HMM_V4(right.X, right.Y, right.Z, 0.0f);
    rot.Columns[1] = HMM_V4(up.X,    up.Y,    up.Z,    0.0f);
    rot.Columns[2] = HMM_V4(-fwd.X,  -fwd.Y,  -fwd.Z,  0.0f);
    rot.Columns[3] = HMM_V4(0.0f,    0.0f,    0.0f,    1.0f);

    // Translation
    HMM_Mat4 trans = HMM_Translate(world_pos);

    // Scale
    HMM_Mat4 scale = HMM_Scale(HMM_V3(config.model_scale, config.model_scale, config.model_scale));

    // Model rotation correction
    HMM_Mat4 fix = HMM_MulM4(
        HMM_Rotate_RH(HMM_AngleDeg(config.model_rotation.Z), HMM_V3(0, 0, 1)),
        HMM_MulM4(
            HMM_Rotate_RH(HMM_AngleDeg(config.model_rotation.Y), HMM_V3(0, 1, 0)),
            HMM_Rotate_RH(HMM_AngleDeg(config.model_rotation.X), HMM_V3(1, 0, 0))
        )
    );

    // Recoil rotation: pitch up + roll tilt (applied in camera space)
    HMM_Mat4 recoil_rot = HMM_MulM4(
        HMM_Rotate_RH(HMM_AngleDeg(-recoil_pitch), HMM_V3(1, 0, 0)),  // pitch up
        HMM_Rotate_RH(HMM_AngleDeg(recoil_roll), HMM_V3(0, 0, 1))     // roll tilt
    );

    // Reload tilt (rotate around forward axis to simulate mag swap)
    HMM_Mat4 reload_rot = HMM_M4D(1.0f);
    if (reload_blend > 0.001f) {
        reload_rot = HMM_Rotate_RH(HMM_AngleDeg(config.reload_tilt * reload_blend),
                                    HMM_V3(0, 0, 1));
    }

    return HMM_MulM4(trans, HMM_MulM4(rot, HMM_MulM4(recoil_rot,
                      HMM_MulM4(reload_rot, HMM_MulM4(scale, fix)))));
}

// ============================================================
//  Effective FOV with ADS
// ============================================================

float Weapon::get_effective_fov(float base_fov) const {
    float ads_fov = base_fov * config.ads_fov_mult;
    return base_fov + ads_blend * (ads_fov - base_fov);
}
