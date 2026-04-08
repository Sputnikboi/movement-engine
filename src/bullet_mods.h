#pragma once

#include <cstdint>

// ============================================================
//  Bullet modification system
//
//  Each round in a weapon's magazine has two mod slots:
//    - Tipping:     physical modification to the projectile
//    - Enchantment: magical/elemental effect on the projectile
//
//  When a round is fired, the mods on that specific round
//  are applied. Some mods may also grant passive bonuses
//  (checked across the whole magazine).
// ============================================================

// --- Tipping types (physical projectile mods) ---
enum class Tipping : uint8_t {
    None = 0,
    Hollow_Point,   // bonus damage to unshielded enemies
    Armor_Piercing, // ignores shield HP
    Sharpened,      // increased crit multiplier for this round
    Splitting,      // round splits into multiple on hit (future)
    COUNT
};

// --- Enchantment types (elemental/magical mods) ---
enum class Enchantment : uint8_t {
    None = 0,
    Incendiary,   // sets enemy on fire (DoT)
    Frost,        // slows enemy movement
    Shock,        // chains to nearby enemies (future)
    Vampiric,     // heals player on hit
    Explosive,    // small AoE on impact
    COUNT
};

// --- A single round's mod loadout ---
struct RoundMod {
    Tipping     tipping     = Tipping::None;
    Enchantment enchantment = Enchantment::None;
};

// --- A weapon's full magazine of moddable rounds ---
static constexpr int MAX_MAGAZINE_CAPACITY = 32;

struct Magazine {
    RoundMod rounds[MAX_MAGAZINE_CAPACITY] = {};
    int      capacity = 0;   // how many slots this magazine has

    void init(int cap) {
        capacity = (cap > MAX_MAGAZINE_CAPACITY) ? MAX_MAGAZINE_CAPACITY : cap;
        for (int i = 0; i < MAX_MAGAZINE_CAPACITY; i++)
            rounds[i] = {};
    }

    // Get the mod for the round about to be fired.
    // round_index = (capacity - ammo_remaining) before decrement,
    // i.e. 0 = first round fired, capacity-1 = last round.
    RoundMod get(int round_index) const {
        if (round_index < 0 || round_index >= capacity)
            return {};
        return rounds[round_index];
    }

    // Set mod on a specific round slot.
    void set_tipping(int slot, Tipping t) {
        if (slot >= 0 && slot < capacity) rounds[slot].tipping = t;
    }
    void set_enchantment(int slot, Enchantment e) {
        if (slot >= 0 && slot < capacity) rounds[slot].enchantment = e;
    }

    // Count how many rounds have a specific tipping/enchantment
    // (useful for passive effects that scale with count).
    int count_tipping(Tipping t) const {
        int n = 0;
        for (int i = 0; i < capacity; i++)
            if (rounds[i].tipping == t) n++;
        return n;
    }
    int count_enchantment(Enchantment e) const {
        int n = 0;
        for (int i = 0; i < capacity; i++)
            if (rounds[i].enchantment == e) n++;
        return n;
    }

    // Check if any round has a given mod (for passive bonuses).
    bool has_tipping(Tipping t) const { return count_tipping(t) > 0; }
    bool has_enchantment(Enchantment e) const { return count_enchantment(e) > 0; }
};

// --- Display names ---
inline const char* tipping_name(Tipping t) {
    switch (t) {
        case Tipping::None:           return "None";
        case Tipping::Hollow_Point:   return "Hollow Point";
        case Tipping::Armor_Piercing: return "Armor Piercing";
        case Tipping::Sharpened:      return "Sharpened";
        case Tipping::Splitting:      return "Splitting";
        default:                      return "???";
    }
}

inline const char* enchantment_name(Enchantment e) {
    switch (e) {
        case Enchantment::None:       return "None";
        case Enchantment::Incendiary: return "Incendiary";
        case Enchantment::Frost:      return "Frost";
        case Enchantment::Shock:      return "Shock";
        case Enchantment::Vampiric:   return "Vampiric";
        case Enchantment::Explosive:  return "Explosive";
        default:                      return "???";
    }
}

// Short color-coded descriptions for HUD/shop
inline const char* tipping_desc(Tipping t) {
    switch (t) {
        case Tipping::Hollow_Point:   return "+50% dmg to unshielded";
        case Tipping::Armor_Piercing: return "Ignores shields";
        case Tipping::Sharpened:      return "+1.5x crit multiplier";
        case Tipping::Splitting:      return "Splits on hit";
        default:                      return "";
    }
}

inline const char* enchantment_desc(Enchantment e) {
    switch (e) {
        case Enchantment::Incendiary: return "Burns for 3s";
        case Enchantment::Frost:      return "Slows 40% for 2s";
        case Enchantment::Shock:      return "Chains to nearby";
        case Enchantment::Vampiric:   return "Heals 10% of dmg";
        case Enchantment::Explosive:  return "Small AoE blast";
        default:                      return "";
    }
}

// Magazine mod application state (after buying from shop)
struct PendingModApplication {
    bool active = false;
    bool is_tipping = false;     // true = tipping, false = enchantment
    Tipping tipping = Tipping::None;
    Enchantment enchantment = Enchantment::None;
    int applications_left = 0;   // how many rounds can still be modded
    int max_applications = 2;    // total allowed per purchase
};
