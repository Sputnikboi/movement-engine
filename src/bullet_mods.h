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

// --- Enchantment types (passive weapon-wide buffs, scale by count in magazine) ---
enum class Enchantment : uint8_t {
    None = 0,
    Wrath,       // +5 damage per enchanted round
    Gilded,      // +5 gold at end of round per enchanted round
    Etheral,     // +1 mag capacity per enchanted round (limited)
    Storming,    // +5% fire rate per enchanted round
    Fortified,   // +10 max HP per enchanted round
    Vampiric,    // +1 HP per kill per enchanted round
    Levitating,  // +15% reload speed per enchanted round
    Catalytic,   // debuffs +20% more effective per enchanted round
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

    // Resize magazine (preserves existing round mods, new slots are empty)
    void resize(int new_cap) {
        if (new_cap < 0) new_cap = 0;
        if (new_cap > MAX_MAGAZINE_CAPACITY) new_cap = MAX_MAGAZINE_CAPACITY;
        // Clear any slots beyond new capacity
        for (int i = new_cap; i < capacity; i++) rounds[i] = {};
        capacity = new_cap;
    }

    // Swap two round slots (for reordering in shop)
    void swap(int a, int b) {
        if (a >= 0 && a < capacity && b >= 0 && b < capacity && a != b) {
            RoundMod tmp = rounds[a];
            rounds[a] = rounds[b];
            rounds[b] = tmp;
        }
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
        case Tipping::Crystal_Tipped: return 2;
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
        case Enchantment::Wrath:      return "Wrath";
        case Enchantment::Gilded:     return "Gilded";
        case Enchantment::Etheral:    return "Etheral";
        case Enchantment::Storming:   return "Storming";
        case Enchantment::Fortified:  return "Fortified";
        case Enchantment::Vampiric:   return "Vampiric";
        case Enchantment::Levitating: return "Levitating";
        case Enchantment::Catalytic:  return "Catalytic";
        default:                      return "???";
    }
}

inline int enchantment_max_applications(Enchantment e) {
    if (e == Enchantment::Etheral) return 1; // very limited
    return 2; // default for all others
}

// Short color-coded descriptions for HUD/shop
inline const char* tipping_desc(Tipping t) {
    switch (t) {
        case Tipping::Sharpened:      return "+10 flat damage";
        case Tipping::Piercing:       return "Pierces enemies & shields";
        case Tipping::Crystal_Tipped: return "2x dmg, 10% chance to shatter";
        case Tipping::Aerodynamic:    return "+20% fire rate (all), +20% dmg & 2x proj speed (projectiles)";
        case Tipping::Poison_Tipped:  return "Poison: 6 dmg/s per stack, 5s duration";
        case Tipping::Blank:          return "Round is skipped when firing";
        case Tipping::Split:          return "Fires this round twice";
        case Tipping::Serrated:       return "+10% damage taken per stack (permanent)";
        default:                      return "";
    }
}

inline const char* enchantment_desc(Enchantment e) {
    switch (e) {
        case Enchantment::Wrath:      return "+5 weapon damage per card";
        case Enchantment::Gilded:     return "+5 gold per card (end of room)";
        case Enchantment::Etheral:    return "+1 mag capacity per card (max 1)";
        case Enchantment::Storming:   return "+5% fire rate per card";
        case Enchantment::Fortified:  return "+10 max HP per card";
        case Enchantment::Vampiric:   return "+1 HP per kill, per card";
        case Enchantment::Levitating: return "+15% reload speed per card";
        case Enchantment::Catalytic:  return "Debuffs +20% stronger per card";
        default:                      return "";
    }
}

// Computed weapon bonuses from magazine enchantments (recalculate when mag changes)
struct WeaponBonuses {
    float bonus_damage     = 0.0f;  // Wrath: +5 per round
    int   bonus_gold       = 0;     // Gilded: +5 per round (end of room)
    int   bonus_mag        = 0;     // Etheral: +1 per round
    float fire_rate_mult   = 1.0f;  // Storming: +5% per round
    float bonus_max_hp     = 0.0f;  // Fortified: +10 per round
    int   vampiric_heal    = 0;     // Vampiric: +1 HP per kill per round
    float reload_speed_mult= 1.0f;  // Levitating: +15% per round (faster = lower time)
    float catalytic_mult   = 1.0f;  // Catalytic: debuffs +20% per round

    void compute(const Magazine& mag) {
        *this = WeaponBonuses{};
        int wrath_n      = mag.count_enchantment(Enchantment::Wrath);
        int gilded_n     = mag.count_enchantment(Enchantment::Gilded);
        int etheral_n    = mag.count_enchantment(Enchantment::Etheral);
        int storming_n   = mag.count_enchantment(Enchantment::Storming);
        int fortified_n  = mag.count_enchantment(Enchantment::Fortified);
        int vampiric_n   = mag.count_enchantment(Enchantment::Vampiric);
        int levitating_n = mag.count_enchantment(Enchantment::Levitating);
        int catalytic_n  = mag.count_enchantment(Enchantment::Catalytic);

        bonus_damage      = 5.0f * wrath_n;
        bonus_gold        = 5 * gilded_n;
        bonus_mag         = etheral_n;
        fire_rate_mult    = 1.0f + 0.05f * storming_n;
        bonus_max_hp      = 10.0f * fortified_n;
        vampiric_heal     = vampiric_n;
        reload_speed_mult = 1.0f + 0.15f * levitating_n; // multiply into reload to make it faster
        catalytic_mult    = 1.0f + 0.20f * catalytic_n;
    }
};

// Magazine mod application state (after buying from shop)
struct PendingModApplication {
    bool active = false;
    bool is_tipping = false;     // true = tipping, false = enchantment
    Tipping tipping = Tipping::None;
    Enchantment enchantment = Enchantment::None;
    int max_applications = 2;    // max rounds selectable
    int cost = 0;                // purchase cost (for cancel refund)

    // Selection state (toggle per round)
    bool selected[MAX_MAGAZINE_CAPACITY] = {};
    int  selected_count = 0;

    void clear_selection() {
        for (int i = 0; i < MAX_MAGAZINE_CAPACITY; i++) selected[i] = false;
        selected_count = 0;
    }

    void toggle(int slot) {
        if (slot < 0 || slot >= MAX_MAGAZINE_CAPACITY) return;
        if (selected[slot]) {
            selected[slot] = false;
            selected_count--;
        } else if (selected_count < max_applications) {
            selected[slot] = true;
            selected_count++;
        }
    }
};
