#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>
#include <cmath>

#include "renderer.h"
#include "camera.h"
#include "mesh.h"
#include "collision.h"
#include "player.h"

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

    // Hide cursor and warp to center — works reliably with remote desktop
    // (Moonlight, Parsec, etc.) unlike SDL relative mouse mode
    SDL_HideCursor();
    {
        int w, h;
        SDL_GetWindowSize(window, &w, &h);
        SDL_WarpMouseInWindow(window, w / 2.0f, h / 2.0f);
    }

    // --- Build level ---
    Mesh level = create_test_level();

    // --- Build collision world ---
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

    // --- Player + camera ---
    Camera camera;
    Player player;
    player.position = HMM_V3(0.0f, 1.0f, 15.0f);

    bool noclip = false;
    float fly_speed = 15.0f;

    // --- Fixed timestep ---
    constexpr float TICK_RATE = 1.0f / 128.0f;
    float accumulator = 0.0f;

    // --- Input state ---
    InputState input{};
    bool jump_pressed = false;

    // --- Mouse warp state ---
    bool first_frame = true;  // skip first delta (junk from initial warp)

    bool running = true;
    Uint64 last_time = SDL_GetPerformanceCounter();

    while (running) {
        // --- Compute mouse delta from center of window ---
        int ww, wh;
        SDL_GetWindowSize(window, &ww, &wh);
        float center_x = ww / 2.0f;
        float center_y = wh / 2.0f;

        float mx, my;
        SDL_GetMouseState(&mx, &my);

        float mouse_dx = mx - center_x;
        float mouse_dy = my - center_y;

        // Warp back to center immediately
        SDL_WarpMouseInWindow(window, center_x, center_y);

        // Discard first frame's delta (garbage from initial positioning)
        if (first_frame) {
            mouse_dx = 0.0f;
            mouse_dy = 0.0f;
            first_frame = false;
        }

        // Drain events (warp generates motion events — we ignore them
        // since we already computed delta from absolute position above)
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) running = false;
                if (event.key.key == SDLK_F1)     camera.flip_x();
                if (event.key.key == SDLK_F2)     camera.flip_y();
                if (event.key.key == SDLK_F3)     camera.adjust_sensitivity(0.5f);
                if (event.key.key == SDLK_F4)     camera.adjust_sensitivity(2.0f);
                if (event.key.key == SDLK_V) {
                    noclip = !noclip;
                    printf("Noclip: %s\n", noclip ? "ON" : "OFF");
                    if (noclip)
                        camera.position = player.eye_position();
                }
                if (event.key.key == SDLK_SPACE)  jump_pressed = true;
                break;
            case SDL_EVENT_KEY_UP:
                if (event.key.key == SDLK_SPACE)  jump_pressed = false;
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                renderer.on_resize();
                break;
            // Mouse motion events intentionally not processed —
            // we use absolute position delta above instead
            }
        }

        // --- Timing ---
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - last_time) / static_cast<float>(SDL_GetPerformanceFrequency());
        last_time = now;
        if (dt > 0.1f) dt = 0.1f;

        // --- Mouse look ---
        camera.mouse_look(mouse_dx, mouse_dy);

        // --- Movement ---
        if (noclip) {
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
        } else {
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

    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
