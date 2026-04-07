#pragma once

#include "level_loader.h"
#include "bullet_mods.h"
#include "vendor/HandmadeMath.h"

struct ProcGenConfig {
    // Room dimensions
    float room_width_min   = 60.0f;
    float room_width_max   = 100.0f;
    float room_depth_min   = 60.0f;
    float room_depth_max   = 100.0f;
    float room_height      = 25.0f;
    float wall_thickness   = 0.5f;

    // Boxes / cover
    int   box_count_min    = 8;
    int   box_count_max    = 20;
    float box_size_min     = 1.5f;
    float box_size_max     = 3.5f;
    float box_height_min   = 1.0f;
    float box_height_max   = 5.0f;
    float box_margin       = 2.0f;   // min distance from walls
    float box_stack_chance = 0.3f;   // chance a box gets a smaller box stacked on top

    // Box clusters
    float cluster_chance   = 0.4f;   // chance a box spawns as cluster center
    int   cluster_size_min = 3;
    int   cluster_size_max = 5;

    // Floor terrain (smooth hills/depressions)
    int   hill_count_min   = 4;
    int   hill_count_max   = 8;
    float hill_height_min  = 1.0f;
    float hill_height_max  = 4.0f;
    float hill_radius_min  = 8.0f;
    float hill_radius_max  = 20.0f;
    int   floor_grid_res   = 96;     // grid subdivisions

    // Tall structures (pillars, towers)
    int   tall_count_min   = 3;
    int   tall_count_max   = 6;
    float tall_size_min    = 2.0f;
    float tall_size_max    = 6.0f;
    float tall_height_min  = 10.0f;
    float tall_height_max  = 22.0f;
    HMM_Vec3 tall_color    = {0.28f, 0.28f, 0.33f};

    // Enemies — budget system
    int   enemy_budget_base = 8;     // starting enemy count (room 1)
    int   enemy_budget_per_room = 2; // extra enemies per room cleared
    int   enemy_budget_max  = 40;    // hard cap
    int   room_number       = 1;     // current room (set before generate)

    // Spawn weights (relative probability per type, shifted by difficulty)
    float weight_drone     = 5.0f;
    float weight_rusher    = 4.0f;
    float weight_turret    = 1.5f;
    float weight_tank      = 0.5f;   // rare early, scales up
    float weight_bomber    = 1.0f;
    float weight_shielder  = 0.5f;   // rare early

    float enemy_height     = 3.0f;

    // Difficulty scaling (applied per room)
    float difficulty        = 1.0f;  // computed from room_number
    float hp_scale_per_room = 0.06f; // +6% HP per room
    float dmg_scale_per_room= 0.04f; // +4% damage per room
    float spd_scale_per_room= 0.02f; // +2% speed per room

    // Manual overrides (if > 0, ignores budget and uses fixed counts)
    int   drone_count      = 0;
    int   rusher_count     = 0;
    int   turret_count     = 0;
    int   tank_count       = 0;
    int   bomber_count     = 0;
    int   shielder_count   = 0;

    // Colors
    HMM_Vec3 floor_color   = {0.3f, 0.3f, 0.35f};
    HMM_Vec3 wall_color    = {0.25f, 0.25f, 0.3f};
    HMM_Vec3 ceiling_color = {0.2f, 0.2f, 0.25f};
    HMM_Vec3 box_color     = {0.4f, 0.35f, 0.3f};
    HMM_Vec3 ramp_color    = {0.35f, 0.35f, 0.3f};

    // Seed (0 = random)
    unsigned int seed      = 0;
};

struct DoorInfo {
    HMM_Vec3 position;
    float    yaw;
    bool     is_exit;
    bool     locked;
};

LevelData generate_level(const ProcGenConfig& config,
                         const Mesh* door_mesh = nullptr,
                         std::vector<DoorInfo>* doors_out = nullptr);

// ============================================================
//  Shop room — physical room between combat rooms
// ============================================================

enum class ShopStandType : uint8_t {
    Weapon,
    Healthpack,
    ModTipping,
    ModEnchantment,
    Empty,
};

struct ShopStand {
    HMM_Vec3      position;       // center of the pedestal top
    ShopStandType type;
    int           weapon_index;   // which weapon (only for Weapon type)
    int           cost;
    bool          purchased;      // already bought this visit
    const char*   label;          // display name

    // Mod stand fields
    Tipping       offered_tipping     = Tipping::None;
    Enchantment   offered_enchantment = Enchantment::None;
};

struct ShopRoomData {
    LevelData     level;
    std::vector<ShopStand> stands;
    HMM_Vec3      exit_door_pos;  // position of the exit door
};

ShopRoomData generate_shop_room(const Mesh* door_mesh = nullptr,
                                std::vector<DoorInfo>* doors_out = nullptr);
