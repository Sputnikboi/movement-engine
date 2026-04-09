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
    Sharpened,       // +10 flat damage
    Piercing,        // passes through enemies and shields
    Crystal_Tipped,  // 2x damage, 1/10 chance to lose tipping on hit
    Aerodynamic,     // 20% faster fire rate, 2x proj speed, 20% more damage
    Poison_Tipped,   // applies poison stack (4 dmg/s per stack, 5s, refreshes)
    Blank,           // round is skipped entirely when firing
    Split,           // round fires twice
    Serrated,        // applies bleed stack: +10% damage taken per stack (permanent)
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

// --- Per-tipping application counts ---
inline int tipping_max_applications(Tipping t) {
    switch (t) {
        case Tipping::Sharpened:      return 3;
        case Tipping::Piercing:       return 2;
        case Tipping::Crystal_Tipped: return 1;
        case Tipping::Aerodynamic:    return 1;
        case Tipping::Poison_Tipped:  return 2;
        case Tipping::Blank:          return 2;
        case Tipping::Split:          return 2;
        case Tipping::Serrated:       return 2;
        default:                      return 1;
    }
}

// --- Display names ---
inline const char* tipping_name(Tipping t) {
    switch (t) {
        case Tipping::None:           return "None";
        case Tipping::Sharpened:      return "Sharpened";
        case Tipping::Piercing:       return "Piercing";
        case Tipping::Crystal_Tipped: return "Crystal Tipped";
        case Tipping::Aerodynamic:    return "Aerodynamic";
        case Tipping::Poison_Tipped:  return "Poison Tipped";
        case Tipping::Blank:          return "Blank";
        case Tipping::Split:          return "Split";
        case Tipping::Serrated:       return "Serrated";
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
        case Tipping::Sharpened:      return "+10 flat damage";
        case Tipping::Piercing:       return "Pierces enemies & shields";
        case Tipping::Crystal_Tipped: return "2x dmg, 10% chance to shatter";
        case Tipping::Aerodynamic:    return "+20% fire rate & dmg, 2x proj speed";
        case Tipping::Poison_Tipped:  return "Poison: 4 dmg/s per stack, 5s";
        case Tipping::Blank:          return "Round is skipped when firing";
        case Tipping::Split:          return "Fires this round twice";
        case Tipping::Serrated:       return "+10% damage taken per stack (permanent)";
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
