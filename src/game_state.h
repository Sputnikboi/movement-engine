#pragma once

#include "vendor/HandmadeMath.h"
#include "camera.h"
#include "player.h"
#include "renderer.h"
#include "collision.h"
#include "entity.h"
#include "weapon.h"
#include "effects.h"
#include "procgen.h"
#include "keybinds.h"
#include "config.h"
#include "room_stats.h"
#include "drone.h"
#include "rusher.h"
#include "turret.h"
#include "tank.h"
#include "bomber.h"
#include "shielder.h"

#include <SDL3/SDL.h>
#include <string>
#include <vector>

// Shared game state passed to subsystems (shop, hud, debug menu).
// All fields are references/pointers back into main()'s locals.
struct GameState {
    // Window
    SDL_Window*       window;

    // Core
    Camera&           camera;
    Player&           player;
    Renderer&         renderer;
    CollisionWorld&   collision;
    Config&           config;
    Keybinds&         kb;
    EffectSystem&     effects;

    // Entities
    Entity*           entities;
    int               max_entities;
    DroneConfig&      drone_cfg;
    RusherConfig&     rusher_cfg;
    TurretConfig&     turret_cfg;
    TankConfig&       tank_cfg;
    BomberConfig&     bomber_cfg;
    ShielderConfig&   shielder_cfg;
    bool&             ai_enabled;

    // Weapons
    static constexpr int MAX_WEAPONS = 4;
    Weapon*           weapons;         // array [MAX_WEAPONS]
    int&              active_weapon;
    int&              pending_weapon;
    int&              num_weapons;
    int*              weapon_level;    // array [MAX_WEAPONS]

    // Procgen / rooms
    ProcGenConfig&    procgen_cfg;
    int&              rooms_cleared;
    Mesh*             door_mesh_ptr;
    std::vector<DoorInfo>& active_doors;
    std::string&      current_level_name;

    // Shop
    int&              currency;
    bool&             show_shop;
    bool&             in_shop_room;
    int&              shop_weapon;
    ShopRoomData&     shop_data;
    int&              shop_nearby_stand;
    float&            shop_interact_cooldown;
    PendingModApplication& pending_mod;
    int&                  pending_stand_idx;  // shop stand to mark purchased on Apply

    // Room stats
    RoomStats&        room_stats;
    bool&             show_room_summary;

    // Death / restart
    bool&             player_dead;
    float&            death_timer;
    bool&             show_death_screen;

    // UI flags
    bool&             show_settings;
    bool&             show_hud;
    bool&             show_damage_numbers;
    bool&             show_ladder_debug;
    bool&             show_magazine_view;
    bool&             noclip;
    bool&             running;
    float&            fly_speed;

    // Keybind rebinding (shared between event loop and debug menu)
    int&              rebinding_action;
    int&              rebinding_slot;

    // Level browser (debug menu)
    std::vector<std::string>& level_files;
    bool&             levels_scanned;
    char*             level_path_buf;  // char[512]

    // Apply weapon upgrades after buy/upgrade
    void apply_weapon_upgrades(int w) {
        int ups = weapon_level[w] - 1;
        if (ups <= 0) return;
        switch (w) {
            case 0: // Glock
                weapons[w].config.damage    += ups * 1.0f;
                weapons[w].config.fire_rate *= (1.0f + ups * 0.05f);
                break;
            case 1: // Wingman
                for (int i = 0; i < ups; i++)
                    weapons[w].config.damage *= 1.1f;
                break;
            case 2: // Knife
                weapons[w].config.damage          += ups * 5.0f;
                weapons[w].config.crit_multiplier += ups * 0.1f;
                break;
        }
        weapons[w].ammo = weapons[w].config.mag_size;
    }
};
