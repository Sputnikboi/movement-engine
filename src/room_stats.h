#pragma once

#include "entity.h"
#include "bullet_mods.h"

// Per-room combat statistics, reset at room start.
struct RoomStats {
    // --- Damage dealt breakdown ---
    float dmg_base        = 0.0f;  // raw weapon damage (before tipping)
    float dmg_sharpened   = 0.0f;  // bonus from Sharpened (+10 flat)
    float dmg_crystal     = 0.0f;  // bonus from Crystal Tipped (2x portion)
    float dmg_aerodynamic = 0.0f;  // bonus from Aerodynamic (+20%)
    float dmg_poison      = 0.0f;  // total poison tick damage
    float dmg_bleed_bonus = 0.0f;  // extra damage from bleed multiplier
    float dmg_total       = 0.0f;  // total damage dealt to enemies

    // --- Damage taken ---
    float taken_total     = 0.0f;
    float taken_rusher    = 0.0f;
    float taken_turret    = 0.0f;
    float taken_tank      = 0.0f;
    float taken_bomber    = 0.0f;
    float taken_projectile = 0.0f; // enemy projectiles

    // --- Currency earned breakdown ---
    int gold_total        = 0;
    int gold_drone        = 0;
    int gold_rusher       = 0;
    int gold_turret       = 0;
    int gold_tank         = 0;
    int gold_bomber       = 0;
    int gold_shielder     = 0;
    int gold_no_damage    = 0;     // bonus for taking zero damage

    // --- Kill counts ---
    int kills_total       = 0;

    void reset() { *this = RoomStats{}; }

    // Record dealt damage with tipping context
    void record_dealt(float base, float actual, Tipping tip, float bleed_mult) {
        float bleed_extra = base * (bleed_mult - 1.0f);
        dmg_bleed_bonus += bleed_extra;

        if (tip == Tipping::Sharpened)        dmg_sharpened   += 10.0f;
        else if (tip == Tipping::Crystal_Tipped) dmg_crystal  += base; // the 2x portion = base
        else if (tip == Tipping::Aerodynamic) dmg_aerodynamic += base * 0.2f;

        dmg_base += actual - bleed_extra;
        if (tip == Tipping::Sharpened)        dmg_base -= 10.0f;
        else if (tip == Tipping::Crystal_Tipped) dmg_base -= base;
        else if (tip == Tipping::Aerodynamic) dmg_base -= base * 0.2f;

        dmg_total += actual;
    }

    void record_poison(float dmg) {
        dmg_poison += dmg;
        dmg_total  += dmg;
    }

    void record_taken(float dmg, EntityType src) {
        taken_total += dmg;
        switch (src) {
            case EntityType::Rusher:  taken_rusher     += dmg; break;
            case EntityType::Turret:  taken_turret     += dmg; break;
            case EntityType::Tank:    taken_tank       += dmg; break;
            case EntityType::Bomber:  taken_bomber     += dmg; break;
            default:                  taken_projectile += dmg; break;
        }
    }

    void record_kill(EntityType t, int gold) {
        kills_total++;
        gold_total += gold;
        switch (t) {
            case EntityType::Drone:    gold_drone    += gold; break;
            case EntityType::Rusher:   gold_rusher   += gold; break;
            case EntityType::Turret:   gold_turret   += gold; break;
            case EntityType::Tank:     gold_tank     += gold; break;
            case EntityType::Bomber:   gold_bomber   += gold; break;
            case EntityType::Shielder: gold_shielder += gold; break;
            default: break;
        }
    }

    void finalize_no_damage_bonus() {
        if (taken_total <= 0.0f) {
            gold_no_damage = 5;
            gold_total += gold_no_damage;
        }
    }
};

// Draw the end-of-room summary popup. Returns true when the player dismisses it.
bool draw_room_summary(const RoomStats& stats, int room_number);
