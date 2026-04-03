#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <cmath>

#include "renderer.h"
#include "camera.h"
#include "mesh.h"
#include "collision.h"
#include "player.h"
#include "config.h"

#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_impl_sdl3.h"
#include "vendor/imgui/imgui_impl_vulkan.h"

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

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

    // --- Build level + collision ---
    Mesh level = create_test_level();
    CollisionWorld collision;
    collision.build_from_mesh(level);

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

    // Make it look a bit nicer
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

    player.position = HMM_V3(0.0f, 1.0f, 15.0f);

    bool noclip = false;
    bool show_settings = false;
    bool show_hud = true;
    float fly_speed = 15.0f;

    // --- Fixed timestep ---
    constexpr float TICK_RATE = 1.0f / 128.0f;
    float accumulator = 0.0f;

    // --- Input state ---
    InputState input{};
    bool jump_pressed = false;

    bool running = true;
    Uint64 last_time = SDL_GetPerformanceCounter();

    // FPS counter
    float fps_timer = 0.0f;
    int frame_count = 0;
    float display_fps = 0.0f;

    while (running) {
        // --- Accumulate mouse motion ---
        float mouse_dx = 0.0f, mouse_dy = 0.0f;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Let ImGui process events when settings is open
            if (show_settings)
                ImGui_ImplSDL3_ProcessEvent(&event);

            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    if (show_settings) {
                        show_settings = false;
                        SDL_SetWindowRelativeMouseMode(window, true);
                    } else {
                        running = false;
                    }
                }
                if (event.key.key == SDLK_TAB && !event.key.repeat) {
                    show_settings = !show_settings;
                    SDL_SetWindowRelativeMouseMode(window, !show_settings);
                }
                if (event.key.key == SDLK_V && !show_settings) {
                    noclip = !noclip;
                    printf("Noclip: %s\n", noclip ? "ON" : "OFF");
                    if (noclip)
                        camera.position = player.eye_position();
                }
                if (event.key.key == SDLK_H && !event.key.repeat)
                    show_hud = !show_hud;
                if (event.key.key == SDLK_SPACE && !show_settings)
                    jump_pressed = true;
                break;
            case SDL_EVENT_KEY_UP:
                if (event.key.key == SDLK_SPACE)
                    jump_pressed = false;
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

        // FPS counter
        fps_timer += dt;
        frame_count++;
        if (fps_timer >= 0.5f) {
            display_fps = static_cast<float>(frame_count) / fps_timer;
            frame_count = 0;
            fps_timer = 0.0f;
        }

        // --- Mouse look (only when settings closed) ---
        if (!show_settings)
            camera.mouse_look(mouse_dx, mouse_dy);

        // --- Movement ---
        if (noclip && !show_settings) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            HMM_Vec3 move_dir = HMM_V3(0, 0, 0);
            HMM_Vec3 fwd   = camera.forward_flat();
            HMM_Vec3 right = camera.right();

            if (keys[SDL_SCANCODE_W])      move_dir = HMM_AddV3(move_dir, fwd);
            if (keys[SDL_SCANCODE_S])      move_dir = HMM_SubV3(move_dir, fwd);
            if (keys[SDL_SCANCODE_D])      move_dir = HMM_AddV3(move_dir, right);
            if (keys[SDL_SCANCODE_A])      move_dir = HMM_SubV3(move_dir, right);
            if (keys[SDL_SCANCODE_SPACE])  move_dir.Y += 1.0f;
            if (keys[SDL_SCANCODE_LSHIFT]) move_dir.Y -= 1.0f;

            float len = HMM_LenV3(move_dir);
            if (len > 0.001f) {
                move_dir = HMM_MulV3F(move_dir, 1.0f / len);
                float speed = fly_speed;
                if (keys[SDL_SCANCODE_LCTRL]) speed *= 3.0f;
                camera.position = HMM_AddV3(camera.position, HMM_MulV3F(move_dir, speed * dt));
            }
        } else if (!show_settings) {
            const bool* keys = SDL_GetKeyboardState(nullptr);

            input.forward = 0.0f;
            input.right   = 0.0f;
            if (keys[SDL_SCANCODE_W]) input.forward += 1.0f;
            if (keys[SDL_SCANCODE_S]) input.forward -= 1.0f;
            if (keys[SDL_SCANCODE_D]) input.right   += 1.0f;
            if (keys[SDL_SCANCODE_A]) input.right   -= 1.0f;
            input.jump = jump_pressed;
            input.yaw  = camera.yaw;

            accumulator += dt;
            while (accumulator >= TICK_RATE) {
                player.update(TICK_RATE, input, collision);
                accumulator -= TICK_RATE;
                input.jump = false;
            }

            camera.position = player.eye_position();
        }

        // --- ImGui frame ---
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // --- HUD overlay (top-left) ---
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
            ImGui::Text("%s", player.grounded ? "GROUND" : "AIR");
            if (noclip) ImGui::Text("NOCLIP");
            ImGui::Separator();
            ImGui::TextDisabled("TAB: settings  V: noclip  H: hide HUD");

            ImGui::End();
        }

        // --- Settings window ---
        if (show_settings) {
            ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos(ImVec2(430, 100), ImGuiCond_FirstUseEver);

            ImGui::Begin("Settings", &show_settings);

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

            // --- Movement ---
            if (ImGui::CollapsingHeader("Movement")) {
                ImGui::SliderFloat("Gravity",          &player.gravity,        0.0f, 40.0f);
                ImGui::SliderFloat("Max Speed",        &player.max_speed,      1.0f, 30.0f);
                ImGui::SliderFloat("Air Wish Speed",   &player.air_wish_speed, 0.1f, 5.0f, "%.2f");
                ImGui::SliderFloat("Ground Accel",     &player.ground_accel,   1.0f, 50.0f);
                ImGui::SliderFloat("Air Accel",        &player.air_accel,      1.0f, 200.0f);
                ImGui::SliderFloat("Friction",         &player.friction,       0.0f, 20.0f);
                ImGui::SliderFloat("Jump Speed",       &player.jump_speed,     1.0f, 20.0f);

                ImGui::Spacing();
                if (ImGui::Button("Reset to Source defaults")) {
                    player.gravity        = 20.0f;
                    player.max_speed      = 8.0f;
                    player.air_wish_speed = 0.76f;
                    player.ground_accel   = 10.0f;
                    player.air_accel      = 70.0f;
                    player.friction       = 6.0f;
                    player.jump_speed     = 7.2f;
                }
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

            ImGui::End();

            // If user closed via X button
            if (!show_settings)
                SDL_SetWindowRelativeMouseMode(window, true);
        }

        ImGui::Render();

        // --- Build scene data ---
        float aspect = 16.0f / 9.0f;
        {
            int w, h;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            if (h > 0) aspect = static_cast<float>(w) / static_cast<float>(h);
        }

        SceneData scene{};
        scene.view       = camera.view_matrix();
        scene.projection = camera.projection_matrix(aspect);
        scene.light_dir  = HMM_V4(0.4f, 0.8f, 0.3f, 0.0f);
        scene.camera_pos = HMM_V4(camera.position.X, camera.position.Y, camera.position.Z, 0.0f);

        renderer.draw_frame(scene);
    }

    // Save config on exit
    config.pull(camera, player);
    config.save();

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
