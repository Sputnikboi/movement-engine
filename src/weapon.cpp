#include "weapon.h"
#include <cmath>
#include <cstdio>

// ============================================================
//  Wingman preset
// ============================================================

void Weapon::init_wingman() {
    config.name            = "Wingman";
    config.damage          = 70.0f;
    config.fire_rate       = 2.0f;
    config.range           = 100.0f;
    config.mag_size        = 6;
    config.reload_time     = 2.5f;
    config.crit_multiplier = 2.0f;

    config.ads_fov_mult    = 0.7f;
    config.ads_sens_mult   = 0.67f;
    config.ads_speed       = 12.0f;

    config.hip_offset      = HMM_V3(0.25f, -0.2f, 0.4f);
    config.ads_offset      = HMM_V3(0.0f, -0.125f, 0.3f);

    config.recoil_kick     = 0.06f;
    config.recoil_pitch    = -20.0f;
    config.recoil_roll     = 8.0f;
    config.recoil_side     = 0.045f;
    config.recoil_recovery = 10.0f;
    config.recoil_tilt_dir = 1.0f;

    config.reload_buffer_delay = 0.3f;

    config.model_scale     = 1.0f;
    config.model_rotation  = HMM_V3(0.0f, 90.0f, 0.0f);
    config.model_path      = nullptr;

    config.reload_phase1    = 0.25f;
    config.reload_phase2    = 0.80f;
    config.reload_drop_dist = 0.140f;
    config.reload_tilt      = 25.0f;
    config.mag_drop_dist    = 0.850f;
    config.mag_insert_dist  = 0.6f;

    ammo  = config.mag_size;
    magazine.init(config.mag_size);
    last_fired_mod = {};
    state = WeaponState::IDLE;
    reload_buffered = false;
    reload_phase = ReloadPhase::NONE;
    reload_progress = 0.0f;
}

// ============================================================
//  Glock preset
// ============================================================

void Weapon::init_glock() {
    config.name            = "Glock";
    config.damage          = 12.0f;
    config.fire_rate       = 8.0f;
    config.range           = 60.0f;
    config.mag_size        = 17;
    config.reload_time     = 1.2f;
    config.crit_multiplier = 1.5f;

    config.ads_fov_mult    = 0.8f;
    config.ads_sens_mult   = 0.75f;
    config.ads_speed       = 14.0f;

    config.hip_offset      = HMM_V3(0.25f, -0.2f, 0.4f);
    config.ads_offset      = HMM_V3(0.0f, -0.125f, 0.3f);

    config.recoil_kick     = 0.02f;
    config.recoil_pitch    = -6.0f;
    config.recoil_roll     = 3.0f;
    config.recoil_side     = 0.015f;
    config.recoil_recovery = 14.0f;
    config.recoil_tilt_dir = 1.0f;

    config.reload_buffer_delay = 0.15f;

    config.model_scale     = 1.0f;
    config.model_rotation  = HMM_V3(0.0f, 90.0f, 0.0f);

    config.reload_phase1    = 0.20f;
    config.reload_phase2    = 0.75f;
    config.reload_drop_dist = 0.100f;
    config.reload_tilt      = 15.0f;
    config.mag_drop_dist    = 0.650f;
    config.mag_insert_dist  = 0.5f;

    ammo  = config.mag_size;
    magazine.init(config.mag_size);
    last_fired_mod = {};
    state = WeaponState::IDLE;
    reload_buffered = false;
    reload_phase = ReloadPhase::NONE;
    reload_progress = 0.0f;
}

// ============================================================
//  Throwing Knife preset
// ============================================================

void Weapon::init_knife() {
    config.name            = "Throwing Knife";
    config.fire_mode       = FireMode::PROJECTILE;
    config.damage          = 60.0f;
    config.fire_rate       = 1.5f;
    config.range           = 200.0f;
    config.mag_size        = 15;
    config.reload_time     = 0.0f;
    config.crit_multiplier = 2.5f;
    config.infinite_ammo   = true;
    config.no_ads          = true;
    config.lightweight     = true;

    config.proj_speed      = 70.0f;
    config.proj_radius     = 0.3f;
    config.proj_lifetime   = 3.0f;

    config.ads_fov_mult    = 1.0f;
    config.ads_sens_mult   = 1.0f;
    config.ads_speed       = 1.0f;

    config.hip_offset      = HMM_V3(0.2f, -0.18f, 0.35f);
    config.ads_offset      = HMM_V3(0.2f, -0.18f, 0.35f);  // same as hip (no ADS)

    config.recoil_kick     = 0.0f;
    config.recoil_pitch    = 0.0f;
    config.recoil_roll     = 0.0f;
    config.recoil_side     = 0.0f;
    config.recoil_recovery = 10.0f;
    config.recoil_tilt_dir = 1.0f;

    config.reload_buffer_delay = 0.0f;

    config.model_scale     = 0.054f;
    config.model_rotation  = HMM_V3(0.0f, 0.0f, 0.0f);
    config.model_path      = "Kunai.glb";

    config.reload_phase1    = 0.0f;
    config.reload_phase2    = 0.0f;
    config.reload_drop_dist = 0.0f;
    config.reload_tilt      = 0.0f;
    config.mag_drop_dist    = 0.0f;
    config.mag_insert_dist  = 0.0f;

    ammo  = 1;
    state = WeaponState::IDLE;
    reload_buffered = false;
    reload_phase = ReloadPhase::NONE;
    reload_progress = 0.0f;
}

// ============================================================
//  Begin weapon swap (lower current weapon)
// ============================================================

void Weapon::begin_swap() {
    if (state == WeaponState::SWAPPING) return;
    state = WeaponState::SWAPPING;
    swap_timer = swap_duration;
    swap_raising = false;  // lowering first
    holster_lowering = false;
    holster_raising = false;
    fire_timer = 0.0f;
    reload_buffered = false;
    reload_phase = ReloadPhase::NONE;
}

void Weapon::begin_holster() {
    if (state == WeaponState::SWAPPING) return;
    state = WeaponState::SWAPPING;
    swap_timer = swap_duration;
    swap_raising = false;   // lowering
    holster_lowering = true;
    holster_raising = false;
    fire_timer = 0.0f;
    reload_buffered = false;
    reload_phase = ReloadPhase::NONE;
}

void Weapon::begin_unholster() {
    state = WeaponState::SWAPPING;
    swap_timer = swap_duration;
    swap_raising = true;    // raising
    holster_lowering = false;
    holster_raising = true;
}

// ============================================================
//  Update — timers, ADS blend, recoil recovery, state machine
// ============================================================

void Weapon::update(float dt, bool fire_pressed, bool reload_pressed, bool ads_input) {
    // Tick fire cooldown
    if (fire_timer > 0.0f) fire_timer -= dt;

    // Always buffer the reload press during FIRING — we check the delay
    // when deciding whether to consume the buffer, not when capturing it.
    if (reload_pressed && state == WeaponState::FIRING && ammo < config.mag_size)
        reload_buffered = true;

    // ADS blend
    ads_held = ads_input && !config.no_ads;
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

    case WeaponState::FIRING: {
        // How long since the shot was fired
        float fire_cooldown = 1.0f / config.fire_rate;
        float time_since_shot = fire_cooldown - fire_timer;

        // If we have a buffered reload and enough time has passed,
        // transition to RELOADING early (don't wait for full cooldown).
        if (reload_buffered && ammo < config.mag_size &&
            time_since_shot >= config.reload_buffer_delay)
        {
            state = WeaponState::RELOADING;
            reload_timer = config.reload_time;
            reload_buffered = false;
            fire_timer = 0.0f;
            reload_phase = ReloadPhase::MAG_OUT;
            break;
        }

        if (fire_timer <= 0.0f) {
            state = WeaponState::IDLE;
            // Consume buffered reload on natural cooldown end too
            if (reload_buffered && ammo < config.mag_size) {
                state = WeaponState::RELOADING;
                reload_timer = config.reload_time;
                reload_buffered = false;
                reload_phase = ReloadPhase::MAG_OUT;
            }
        }
        break;
    }

    case WeaponState::SWAPPING:
        swap_timer -= dt;
        if (swap_timer <= 0.0f) {
            if (!swap_raising) {
                if (holster_lowering) {
                    // Holster complete — main.cpp will set weapon_holstered
                    // and clear holster_lowering
                    state = WeaponState::IDLE;
                    swap_timer = 0.0f;
                } else {
                    // Lowered — caller should now switch weapon data
                    // Raise phase handled after weapon data swap in main.cpp
                    swap_timer = 0.0f;
                    // Stay in SWAPPING until caller sets swap_raising
                }
            } else {
                // Raised — done
                state = WeaponState::IDLE;
                swap_timer = 0.0f;
                holster_raising = false;
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
            // Don't reinit magazine — mods persist across reloads
            last_fired_mod = {};
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
    if (state == WeaponState::SWAPPING) return false;
    if (fire_timer > 0.0f) return false;
    if (ammo <= 0 && !config.infinite_ammo) return false;

    // Record the mod on the round about to be fired
    int round_index = config.mag_size - ammo;  // 0 = first shot
    last_fired_mod = magazine.get(round_index);

    if (!config.infinite_ammo)
        ammo--;
    fire_timer = 1.0f / config.fire_rate;
    state = WeaponState::FIRING;

    // Apply recoil — side follows tilt direction
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

HMM_Mat4 Weapon::build_base_transform(const Camera& cam, HMM_Vec3 local_offset,
                                        float extra_tilt_deg) const {
    // Lerp between hip and ADS offset (camera-relative)
    HMM_Vec3 offset = HMM_LerpV3(config.hip_offset, ads_blend, config.ads_offset);

    // Apply recoil displacement (camera-relative)
    offset.Z -= recoil_offset;        // kick backward
    offset.X += recoil_side;          // sideways shift

    // Swap: gun drops down
    float swap_blend = 0.0f;
    if (state == WeaponState::SWAPPING) {
        if (!swap_raising) {
            // Lowering: 1.0 at start → 0.0 when timer reaches 0
            swap_blend = swap_timer / swap_duration;
            swap_blend = 1.0f - swap_blend; // 0→1 as it lowers
        } else {
            // Raising: timer counts down, 1→0 as it raises
            swap_blend = swap_timer / swap_duration;
        }
        offset.Y -= 0.4f * swap_blend;
    }

    // Reload: gun drops and tilts based on phase
    float drop_blend = 0.0f;
    float tilt_blend = 0.0f;
    if (state == WeaponState::RELOADING && config.reload_time > 0.0f) {
        float t = reload_progress;
        float p1 = config.reload_phase1;
        float p2 = config.reload_phase2;

        if (t < p1) {
            float local_t = t / p1;
            float ease = local_t * local_t;
            drop_blend = ease;
            tilt_blend = ease;
        } else if (t < p2) {
            drop_blend = 1.0f;
            tilt_blend = 1.0f;
        } else {
            float local_t = (t - p2) / (1.0f - p2);
            float ease = 1.0f - local_t * local_t;
            drop_blend = ease;
            tilt_blend = ease;
        }
    }
    offset.Y -= config.reload_drop_dist * drop_blend;

    // Build camera-relative world position (no local_offset here)
    HMM_Vec3 fwd   = cam.forward();
    HMM_Vec3 right = HMM_MulV3F(cam.right(), -1.0f); // negate to match RH view matrix convention
    HMM_Vec3 up    = HMM_Cross(right, fwd);

    HMM_Vec3 world_pos = HMM_AddV3(cam.position,
        HMM_AddV3(HMM_MulV3F(right, offset.X),
            HMM_AddV3(HMM_MulV3F(up, offset.Y),
                       HMM_MulV3F(fwd, offset.Z))));

    // Camera orientation matrix
    HMM_Mat4 rot = HMM_M4D(1.0f);
    rot.Columns[0] = HMM_V4(right.X, right.Y, right.Z, 0.0f);
    rot.Columns[1] = HMM_V4(up.X,    up.Y,    up.Z,    0.0f);
    rot.Columns[2] = HMM_V4(-fwd.X,  -fwd.Y,  -fwd.Z,  0.0f);
    rot.Columns[3] = HMM_V4(0.0f,    0.0f,    0.0f,    1.0f);

    HMM_Mat4 trans = HMM_Translate(world_pos);
    HMM_Mat4 scale = HMM_Scale(HMM_V3(config.model_scale, config.model_scale, config.model_scale));

    // Model rotation correction (Blender export fix)
    HMM_Mat4 fix = HMM_MulM4(
        HMM_Rotate_RH(HMM_AngleDeg(config.model_rotation.Z), HMM_V3(0, 0, 1)),
        HMM_MulM4(
            HMM_Rotate_RH(HMM_AngleDeg(config.model_rotation.Y), HMM_V3(0, 1, 0)),
            HMM_Rotate_RH(HMM_AngleDeg(config.model_rotation.X), HMM_V3(1, 0, 0))
        )
    );

    // Local-space translation: applied after scale*fix so the offset is
    // relative to the mag's actual position on the gun model, not the
    // gun's origin / camera position.
    HMM_Mat4 local_trans = HMM_M4D(1.0f);
    if (fabsf(local_offset.X) > 0.0001f || fabsf(local_offset.Y) > 0.0001f ||
        fabsf(local_offset.Z) > 0.0001f) {
        local_trans = HMM_Translate(local_offset);
    }

    // Recoil rotation: pitch + roll tilt
    HMM_Mat4 recoil_rot = HMM_MulM4(
        HMM_Rotate_RH(HMM_AngleDeg(-recoil_pitch), HMM_V3(1, 0, 0)),
        HMM_Rotate_RH(HMM_AngleDeg(recoil_roll), HMM_V3(0, 0, 1))
    );

    // Reload tilt
    float total_tilt = config.reload_tilt * tilt_blend + extra_tilt_deg;
    HMM_Mat4 reload_rot = HMM_M4D(1.0f);
    if (fabsf(total_tilt) > 0.001f) {
        reload_rot = HMM_Rotate_RH(HMM_AngleDeg(total_tilt), HMM_V3(0, 0, 1));
    }

    // Vertex transform order (rightmost first):
    //   fix -> scale -> local_translate -> reload_rot -> recoil_rot -> rot -> trans
    return HMM_MulM4(trans, HMM_MulM4(rot, HMM_MulM4(recoil_rot,
                      HMM_MulM4(reload_rot, HMM_MulM4(local_trans,
                      HMM_MulM4(scale, fix))))));
}

// ============================================================
//  Viewmodel matrix (gun body)
// ============================================================

HMM_Mat4 Weapon::get_viewmodel_matrix(const Camera& cam) const {
    return build_base_transform(cam, HMM_V3(0, 0, 0), 0.0f);
}

// ============================================================
//  Mag matrix — offset from body during reload phases
//  Drop goes downward (-Y), insert comes from forward (-Z)
//  since the mag slides in from the direction the gun faces.
// ============================================================

HMM_Mat4 Weapon::get_mag_matrix(const Camera& cam) const {
    if (state != WeaponState::RELOADING || !has_mag_submesh)
        return get_viewmodel_matrix(cam);

    float t = reload_progress;
    float p1 = config.reload_phase1;
    float p2 = config.reload_phase2;

    // Offsets in model local space (after fix rotation):
    //   Y = gun's up axis, so -Y drops the mag straight down from its
    //   attachment point, not from the gun's origin.
    HMM_Vec3 local_off = HMM_V3(0, 0, 0);

    if (t < p1) {
        // Phase 1: mag detaches and drops straight down
        float local_t = t / p1;
        float ease = local_t * local_t;
        local_off.Y = -config.mag_drop_dist * ease;
    } else if (t < p2) {
        // Phase 2: new mag slides up from directly below the mag well
        float local_t = (t - p1) / (p2 - p1);
        float ease = 1.0f - (1.0f - local_t) * (1.0f - local_t); // ease-out
        float remain = 1.0f - ease;
        local_off.Y = -config.mag_insert_dist * remain;
    }
    // Phase 3: mag attached, no offset

    return build_base_transform(cam, local_off, 0.0f);
}

// ============================================================
//  Effective FOV with ADS
// ============================================================

float Weapon::get_effective_fov(float base_fov) const {
    float ads_fov = base_fov * config.ads_fov_mult;
    return base_fov + ads_blend * (ads_fov - base_fov);
}
