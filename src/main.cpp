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
#include "procgen.h"
#include "entity_render.h"
#include "effects.h"
#include "weapon.h"

#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_impl_sdl3.h"
#include "vendor/imgui/imgui_impl_vulkan.h"

namespace fs = std::filesystem;

// Scan a directory for .glb/.gltf files
static std::vector<std::string> scan_levels(const std::string& dir) {
    std::vector<std::string> files;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return files;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file()) continue;
        auto ext = entry.path().extension().string();
        // lowercase compare
        for (auto& c : ext) c = static_cast<char>(tolower(c));
        if (ext == ".glb" || ext == ".gltf") {
            files.push_back(entry.path().string());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

// Load a level and update all engine state
static bool load_level(const std::string& path,
                       Renderer& renderer, CollisionWorld& collision,
                       Player& player, Camera& camera,
                       bool& noclip, std::string& current_level_name,
                       Entity entities[] = nullptr, int max_entities = 0,
                       const DroneConfig* drone_cfg = nullptr,
                       const RusherConfig* rusher_cfg = nullptr)
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
    bool ai_enabled = true;
    float fly_speed = 15.0f;

    // Level browser state
    std::string current_level_name = (custom_level && level_arg) ? fs::path(level_arg).filename().string()
                                   : custom_level              ? "Room1.glb"
                                   :                             "Procedural";
    ProcGenConfig procgen_cfg;
    std::vector<std::string> level_files;
    bool levels_scanned = false;
    char level_path_buf[512] = "";

    // --- Entities ---
    Entity entities[MAX_ENTITIES] = {};
    DroneConfig drone_cfg;
    RusherConfig rusher_cfg;
    EffectSystem effects;
    effects.init();
    float total_time = 0.0f;

    // Spawn preplaced enemies from level data
    for (const auto& es : initial_enemy_spawns) {
        if (es.type == EntityType::Drone)
            drone_spawn(entities, MAX_ENTITIES, es.position, drone_cfg);
        else if (es.type == EntityType::Rusher)
            rusher_spawn(entities, MAX_ENTITIES, es.position, rusher_cfg);
    }
    if (!initial_enemy_spawns.empty())
        printf("Spawned %zu preplaced enemies\n", initial_enemy_spawns.size());

    // --- Weapon ---
    Weapon weapon;
    weapon.init_wingman();
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
                weapon.viewmodel_mesh = std::move(vm_data.mesh);
                weapon.mesh_loaded = true;
                printf("Loaded viewmodel from '%s': %zu verts, %zu indices\n",
                       vp, weapon.viewmodel_mesh.vertices.size(),
                       weapon.viewmodel_mesh.indices.size());
                // Find "Mag" sub-mesh
                for (const auto& sub : vm_data.submeshes) {
                    if (strncmp(sub.name, "Mag", 3) == 0 || strncmp(sub.name, "mag", 3) == 0) {
                        weapon.mag_index_start = sub.index_start;
                        weapon.mag_index_count = sub.index_count;
                        weapon.has_mag_submesh = true;
                        printf("  Found mag sub-mesh '%s': indices %u..%u (%u)\n",
                               sub.name, sub.index_start,
                               sub.index_start + sub.index_count, sub.index_count);
                    }
                }
                break;
            }
        }
        if (!weapon.mesh_loaded)
            printf("WARNING: Could not load viewmodel (tried assets/, ../assets/, ./)\n");
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

    while (running) {
        float mouse_dx = 0.0f, mouse_dy = 0.0f;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (show_settings)
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
                    show_settings = !show_settings;
                    rebinding_action = -1;
                    SDL_SetWindowRelativeMouseMode(window, !show_settings);
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
                    if (kb.matches_scancode(Action::ToggleFullscreen, event.key.scancode) && !event.key.repeat) {
                        Uint32 flags = SDL_GetWindowFlags(window);
                        SDL_SetWindowFullscreen(window, (flags & SDL_WINDOW_FULLSCREEN) ? false : true);
                    }
                    if (kb.matches_scancode(Action::Jump, event.key.scancode))
                        jump_held = true;
                    if (kb.matches_scancode(Action::Interact, event.key.scancode) && !event.key.repeat)
                        interact_pressed = true;
                    if (kb.matches_scancode(Action::Holster, event.key.scancode) && !event.key.repeat)
                        player.weapon_holstered = !player.weapon_holstered;
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

        if (!show_settings) {
            float sens_scale = 1.0f - weapon.ads_blend * (1.0f - weapon.config.ads_sens_mult);
            camera.mouse_look(mouse_dx * sens_scale, mouse_dy * sens_scale);
        }

        // Get keyboard state once per frame (used by movement, weapon, etc.)
        const bool* keys_frame = SDL_GetKeyboardState(nullptr);

        // --- Movement ---
        if (noclip && !show_settings) {
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
        } else if (!show_settings) {
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

        // --- Weapon update & shooting ---
        {
            bool holstered = player.weapon_holstered;
            bool fire_pressed = !show_settings && !noclip && !holstered && kb.held(Action::Shoot, keys_frame);
            bool reload_pressed = !show_settings && !noclip && !holstered && kb.held(Action::Reload, keys_frame);
            bool ads_input = !show_settings && !noclip && !holstered && kb.held(Action::ADS, keys_frame);

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
                }

                HMM_Vec3 ray_origin = camera.position;
                HMM_Vec3 ray_dir    = camera.forward();

                float best_t = weapon.config.range;
                int best_idx = -1;

                for (int i = 0; i < MAX_ENTITIES; i++) {
                    Entity& e = entities[i];
                    if (!e.alive) continue;
                    if (e.type != EntityType::Drone && e.type != EntityType::Rusher) continue;

                    // Ray-sphere intersection
                    HMM_Vec3 oc = HMM_SubV3(ray_origin, e.position);
                    float b = HMM_DotV3(oc, ray_dir);
                    float c = HMM_DotV3(oc, oc) - e.radius * e.radius;
                    float disc = b * b - c;
                    if (disc < 0) continue;

                    float t = -b - sqrtf(disc);
                    if (t < 0) t = -b + sqrtf(disc);
                    if (t > 0 && t < best_t) {
                        HitResult wall = collision.raycast(ray_origin, ray_dir, t);
                        if (!wall.hit) {
                            best_t = t;
                            best_idx = i;
                        }
                    }
                }

                if (best_idx >= 0) {
                    Entity& hit_ent = entities[best_idx];
                    hit_ent.health -= weapon.config.damage;
                    hit_ent.hit_flash = drone_cfg.hit_flash_time;

                    // Wake up idle enemies on hit
                    if (hit_ent.type == EntityType::Drone && hit_ent.ai_state == DRONE_IDLE)
                        hit_ent.ai_state = DRONE_CHASING;
                    if (hit_ent.type == EntityType::Rusher && hit_ent.ai_state == RUSHER_IDLE)
                        hit_ent.ai_state = RUSHER_CHASING;

                    // Determine dying state for this entity type
                    uint8_t dying_state = (hit_ent.type == EntityType::Rusher)
                                          ? (uint8_t)RUSHER_DYING : (uint8_t)DRONE_DYING;
                    float tumble = (hit_ent.type == EntityType::Rusher)
                                   ? rusher_cfg.death_tumble_speed : drone_cfg.death_tumble_speed;

                    if (hit_ent.health <= 0 && hit_ent.ai_state != dying_state) {
                        hit_ent.ai_state = dying_state;
                        hit_ent.death_timer = 0.0f;
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
            }
        }

        // --- Update entities (only when not paused) ---
        if (!show_settings) {
            // Track dying drones to spawn explosions when they hit ground
            struct DyingEnemy { int idx; HMM_Vec3 pos; bool was_alive; };
            drone_tick_frame();
            rusher_tick_frame();

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
                        if (rusher_check_player_hit(e, player.position, player.radius, rusher_cfg)) {
                            // TODO: player takes damage
                        }
                    }
                }
            }

            // Check which dying enemies just expired → explosion
            for (int d = 0; d < dying_count; d++) {
                Entity& e = entities[dying[d].idx];
                if (!e.alive && dying[d].was_alive) {
                    effects.spawn_drone_explosion(dying[d].pos);
                }
            }

            projectiles_update(entities, MAX_ENTITIES, collision, dt);

            // Helper: is entity a live (non-dying) enemy?
            auto is_live_enemy = [](const Entity& e) -> bool {
                if (!e.alive) return false;
                if (e.type == EntityType::Drone)  return e.ai_state != DRONE_DYING && e.ai_state != DRONE_DEAD;
                if (e.type == EntityType::Rusher) return e.ai_state != RUSHER_DYING && e.ai_state != RUSHER_DEAD;
                return false;
            };

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

            // --- Projectile-player collision ---
            for (int i = 0; i < MAX_ENTITIES; i++) {
                Entity& e = entities[i];
                if (!e.alive || e.type != EntityType::Projectile) continue;
                HMM_Vec3 to_player = HMM_SubV3(player.eye_position(), e.position);
                float dist_sq = HMM_DotV3(to_player, to_player);
                float hit_radius = e.radius + player.radius;
                if (dist_sq < hit_radius * hit_radius) {
                    // TODO: player takes damage
                    e.alive = false;
                }
            }
        } // end !show_settings

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
                if (e.type == EntityType::Drone || e.type == EntityType::Rusher) {
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

        // Door interaction: pressing interact near unlocked exit → new room
        if (interact_pressed && near_exit_door && !exit_door_locked && !show_settings) {
            printf("Entering next room...\n");
            procgen_cfg.seed = 0; // random seed each time

            LevelData pld = generate_level(procgen_cfg, door_mesh_ptr, &active_doors);

            // Clear entities + effects
            for (int i = 0; i < MAX_ENTITIES; i++) entities[i].alive = false;
            effects.init();

            // Build collision
            collision.triangles.clear();
            collision.ladder_volumes.clear();
            collision.build_from_mesh(pld.mesh);

            // Upload to renderer
            renderer.reload_mesh(pld.mesh);

            // Spawn player facing into room (+Z = yaw PI/2)
            player.position = pld.spawn_pos;
            player.velocity = HMM_V3(0, 0, 0);
            camera.yaw = HMM_PI32 / 2.0f; camera.pitch = 0;
            noclip = false;

            // Spawn enemies
            for (const auto& es : pld.enemy_spawns) {
                if (es.type == EntityType::Drone)
                    drone_spawn(entities, MAX_ENTITIES, es.position, drone_cfg);
                else if (es.type == EntityType::Rusher)
                    rusher_spawn(entities, MAX_ENTITIES, es.position, rusher_cfg);
            }

            current_level_name = "Procedural";
        }
        interact_pressed = false; // consume

        // Build frustum from current camera for culling
        Frustum frustum;
        {
            float aspect = (float)renderer.swapchain_width() / (float)renderer.swapchain_height();
            HMM_Mat4 vp = HMM_MulM4(camera.projection_matrix(aspect), camera.view_matrix());
            frustum.extract(vp);
        }

        // Build entity mesh + opaque death effects (frustum-culled)
        Mesh entity_mesh = build_entity_mesh(entities, MAX_ENTITIES, frustum);
        effects.append_to_mesh(entity_mesh);

        // Transparent death effects (outer glow)
        Mesh transparent_mesh;
        effects.append_transparent(transparent_mesh);

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
        if (show_hud && !show_settings) {
            ImGui::SetNextWindowPos(ImVec2(10, 10));
            ImGui::SetNextWindowBgAlpha(0.4f);
            ImGui::Begin("##hud", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings);

            ImGui::Text("FPS: %.0f", display_fps);
            float speed_xz = sqrtf(player.velocity.X * player.velocity.X +
                                   player.velocity.Z * player.velocity.Z);
            ImGui::Text("Speed: %.1f u/s", speed_xz);
            ImGui::Text("Pos: %.1f %.1f %.1f", player.position.X, player.position.Y, player.position.Z);
            const char* state = "AIR";
            if (player.grounded) {
                if (player.sliding)       state = player.power_sliding ? "POWER SLIDE" : "SLIDE";
                else if (player.crouched) state = "CROUCH";
                else                      state = "GROUND";
            }
            ImGui::Text("%s", state);
            if (player.lurch_timer > 0.0f) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1,0.7f,0.2f,1), "LURCH");
            }
            if (noclip) ImGui::Text("NOCLIP");
            // Count alive enemies
            int drone_count = 0, rusher_count_hud = 0;
            for (int i = 0; i < MAX_ENTITIES; i++) {
                if (!entities[i].alive) continue;
                if (entities[i].type == EntityType::Drone) drone_count++;
                if (entities[i].type == EntityType::Rusher) rusher_count_hud++;
            }
            if (drone_count > 0 || rusher_count_hud > 0)
                ImGui::Text("Enemies: %d", drone_count + rusher_count_hud);
            for (const auto& d : active_doors) {
                if (d.is_exit) {
                    if (d.locked)
                        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "EXIT: LOCKED (%d remaining)", drone_count + rusher_count_hud);
                    else if (near_exit_door)
                        ImGui::TextColored(ImVec4(1,1,0.3f,1), "Press [%s] to enter next room",
                                           input_code_name(kb.get(Action::Interact, 0)));
                    else
                        ImGui::TextColored(ImVec4(0.3f,1,0.3f,1), "EXIT: UNLOCKED");
                }
            }

            // --- Weapon HUD ---
            ImGui::Separator();
            ImGui::Text("Wingman  %d / %d", weapon.ammo, weapon.config.mag_size);
            if (weapon.state == WeaponState::RELOADING) {
                float pct = 1.0f - weapon.reload_timer / weapon.config.reload_time;
                ImGui::ProgressBar(pct, ImVec2(-1, 4), "");
                const char* phase_name =
                    weapon.reload_phase == ReloadPhase::MAG_OUT  ? "MAG OUT" :
                    weapon.reload_phase == ReloadPhase::MAG_SWAP ? "MAG SWAP" :
                    weapon.reload_phase == ReloadPhase::GUN_UP   ? "GUN UP" : "RELOADING";
                ImGui::TextColored(ImVec4(1,1,0,1), "%s...", phase_name);
            }
            if (weapon.ads_blend > 0.01f)
                ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "ADS");

            ImGui::Separator();
            ImGui::TextDisabled("ESC: settings  H: hide HUD  R: reload  RMB: aim");

            ImGui::End();

            // --- Crosshair ---
            ImDrawList* draw = ImGui::GetForegroundDrawList();
            ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
            float cs = 8.0f;
            float ct = 2.0f;
            ImU32 cross_col = IM_COL32(255, 255, 255, 200);
            draw->AddLine(ImVec2(center.x - cs, center.y), ImVec2(center.x + cs, center.y), cross_col, ct);
            draw->AddLine(ImVec2(center.x, center.y - cs), ImVec2(center.x, center.y + cs), cross_col, ct);
        }

        // --- Settings window ---
        if (show_settings) {
            ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2(420, 60), ImGuiCond_FirstUseEver);

            ImGui::Begin("Settings", &show_settings);

            // --- Level ---
            if (ImGui::CollapsingHeader("Level", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::Text("Current: %s", current_level_name.c_str());
                ImGui::Spacing();

                // Scan for levels on first open
                if (!levels_scanned) {
                    level_files = scan_levels("levels");
                    // Also try relative to exe on Windows
                    if (level_files.empty())
                        level_files = scan_levels("../levels");
                    if (level_files.empty())
                        level_files = scan_levels("../../levels");
                    levels_scanned = true;
                }

                if (ImGui::Button("Refresh Level List")) {
                    level_files = scan_levels("levels");
                    if (level_files.empty())
                        level_files = scan_levels("../levels");
                    if (level_files.empty())
                        level_files = scan_levels("../../levels");
                }

                ImGui::SameLine();
                if (ImGui::Button("Built-in Test Level")) {
                    Mesh test = create_test_level();
                    renderer.reload_mesh(test);
                    collision.triangles.clear();
                    collision.build_from_mesh(test);
                    player.position = HMM_V3(0, 1, 15);
                    player.velocity = HMM_V3(0, 0, 0);
                    noclip = false;
                    camera.position = player.eye_position();
                    current_level_name = "built-in test level";
                }

                if (!level_files.empty()) {
                    ImGui::Spacing();
                    ImGui::BeginChild("##levellist", ImVec2(0, 150), ImGuiChildFlags_Borders);
                    for (auto& path : level_files) {
                        std::string name = fs::path(path).filename().string();
                        if (ImGui::Selectable(name.c_str())) {
                            load_level(path, renderer, collision, player, camera,
                                       noclip, current_level_name,
                                       entities, MAX_ENTITIES, &drone_cfg, &rusher_cfg);
                        }
                    }
                    ImGui::EndChild();
                } else {
                    ImGui::TextDisabled("No .glb/.gltf files found in levels/");
                }

                ImGui::Spacing();
                ImGui::Text("Or enter a path:");
                ImGui::SetNextItemWidth(-80);
                ImGui::InputText("##lvlpath", level_path_buf, sizeof(level_path_buf));
                ImGui::SameLine();
                if (ImGui::Button("Load") && level_path_buf[0]) {
                    load_level(level_path_buf, renderer, collision, player, camera,
                               noclip, current_level_name,
                               entities, MAX_ENTITIES, &drone_cfg, &rusher_cfg);
                }

                ImGui::Separator();
                ImGui::Text("Procedural Generation");
                if (ImGui::Button("Generate New Level")) {
                    LevelData pld = generate_level(procgen_cfg, door_mesh_ptr, &active_doors);

                    // Clear entities + effects
                    for (int i = 0; i < MAX_ENTITIES; i++) entities[i].alive = false;
                    effects.init();

                    // Build collision
                    collision.triangles.clear();
                    collision.ladder_volumes.clear();
                    collision.build_from_mesh(pld.mesh);

                    // Upload to renderer
                    renderer.reload_mesh(pld.mesh);

                    // Spawn player facing into room (+Z = yaw PI/2)
                    player.position = pld.spawn_pos;
                    player.velocity = HMM_V3(0, 0, 0);
                    camera.yaw = HMM_PI32 / 2.0f; camera.pitch = 0;
                    noclip = false;

                    // Spawn enemies
                    for (const auto& es : pld.enemy_spawns) {
                        if (es.type == EntityType::Drone)
                            drone_spawn(entities, MAX_ENTITIES, es.position, drone_cfg);
                        else if (es.type == EntityType::Rusher)
                            rusher_spawn(entities, MAX_ENTITIES, es.position, rusher_cfg);
                    }

                    current_level_name = "Procedural";
                }
                ImGui::SameLine();
                ImGui::InputInt("Seed", (int*)&procgen_cfg.seed);

                ImGui::SliderFloat2("Room W", &procgen_cfg.room_width_min, 15.0f, 80.0f, "%.0f");
                ImGui::SliderFloat2("Room D", &procgen_cfg.room_depth_min, 15.0f, 80.0f, "%.0f");
                ImGui::SliderFloat("Room Height", &procgen_cfg.room_height, 5.0f, 25.0f, "%.0f");
                ImGui::SliderInt("Boxes Min", &procgen_cfg.box_count_min, 0, 30);
                ImGui::SliderInt("Boxes Max", &procgen_cfg.box_count_max, 0, 30);
                ImGui::SliderInt("Platforms", &procgen_cfg.platform_count, 0, 6);
                ImGui::Checkbox("Ramps", &procgen_cfg.gen_ramps);
                ImGui::SliderInt("Drones", &procgen_cfg.drone_count, 0, 20);
                ImGui::SliderInt("Rushers", &procgen_cfg.rusher_count, 0, 20);
            }

            // --- Enemies ---
            if (ImGui::CollapsingHeader("Weapon")) {
                ImGui::Text("State: %s",
                    weapon.state == WeaponState::IDLE ? "Idle" :
                    weapon.state == WeaponState::FIRING ? "Firing" :
                    weapon.state == WeaponState::RELOADING ? "Reloading" : "?");
                ImGui::Separator();

                ImGui::SliderFloat("Damage",       &weapon.config.damage,     1.0f, 200.0f);
                ImGui::SliderFloat("Fire Rate",    &weapon.config.fire_rate,  0.5f, 10.0f, "%.1f shots/s");
                ImGui::SliderFloat("Range",        &weapon.config.range,      10.0f, 500.0f);
                ImGui::SliderFloat("Reload Time",  &weapon.config.reload_time, 0.5f, 5.0f, "%.1fs");
                ImGui::SliderFloat("Crit Multi",   &weapon.config.crit_multiplier, 1.0f, 5.0f, "%.1fx");
                ImGui::Separator();

                ImGui::Text("Viewmodel");
                ImGui::SliderFloat("Model Scale",     &weapon.config.model_scale, 0.01f, 5.0f, "%.3f");
                ImGui::SliderFloat3("Model Rotation", &weapon.config.model_rotation.X, -180.0f, 180.0f, "%.1f deg");
                ImGui::SliderFloat3("Hip Offset",     &weapon.config.hip_offset.X, -1.0f, 1.0f, "%.3f");
                ImGui::SliderFloat3("ADS Offset",     &weapon.config.ads_offset.X, -1.0f, 1.0f, "%.3f");
                ImGui::Separator();

                ImGui::Text("ADS");
                ImGui::SliderFloat("ADS FOV Mult",  &weapon.config.ads_fov_mult,  0.5f, 1.0f, "%.2f");
                ImGui::SliderFloat("ADS Sens Mult", &weapon.config.ads_sens_mult, 0.1f, 1.0f, "%.2f");
                ImGui::SliderFloat("ADS Speed",     &weapon.config.ads_speed,     1.0f, 20.0f);
                ImGui::Separator();

                ImGui::Text("Recoil");
                ImGui::SliderFloat("Recoil Kick",     &weapon.config.recoil_kick,     0.0f, 0.2f, "%.3f");
                ImGui::SliderFloat("Recoil Pitch",    &weapon.config.recoil_pitch,    -60.0f, 60.0f, "%.1f deg");
                ImGui::SliderFloat("Recoil Roll",     &weapon.config.recoil_roll,     0.0f, 20.0f, "%.1f deg");
                ImGui::SliderFloat("Recoil Side",     &weapon.config.recoil_side,     0.0f, 0.1f, "%.3f");
                ImGui::SliderFloat("Recoil Recovery", &weapon.config.recoil_recovery, 1.0f, 30.0f);
                {
                    const char* tilt_items[] = { "Right", "Left" };
                    int tilt_idx = (weapon.config.recoil_tilt_dir >= 0.0f) ? 0 : 1;
                    if (ImGui::Combo("Tilt Direction", &tilt_idx, tilt_items, 2))
                        weapon.config.recoil_tilt_dir = (tilt_idx == 0) ? 1.0f : -1.0f;
                }
                ImGui::SliderFloat("Reload Buffer Delay", &weapon.config.reload_buffer_delay, 0.0f, 1.0f, "%.2fs");
                ImGui::Separator();

                ImGui::Text("Reload Anim (3-phase)");
                ImGui::SliderFloat("Phase1 End (mag out)", &weapon.config.reload_phase1, 0.05f, 0.5f, "%.2f");
                ImGui::SliderFloat("Phase2 End (mag swap)", &weapon.config.reload_phase2, 0.1f, 0.9f, "%.2f");
                ImGui::SliderFloat("Reload Drop",     &weapon.config.reload_drop_dist, 0.0f, 0.5f, "%.3f");
                ImGui::SliderFloat("Reload Tilt",     &weapon.config.reload_tilt,      0.0f, 60.0f, "%.1f deg");
                ImGui::SliderFloat("Mag Drop Dist",   &weapon.config.mag_drop_dist,    0.0f, 1.0f, "%.3f");
                ImGui::SliderFloat("Mag Insert Dist", &weapon.config.mag_insert_dist,  0.0f, 0.5f, "%.3f");
                if (weapon.has_mag_submesh)
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Mag sub-mesh: found (%u indices)", weapon.mag_index_count);
                else
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Mag sub-mesh: not found");

                if (ImGui::Button("Reset Weapon Defaults")) {
                    weapon.init_wingman();
                }
            }

            if (ImGui::CollapsingHeader("Enemies")) {
                ImGui::Checkbox("AI Enabled", &ai_enabled);
                ImGui::Separator();
                if (ImGui::Button("Spawn Drone (in front of player)")) {
                    HMM_Vec3 fwd = camera.forward_flat();
                    HMM_Vec3 spawn = HMM_AddV3(player.position, HMM_MulV3F(fwd, 10.0f));
                    spawn.Y += 3.0f;
                    int idx = drone_spawn(entities, MAX_ENTITIES, spawn, drone_cfg);
                    if (idx >= 0) entities[idx].ai_state = DRONE_CHASING; // immediate aggro
                }
                ImGui::SameLine();
                if (ImGui::Button("Kill All")) {
                    for (int i = 0; i < MAX_ENTITIES; i++)
                        entities[i].alive = false;
                }

                // Count alive entities
                int drone_count = 0, proj_count = 0;
                for (int i = 0; i < MAX_ENTITIES; i++) {
                    if (!entities[i].alive) continue;
                    if (entities[i].type == EntityType::Drone) drone_count++;
                    if (entities[i].type == EntityType::Projectile) proj_count++;
                }
                ImGui::Text("Drones: %d  Projectiles: %d", drone_count, proj_count);

                ImGui::Separator();
                ImGui::Text("Stats");
                ImGui::SliderFloat("Drone Health",     &drone_cfg.drone_health,     1.0f, 200.0f);
                ImGui::SliderFloat("Drone Radius",     &drone_cfg.drone_radius,     0.2f, 2.0f);
                ImGui::SliderFloat("Detection Range",  &drone_cfg.detection_range,  5.0f, 100.0f);
                ImGui::SliderFloat("Attack Range",     &drone_cfg.attack_range,     5.0f, 50.0f);
                ImGui::SliderFloat("Circle Distance",  &drone_cfg.circle_distance,  3.0f, 20.0f);
                ImGui::SliderFloat("Attack Windup",    &drone_cfg.attack_windup,    0.1f, 3.0f, "%.2fs");

                ImGui::Separator();
                ImGui::Text("Movement");
                ImGui::SliderFloat("Acceleration",     &drone_cfg.acceleration,     1.0f, 20.0f);
                ImGui::SliderFloat("Chase Speed Min",  &drone_cfg.chase_speed_min,  1.0f, 20.0f);
                ImGui::SliderFloat("Chase Speed Max",  &drone_cfg.chase_speed_max,  1.0f, 20.0f);
                ImGui::SliderFloat("Circle Speed Min", &drone_cfg.circle_speed_min, 1.0f, 20.0f);
                ImGui::SliderFloat("Circle Speed Max", &drone_cfg.circle_speed_max, 1.0f, 20.0f);

                ImGui::Separator();
                ImGui::Text("Hover");
                ImGui::SliderFloat("Hover Force",      &drone_cfg.hover_force,      1.0f, 30.0f);
                ImGui::SliderFloat("Hover Height Min", &drone_cfg.hover_height_min, 0.5f, 5.0f);
                ImGui::SliderFloat("Hover Height Max", &drone_cfg.hover_height_max, 0.5f, 5.0f);
                ImGui::SliderFloat("Bob Amplitude",    &drone_cfg.bob_amp_min,      0.0f, 2.0f);
                ImGui::SliderFloat("Bob Frequency",    &drone_cfg.bob_freq_min,     0.1f, 3.0f);

                ImGui::Separator();
                ImGui::Text("Wander (idle)");
                ImGui::SliderFloat("Wander Radius",    &drone_cfg.wander_radius,    2.0f, 20.0f);
                ImGui::SliderFloat("Wander Speed",     &drone_cfg.wander_speed,     0.5f, 10.0f);
                ImGui::SliderFloat("Wall Avoid Dist",  &drone_cfg.wall_avoid_dist,  0.5f, 5.0f);
                ImGui::SliderFloat("Wall Avoid Force", &drone_cfg.wall_avoid_force, 1.0f, 20.0f);

                ImGui::Separator();
                ImGui::Text("Projectile");
                ImGui::SliderFloat("Proj Speed",       &drone_cfg.projectile_speed,  5.0f, 50.0f);
                ImGui::SliderFloat("Proj Damage",      &drone_cfg.projectile_damage, 1.0f, 50.0f);

                ImGui::Separator();
                ImGui::Text("Death");
                ImGui::SliderFloat("Death Gravity",    &drone_cfg.death_gravity,     5.0f, 30.0f);
                ImGui::SliderFloat("Tumble Speed",     &drone_cfg.death_tumble_speed,1.0f, 20.0f);
                ImGui::SliderFloat("Hit Flash Time",   &drone_cfg.hit_flash_time,    0.05f, 0.5f, "%.2fs");

                ImGui::Separator();
                ImGui::Separator();
                ImGui::Text("=== RUSHER ===");
                if (ImGui::Button("Spawn Rusher (in front of player)")) {
                    HMM_Vec3 fwd = camera.forward_flat();
                    HMM_Vec3 spawn = HMM_AddV3(player.position, HMM_MulV3F(fwd, 10.0f));
                    spawn.Y += 3.0f;
                    int idx = rusher_spawn(entities, MAX_ENTITIES, spawn, rusher_cfg);
                    if (idx >= 0) entities[idx].ai_state = RUSHER_CHASING;
                }

                int rusher_count = 0;
                for (int i = 0; i < MAX_ENTITIES; i++)
                    if (entities[i].alive && entities[i].type == EntityType::Rusher) rusher_count++;
                ImGui::Text("Rushers: %d", rusher_count);

                ImGui::Separator();
                ImGui::Text("Stats");
                ImGui::SliderFloat("Rusher Health",      &rusher_cfg.health,         1.0f, 100.0f);
                ImGui::SliderFloat("Rusher Radius",      &rusher_cfg.radius,         0.2f, 2.0f);
                ImGui::SliderFloat("R Detection Range",  &rusher_cfg.detection_range, 5.0f, 100.0f);
                ImGui::SliderFloat("Melee Damage",       &rusher_cfg.melee_damage,   1.0f, 100.0f);

                ImGui::Separator();
                ImGui::Text("Movement");
                ImGui::SliderFloat("R Chase Speed",      &rusher_cfg.chase_speed,    5.0f, 30.0f);
                ImGui::SliderFloat("R Acceleration",     &rusher_cfg.acceleration,   1.0f, 20.0f);
                ImGui::SliderFloat("R Hover Height",     &rusher_cfg.hover_height,   0.5f, 6.0f);
                ImGui::SliderFloat("R Hover Force",      &rusher_cfg.hover_force,    1.0f, 30.0f);

                ImGui::Separator();
                ImGui::Text("Dash Attack");
                ImGui::SliderFloat("R Attack Range",     &rusher_cfg.attack_range,   2.0f, 20.0f);
                ImGui::SliderFloat("Charge Up Time",     &rusher_cfg.charge_up_time, 0.1f, 3.0f, "%.2fs");
                ImGui::SliderFloat("Braking Force",      &rusher_cfg.braking_force,  1.0f, 30.0f);
                ImGui::SliderFloat("Dash Force",         &rusher_cfg.dash_force,     10.0f, 100.0f);
                ImGui::SliderFloat("Dash Duration",      &rusher_cfg.dash_duration,  0.1f, 2.0f, "%.2fs");
                ImGui::SliderFloat("Dash Cooldown",      &rusher_cfg.dash_cooldown,  0.1f, 5.0f, "%.2fs");
            }

            // --- Mouse ---
            if (ImGui::CollapsingHeader("Mouse", ImGuiTreeNodeFlags_DefaultOpen)) {
                float sens_display = camera.sensitivity * 10000.0f;
                if (ImGui::SliderFloat("Sensitivity", &sens_display, 0.1f, 50.0f, "%.1f"))
                    camera.sensitivity = sens_display / 10000.0f;

                bool inv_x = camera.invert_x < 0.0f;
                bool inv_y = camera.invert_y < 0.0f;
                if (ImGui::Checkbox("Invert X (horizontal)", &inv_x))
                    camera.invert_x = inv_x ? -1.0f : 1.0f;
                if (ImGui::Checkbox("Invert Y (vertical)", &inv_y))
                    camera.invert_y = inv_y ? -1.0f : 1.0f;
            }

            // --- Video ---
            if (ImGui::CollapsingHeader("Video")) {
                ImGui::SliderFloat("FOV", &camera.fov, 60.0f, 130.0f, "%.0f");
            }

            // --- Keybinds ---
            if (ImGui::CollapsingHeader("Keybinds", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::TextDisabled("Click a slot to rebind. Press a key, mouse button, or scroll wheel.");
                ImGui::TextDisabled("Escape cancels. Right-click a slot to clear it.");
                ImGui::Spacing();

                for (int i = 0; i < ACTION_COUNT; i++) {
                    ImGui::PushID(i);

                    Action action = static_cast<Action>(i);
                    const char* name = action_name(action);

                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%-16s", name);

                    for (int s = 0; s < SLOTS; s++) {
                        ImGui::SameLine(s == 0 ? 160.0f : 300.0f);
                        ImGui::PushID(s);

                        if (rebinding_action == i && rebinding_slot == s) {
                            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.3f, 0.1f, 1.0f));
                            ImGui::Button("...press...");
                            ImGui::PopStyleColor();
                        } else {
                            InputCode code = kb.get(action, s);
                            char label[64];
                            snprintf(label, sizeof(label), "  %s  ", input_code_name(code));
                            if (ImGui::Button(label)) {
                                rebinding_action = i;
                                rebinding_slot = s;
                            }
                            // Right-click to clear
                            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                                kb.set(action, s, INPUT_NONE);
                            }
                        }

                        ImGui::PopID();
                    }

                    ImGui::PopID();
                }

                ImGui::Spacing();
                if (ImGui::Button("Reset Keybinds to Defaults")) {
                    kb.reset_defaults();
                    rebinding_action = -1;
                }
            }

            // --- Movement ---
            if (ImGui::CollapsingHeader("Movement")) {
                ImGui::SliderFloat("Gravity",          &player.gravity,        0.0f, 40.0f);
                ImGui::SliderFloat("Max Speed (Holstered)", &player.max_speed,    1.0f, 30.0f);
                ImGui::SliderFloat("Weapon Speed",     &player.weapon_speed,   1.0f, 30.0f);
                ImGui::SliderFloat("Air Wish Speed",   &player.air_wish_speed, 0.1f, 5.0f, "%.2f");
                ImGui::SliderFloat("Ground Accel",     &player.ground_accel,   1.0f, 50.0f);
                ImGui::Text("Holstered: %s (effective: %.1f u/s, accel: %.1f)",
                            player.weapon_holstered ? "YES" : "NO",
                            player.effective_max_speed(), player.effective_accel());
                ImGui::SliderFloat("Air Accel",        &player.air_accel,      1.0f, 200.0f);
                ImGui::SliderFloat("Friction",         &player.friction,       0.0f, 20.0f);
                ImGui::SliderFloat("Jump Speed",       &player.jump_speed,     1.0f, 20.0f);

                ImGui::Spacing();
                ImGui::Checkbox("Auto-hop (hold jump to bhop)", &player.auto_hop);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Slide");
                ImGui::SliderFloat("Slide Friction",      &player.slide_friction,         0.0f, 5.0f, "%.2f");
                ImGui::SliderFloat("Slide Boost",         &player.slide_boost,            0.0f, 10.0f);
                ImGui::SliderFloat("Slide Min Speed",     &player.slide_min_speed,        0.0f, 15.0f);
                ImGui::SliderFloat("Slide Stop Speed",    &player.slide_stop_speed,       0.0f, 10.0f);
                ImGui::SliderFloat("Slide Boost Cooldown",&player.slide_boost_cooldown,   0.0f, 5.0f);
                ImGui::SliderFloat("Slide Jump Boost",    &player.slide_jump_boost,       0.0f, 10.0f);
                ImGui::Separator();
                ImGui::Text("Speed Cap");
                ImGui::SliderFloat("Soft Speed Cap",     &player.soft_speed_cap,     0.0f, 30.0f);
                ImGui::SliderFloat("Soft Cap Drag",      &player.soft_cap_drag,      0.0f, 10.0f);
                ImGui::SliderFloat("Slope Land Convert",  &player.slope_landing_conversion, 0.0f, 1.0f, "%.2f");
                ImGui::SliderFloat("Crouch Speed",        &player.crouch_speed,           1.0f, 10.0f);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Text("Lurch");
                ImGui::SliderFloat("Lurch Window",    &player.lurch_window,   0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Lurch Strength",  &player.lurch_strength, 0.0f, 1.0f, "%.2f");

                ImGui::Spacing();
                if (ImGui::Button("Reset to Source defaults")) {
                    player.gravity        = 20.0f;
                    player.max_speed      = 8.0f;
                    player.air_wish_speed = 0.76f;
                    player.ground_accel   = 10.0f;
                    player.air_accel      = 70.0f;
                    player.friction       = 6.0f;
                    player.jump_speed     = 7.2f;
                    player.slide_friction = 0.8f;
                    player.slide_boost    = 3.0f;
                    player.slide_min_speed = 6.0f;
                    player.slide_stop_speed = 3.0f;
                    player.slide_boost_cooldown = 2.0f;
                    player.slide_jump_boost = 4.0f;
                    player.slope_landing_conversion = 0.5f;
                    player.crouch_speed   = 4.0f;
                    player.lurch_window   = 0.5f;
                    player.lurch_strength = 0.5f;
                }
            }

            if (ImGui::CollapsingHeader("Ladder")) {
                ImGui::SliderFloat("Climb Speed Mult",  &player.ladder_speed_mult, 0.1f, 2.0f, "%.2fx");
                ImGui::SliderFloat("Jump Off Mult",    &player.ladder_jump_mult,  0.5f, 3.0f, "%.2fx");
                ImGui::Text("Climb: %.1f u/s  Jump-off: %.1f u/s",
                            player.max_speed * player.ladder_speed_mult,
                            player.max_speed * player.ladder_jump_mult);
                ImGui::SliderFloat("Ladder Inflate",  &collision.ladder_inflate, 0.0f, 2.0f, "%.2f");
                ImGui::Checkbox("Show Ladder Volumes", &show_ladder_debug);
                ImGui::Text("Ladder volumes: %zu", collision.ladder_volumes.size());
                if (player.on_ladder)
                    ImGui::TextColored(ImVec4(0.4f,1,0.4f,1), "ON LADDER");
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Save Settings")) {
                config.pull(camera, player);
                config.save();
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Settings")) {
                config.load();
                config.apply(camera, player);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("Quit Game")) {
                running = false;
            }
            ImGui::PopStyleColor(2);

            ImGui::End();

            if (!show_settings) {
                SDL_SetWindowRelativeMouseMode(window, true);
                rebinding_action = -1;
                rebinding_slot = -1;
            }
        }

        ImGui::Render();

        // --- Build scene data ---
        float aspect = 16.0f / 9.0f;
        {
            int w, h;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            if (h > 0) aspect = static_cast<float>(w) / static_cast<float>(h);
        }

        // Use weapon's effective FOV (accounts for ADS)
        float effective_fov = weapon.get_effective_fov(camera.fov);
        Camera render_cam = camera;
        render_cam.fov = effective_fov;

        SceneData scene{};
        scene.view       = render_cam.view_matrix();
        scene.projection = render_cam.projection_matrix(aspect);
        scene.light_dir  = HMM_V4(0.4f, 0.8f, 0.3f, 0.0f);
        scene.camera_pos = HMM_V4(camera.position.X, camera.position.Y, camera.position.Z, 0.0f);

        // Viewmodel scene data — same view, but tighter near plane to prevent clipping
        const Mesh* vm_mesh_ptr = (weapon.mesh_loaded && !player.weapon_holstered) ? &weapon.viewmodel_mesh : nullptr;
        HMM_Mat4 vm_model = weapon.get_viewmodel_matrix(camera);
        HMM_Mat4 vm_mag_model = weapon.get_mag_matrix(camera);
        SceneData vm_scene = scene;
        {
            Camera vm_cam = render_cam;
            vm_cam.near_plane = 0.01f;
            vm_scene.projection = vm_cam.projection_matrix(aspect);
        }

        const HMM_Mat4* mag_ptr = weapon.has_mag_submesh ? &vm_mag_model : nullptr;
        uint32_t mag_start = weapon.has_mag_submesh ? weapon.mag_index_start : 0;
        uint32_t mag_count = weapon.has_mag_submesh ? weapon.mag_index_count : 0;

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
