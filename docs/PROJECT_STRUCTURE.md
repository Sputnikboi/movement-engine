# Movement Engine — Project Structure

Custom game engine built from scratch with C++20, SDL3, and Vulkan.

## Directory Layout

```
movement-engine/
├── CMakeLists.txt              # Build system (CMake 3.24+)
├── .gitignore
├── docs/
│   ├── PROJECT_STRUCTURE.md    # This file
│   └── MISSING_FEATURES.md    # Comparison with Unity version
├── levels/
│   ├── README.md               # Blender → engine workflow guide
│   └── generate_test_arena.py  # Python script to generate test .glb
├── src/
│   ├── main.cpp                # Entry point, game loop, input, UI
│   ├── renderer.h/cpp          # Vulkan renderer
│   ├── camera.h                # FPS camera (mouselook, projection)
│   ├── player.h/cpp            # Player movement physics
│   ├── collision.h/cpp         # Triangle soup collision system
│   ├── bvh.h/cpp               # Bounding volume hierarchy acceleration
│   ├── geo_types.h             # Shared types (Triangle, HitResult)
│   ├── mesh.h/cpp              # Vertex3D, Mesh, built-in test level
│   ├── level_loader.h/cpp      # glTF/GLB file loading (cgltf)
│   ├── entity.h                # Entity struct (tagged union, flat array)
│   ├── drone.h/cpp             # Drone AI (chase/circle/attack/die)
│   ├── entity_render.h/cpp     # Builds mesh from alive entities each frame
│   ├── effects.h/cpp           # Particle effect system + drone explosion
│   ├── keybinds.h              # Rebindable input actions (2 slots each)
│   ├── config.h/cpp            # Settings save/load (settings.ini)
│   ├── shaders/
│   │   ├── mesh.vert/frag      # 3D lit geometry (half-lambert + blinn-phong + fog)
│   │   ├── particle.vert/frag  # Additive billboard particles (ring + blob effects)
│   │   └── triangle.vert/frag  # Original Phase 1 test shader (unused)
│   └── vendor/
│       ├── HandmadeMath.h      # Math library (vectors, matrices, quaternions)
│       ├── cgltf.h             # Single-header glTF parser
│       └── imgui/              # Dear ImGui (Vulkan + SDL3 backends)
└── build/                      # Build output (gitignored)
```

## Architecture Overview

### Game Loop (main.cpp)
```
SDL_Init → Create Window → Build Level → Init Renderer → Init ImGui
    │
    ▼
Main Loop:
    1. Poll SDL events (input, window resize)
    2. Compute delta time
    3. Mouse look (camera.mouse_look)
    4. Movement:
       - Noclip: free-fly camera
       - Player: fixed timestep (128Hz) physics ticks
    5. Hitscan (left click → ray-sphere vs drones → ray-world occlusion)
    6. Update entities (drone AI, projectiles)
    7. Detect dying drone → ground hit → spawn explosion
    8. Update particle effects
    9. Build entity mesh + particle vertex data
    10. ImGui frame (HUD overlay + settings window)
    11. Renderer.draw_frame (opaques → entities → particles → ImGui)
```

### Renderer (renderer.h/cpp)

Two Vulkan graphics pipelines in a single render pass:

**Opaque pipeline (mesh.vert/frag):**
- Back-face culled, depth test + write
- UBO: view, projection, light direction, camera position
- Push constant: model matrix (mat4)
- Draws: level geometry, entity spheres

**Particle pipeline (particle.vert/frag):**
- No culling (billboards), depth test ON, depth write OFF
- Additive blending (src + dst)
- Push constant: time (float)
- Draws: explosion particles, rings, debris

Both share the same descriptor set layout (scene UBO at binding 0).

**Buffer strategy:**
- Level mesh: static vertex/index buffers, replaced on level load
- Entity mesh: dynamic, auto-growing, rebuilt from scratch each frame
- Particle mesh: dynamic, auto-growing, rebuilt each frame

### Physics (player.h/cpp)

**Fixed timestep at 128Hz** — accumulator pattern in main.cpp feeds
physics ticks independent of framerate.

**Quake/Source movement model:**
- `accelerate()`: the core function. Accelerates up to wish_speed in
  wish_direction only. Air strafing and bhop emerge from this math.
- Ground: friction → wish dir (slope-projected if on ramp) → accelerate → collide
- Air: lurch → wish dir → air accelerate (capped wish speed) → gravity → collide
- Jump: edge-triggered (no hold-to-hop unless auto_hop), starts lurch window
- Bhop: no friction on landing tick if jumping immediately
- Crouch: lower height/eye, slower speed
- Slide: crouch while running fast → low friction, momentum-based, power slide boost
- Lurch: on input direction change during post-jump window, lerp velocity toward
  input direction by lurch_strength, preserving speed

**Ground detection:** downward raycast from feet + 0.1, checks slope angle.
Refuses to re-ground when velocity.Y > 0.1 (preserves jumps).

### Collision (collision.h/cpp, bvh.h/cpp)

**Triangle soup** — level mesh converted to Triangle array on load.

**BVH** — median-split binary tree, max 4 triangles per leaf, stack-based
traversal. Accelerates raycast and sphere overlap queries.

**Queries:**
- `raycast()`: Möller-Trumbore ray-triangle, BVH-accelerated
- `sphere_overlap()`: closest-point-on-triangle, returns deepest penetration
- `slide_move()`: Quake PM_SlideMove — move sphere, on overlap push out and
  clip velocity against contact plane, up to 4 iterations for corners

### Entity System (entity.h, drone.h/cpp)

Flat array of 256 Entity structs. Tagged by EntityType (None/Drone/Projectile).
No ECS, no inheritance — just switch on type.

**Drone AI states:** Chase → Circle → Attack → (on kill) Dying → Dead

**Dying state:** ragdoll fall with gravity, spin, knockback from shot.
Explodes on ground impact or 3s timeout.

### Effects (effects.h/cpp)

Pool of 1024 particles. Each has position, velocity, color, size lerp,
lifetime, type (ring vs blob). Updated every frame (not tied to physics tick).

**Drone explosion:** 3D fireball (20 sphere-distributed particles) + core flash
+ 3 staggered expanding rings + upward fire column + debris embers + smoke.

### Config & Keybinds

**Keybinds:** each action has 2 input slots. Supports keyboard scancodes,
mouse buttons, and scroll wheel. Rebindable in settings UI with click-to-bind.

**Config:** INI-style settings.ini. Saves/loads mouse, video, movement params,
keybinds. Auto-saves on exit, auto-loads on start.

### Level Loading (level_loader.h/cpp)

Loads .glb/.gltf via cgltf. Extracts all mesh primitives with world transforms.
Reads vertex colors or material base color as flat vertex color. Searches for
node named "spawn" for player spawn position. In-game level browser in settings.

## Build

```bash
# Linux
sudo apt install vulkan-tools libvulkan-dev glslc vulkan-validationlayers
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
./build/movement_engine [level.glb]

# Windows (needs Vulkan SDK from lunarg.com + CMake)
cmake -B build
cmake --build build --config Debug
build\Debug\movement_engine.exe [level.glb]
```

SDL3 is fetched automatically by CMake. No other dependencies to install.

## Dependencies

| Library | Version | Purpose | Included |
|---------|---------|---------|----------|
| SDL3 | 3.2.8 | Window, input, mouse | FetchContent (auto) |
| Vulkan | 1.3 | Rendering | System SDK |
| HandmadeMath | 2.0.0 | Math (vec/mat/quat) | vendor/ |
| cgltf | 1.14 | glTF loading | vendor/ |
| Dear ImGui | 1.91.8 | UI (settings, HUD) | vendor/ |
| glslc | — | Shader compilation | Vulkan SDK |
