#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cstdio>

#include "renderer.h"
#include "camera.h"
#include "mesh.h"

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

    // Build the test level geometry
    Mesh level = create_test_level();

    Renderer renderer;
    if (!renderer.init(window, level)) {
        fprintf(stderr, "Renderer init failed\n");
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    Camera camera;
    camera.position = HMM_V3(0.0f, 3.0f, 15.0f);

    bool running = true;
    Uint64 last_time = SDL_GetPerformanceCounter();
    float fly_speed = 15.0f;

    while (running) {
        // --- Accumulate mouse motion per frame ---
        float mouse_dx = 0.0f, mouse_dy = 0.0f;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE)   running = false;
                if (event.key.key == SDLK_F1)       camera.flip_x();
                if (event.key.key == SDLK_F2)       camera.flip_y();
                if (event.key.key == SDLK_F3)       camera.adjust_sensitivity(0.5f);
                if (event.key.key == SDLK_F4)       camera.adjust_sensitivity(2.0f);
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                renderer.on_resize();
                break;
            case SDL_EVENT_MOUSE_MOTION:
                mouse_dx += event.motion.xrel;
                mouse_dy += event.motion.yrel;
                break;
            }
        }

        // --- Timing ---
        Uint64 now = SDL_GetPerformanceCounter();
        float dt = static_cast<float>(now - last_time) / static_cast<float>(SDL_GetPerformanceFrequency());
        last_time = now;

        // Clamp dt to prevent huge jumps (alt-tab, debugger, etc.)
        if (dt > 0.1f) dt = 0.1f;

        // --- Mouse look ---
        camera.mouse_look(mouse_dx, mouse_dy);

        // --- Keyboard movement (fly/noclip camera for now) ---
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

        // Normalize to prevent diagonal speed boost, then apply speed
        float len = HMM_LenV3(move_dir);
        if (len > 0.001f) {
            move_dir = HMM_MulV3F(move_dir, 1.0f / len);
            float speed = fly_speed;
            if (keys[SDL_SCANCODE_LCTRL]) speed *= 3.0f;  // sprint
            camera.position = HMM_AddV3(camera.position, HMM_MulV3F(move_dir, speed * dt));
        }

        // --- Build scene data ---
        float aspect = static_cast<float>(1280) / 720.0f;
        // Update aspect from actual window size
        {
            int w, h;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            if (h > 0) aspect = static_cast<float>(w) / static_cast<float>(h);
        }

        SceneData scene{};
        scene.view       = camera.view_matrix();
        scene.projection = camera.projection_matrix(aspect);
        // Light from above-right-forward
        scene.light_dir  = HMM_V4(0.4f, 0.8f, 0.3f, 0.0f);
        scene.camera_pos = HMM_V4(camera.position.X, camera.position.Y, camera.position.Z, 0.0f);

        // --- Render ---
        renderer.draw_frame(scene);
    }

    renderer.shutdown();
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
