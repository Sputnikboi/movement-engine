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

    config.ads_fov_mult    = 0.7f;
    config.ads_sens_mult   = 0.67f;
    config.ads_speed       = 8.0f;

    config.hip_offset      = HMM_V3(0.25f, -0.2f, 0.4f);
    config.ads_offset      = HMM_V3(0.0f, -0.125f, 0.3f);

    config.recoil_kick     = 0.03f;
    config.recoil_pitch    = -30.0f;
    config.recoil_roll     = 5.0f;
    config.recoil_side     = 0.01f;
    config.recoil_recovery = 10.0f;
    config.recoil_tilt_dir = 1.0f;

    config.reload_buffer_delay = 0.3f;

    config.model_scale     = 1.0f;
    config.model_rotation  = HMM_V3(0.0f, 90.0f, 0.0f);

    config.reload_phase1    = 0.25f;
    config.reload_phase2    = 0.55f;
    config.reload_drop_dist = 0.15f;
    config.reload_tilt      = 25.0f;
    config.mag_drop_dist    = 0.4f;
    config.mag_insert_dist  = 0.15f;

    ammo  = config.mag_size;
    state = WeaponState::IDLE;
    reload_buffered = false;
    reload_phase = ReloadPhase::NONE;
    reload_progress = 0.0f;
}

// ============================================================
//  Update — timers, ADS blend, recoil recovery, state machine
// ============================================================

void Weapon::update(float dt, bool fire_pressed, bool reload_pressed, bool ads_input) {
    // Tick fire cooldown
    if (fire_timer > 0.0f) fire_timer -= dt;

    // Buffer reload input during fire cooldown (once enough time has passed)
    if (reload_pressed && state == WeaponState::FIRING && ammo < config.mag_size) {
        float fire_cooldown = 1.0f / config.fire_rate;
        float time_since_shot = fire_cooldown - fire_timer;
        if (time_since_shot >= config.reload_buffer_delay)
            reload_buffered = true;
    }

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
    if (fabsf(recoil_offset) > 0.001f) {
        recoil_offset -= rec * recoil_offset;
        if (fabsf(recoil_offset) < 0.001f) recoil_offset = 0.0f;
    }
    if (fabsf(recoil_pitch) > 0.01f) {
        recoil_pitch -= rec * recoil_pitch;
        if (fabsf(recoil_pitch) < 0.01f) recoil_pitch = 0.0f;
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
        reload_phase = ReloadPhase::NONE;
        reload_progress = 0.0f;
        if ((reload_pressed || reload_buffered) && ammo < config.mag_size) {
            state = WeaponState::RELOADING;
            reload_timer = config.reload_time;
            reload_buffered = false;
            reload_phase = ReloadPhase::MAG_OUT;
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
                reload_phase = ReloadPhase::MAG_OUT;
            }
        }
        break;

    case WeaponState::RELOADING:
        reload_timer -= dt;
        reload_progress = 1.0f - (reload_timer / config.reload_time);
        reload_progress = fminf(fmaxf(reload_progress, 0.0f), 1.0f);

        // Determine current phase
        if (reload_progress < config.reload_phase1)
            reload_phase = ReloadPhase::MAG_OUT;
        else if (reload_progress < config.reload_phase2)
            reload_phase = ReloadPhase::MAG_SWAP;
        else
            reload_phase = ReloadPhase::GUN_UP;

        if (reload_timer <= 0.0f) {
            ammo = config.mag_size;
            state = WeaponState::IDLE;
            reload_phase = ReloadPhase::NONE;
            reload_progress = 0.0f;
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

    // Apply recoil — consistent tilt direction
    recoil_offset += config.recoil_kick;
    recoil_pitch  += config.recoil_pitch;
    float dir = config.recoil_tilt_dir;
    recoil_roll   += config.recoil_roll * dir;
    recoil_side   += config.recoil_side * dir;

    // Auto-reload on empty
    if (ammo <= 0) {
        state = WeaponState::RELOADING;
        reload_timer = config.reload_time;
        reload_phase = ReloadPhase::MAG_OUT;
    }

    return true;
}

// ============================================================
//  Helper: base viewmodel transform (shared by body + mag)
// ============================================================

HMM_Mat4 Weapon::build_base_transform(const Camera& cam, HMM_Vec3 extra_offset,
                                        float extra_tilt_deg) const {
    // Lerp between hip and ADS offset
    HMM_Vec3 offset = HMM_LerpV3(config.hip_offset, ads_blend, config.ads_offset);

    // Apply recoil displacement
    offset.Z -= recoil_offset;        // kick backward
    offset.X += recoil_side;          // sideways shift

    // Reload: gun drops and tilts based on phase
    float drop_blend = 0.0f;
    float tilt_blend = 0.0f;
    if (state == WeaponState::RELOADING && config.reload_time > 0.0f) {
        float t = reload_progress;
        float p1 = config.reload_phase1;
        float p2 = config.reload_phase2;

        if (t < p1) {
            // Phase 1 (MAG_OUT): gun drops down, 0→1 ease-in
            float local_t = t / p1;
            float ease = local_t * local_t; // quadratic ease-in
            drop_blend = ease;
            tilt_blend = ease;
        } else if (t < p2) {
            // Phase 2 (MAG_SWAP): gun stays fully dropped
            drop_blend = 1.0f;
            tilt_blend = 1.0f;
        } else {
            // Phase 3 (GUN_UP): gun returns to idle, 1→0 ease-out
            float local_t = (t - p2) / (1.0f - p2);
            float ease = 1.0f - local_t * local_t; // quadratic ease-out
            drop_blend = ease;
            tilt_blend = ease;
        }
    }
    offset.Y -= config.reload_drop_dist * drop_blend;

    // Add extra offset (for mag separation)
    offset = HMM_AddV3(offset, extra_offset);

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

    HMM_Mat4 trans = HMM_Translate(world_pos);
    HMM_Mat4 scale = HMM_Scale(HMM_V3(config.model_scale, config.model_scale, config.model_scale));

    // Model rotation correction
    HMM_Mat4 fix = HMM_MulM4(
        HMM_Rotate_RH(HMM_AngleDeg(config.model_rotation.Z), HMM_V3(0, 0, 1)),
        HMM_MulM4(
            HMM_Rotate_RH(HMM_AngleDeg(config.model_rotation.Y), HMM_V3(0, 1, 0)),
            HMM_Rotate_RH(HMM_AngleDeg(config.model_rotation.X), HMM_V3(1, 0, 0))
        )
    );

    // Recoil rotation: pitch + roll tilt
    HMM_Mat4 recoil_rot = HMM_MulM4(
        HMM_Rotate_RH(HMM_AngleDeg(-recoil_pitch), HMM_V3(1, 0, 0)),  // pitch
        HMM_Rotate_RH(HMM_AngleDeg(recoil_roll), HMM_V3(0, 0, 1))     // roll
    );

    // Reload tilt
    float total_tilt = config.reload_tilt * tilt_blend + extra_tilt_deg;
    HMM_Mat4 reload_rot = HMM_M4D(1.0f);
    if (fabsf(total_tilt) > 0.001f) {
        reload_rot = HMM_Rotate_RH(HMM_AngleDeg(total_tilt), HMM_V3(0, 0, 1));
    }

    return HMM_MulM4(trans, HMM_MulM4(rot, HMM_MulM4(recoil_rot,
                      HMM_MulM4(reload_rot, HMM_MulM4(scale, fix)))));
}

// ============================================================
//  Viewmodel matrix (gun body — everything except mag during reload)
// ============================================================

HMM_Mat4 Weapon::get_viewmodel_matrix(const Camera& cam) const {
    return build_base_transform(cam, HMM_V3(0, 0, 0), 0.0f);
}

// ============================================================
//  Mag matrix — offset from body during reload phases
// ============================================================

HMM_Mat4 Weapon::get_mag_matrix(const Camera& cam) const {
    if (state != WeaponState::RELOADING || !has_mag_submesh)
        return get_viewmodel_matrix(cam);

    float t = reload_progress;
    float p1 = config.reload_phase1;
    float p2 = config.reload_phase2;
    HMM_Vec3 mag_extra = HMM_V3(0, 0, 0);

    if (t < p1) {
        // Phase 1: mag detaching and dropping
        float local_t = t / p1;
        float ease = local_t * local_t;
        mag_extra.Y = -config.mag_drop_dist * ease;
    } else if (t < p2) {
        // Phase 2: old mag is gone, new mag slides up from below
        float local_t = (t - p1) / (p2 - p1);
        float ease = 1.0f - (1.0f - local_t) * (1.0f - local_t); // ease-out
        // Start from below, end at body position
        mag_extra.Y = -config.mag_insert_dist * (1.0f - ease);
    }
    // Phase 3: mag is attached, moves with body (no extra offset)

    return build_base_transform(cam, mag_extra, 0.0f);
}

// ============================================================
//  Effective FOV with ADS
// ============================================================

float Weapon::get_effective_fov(float base_fov) const {
    float ads_fov = base_fov * config.ads_fov_mult;
    return base_fov + ads_blend * (ads_fov - base_fov);
}
