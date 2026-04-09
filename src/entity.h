#pragma once

#include "vendor/HandmadeMath.h"
#include "bullet_mods.h"
#include <cstdint>

// ============================================================
//  Simple entity types — no ECS, just tagged unions.
//  Good enough for a movement shooter.
// ============================================================

enum class EntityType : uint8_t {
    None,
    Drone,
    Rusher,
    Turret,
    Tank,
    Bomber,
    Shielder,
    Projectile,
};

struct Entity {
    EntityType type = EntityType::None;
    bool       alive = false;

    HMM_Vec3 position = {};
    HMM_Vec3 velocity = {};
    float    yaw      = 0.0f;   // facing direction (radians)
    float    pitch    = 0.0f;   // vertical aim (radians, positive = up)

    float    health    = 0.0f;
    float    max_health = 0.0f;
    float    radius    = 0.5f;   // collision sphere radius

    // AI state
    uint8_t  ai_state  = 0;
    float    ai_timer   = 0.0f;
    float    ai_timer2  = 0.0f;
    int      ai_dir     = 1;        // circle direction

    // Projectile owner (index of entity that fired it, -1 = player)
    int      owner = -1;
    float    damage = 0.0f;
    float    lifetime = 0.0f;
    RoundMod round_mod = {};  // tipping + enchantment carried by this projectile
    int      fired_round_idx = 0; // magazine slot this projectile came from

    // Piercing: track already-hit entities to avoid multi-frame damage
    static constexpr int MAX_PIERCE_HITS = 16;
    int      pierce_hit[MAX_PIERCE_HITS] = {};
    int      pierce_hit_count = 0;

    bool has_pierced(int entity_idx) const {
        for (int i = 0; i < pierce_hit_count; i++)
            if (pierce_hit[i] == entity_idx) return true;
        return false;
    }
    void add_pierced(int entity_idx) {
        if (pierce_hit_count < MAX_PIERCE_HITS)
            pierce_hit[pierce_hit_count++] = entity_idx;
    }

    // Spawn / wander
    HMM_Vec3 spawn_pos    = {};     // original spawn position (wander home)
    HMM_Vec3 wander_target = {};    // current wander destination
    float    wander_timer  = 0.0f;  // time until picking new wander target

    // Randomized per-drone
    float    chase_speed  = 10.0f;
    float    circle_speed = 5.0f;
    float    hover_height = 2.0f;
    float    bob_amp      = 0.5f;
    float    bob_freq     = 1.0f;
    float    bob_seed     = 0.0f;

    // Death ragdoll
    float    death_timer  = 0.0f;
    HMM_Vec3 angular_vel  = {};   // tumble spin during ragdoll
    float    tumble_x     = 0.0f; // accumulated tumble angles
    float    tumble_z     = 0.0f;

    // Hit feedback
    float    hit_flash    = 0.0f; // > 0 means flashing white
    float    shield_hp    = 0.0f; // barrier HP from shielder aura

    // Poison DoT
    int      poison_stacks = 0;   // number of active poison stacks
    float    poison_timer  = 0.0f; // remaining poison duration (refreshes on new stack)

    // Bleed (Serrated tipping) — permanent stacks, +10% damage taken per stack
    int      bleed_stacks  = 0;

    // AI throttle
    uint8_t  ai_frame_id  = 0;     // assigned at spawn, used for staggering
    HMM_Vec3 cached_avoid = {};     // cached wall avoidance force
    float    hover_cache_t = 0.0f;  // cached ground distance for hover
    bool     hover_cache_valid = false;
};

static constexpr int MAX_ENTITIES = 256;
