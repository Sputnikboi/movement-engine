#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <filesystem>

#include "renderer.h"
#include "camera.h"
#include "mesh.h"
#include "collision.h"
#include "player.h"
#include "config.h"
#include "keybinds.h"
#include "level_loader.h"
#include "entity.h"
#include "drone.h"
#include "rusher.h"
#include "turret.h"
#include "tank.h"
#include "bomber.h"
#include "shielder.h"
#include "procgen.h"
#include "entity_render.h"
#include "effects.h"
#include "weapon.h"

#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_impl_sdl3.h"
#include "vendor/imgui/imgui_impl_vulkan.h"

#include "game_state.h"
#include "shop.h"
#include "hud.h"
#include "debug_menu.h"
#include "magazine_view.h"
#include "damage_numbers.h"
#include "bullet_mods.h"

namespace fs = std::filesystem;

// Load a level and update all engine state
static bool load_level(const std::string& path,
                       Renderer& renderer, CollisionWorld& collision,
                       Player& player, Camera& camera,
                       bool& noclip, std::string& current_level_name,
                       Entity entities[] = nullptr, int max_entities = 0,
                       const DroneConfig* drone_cfg = nullptr,
                       const RusherConfig* rusher_cfg = nullptr,
                       const TurretConfig* turret_cfg = nullptr,
                       const TankConfig* tank_cfg = nullptr,
                       const BomberConfig* bomber_cfg = nullptr,
                       const ShielderConfig* shielder_cfg = nullptr)
{
    LevelData ld = load_level_gltf(path);
    if (ld.mesh.vertices.empty()) {
        fprintf(stderr, "Failed to load level: %s\n", path.c_str());
        return false;
    }

    // Build collision BEFORE merging visual-only geometry
    collision.triangles.clear();
    collision.ladder_volumes.clear();
    collision.build_from_mesh(ld.mesh);

    // Merge visual-only geometry (VLadder etc) into render mesh — no collision
    if (!ld.visual_only_mesh.vertices.empty()) {
        uint32_t base = (uint32_t)ld.mesh.vertices.size();
        ld.mesh.vertices.insert(ld.mesh.vertices.end(),
            ld.visual_only_mesh.vertices.begin(), ld.visual_only_mesh.vertices.end());
        for (uint32_t idx : ld.visual_only_mesh.indices)
            ld.mesh.indices.push_back(base + idx);
    }

    // Upload to renderer (now includes visual-only geometry)
    renderer.reload_mesh(ld.mesh);

    // Extract ladder volumes from ladder-specific geometry (no collision surface)
    for (const auto& sub : ld.ladder_submeshes) {
        collision.add_ladder_volume(ld.ladder_mesh, sub.index_start, sub.index_count);
    }

    // Update player
    player.position = ld.spawn_pos;
    player.velocity = HMM_V3(0, 0, 0);
    player.grounded = false;

    if (ld.has_spawn) {
        noclip = false;
        camera.position = player.eye_position();
    } else {
        noclip = true;
        camera.position = HMM_AddV3(ld.spawn_pos, HMM_V3(0, 5, 0));
        printf("No spawn point — starting in noclip.\n");
    }

    // Spawn preplaced enemies
    if (entities && max_entities > 0) {
        // Clear existing entities
        for (int i = 0; i < max_entities; i++)
            entities[i].alive = false;
        for (const auto& es : ld.enemy_spawns) {
            if (es.type == EntityType::Drone && drone_cfg)
                drone_spawn(entities, max_entities, es.position, *drone_cfg);
            else if (es.type == EntityType::Rusher && rusher_cfg)
                rusher_spawn(entities, max_entities, es.position, *rusher_cfg);
            else if (es.type == EntityType::Turret && turret_cfg)
                turret_spawn(entities, max_entities, es.position, *turret_cfg);
            else if (es.type == EntityType::Tank && tank_cfg)
                tank_spawn(entities, max_entities, es.position, *tank_cfg);
            else if (es.type == EntityType::Bomber && bomber_cfg)
                bomber_spawn(entities, max_entities, es.position, *bomber_cfg);
            else if (es.type == EntityType::Shielder && shielder_cfg)
                shielder_spawn(entities, max_entities, es.position, *shielder_cfg);
        }
        if (!ld.enemy_spawns.empty())
            printf("Spawned %zu preplaced enemies\n", ld.enemy_spawns.size());
    }

    // Extract filename for display
    current_level_name = fs::path(path).filename().string();
    return true;
}

int main(int argc, char* argv[]) {

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "Movement Engine",
        1280, 720,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE
    );
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_SetWindowRelativeMouseMode(window, true);

    // --- Load door model early (needed by procgen) ---
    Mesh door_mesh;
    {
        const char* door_paths[] = {
            "assets/Door.glb", "../assets/Door.glb", "../../assets/Door.glb",
        };
        for (const char* dp : door_paths) {
            LevelData dm = load_level_gltf(dp);
            if (!dm.mesh.vertices.empty()) {
                door_mesh = std::move(dm.mesh);
                printf("Loaded door model from '%s': %zu verts\n", dp, door_mesh.vertices.size());
                break;
            }
        }
    }
    Mesh* door_mesh_ptr = door_mesh.vertices.empty() ? nullptr : &door_mesh;
    std::vector<DoorInfo> active_doors;

    // --- Build level + collision ---
    Mesh level;
    HMM_Vec3 spawn_pos = HMM_V3(0.0f, 1.0f, 15.0f);
    bool custom_level = false;
    bool has_spawn = false;
    bool is_procgen = false;
    std::vector<EnemySpawn> initial_enemy_spawns;

    // Determine level path: command line arg, or default to Room1
    const char* level_arg = (argc > 1) ? argv[1] : nullptr;
    static const char* default_paths[] = {
        "levels/Room1.glb", "../levels/Room1.glb", "../../levels/Room1.glb",
        "levels/room1.glb", "../levels/room1.glb", "../../levels/room1.glb",
    };

    LevelData ld;
    if (level_arg) {
        ld = load_level_gltf(level_arg);
        if (ld.mesh.vertices.empty())
            fprintf(stderr, "Failed to load level '%s'\n", level_arg);
    }
    if (ld.mesh.vertices.empty() && !level_arg) {
        for (const char* p : default_paths) {
            ld = load_level_gltf(p);
            if (!ld.mesh.vertices.empty()) {
                printf("Loaded default level: %s\n", p);
                break;
            }
        }
    }

    CollisionWorld collision;

    if (!ld.mesh.vertices.empty()) {
        spawn_pos = ld.spawn_pos;
        has_spawn = ld.has_spawn;
        initial_enemy_spawns = std::move(ld.enemy_spawns);
        custom_level = true;

        // Build collision from mesh BEFORE merging visual-only geo
        collision.build_from_mesh(ld.mesh);

        // Extract ladder volumes
        for (const auto& sub : ld.ladder_submeshes)
            collision.add_ladder_volume(ld.ladder_mesh, sub.index_start, sub.index_count);

        // Merge visual-only geometry into render mesh
        if (!ld.visual_only_mesh.vertices.empty()) {
            uint32_t base = (uint32_t)ld.mesh.vertices.size();
            ld.mesh.vertices.insert(ld.mesh.vertices.end(),
                ld.visual_only_mesh.vertices.begin(), ld.visual_only_mesh.vertices.end());
            for (uint32_t idx : ld.visual_only_mesh.indices)
                ld.mesh.indices.push_back(base + idx);
        }

        level = std::move(ld.mesh);
    } else {
        printf("No level file found, generating procedural level\n");
        ProcGenConfig pg_cfg;
        pg_cfg.room_number = 1;
        pg_cfg.difficulty = 1.0f;
        ld = generate_level(pg_cfg, door_mesh_ptr, &active_doors);
        spawn_pos = ld.spawn_pos;
        has_spawn = ld.has_spawn;
        initial_enemy_spawns = std::move(ld.enemy_spawns);
        custom_level = true;
        is_procgen = true;

        collision.build_from_mesh(ld.mesh);
        level = std::move(ld.mesh);
    }

    // --- Init renderer ---
    Renderer renderer;
    if (!renderer.init(window, level)) {
        fprintf(stderr, "Renderer init failed\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // --- Init ImGui ---
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding   = 6.0f;
    style.FrameRounding    = 4.0f;
    style.GrabRounding     = 4.0f;
    style.WindowBorderSize = 1.0f;
    style.FramePadding     = ImVec2(8, 4);
    style.ItemSpacing      = ImVec2(8, 6);

    ImGui_ImplSDL3_InitForVulkan(window);

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance       = renderer.get_instance();
    init_info.PhysicalDevice = renderer.get_physical_device();
    init_info.Device         = renderer.get_device();
    init_info.QueueFamily    = renderer.get_graphics_family();
    init_info.Queue          = renderer.get_graphics_queue();
    init_info.DescriptorPool = renderer.get_imgui_pool();
    init_info.MinImageCount  = renderer.get_min_image_count();
    init_info.ImageCount     = renderer.get_min_image_count();
    init_info.RenderPass     = renderer.get_render_pass();
    init_info.MSAASamples    = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&init_info);

    // --- Player + camera + config ---
    Camera camera;
    Player player;
    Config config;

    config.load();
    config.apply(camera, player);

    Keybinds& kb = config.keybinds;

    player.position = spawn_pos;
    if (is_procgen) camera.yaw = HMM_PI32 / 2.0f; // face +Z (into room)

    // Start in noclip if custom level has no spawn point
    bool noclip = (custom_level && !has_spawn);
    if (noclip) {
        camera.position = HMM_AddV3(spawn_pos, HMM_V3(0.0f, 5.0f, 0.0f));
        printf("No spawn point — starting in noclip. Press V to drop in.\n");
    }
    bool show_settings = false;
    bool show_hud = true;
    bool show_ladder_debug = false;
    bool show_magazine_view = false;
    bool ai_enabled = true;
    float fly_speed = 15.0f;

    // Level browser state
    std::string current_level_name = (custom_level && level_arg) ? fs::path(level_arg).filename().string()
                                   : custom_level              ? "Room1.glb"
                                   :                             "Procedural";
    ProcGenConfig procgen_cfg;
    int rooms_cleared = 0;
    std::vector<std::string> level_files;
    bool levels_scanned = false;
    char level_path_buf[512] = "";

    // --- Entities ---
    Entity entities[MAX_ENTITIES] = {};
    DroneConfig drone_cfg;
    RusherConfig rusher_cfg;
    TurretConfig turret_cfg;
    TankConfig tank_cfg;
    BomberConfig bomber_cfg;
    ShielderConfig shielder_cfg;
    EffectSystem effects;
    DamageNumberSystem dmg_numbers;
    effects.init();
    float total_time = 0.0f;

    // Spawn preplaced enemies from level data
    for (const auto& es : initial_enemy_spawns) {
        if (es.type == EntityType::Drone)
            drone_spawn(entities, MAX_ENTITIES, es.position, drone_cfg);
        else if (es.type == EntityType::Rusher)
            rusher_spawn(entities, MAX_ENTITIES, es.position, rusher_cfg);
        else if (es.type == EntityType::Turret)
            turret_spawn(entities, MAX_ENTITIES, es.position, turret_cfg);
        else if (es.type == EntityType::Tank)
            tank_spawn(entities, MAX_ENTITIES, es.position, tank_cfg);
        else if (es.type == EntityType::Bomber)
            bomber_spawn(entities, MAX_ENTITIES, es.position, bomber_cfg);
        else if (es.type == EntityType::Shielder)
            shielder_spawn(entities, MAX_ENTITIES, es.position, shielder_cfg);
    }
    if (!initial_enemy_spawns.empty())
        printf("Spawned %zu preplaced enemies\n", initial_enemy_spawns.size());

    // --- Weapons ---
    constexpr int MAX_WEAPONS = 4;
    Weapon weapons[MAX_WEAPONS];
    int    active_weapon = 0;
    int    pending_weapon = -1;  // weapon to switch to after lowering
    int    num_weapons = 1;      // weapons unlocked (start with 1)

    // All weapon templates initialised (for shop/model loading), only slot 0 active at start
    weapons[0].init_glock();
    weapons[1].init_wingman();
    weapons[2].init_knife();

    // Shop system
    int    currency = 0;
    bool   show_shop = false;       // legacy flag — kept for gating, true when in_shop_room
    bool   in_shop_room = false;    // player is physically in the shop room
    int    shop_weapon = -1;        // which weapon is offered this shop (-1 = none yet)
    int    weapon_level[MAX_WEAPONS] = {1, 0, 0, 0}; // 0=not owned, 1+=owned+level
    ShopRoomData shop_data;         // current shop room geometry + stands
    int    shop_nearby_stand = -1;  // index of stand player is near (-1 = none)
    float  shop_interact_cooldown = 0.0f; // prevent double-buy
    PendingModApplication pending_mod;
    int pending_stand_idx = -1;

    // Room stats
    RoomStats room_stats;
    bool show_room_summary = false;

    auto kill_reward = [](EntityType t) -> int {
        switch (t) {
            case EntityType::Drone:    return 1;
            case EntityType::Rusher:   return 1;
            case EntityType::Turret:   return 2;
            case EntityType::Tank:     return 5;
            case EntityType::Bomber:   return 2;
            case EntityType::Shielder: return 3;
            default: return 0;
        }
    };

    {
        // Try several paths — exe might run from build/ or project root
        const char* vm_paths[] = {
            "assets/wingman.glb",
            "../assets/wingman.glb",
            "../../assets/wingman.glb",
            "wingman.glb",
        };
        for (const char* vp : vm_paths) {
            LevelData vm_data = load_level_gltf(vp);
            if (!vm_data.mesh.vertices.empty()) {
                printf("Loaded viewmodel from '%s': %zu verts, %zu indices\n",
                       vp, vm_data.mesh.vertices.size(),
                       vm_data.mesh.indices.size());
                // Share mesh with all weapons (until they get unique models)
                uint32_t mag_start = 0, mag_count = 0;
                bool has_mag = false;
                for (const auto& sub : vm_data.submeshes) {
                    if (strncmp(sub.name, "Mag", 3) == 0 || strncmp(sub.name, "mag", 3) == 0) {
                        mag_start = sub.index_start;
                        mag_count = sub.index_count;
                        has_mag = true;
                        printf("  Found mag sub-mesh '%s': indices %u..%u (%u)\n",
                               sub.name, sub.index_start,
                               sub.index_start + sub.index_count, sub.index_count);
                    }
                }
                for (int w = 0; w < MAX_WEAPONS; w++) {
                    weapons[w].viewmodel_mesh = vm_data.mesh; // copy
                    weapons[w].mesh_loaded = true;
                    weapons[w].mag_index_start = mag_start;
                    weapons[w].mag_index_count = mag_count;
                    weapons[w].has_mag_submesh = has_mag;
                }
                break;
            }
        }
        if (!weapons[0].mesh_loaded)
            printf("WARNING: Could not load viewmodel (tried assets/, ../assets/, ./)\n");

        // Load per-weapon models (overrides shared mesh)
        const char* vm_prefixes[] = {"assets/", "../assets/", "../../assets/", ""};
        for (int w = 0; w < MAX_WEAPONS; w++) {
            if (!weapons[w].config.model_path) continue;
            for (const char* pfx : vm_prefixes) {
                std::string mp = std::string(pfx) + weapons[w].config.model_path;
                LevelData wm = load_level_gltf(mp.c_str());
                if (!wm.mesh.vertices.empty()) {
                    weapons[w].viewmodel_mesh = std::move(wm.mesh);
                    weapons[w].mesh_loaded = true;
                    weapons[w].has_mag_submesh = false;
                    for (const auto& sub : wm.submeshes) {
                        if (strncmp(sub.name, "Mag", 3) == 0 || strncmp(sub.name, "mag", 3) == 0) {
                            weapons[w].mag_index_start = sub.index_start;
                            weapons[w].mag_index_count = sub.index_count;
                            weapons[w].has_mag_submesh = true;
                        }
                    }
                    // Darken vertex colors to avoid overexposure on close viewmodels
                    for (auto& v : weapons[w].viewmodel_mesh.vertices) {
                        v.color[0] *= 0.45f;
                        v.color[1] *= 0.45f;
                        v.color[2] *= 0.45f;
                    }
                    printf("Loaded weapon model '%s': %zu verts\n",
                           mp.c_str(), weapons[w].viewmodel_mesh.vertices.size());
                    break;
                }
            }
        }
    }

    // --- Fixed timestep ---
    constexpr float TICK_RATE = 1.0f / 128.0f;
    float accumulator = 0.0f;

    // --- Input state ---
    InputState input{};
    bool jump_held = false;
    bool interact_pressed = false;  // single-frame flag, consumed each frame
    bool scroll_jump_pulse = false;  // one-frame pulse from scroll wheel

    // Scroll wheel pulses for movement actions (persists for a few ticks)
    float scroll_forward_pulse = 0.0f;  // +1 or -1 when wheel bound to forward/back
    float scroll_right_pulse   = 0.0f;  // +1 or -1 when wheel bound to left/right
    int   scroll_move_ticks    = 0;     // ticks remaining for scroll movement pulse
    static constexpr int SCROLL_MOVE_TICK_COUNT = 1; // how many ticks a scroll pulse lasts

    // --- Key rebind state ---
    // -1 = not rebinding, otherwise action_index * SLOTS + slot
    int rebinding_action = -1;
    int rebinding_slot   = -1;

    bool running = true;
    Uint64 last_time = SDL_GetPerformanceCounter();

    // FPS counter
    float fps_timer = 0.0f;
    int frame_count = 0;
    float display_fps = 0.0f;

    // --- Construct shared game state ---
    GameState gs {
        /* window */            window,
        /* camera */            camera,
        /* player */            player,
        /* renderer */          renderer,
        /* collision */         collision,
        /* config */            config,
        /* kb */                kb,
        /* effects */           effects,
        /* entities */          entities,
        /* max_entities */      MAX_ENTITIES,
        /* drone_cfg */         drone_cfg,
        /* rusher_cfg */        rusher_cfg,
        /* turret_cfg */        turret_cfg,
        /* tank_cfg */          tank_cfg,
        /* bomber_cfg */        bomber_cfg,
        /* shielder_cfg */      shielder_cfg,
        /* ai_enabled */        ai_enabled,
        /* weapons */           weapons,
        /* active_weapon */     active_weapon,
        /* pending_weapon */    pending_weapon,
        /* num_weapons */       num_weapons,
        /* weapon_level */      weapon_level,
        /* procgen_cfg */       procgen_cfg,
        /* rooms_cleared */     rooms_cleared,
        /* door_mesh_ptr */     door_mesh_ptr,
        /* active_doors */      active_doors,
        /* current_level_name */current_level_name,
        /* currency */          currency,
        /* show_shop */         show_shop,
        /* in_shop_room */      in_shop_room,
        /* shop_weapon */       shop_weapon,
        /* shop_data */         shop_data,
        /* shop_nearby_stand */ shop_nearby_stand,
        /* shop_interact_cooldown */ shop_interact_cooldown,
        /* pending_mod */       pending_mod,
        /* pending_stand_idx */ pending_stand_idx,
        /* room_stats */        room_stats,
        /* show_room_summary */ show_room_summary,
        /* show_settings */     show_settings,
        /* show_hud */          show_hud,
        /* show_ladder_debug */ show_ladder_debug,
        /* show_magazine_view */ show_magazine_view,
        /* noclip */            noclip,
        /* running */           running,
        /* fly_speed */         fly_speed,
        /* rebinding_action */  rebinding_action,
        /* rebinding_slot */    rebinding_slot,
        /* level_files */       level_files,
        /* levels_scanned */    levels_scanned,
        /* level_path_buf */    level_path_buf,
    };

    // Load-level callback for debug menu
    auto load_level_fn = [&](const std::string& path) -> bool {
        return load_level(path, renderer, collision, player, camera, noclip,
                          current_level_name, entities, MAX_ENTITIES,
                          &drone_cfg, &rusher_cfg, &turret_cfg,
                          &tank_cfg, &bomber_cfg, &shielder_cfg);
    };

    while (running) {
        // Helper: is entity a live (non-dying) enemy?
        auto is_live_enemy = [&](const Entity& e) -> bool {
            if (!e.alive) return false;
            if (e.type == EntityType::Drone)    return e.ai_state != DRONE_DYING    && e.ai_state != DRONE_DEAD;
            if (e.type == EntityType::Rusher)   return e.ai_state != RUSHER_DYING   && e.ai_state != RUSHER_DEAD;
            if (e.type == EntityType::Turret)   return e.ai_state != TURRET_DYING   && e.ai_state != TURRET_DEAD;
            if (e.type == EntityType::Tank)     return e.ai_state != TANK_DYING     && e.ai_state != TANK_DEAD;
            if (e.type == EntityType::Bomber)   return e.ai_state != BOMBER_DYING   && e.ai_state != BOMBER_DEAD;
            if (e.type == EntityType::Shielder) return e.ai_state != SHIELDER_DYING && e.ai_state != SHIELDER_DEAD;
            return false;
        };

        float mouse_dx = 0.0f, mouse_dy = 0.0f;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (show_settings || show_magazine_view || show_room_summary)
                ImGui_ImplSDL3_ProcessEvent(&event);

            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                // Rebind capture
                if (rebinding_action >= 0 && !event.key.repeat) {
                    if (event.key.scancode == SDL_SCANCODE_ESCAPE) {
                        rebinding_action = -1;
                    } else {
                        kb.set(static_cast<Action>(rebinding_action),
                               rebinding_slot,
                               static_cast<InputCode>(event.key.scancode));
                        rebinding_action = -1;
                    }
                    break;
                }

                if (event.key.key == SDLK_F5 && !event.key.repeat) {
                    g_collision_log = !g_collision_log;
                    printf("Collision logging: %s\n", g_collision_log ? "ON" : "OFF");
                }

                if (event.key.key == SDLK_ESCAPE && !event.key.repeat) {
                    if (show_magazine_view && pending_mod.active) {
                        // Cancel mod application — refund
                        currency += pending_mod.cost;
                        printf("Cancelled mod application — refunded %d gold\n", pending_mod.cost);
                        pending_mod = {};
                        pending_stand_idx = -1;
                        // Keep mag view open
                    } else if (show_magazine_view) {
                        show_magazine_view = false;
                        SDL_SetWindowRelativeMouseMode(window, true);
                    } else {
                        show_settings = !show_settings;
                        rebinding_action = -1;
                        SDL_SetWindowRelativeMouseMode(window, !show_settings);
                    }
                }
                if (!show_settings) {
                    if (kb.matches_scancode(Action::Noclip, event.key.scancode) && !event.key.repeat) {
                        noclip = !noclip;
                        printf("Noclip: %s\n", noclip ? "ON" : "OFF");
                        if (noclip) {
                            camera.position = player.eye_position();
                        } else {
                            // Drop player where the camera is
                            player.position = HMM_SubV3(camera.position, HMM_V3(0.0f, player.current_eye_offset(), 0.0f));
                            player.velocity = HMM_V3(0, 0, 0);
                        }
                    }
                    if (kb.matches_scancode(Action::ToggleHUD, event.key.scancode) && !event.key.repeat)
                        show_hud = !show_hud;
                    if (kb.matches_scancode(Action::MagazineView, event.key.scancode) && !event.key.repeat && !pending_mod.active) {
                        show_magazine_view = !show_magazine_view;
                        SDL_SetWindowRelativeMouseMode(window, !show_magazine_view && !show_settings);
                    }
                    if (kb.matches_scancode(Action::ToggleFullscreen, event.key.scancode) && !event.key.repeat) {
                        Uint32 flags = SDL_GetWindowFlags(window);
                        SDL_SetWindowFullscreen(window, (flags & SDL_WINDOW_FULLSCREEN) ? false : true);
                    }
                    if (kb.matches_scancode(Action::Jump, event.key.scancode))
                        jump_held = true;
                    if (kb.matches_scancode(Action::Interact, event.key.scancode) && !event.key.repeat)
                        interact_pressed = true;
                    if (kb.matches_scancode(Action::Holster, event.key.scancode) && !event.key.repeat) {
                        Weapon& hw = weapons[active_weapon];
                        if (player.weapon_holstered) {
                            // Unholster: start raising
                            player.weapon_holstered = false;
                            hw.begin_unholster();
                        } else if (hw.state == WeaponState::IDLE || hw.state == WeaponState::FIRING) {
                            // Holster: start lowering
                            hw.begin_holster();
                        }
                    }
                }
                break;

            case SDL_EVENT_KEY_UP:
                if (kb.matches_scancode(Action::Jump, event.key.scancode))
                    jump_held = false;
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                // Rebind capture for mouse buttons
                if (rebinding_action >= 0) {
                    InputCode btn = INPUT_NONE;
                    if (event.button.button == SDL_BUTTON_LEFT)   btn = INPUT_MOUSE_LEFT;
                    if (event.button.button == SDL_BUTTON_RIGHT)  btn = INPUT_MOUSE_RIGHT;
                    if (event.button.button == SDL_BUTTON_MIDDLE) btn = INPUT_MOUSE_MIDDLE;
                    if (btn != INPUT_NONE) {
                        kb.set(static_cast<Action>(rebinding_action), rebinding_slot, btn);
                        rebinding_action = -1;
                    }
                    break;
                }
                // Shooting is now handled via keybinds in the game loop
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                // Rebind capture for mouse wheel
                if (rebinding_action >= 0) {
                    InputCode wc = (event.wheel.y > 0) ? INPUT_MOUSE_WHEEL_UP : INPUT_MOUSE_WHEEL_DOWN;
                    kb.set(static_cast<Action>(rebinding_action), rebinding_slot, wc);
                    rebinding_action = -1;
                    break;
                }
                if (!show_settings) {
                    InputCode wc = (event.wheel.y > 0) ? INPUT_MOUSE_WHEEL_UP : INPUT_MOUSE_WHEEL_DOWN;
                    if (kb.matches_wheel(Action::Jump, wc))
                        scroll_jump_pulse = true;

                    // Movement actions from scroll wheel
                    if (kb.matches_wheel(Action::MoveForward, wc)) {
                        scroll_forward_pulse = 1.0f;
                        scroll_move_ticks = SCROLL_MOVE_TICK_COUNT;
                    }
                    if (kb.matches_wheel(Action::MoveBack, wc)) {
                        scroll_forward_pulse = -1.0f;
                        scroll_move_ticks = SCROLL_MOVE_TICK_COUNT;
                    }
                    if (kb.matches_wheel(Action::MoveRight, wc)) {
                        scroll_right_pulse = 1.0f;
                        scroll_move_ticks = SCROLL_MOVE_TICK_COUNT;
                    }
                    if (kb.matches_wheel(Action::MoveLeft, wc)) {
                        scroll_right_pulse = -1.0f;
                        scroll_move_ticks = SCROLL_MOVE_TICK_COUNT;
                    }
                }
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                renderer.on_resize();
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if (!show_settings) {
                    mouse_dx += event.motion.xrel;
                    mouse_dy += event.motion.yrel;
                }
                break;
            }
        }

        // --- Timing ---
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - last_time) / static_cast<float>(SDL_GetPerformanceFrequency());
        last_time = now;
        if (dt > 0.1f) dt = 0.1f;

        fps_timer += dt;
        frame_count++;
        if (fps_timer >= 0.5f) {
            display_fps = static_cast<float>(frame_count) / fps_timer;
            frame_count = 0;
            fps_timer = 0.0f;
        }

        if (!show_settings && !show_magazine_view && !show_room_summary) {
            float sens_scale = 1.0f - weapons[active_weapon].ads_blend * (1.0f - weapons[active_weapon].config.ads_sens_mult);
            camera.mouse_look(mouse_dx * sens_scale, mouse_dy * sens_scale);
        }

        // Get keyboard state once per frame (used by movement, weapon, etc.)
        const bool* keys_frame = SDL_GetKeyboardState(nullptr);

        // --- Movement ---
        if (noclip && !show_settings && !show_magazine_view) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            HMM_Vec3 move_dir = HMM_V3(0, 0, 0);
            HMM_Vec3 fwd   = camera.forward_flat();
            HMM_Vec3 right = camera.right();

            if (kb.held(Action::MoveForward, keys)) move_dir = HMM_AddV3(move_dir, fwd);
            if (kb.held(Action::MoveBack,    keys)) move_dir = HMM_SubV3(move_dir, fwd);
            if (kb.held(Action::MoveRight,   keys)) move_dir = HMM_AddV3(move_dir, right);
            if (kb.held(Action::MoveLeft,    keys)) move_dir = HMM_SubV3(move_dir, right);
            if (kb.held(Action::Jump,        keys)) move_dir.Y += 1.0f;
            if (kb.held(Action::Descend,     keys)) move_dir.Y -= 1.0f;

            float len = HMM_LenV3(move_dir);
            if (len > 0.001f) {
                move_dir = HMM_MulV3F(move_dir, 1.0f / len);
                float speed = fly_speed;
                if (kb.held(Action::Sprint, keys)) speed *= 3.0f;
                camera.position = HMM_AddV3(camera.position, HMM_MulV3F(move_dir, speed * dt));
            }
        } else if (!show_settings && !show_magazine_view) {
            const bool* keys = SDL_GetKeyboardState(nullptr);

            input.forward = 0.0f;
            input.right   = 0.0f;
            if (kb.held(Action::MoveForward, keys)) input.forward += 1.0f;
            if (kb.held(Action::MoveBack,    keys)) input.forward -= 1.0f;
            if (kb.held(Action::MoveRight,   keys)) input.right   += 1.0f;
            if (kb.held(Action::MoveLeft,    keys)) input.right   -= 1.0f;

            // Apply scroll wheel movement pulses
            if (scroll_move_ticks > 0) {
                input.forward += scroll_forward_pulse;
                input.right   += scroll_right_pulse;
                // Clamp to [-1, 1]
                if (input.forward >  1.0f) input.forward =  1.0f;
                if (input.forward < -1.0f) input.forward = -1.0f;
                if (input.right   >  1.0f) input.right   =  1.0f;
                if (input.right   < -1.0f) input.right   = -1.0f;
            }

            input.jump_held   = jump_held || scroll_jump_pulse;
            input.crouch_held = kb.held(Action::Crouch, keys);
            input.yaw         = camera.yaw;
            input.pitch       = camera.pitch;

            accumulator += dt;
            while (accumulator >= TICK_RATE) {
                player.update(TICK_RATE, input, collision);
                accumulator -= TICK_RATE;
                // Only clear scroll pulse after a tick actually ran and saw it
                if (scroll_jump_pulse) {
                    scroll_jump_pulse = false;
                    input.jump_held = jump_held;
                }
                // Tick down scroll movement pulse
                if (scroll_move_ticks > 0) {
                    scroll_move_ticks--;
                    if (scroll_move_ticks <= 0) {
                        scroll_forward_pulse = 0.0f;
                        scroll_right_pulse   = 0.0f;
                        // Recompute input without scroll pulses for remaining ticks
                        input.forward = 0.0f;
                        input.right   = 0.0f;
                        if (kb.held(Action::MoveForward, keys)) input.forward += 1.0f;
                        if (kb.held(Action::MoveBack,    keys)) input.forward -= 1.0f;
                        if (kb.held(Action::MoveRight,   keys)) input.right   += 1.0f;
                        if (kb.held(Action::MoveLeft,    keys)) input.right   -= 1.0f;
                    }
                }
            }
            // DON'T clear scroll_jump_pulse here — if no tick ran this frame,
            // keep it alive until one does

            camera.position = player.eye_position();
        }

        total_time += dt;

        // --- Weapon switching ---
        {
            // Single-weapon system: no number-key switching.
            // Weapon swaps only happen via the shop (buy replaces current).

            // Check if holster lowering is done — set holstered flag
            {
                Weapon& hw = weapons[active_weapon];
                if (hw.holster_lowering && hw.state == WeaponState::IDLE) {
                    // Lowering animation finished, now mark holstered
                    player.weapon_holstered = true;
                    hw.holster_lowering = false;
                }
            }

            // Check if lowering is done — swap weapon data and start raising
            Weapon& aw = weapons[active_weapon];
            if (aw.state == WeaponState::SWAPPING && !aw.swap_raising && aw.swap_timer <= 0.0f
                && pending_weapon >= 0) {
                active_weapon = pending_weapon;
                pending_weapon = -1;
                Weapon& nw = weapons[active_weapon];
                nw.state = WeaponState::SWAPPING;
                nw.swap_timer = nw.swap_duration;
                nw.swap_raising = true;
            }
        }

        // --- Weapon update & shooting ---
        {
            Weapon& weapon = weapons[active_weapon];
            player.weapon_lightweight = weapon.config.lightweight;
            bool holstered = player.weapon_holstered;
            bool fire_pressed = !show_settings && !show_magazine_view && !noclip && !holstered && kb.held(Action::Shoot, keys_frame);
            bool reload_pressed = !show_settings && !show_magazine_view && !noclip && !holstered && kb.held(Action::Reload, keys_frame);
            bool ads_input = !show_settings && !show_magazine_view && !noclip && !holstered && kb.held(Action::ADS, keys_frame);

            weapon.update(dt, fire_pressed, reload_pressed, ads_input);

            // Try to fire — if weapon fires, do hitscan
            if (fire_pressed && weapon.try_fire()) {
                // Gunshot alert: wake idle enemies within weapon range
                for (int i = 0; i < MAX_ENTITIES; i++) {
                    Entity& e = entities[i];
                    if (!e.alive) continue;
                    float d = HMM_LenV3(HMM_SubV3(e.position, camera.position));
                    if (d > weapon.config.range) continue;
                    if (e.type == EntityType::Drone  && e.ai_state == DRONE_IDLE)
                        e.ai_state = DRONE_CHASING;
                    if (e.type == EntityType::Rusher && e.ai_state == RUSHER_IDLE)
                        e.ai_state = RUSHER_CHASING;
                    if (e.type == EntityType::Turret && e.ai_state == TURRET_IDLE)
                        e.ai_state = TURRET_TRACKING;
                    if (e.type == EntityType::Tank && e.ai_state == TANK_IDLE)
                        e.ai_state = TANK_CHASING;
                    if (e.type == EntityType::Bomber && e.ai_state == BOMBER_IDLE)
                        e.ai_state = BOMBER_APPROACH;
                    if (e.type == EntityType::Shielder && e.ai_state == SHIELDER_IDLE)
                        e.ai_state = SHIELDER_CHASING;
                }

              if (weapon.config.fire_mode == FireMode::PROJECTILE) {
                // Spawn a player projectile
                HMM_Vec3 fwd = camera.forward();
                constexpr float GRACE = 0.07f;
                constexpr float SPAWN_FWD = 1.5f;

                // Real projectile — spawns ahead, invisible during grace period
                int real_idx = -1;
                for (int i = 0; i < MAX_ENTITIES; i++) {
                    if (!entities[i].alive) {
                        Entity& p = entities[i];
                        p = Entity{};
                        p.type     = EntityType::Projectile;
                        p.alive    = true;
                        p.position = HMM_AddV3(camera.position, HMM_MulV3F(fwd, SPAWN_FWD));
                        float pspeed = weapon.config.proj_speed;
                        if (weapon.last_fired_mod.tipping == Tipping::Aerodynamic) pspeed *= 2.0f;
                        p.velocity = HMM_MulV3F(fwd, pspeed);
                        p.radius   = weapon.config.proj_radius;
                        p.damage   = weapon.config.damage;
                        p.round_mod = weapon.last_fired_mod;
                        p.fired_round_idx = weapon.last_fired_round;
                        p.owner    = -3; // player knife projectile
                        p.lifetime = weapon.config.proj_lifetime;
                        p.ai_timer = GRACE; // render grace period
                        p.yaw   = atan2f(fwd.X, fwd.Z);
                        p.pitch = -asinf(fwd.Y);
                        real_idx = i;
                        break;
                    }
                }

                // Dummy projectile — visual only, travels from hand to where real one appears
                if (real_idx >= 0) {
                    HMM_Vec3 cam_right = camera.right();
                    HMM_Vec3 cam_up    = HMM_V3(0.0f, 1.0f, 0.0f);
                    HMM_Vec3 hip = weapon.config.hip_offset;
                    HMM_Vec3 hand_pos = HMM_AddV3(camera.position,
                        HMM_AddV3(HMM_MulV3F(cam_right, hip.X),
                        HMM_AddV3(HMM_MulV3F(cam_up, hip.Y),
                                  HMM_MulV3F(fwd, hip.Z))));
                    // Target: where real projectile will be when grace ends
                    float dspeed = weapon.config.proj_speed;
                    if (weapon.last_fired_mod.tipping == Tipping::Aerodynamic) dspeed *= 2.0f;
                    HMM_Vec3 target = HMM_AddV3(
                        HMM_AddV3(camera.position, HMM_MulV3F(fwd, SPAWN_FWD)),
                        HMM_MulV3F(fwd, dspeed * GRACE));
                    HMM_Vec3 dummy_vel = HMM_MulV3F(HMM_SubV3(target, hand_pos), 1.0f / GRACE);

                    for (int i = 0; i < MAX_ENTITIES; i++) {
                        if (!entities[i].alive) {
                            Entity& d = entities[i];
                            d = Entity{};
                            d.type     = EntityType::Projectile;
                            d.alive    = true;
                            d.position = hand_pos;
                            d.velocity = dummy_vel;
                            d.radius   = weapon.config.proj_radius;
                            d.damage   = 0.0f;
                            d.owner    = -4; // dummy visual projectile
                            d.lifetime = GRACE;
                            d.yaw   = atan2f(fwd.X, fwd.Z);
                            d.pitch = -asinf(fwd.Y);
                            break;
                        }
                    }
                }
              } else {
                // Hitscan
                HMM_Vec3 ray_origin = camera.position;
                HMM_Vec3 ray_dir    = camera.forward();
                RoundMod rm = weapon.last_fired_mod;
                bool piercing = (rm.tipping == Tipping::Piercing);

                // Collect all hits along the ray (sorted by distance)
                struct RayHit { float t; int idx; };
                RayHit hits[MAX_ENTITIES];
                int hit_count = 0;

                float wall_t = weapon.config.range;
                {
                    HitResult wall = collision.raycast(ray_origin, ray_dir, weapon.config.range);
                    if (wall.hit) wall_t = wall.t;
                }

                for (int i = 0; i < MAX_ENTITIES; i++) {
                    Entity& e = entities[i];
                    if (!is_live_enemy(e)) continue;

                    HMM_Vec3 oc = HMM_SubV3(ray_origin, e.position);
                    float b = HMM_DotV3(oc, ray_dir);
                    float c = HMM_DotV3(oc, oc) - e.radius * e.radius;
                    float disc = b * b - c;
                    if (disc < 0) continue;

                    float t = -b - sqrtf(disc);
                    if (t < 0) t = -b + sqrtf(disc);
                    if (t > 0 && t < wall_t) {
                        hits[hit_count++] = {t, i};
                    }
                }

                // Sort by distance
                for (int a = 0; a < hit_count - 1; a++)
                    for (int b2 = a + 1; b2 < hit_count; b2++)
                        if (hits[b2].t < hits[a].t) { RayHit tmp = hits[a]; hits[a] = hits[b2]; hits[b2] = tmp; }

                // Apply damage to first hit, or all hits if piercing
                int apply_count = piercing ? hit_count : (hit_count > 0 ? 1 : 0);

                for (int h = 0; h < apply_count; h++) {
                    int eidx = hits[h].idx;
                    float hit_t = hits[h].t;
                    Entity& hit_ent = entities[eidx];

                    float base_dmg = weapon.config.damage;

                    // Bleed multiplier (Serrated stacks — applied before tipping)
                    base_dmg *= (1.0f + 0.1f * hit_ent.bleed_stacks);

                    // Tipping effects
                    if (rm.tipping == Tipping::Sharpened)
                        base_dmg += 10.0f;
                    if (rm.tipping == Tipping::Crystal_Tipped)
                        base_dmg *= 2.0f;

                    // Apply shield barrier absorption (Piercing bypasses)
                    float actual_dmg = piercing ? base_dmg : shielder_absorb_damage(hit_ent, base_dmg);
                    hit_ent.health -= actual_dmg;
                    float actual_dmg_display = actual_dmg;

                    // Record stats
                    float bleed_mult = 1.0f + 0.1f * hit_ent.bleed_stacks;
                    room_stats.record_dealt(weapon.config.damage, actual_dmg, rm.tipping, bleed_mult);

                    // Set hit flash per type
                    if (hit_ent.type == EntityType::Turret)
                        hit_ent.hit_flash = turret_cfg.hit_flash_time;
                    else if (hit_ent.type == EntityType::Tank)
                        hit_ent.hit_flash = tank_cfg.hit_flash_time;
                    else if (hit_ent.type == EntityType::Bomber)
                        hit_ent.hit_flash = bomber_cfg.hit_flash_time;
                    else if (hit_ent.type == EntityType::Shielder)
                        hit_ent.hit_flash = shielder_cfg.hit_flash_time;
                    else
                        hit_ent.hit_flash = drone_cfg.hit_flash_time;

                    // Poison tipping
                    if (rm.tipping == Tipping::Poison_Tipped) {
                        hit_ent.poison_stacks++;
                        hit_ent.poison_timer = 5.0f;
                    }

                    // Serrated: apply bleed stack (permanent)
                    if (rm.tipping == Tipping::Serrated)
                        hit_ent.bleed_stacks++;

                    // Crystal Tipped: 1/10 chance to shatter (clear from magazine)
                    if (rm.tipping == Tipping::Crystal_Tipped) {
                        if (rand() % 10 == 0)
                            weapon.magazine.set_tipping(weapon.last_fired_round, Tipping::None);
                    }

                    // Enchantment effects
                    if (rm.enchantment == Enchantment::Vampiric) {
                        player.health += actual_dmg_display * 0.1f;
                        if (player.health > player.max_health)
                            player.health = player.max_health;
                    }

                    // Floating damage number at hit point
                    {
                        HMM_Vec3 hit_pos = HMM_AddV3(ray_origin, HMM_MulV3F(ray_dir, hit_t));
                        hit_pos.Y += 0.3f;
                        hit_pos.X += randf(-0.2f, 0.2f);
                        hit_pos.Z += randf(-0.2f, 0.2f);
                        bool is_kill = (hit_ent.health <= 0);
                        dmg_numbers.spawn(hit_pos, (int)actual_dmg_display, eidx, is_kill);
                    }

                    // Wake up idle enemies on hit
                    if (hit_ent.type == EntityType::Drone && hit_ent.ai_state == DRONE_IDLE)
                        hit_ent.ai_state = DRONE_CHASING;
                    if (hit_ent.type == EntityType::Rusher && hit_ent.ai_state == RUSHER_IDLE)
                        hit_ent.ai_state = RUSHER_CHASING;
                    if (hit_ent.type == EntityType::Turret && hit_ent.ai_state == TURRET_IDLE)
                        hit_ent.ai_state = TURRET_TRACKING;
                    if (hit_ent.type == EntityType::Tank && hit_ent.ai_state == TANK_IDLE)
                        hit_ent.ai_state = TANK_CHASING;
                    if (hit_ent.type == EntityType::Bomber && hit_ent.ai_state == BOMBER_IDLE)
                        hit_ent.ai_state = BOMBER_APPROACH;
                    if (hit_ent.type == EntityType::Shielder && hit_ent.ai_state == SHIELDER_IDLE)
                        hit_ent.ai_state = SHIELDER_CHASING;

                    // Determine dying state for this entity type
                    uint8_t dying_state = (uint8_t)DRONE_DYING;
                    float tumble = drone_cfg.death_tumble_speed;
                    if (hit_ent.type == EntityType::Rusher)  { dying_state = RUSHER_DYING;  tumble = rusher_cfg.death_tumble_speed; }
                    if (hit_ent.type == EntityType::Turret)  { dying_state = TURRET_DYING;  tumble = turret_cfg.death_tumble_speed; }
                    if (hit_ent.type == EntityType::Tank)    { dying_state = TANK_DYING;    tumble = tank_cfg.death_tumble_speed; }
                    if (hit_ent.type == EntityType::Bomber)  { dying_state = BOMBER_DYING;  tumble = bomber_cfg.death_tumble_speed; }
                    if (hit_ent.type == EntityType::Shielder){ dying_state = SHIELDER_DYING; tumble = shielder_cfg.death_tumble_speed; }

                    if (hit_ent.health <= 0 && hit_ent.ai_state != dying_state) {
                        hit_ent.ai_state = dying_state;
                        hit_ent.death_timer = 0.0f;
                        { int kr = kill_reward(hit_ent.type); currency += kr; room_stats.record_kill(hit_ent.type, kr); }
                        hit_ent.velocity.Y += 3.0f;
                        HMM_Vec3 knockback = HMM_MulV3F(camera.forward(), 5.0f);
                        hit_ent.velocity = HMM_AddV3(hit_ent.velocity, knockback);
                        hit_ent.angular_vel = HMM_V3(
                            randf(-1.0f, 1.0f) * tumble * 57.3f,
                            0.0f,
                            randf(-1.0f, 1.0f) * tumble * 57.3f
                        );
                    }
                }
              } // end hitscan else
            }
        }

        // --- Update entities (only when not paused) ---
        if (!show_settings && !show_shop && !show_magazine_view) {
            // --- Player damage decay ---
            if (player.damage_accum > 0.0f) {
                player.damage_accum -= player.damage_accum * (2.0f / player.damage_decay) * dt;
                if (player.damage_accum < 0.1f) player.damage_accum = 0.0f;
            }
            if (player.health < 0.0f) player.health = 0.0f;

            // Track dying drones to spawn explosions when they hit ground
            struct DyingEnemy { int idx; HMM_Vec3 pos; bool was_alive; };
            drone_tick_frame();
            rusher_tick_frame();
            turret_tick_frame();
            tank_tick_frame();
            bomber_tick_frame();
            shielder_tick_frame();

            DyingEnemy dying[MAX_ENTITIES];
            int dying_count = 0;

            for (int i = 0; i < MAX_ENTITIES; i++) {
                Entity& e = entities[i];
                if (!e.alive) continue;
                if (e.type == EntityType::Drone) {
                    if (e.ai_state == DRONE_DYING)
                        dying[dying_count++] = {i, e.position, true};
                    if (ai_enabled)
                        drone_update(e, entities, MAX_ENTITIES,
                                     player.position, collision, drone_cfg, dt, total_time);
                }
                if (e.type == EntityType::Rusher) {
                    if (e.ai_state == RUSHER_DYING)
                        dying[dying_count++] = {i, e.position, true};
                    if (ai_enabled) {
                        rusher_update(e, entities, MAX_ENTITIES,
                                      player.position, collision, rusher_cfg, dt, total_time);
                        if (rusher_check_player_hit(e, player.capsule_bottom(), player.capsule_top(), player.radius, rusher_cfg)) {
                            player.health -= rusher_cfg.melee_damage;
                            player.damage_accum += rusher_cfg.melee_damage;
                            room_stats.record_taken(rusher_cfg.melee_damage, EntityType::Rusher);
                        }
                    }
                }
                if (e.type == EntityType::Turret) {
                    if (e.ai_state == TURRET_DYING)
                        dying[dying_count++] = {i, e.position, true};
                    if (ai_enabled) {
                        turret_update(e, entities, MAX_ENTITIES,
                                      player.position, collision, turret_cfg, dt, total_time);
                        float tdmg = 0.0f;
                        if (turret_check_player_hit(e, player.capsule_bottom(), player.capsule_top(),
                                                    player.radius, collision, turret_cfg, tdmg)) {
                            float dmg = tdmg * dt;  // beam_dps * dt
                            player.health -= dmg;
                            player.damage_accum += dmg;
                            room_stats.record_taken(dmg, EntityType::Turret);
                        }
                    }
                }
                if (e.type == EntityType::Tank) {
                    if (e.ai_state == TANK_DYING)
                        dying[dying_count++] = {i, e.position, true};
                    if (ai_enabled) {
                        tank_update(e, entities, MAX_ENTITIES,
                                    player.position, collision, tank_cfg, dt, total_time);
                        float tdmg = 0.0f;
                        HMM_Vec3 kb = {};
                        if (tank_check_player_hit(e, player.capsule_bottom(), player.capsule_top(),
                                                  player.radius, tank_cfg, tdmg, kb)) {
                            // Apply knockback to player
                            player.velocity = HMM_AddV3(player.velocity, kb);
                            player.health -= tdmg;
                            player.damage_accum += tdmg;
                            room_stats.record_taken(tdmg, EntityType::Tank);
                        }
                    }
                }
                if (e.type == EntityType::Bomber) {
                    if (e.ai_state == BOMBER_DYING)
                        dying[dying_count++] = {i, e.position, true};
                    // Track exploding bombers for death effect
                    if (e.ai_state == BOMBER_EXPLODING)
                        dying[dying_count++] = {i, e.position, true};
                    if (ai_enabled) {
                        bomber_update(e, entities, MAX_ENTITIES,
                                      player.position, collision, bomber_cfg, dt, total_time);
                        float bdmg = 0.0f;
                        HMM_Vec3 bkb = {};
                        if (bomber_check_explosion(e, player.capsule_bottom(), player.capsule_top(),
                                                   player.radius, bomber_cfg, bdmg, bkb)) {
                            player.velocity = HMM_AddV3(player.velocity, bkb);
                            player.health -= bdmg;
                            player.damage_accum += bdmg;
                            room_stats.record_taken(bdmg, EntityType::Bomber);
                        }
                    }
                }
                if (e.type == EntityType::Shielder) {
                    if (e.ai_state == SHIELDER_DYING)
                        dying[dying_count++] = {i, e.position, true};
                    if (ai_enabled)
                        shielder_update(e, entities, MAX_ENTITIES,
                                        player.position, collision, shielder_cfg, dt, total_time);
                }

                // Poison DoT tick
                if (e.poison_stacks > 0 && e.type != EntityType::Projectile) {
                    float poison_dmg = 4.0f * e.poison_stacks * dt;
                    e.health -= poison_dmg;
                    room_stats.record_poison(poison_dmg);
                    e.poison_timer -= dt;
                    if (e.poison_timer <= 0.0f) {
                        e.poison_stacks = 0;
                        e.poison_timer = 0.0f;
                    }
                }
            }

            // Apply shielder barriers to allies
            if (!show_settings) {
                shielder_apply_barriers(entities, MAX_ENTITIES, shielder_cfg, dt);
            }

            // Check which dying enemies just expired → explosion
            for (int d = 0; d < dying_count; d++) {
                Entity& e = entities[dying[d].idx];
                if (!e.alive && dying[d].was_alive) {
                    if (e.type == EntityType::Bomber)
                        effects.spawn_bomber_explosion(dying[d].pos);
                    else
                        effects.spawn_drone_explosion(dying[d].pos);
                }
            }

            // --- Enemy-player collision (push apart) ---
            for (int i = 0; i < MAX_ENTITIES; i++) {
                Entity& e = entities[i];
                if (!is_live_enemy(e)) continue;

                HMM_Vec3 delta = HMM_SubV3(player.position, e.position);
                float dist = HMM_LenV3(delta);
                float min_dist = player.radius + e.radius;
                if (dist < min_dist && dist > 0.001f) {
                    HMM_Vec3 push_dir = HMM_MulV3F(delta, 1.0f / dist);
                    float overlap = min_dist - dist;
                    player.position = HMM_AddV3(player.position,
                                                HMM_MulV3F(push_dir, overlap * 0.5f));
                    e.position = HMM_SubV3(e.position,
                                           HMM_MulV3F(push_dir, overlap * 0.5f));
                }
            }

            // --- Enemy-enemy collision (push apart) ---
            for (int i = 0; i < MAX_ENTITIES; i++) {
                Entity& a = entities[i];
                if (!is_live_enemy(a)) continue;
                for (int j = i + 1; j < MAX_ENTITIES; j++) {
                    Entity& b = entities[j];
                    if (!is_live_enemy(b)) continue;

                    HMM_Vec3 delta = HMM_SubV3(b.position, a.position);
                    float dist = HMM_LenV3(delta);
                    float min_dist = a.radius + b.radius;
                    if (dist < min_dist && dist > 0.001f) {
                        HMM_Vec3 push_dir = HMM_MulV3F(delta, 1.0f / dist);
                        float overlap = min_dist - dist;
                        a.position = HMM_SubV3(a.position,
                                               HMM_MulV3F(push_dir, overlap * 0.5f));
                        b.position = HMM_AddV3(b.position,
                                               HMM_MulV3F(push_dir, overlap * 0.5f));
                    }
                }
            }

            // --- Player projectile-vs-enemy collision (knife etc) ---
            for (int i = 0; i < MAX_ENTITIES; i++) {
                Entity& proj = entities[i];
                if (!proj.alive || proj.type != EntityType::Projectile) continue;
                if (proj.owner != -3) continue; // only real player projectiles (skip dummy -4)
                if (proj.ai_state == 1) continue; // stuck in wall, no more hits

                bool hit_something = false;
                for (int j = 0; j < MAX_ENTITIES; j++) {
                    Entity& e = entities[j];
                    if (j == i) continue;
                    if (!is_live_enemy(e)) continue;
                    if (proj.has_pierced(j)) continue; // already damaged this enemy

                    float dist = HMM_LenV3(HMM_SubV3(proj.position, e.position));
                    if (dist < proj.radius + e.radius) {
                        // Apply tipping mods to base damage
                        RoundMod rm = proj.round_mod;
                        float base_dmg = proj.damage;

                        // Bleed multiplier (Serrated stacks — applied before tipping)
                        base_dmg *= (1.0f + 0.1f * e.bleed_stacks);

                        if (rm.tipping == Tipping::Sharpened)
                            base_dmg += 10.0f;
                        if (rm.tipping == Tipping::Crystal_Tipped)
                            base_dmg *= 2.0f;
                        if (rm.tipping == Tipping::Aerodynamic)
                            base_dmg *= 1.2f;

                        // Apply shield absorption (Piercing bypasses)
                        bool piercing = (rm.tipping == Tipping::Piercing);
                        float actual_dmg = piercing ? base_dmg : shielder_absorb_damage(e, base_dmg);
                        e.health -= actual_dmg;

                        // Record stats
                        {
                            float bleed_mult = 1.0f + 0.1f * e.bleed_stacks;
                            room_stats.record_dealt(proj.damage, actual_dmg, rm.tipping, bleed_mult);
                        }

                        // Poison tipping
                        if (rm.tipping == Tipping::Poison_Tipped) {
                            e.poison_stacks++;
                            e.poison_timer = 5.0f;
                        }

                        // Serrated: apply bleed stack (permanent)
                        if (rm.tipping == Tipping::Serrated)
                            e.bleed_stacks++;

                        // Crystal Tipped: 1/10 chance to shatter (clear from magazine)
                        if (rm.tipping == Tipping::Crystal_Tipped) {
                            if (rand() % 10 == 0) {
                                proj.round_mod.tipping = Tipping::None;
                                weapons[active_weapon].magazine.set_tipping(
                                    proj.fired_round_idx, Tipping::None);
                            }
                        }

                        // Enchantment effects
                        if (rm.enchantment == Enchantment::Vampiric) {
                            player.health += actual_dmg * 0.1f;
                            if (player.health > player.max_health)
                                player.health = player.max_health;
                        }

                        // Floating damage number at hit point
                        {
                            HMM_Vec3 hit_pos = e.position;
                            hit_pos.Y += e.radius * 0.5f;
                            hit_pos.X += randf(-0.2f, 0.2f);
                            hit_pos.Z += randf(-0.2f, 0.2f);
                            bool is_kill = (e.health <= 0);
                            dmg_numbers.spawn(hit_pos, (int)actual_dmg, j, is_kill);
                        }

                        // Hit flash
                        if (e.type == EntityType::Turret)       e.hit_flash = turret_cfg.hit_flash_time;
                        else if (e.type == EntityType::Tank)    e.hit_flash = tank_cfg.hit_flash_time;
                        else if (e.type == EntityType::Bomber)  e.hit_flash = bomber_cfg.hit_flash_time;
                        else if (e.type == EntityType::Shielder)e.hit_flash = shielder_cfg.hit_flash_time;
                        else                                    e.hit_flash = drone_cfg.hit_flash_time;

                        // Wake idle enemies
                        if (e.type == EntityType::Drone  && e.ai_state == DRONE_IDLE)   e.ai_state = DRONE_CHASING;
                        if (e.type == EntityType::Rusher && e.ai_state == RUSHER_IDLE)  e.ai_state = RUSHER_CHASING;
                        if (e.type == EntityType::Turret && e.ai_state == TURRET_IDLE)  e.ai_state = TURRET_TRACKING;
                        if (e.type == EntityType::Tank   && e.ai_state == TANK_IDLE)    e.ai_state = TANK_CHASING;
                        if (e.type == EntityType::Bomber && e.ai_state == BOMBER_IDLE)  e.ai_state = BOMBER_APPROACH;
                        if (e.type == EntityType::Shielder && e.ai_state == SHIELDER_IDLE) e.ai_state = SHIELDER_CHASING;

                        // Kill check
                        uint8_t dying_state = (uint8_t)DRONE_DYING;
                        float tumble = drone_cfg.death_tumble_speed;
                        if (e.type == EntityType::Rusher)  { dying_state = RUSHER_DYING;  tumble = rusher_cfg.death_tumble_speed; }
                        if (e.type == EntityType::Turret)  { dying_state = TURRET_DYING;  tumble = turret_cfg.death_tumble_speed; }
                        if (e.type == EntityType::Tank)    { dying_state = TANK_DYING;    tumble = tank_cfg.death_tumble_speed; }
                        if (e.type == EntityType::Bomber)  { dying_state = BOMBER_DYING;  tumble = bomber_cfg.death_tumble_speed; }
                        if (e.type == EntityType::Shielder){ dying_state = SHIELDER_DYING; tumble = shielder_cfg.death_tumble_speed; }

                        if (e.health <= 0 && e.ai_state != dying_state) {
                            e.ai_state = dying_state;
                            e.death_timer = 0.0f;
                            { int kr = kill_reward(e.type); currency += kr; room_stats.record_kill(e.type, kr); }
                            e.velocity.Y += 3.0f;
                            HMM_Vec3 kb = HMM_NormV3(proj.velocity);
                            e.velocity = HMM_AddV3(e.velocity, HMM_MulV3F(kb, 5.0f));
                            e.angular_vel = HMM_V3(
                                randf(-1.0f, 1.0f) * tumble * 57.3f, 0.0f,
                                randf(-1.0f, 1.0f) * tumble * 57.3f);
                        }

                        if (!piercing) {
                            hit_something = true;
                            break; // one hit per projectile (unless piercing)
                        } else {
                            proj.add_pierced(j); // don't hit this enemy again
                        }
                    }
                }
                if (hit_something) proj.alive = false;
            }

            // --- Projectile-player collision (capsule hitbox) ---
            for (int i = 0; i < MAX_ENTITIES; i++) {
                Entity& e = entities[i];
                if (!e.alive || e.type != EntityType::Projectile) continue;

                if (e.owner == -3 || e.owner == -4) continue; // skip player projectiles + dummy
                if (sphere_capsule_overlap(e.position, e.radius,
                                           player.capsule_bottom(), player.capsule_top(),
                                           player.radius)) {
                    player.health -= e.damage;
                    player.damage_accum += e.damage;
                    room_stats.record_taken(e.damage, EntityType::Projectile);
                    e.alive = false;
                }
            }
        } // end !show_settings

        // Projectiles always update (even in shop) so knives don't freeze mid-air
        projectiles_update(entities, MAX_ENTITIES, collision, dt);

        // Update effects
        effects.update(dt);

        // Check if all enemies dead → unlock exit door
        bool near_exit_door = false;
        bool exit_door_locked = true;
        {
            bool any_alive = false;
            for (int i = 0; i < MAX_ENTITIES; i++) {
                const Entity& e = entities[i];
                if (!e.alive) continue;
                if (e.type == EntityType::Drone || e.type == EntityType::Rusher ||
                    e.type == EntityType::Turret || e.type == EntityType::Tank ||
                    e.type == EntityType::Bomber || e.type == EntityType::Shielder) {
                    any_alive = true;
                    break;
                }
            }
            for (auto& d : active_doors) {
                if (d.is_exit && d.locked && !any_alive) {
                    d.locked = false;
                    printf("EXIT UNLOCKED!\n");
                }
                // Check proximity to exit door
                if (d.is_exit) {
                    exit_door_locked = d.locked;
                    HMM_Vec3 diff = HMM_SubV3(player.position, d.position);
                    diff.Y = 0; // horizontal distance only
                    float dist_sq = HMM_DotV3(diff, diff);
                    if (dist_sq < 3.0f * 3.0f) // within 3m
                        near_exit_door = true;
                }
            }
        }

        // --- Door interact → stats screen → shop ---
        if (interact_pressed && near_exit_door && !exit_door_locked && !show_settings && !in_shop_room && !show_room_summary) {
            // Show stats screen first; shop_enter happens on dismiss
            room_stats.finalize_no_damage_bonus();
            if (room_stats.gold_no_damage > 0)
                currency += room_stats.gold_no_damage;
            show_room_summary = true;
            interact_pressed = false;
        }
        // --- Shop: stand interaction + exit to next room ---
        shop_tick(gs, dt, interact_pressed);
        interact_pressed = false; // consume

        // Build frustum from current camera for culling
        Frustum frustum;
        {
            float aspect = (float)renderer.swapchain_width() / (float)renderer.swapchain_height();
            HMM_Mat4 vp = HMM_MulM4(camera.projection_matrix(aspect), camera.view_matrix());
            frustum.extract(vp);
        }

        // Update floating damage numbers
        dmg_numbers.update(dt);

        // Build entity mesh + opaque death effects (frustum-culled)
        Mesh entity_mesh = build_entity_mesh(entities, MAX_ENTITIES, frustum);
        effects.append_to_mesh(entity_mesh);

        // Health bars (billboarded geometry)
        HMM_Vec3 cam_right = camera.right();
        HMM_Vec3 cam_up = HMM_NormV3(HMM_Cross(camera.forward(), cam_right));
        build_health_bars(entity_mesh, entities, MAX_ENTITIES, frustum, cam_right, cam_up);

        // Shop: spinning weapon display models on stands
        shop_build_display_meshes(gs, entity_mesh, total_time);

        // Transparent death effects (outer glow)
        Mesh transparent_mesh;
        effects.append_transparent(transparent_mesh);

        // Shield bubbles around shielded enemies
        build_shield_bubbles(transparent_mesh, entities, MAX_ENTITIES, frustum);

        // Turret laser/beam/particle effects
        build_turret_effects(entity_mesh, transparent_mesh,
                             entities, MAX_ENTITIES, collision, frustum, total_time);

        // Aerodynamic projectile wind trails
        {
            HMM_Vec3 cr = camera.right();
            HMM_Vec3 cu = HMM_NormV3(HMM_Cross(camera.forward(), cr));
            for (int i = 0; i < MAX_ENTITIES; i++) {
                const Entity& e = entities[i];
                if (!e.alive || e.type != EntityType::Projectile) continue;
                if (e.owner != -3 && e.owner != -4) continue; // player knives only
                if (e.round_mod.tipping != Tipping::Aerodynamic) continue;
                if (e.ai_state == 1) continue; // stuck in wall

                HMM_Vec3 vel = e.velocity;
                float speed = HMM_LenV3(vel);
                if (speed < 0.1f) continue;
                HMM_Vec3 fwd_dir = HMM_MulV3F(vel, 1.0f / speed);

                // 4 wispy trail lines behind the projectile
                for (int s = 0; s < 4; s++) {
                    float phase = total_time * 8.0f + (float)s * 1.57f;
                    float wobble_r = sinf(phase) * 0.06f;
                    float wobble_u = cosf(phase * 1.3f) * 0.06f;

                    HMM_Vec3 offset = HMM_AddV3(
                        HMM_MulV3F(cr, wobble_r),
                        HMM_MulV3F(cu, wobble_u));

                    float trail_len = 0.6f + sinf(phase * 0.7f) * 0.15f;
                    HMM_Vec3 tip = HMM_AddV3(e.position, offset);
                    HMM_Vec3 tail = HMM_SubV3(tip, HMM_MulV3F(fwd_dir, trail_len));

                    float w = 0.015f;
                    HMM_Vec3 side = HMM_MulV3F(cr, w);
                    HMM_Vec3 up_off = HMM_MulV3F(cu, w);

                    // Horizontal streak
                    append_emissive_quad(transparent_mesh,
                        HMM_AddV3(tip, side), HMM_SubV3(tip, side),
                        HMM_SubV3(tail, side), HMM_AddV3(tail, side),
                        0.5f, 0.9f, 1.0f, 0.35f);
                    // Vertical streak
                    append_emissive_quad(transparent_mesh,
                        HMM_AddV3(tip, up_off), HMM_SubV3(tip, up_off),
                        HMM_SubV3(tail, up_off), HMM_AddV3(tail, up_off),
                        0.5f, 0.9f, 1.0f, 0.25f);
                }
            }
        }

        // Debug: visualize ladder volumes as transparent green boxes
        if (show_ladder_debug) {
            for (const auto& vol : collision.ladder_volumes) {
                HMM_Vec3 mn = vol.mins, mx = vol.maxs;
                // 8 corners
                HMM_Vec3 c[8] = {
                    {mn.X,mn.Y,mn.Z}, {mx.X,mn.Y,mn.Z}, {mx.X,mx.Y,mn.Z}, {mn.X,mx.Y,mn.Z},
                    {mn.X,mn.Y,mx.Z}, {mx.X,mn.Y,mx.Z}, {mx.X,mx.Y,mx.Z}, {mn.X,mx.Y,mx.Z},
                };
                // 6 faces as 2 tris each (emissive: normal.x = alpha)
                uint32_t faces[6][4] = {
                    {0,1,2,3}, {4,5,6,7}, // -Z, +Z
                    {0,1,5,4}, {2,3,7,6}, // -Y, +Y
                    {0,3,7,4}, {1,2,6,5}, // -X, +X
                };
                uint32_t base = (uint32_t)transparent_mesh.vertices.size();
                for (int i = 0; i < 8; i++) {
                    Vertex3D v;
                    v.pos[0] = c[i].X; v.pos[1] = c[i].Y; v.pos[2] = c[i].Z;
                    v.normal[0] = 0.25f; v.normal[1] = 0.0f; v.normal[2] = 0.0f; // alpha
                    v.color[0] = 0.1f; v.color[1] = 1.0f; v.color[2] = 0.2f; // green
                    transparent_mesh.vertices.push_back(v);
                }
                for (int f = 0; f < 6; f++) {
                    uint32_t a = base+faces[f][0], b = base+faces[f][1];
                    uint32_t cc = base+faces[f][2], d = base+faces[f][3];
                    transparent_mesh.indices.push_back(a);
                    transparent_mesh.indices.push_back(b);
                    transparent_mesh.indices.push_back(cc);
                    transparent_mesh.indices.push_back(a);
                    transparent_mesh.indices.push_back(cc);
                    transparent_mesh.indices.push_back(d);
                }
            }
        }

        // Empty — particle pipeline no-ops with 0 indices
        std::vector<ParticleVertex> particle_verts;
        std::vector<uint32_t> particle_indices;

        // --- ImGui frame ---
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // --- HUD ---
        {
            int enemy_count_hud = 0;
            for (int i = 0; i < MAX_ENTITIES; i++) {
                const Entity& e = entities[i];
                if (!e.alive) continue;
                if (e.type == EntityType::Drone || e.type == EntityType::Rusher ||
                    e.type == EntityType::Turret || e.type == EntityType::Tank ||
                    e.type == EntityType::Bomber || e.type == EntityType::Shielder)
                    enemy_count_hud++;
            }
            HudContext ctx{display_fps, near_exit_door, exit_door_locked, enemy_count_hud};
            hud_draw(gs, ctx);
        }

        // --- Shop room HUD ---
        shop_draw_hud(gs);

        // --- Magazine card view ---
        magazine_view_draw(gs);

        // --- Room summary screen ---
        if (show_room_summary) {
            if (draw_room_summary(room_stats, rooms_cleared + 1)) {
                show_room_summary = false;
                shop_enter(gs);
            }
        }

        // --- Floating damage numbers (screen-space) ---
        {
            ImGuiIO& io = ImGui::GetIO();
            float sw = io.DisplaySize.x, sh = io.DisplaySize.y;
            float aspect_dmg = (sh > 0) ? sw / sh : 1.0f;
            HMM_Mat4 vp = HMM_MulM4(camera.projection_matrix(aspect_dmg), camera.view_matrix());
            dmg_numbers.draw_ui(vp, sw, sh);
        }

        // --- Settings / debug menu ---
        debug_menu_draw(gs, load_level_fn);

        ImGui::Render();

        // --- Build scene data ---
        float aspect = 16.0f / 9.0f;
        {
            int w, h;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            if (h > 0) aspect = static_cast<float>(w) / static_cast<float>(h);
        }

        // Use weapon's effective FOV (accounts for ADS)
        float effective_fov = weapons[active_weapon].get_effective_fov(camera.fov);
        Camera render_cam = camera;
        render_cam.fov = effective_fov;

        SceneData scene{};
        scene.view       = render_cam.view_matrix();
        scene.projection = render_cam.projection_matrix(aspect);
        scene.light_dir  = HMM_V4(0.4f, 0.8f, 0.3f, 0.0f);
        scene.camera_pos = HMM_V4(camera.position.X, camera.position.Y, camera.position.Z, 0.0f);

        // Viewmodel scene data — same view, but tighter near plane to prevent clipping
        const Mesh* vm_mesh_ptr = nullptr;
        {
            Weapon& vw = weapons[active_weapon];
            bool show_vm = vw.mesh_loaded && (!player.weapon_holstered || vw.holster_raising);
            if (show_vm) vm_mesh_ptr = &vw.viewmodel_mesh;
        }
        HMM_Mat4 vm_model = weapons[active_weapon].get_viewmodel_matrix(camera);
        HMM_Mat4 vm_mag_model = weapons[active_weapon].get_mag_matrix(camera);
        SceneData vm_scene = scene;
        {
            Camera vm_cam = render_cam;
            vm_cam.near_plane = 0.01f;
            vm_scene.projection = vm_cam.projection_matrix(aspect);
        }

        const HMM_Mat4* mag_ptr = weapons[active_weapon].has_mag_submesh ? &vm_mag_model : nullptr;
        uint32_t mag_start = weapons[active_weapon].has_mag_submesh ? weapons[active_weapon].mag_index_start : 0;
        uint32_t mag_count = weapons[active_weapon].has_mag_submesh ? weapons[active_weapon].mag_index_count : 0;

        const Mesh* trans_ptr = transparent_mesh.indices.empty() ? nullptr : &transparent_mesh;
        renderer.draw_frame(scene, &entity_mesh, &particle_verts, &particle_indices,
                            total_time, vm_mesh_ptr, &vm_model, &vm_scene,
                            mag_ptr, mag_start, mag_count, trans_ptr);
    }

    config.pull(camera, player);
    config.save();

    renderer.wait_idle();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
