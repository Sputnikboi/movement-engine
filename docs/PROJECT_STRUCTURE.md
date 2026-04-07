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
├── assets/
│   ├── Door.glb                # Door model (entry/exit)
│   ├── Kunai.glb               # Throwing knife viewmodel
│   ├── Ladder.glb              # Ladder mesh
│   └── wingman.glb             # Wingman pistol viewmodel
├── levels/
│   ├── README.md               # Blender → engine workflow guide
│   └── generate_test_arena.py  # Python script to generate test .glb
├── src/
│   ├── main.cpp                # Entry point, game loop, input, rendering dispatch
│   ├── game_state.h            # Shared GameState struct (refs into main locals)
│   │
│   ├── renderer.h/cpp          # Vulkan renderer (pipelines, mesh upload, draw)
│   ├── camera.h                # FPS camera (mouselook, projection)
│   ├── mesh.h/cpp              # Vertex3D, Mesh, GLB loading, built-in test level
│   ├── level_loader.h/cpp      # glTF/GLB file loading (cgltf), spawn extraction
│   │
│   ├── player.h/cpp            # Player movement physics (Quake/Source model)
│   ├── collision.h/cpp         # Triangle soup collision (swept sphere, slide move)
│   ├── bvh.h/cpp               # Bounding volume hierarchy for collision accel
│   ├── geo_types.h             # Shared types (Triangle, HitResult, AABB)
│   │
│   ├── weapon.h/cpp            # Weapon system (Glock, Wingman, Knife), viewmodel anim
│   ├── entity.h                # Entity struct (tagged union, flat array)
│   ├── entity_render.h/cpp     # Builds dynamic mesh from alive entities each frame
│   ├── effects.h/cpp           # Particle system (muzzle flash, blood, explosions)
│   │
│   ├── drone.h/cpp             # Drone AI (chase/circle/attack/die)
│   ├── rusher.h/cpp            # Rusher AI (ground melee, dash attack)
│   ├── turret.h/cpp            # Turret AI (stationary, beam weapon)
│   ├── tank.h/cpp              # Tank AI (heavy, slow, stomp attack)
│   ├── bomber.h/cpp            # Bomber AI (flying, dive bomb, explosion)
│   ├── shielder.h/cpp          # Shielder AI (shields nearby allies)
│   │
│   ├── procgen.h/cpp           # Procedural room generation + shop room
│   ├── shop.h/cpp              # Physical shop room (enter, buy, exit)
│   ├── hud.h/cpp               # Gameplay HUD (HP, ammo, crosshair, vignette)
│   ├── debug_menu.h/cpp        # Settings/debug window (tuning, level browser)
│   │
│   ├── keybinds.h              # Rebindable input actions (2 slots each)
│   ├── config.h/cpp            # Settings save/load (settings.ini)
│   │
│   ├── shaders/
│   │   ├── mesh.vert/frag      # 3D lit geometry (half-lambert + blinn-phong + fog)
│   │   ├── particle.vert/frag  # Additive billboard particles
│   │   └── triangle.vert/frag  # Original Phase 1 test shader (unused)
│   └── vendor/
│       ├── HandmadeMath.h      # Math library (vectors, matrices, quaternions)
│       ├── cgltf.h             # Single-header glTF parser
│       └── imgui/              # Dear ImGui (Vulkan + SDL3 backends)
└── build/                      # Build output (gitignored)
```

## Architecture Overview

### Game Loop (main.cpp, ~1400 lines)
```
SDL_Init → Create Window → Load Assets → Build Level → Init Renderer → Init ImGui
    │
    ▼
Main Loop:
    1. Poll SDL events (input, window resize, keybind rebinding)
    2. Compute delta time
    3. Mouse look (camera.mouse_look)
    4. Movement:
       - Noclip: free-fly camera
       - Player: fixed timestep (128Hz) physics ticks
    5. Weapon update (fire, reload, swap, ADS, viewmodel animation)
    6. Hitscan (left click → ray vs entities → damage + effects)
    7. Update entities (6 enemy types, projectiles, AI)
    8. Door proximity check + shop/room transitions
    9. Update particle effects
    10. Build entity mesh + particle vertex data
    11. ImGui frame (HUD, shop HUD, debug menu)
    12. Renderer.draw_frame (opaques → entities → viewmodel → particles → ImGui)
```

### Subsystem Split

Game state is shared via `GameState` (game_state.h), a struct of references
into main()'s locals. Subsystems receive it by reference:

```
main.cpp  ──constructs──>  GameState  <──used by──  shop.cpp
                                      <──used by──  hud.cpp
                                      <──used by──  debug_menu.cpp
```

| Module | Role |
|--------|------|
| `shop.cpp` | Physical shop room: enter transition, stand buying, exit to next combat room, shop HUD |
| `hud.cpp` | Gameplay HUD: HP bar, speed, ammo, crosshair, damage vignette, door prompts |
| `debug_menu.cpp` | Settings window: level browser, weapon/enemy/movement tuning, keybinds, video |

### Renderer (renderer.h/cpp)

Three Vulkan graphics pipelines in a single render pass:

**Opaque pipeline (mesh.vert/frag):**
- Back-face culled, depth test + write
- UBO: view, projection, light direction, camera position
- Push constant: model matrix (mat4)
- Draws: level geometry, entity meshes

**Viewmodel pipeline (mesh.vert/frag, separate depth):**
- Depth-only pre-pass + lit pass with own near/far
- Prevents weapon clipping through walls
- Push constant: model matrix for weapon positioning

**Particle pipeline (particle.vert/frag):**
- No culling (billboards), depth test ON, depth write OFF
- Additive blending (src + dst)
- Push constant: time (float)
- Draws: muzzle flash, blood, explosions, hit sparks

**Buffer strategy:**
- Level mesh: static vertex/index buffers, replaced on level load
- Entity mesh: dynamic, auto-growing, rebuilt from scratch each frame
- Particle mesh: dynamic, auto-growing, rebuilt each frame

### Weapons (weapon.h/cpp)

Three weapons with full FPS mechanics:

| Weapon | Type | Key features |
|--------|------|-------------|
| Glock | Full-auto pistol | Fast fire rate, low damage, recoil recovery |
| Wingman | Revolver | Slow fire, high damage, ADS accuracy |
| Throwing Knife | Melee/ranged | Crit multiplier, fast swap |

**Shared systems:** ADS (aim down sights) with FOV zoom + sensitivity scaling,
3-phase reload animation (mag out → mag swap → gun up), procedural recoil
(kick + pitch + roll + recovery), weapon swap with raise/lower animation.

**Upgrades:** per-weapon level tracked across rooms. Glock: +dmg/+fire rate,
Wingman: multiplicative damage, Knife: +dmg/+crit.

### Procedural Generation (procgen.h/cpp)

Generates combat rooms with configurable parameters:

- Room dimensions (width, depth, height)
- Cover boxes (regular + tall pillars + stacked clusters)
- Terrain hills (noise-based heightmap patches)
- Entry/exit doors with lock-until-cleared mechanic
- Enemy spawns (budget-based with per-type weights + difficulty scaling)
- Difficulty scaling: HP, damage, speed multipliers per room

**Shop room generation:** Separate `generate_shop_room()` builds a small room
with hexagonal pedestal stands for weapon, healthpack, and 2 future upgrade
slots. Player physically walks around and interacts with stands.

### Physics (player.h/cpp)

**Fixed timestep at 128Hz** — accumulator pattern in main.cpp feeds
physics ticks independent of framerate.

**Quake/Source movement model:**
- `accelerate()`: the core function. Accelerates up to wish_speed in
  wish_direction only. Air strafing and bhop emerge from this math.
- Ground: friction → wish dir (slope-projected) → accelerate → collide
- Air: lurch → wish dir → air accelerate (capped wish speed) → gravity → collide
- Jump: edge-triggered (hold-to-hop with auto_hop), starts lurch window
- Bhop: no friction on landing tick if jumping immediately
- Crouch: lower height/eye, slower speed
- Slide: crouch while running fast → low friction, momentum-based, power slide boost
- Lurch: on input direction change during post-jump window, lerp velocity toward
  input direction by lurch_strength, preserving speed
- Wall jump: air-jump off walls
- Ladder climbing: volume-based detection, directional movement

### Collision (collision.h/cpp, bvh.h/cpp)

**Triangle soup** — level mesh converted to Triangle array on load.

**BVH** — median-split binary tree, max 4 triangles per leaf, stack-based
traversal. Accelerates raycast and sphere overlap queries.

**Queries:**
- `raycast()`: Möller-Trumbore ray-triangle, BVH-accelerated
- `sphere_overlap()`: closest-point-on-triangle, returns deepest penetration
- `slide_move()`: Quake PM_SlideMove — move sphere, on overlap push out and
  clip velocity against contact plane, up to 4 iterations for corners

### Entity System (entity.h, drone/rusher/turret/tank/bomber/shielder .h/cpp)

Flat array of 256 Entity structs. Tagged by EntityType. No ECS, no
inheritance — just switch on type.

| Enemy | Behavior | Key mechanic |
|-------|----------|-------------|
| Drone | Flying, strafes around player | Projectile attacks, hover with noise bob |
| Rusher | Ground melee, charges at player | Dash attack with cooldown |
| Turret | Stationary, rotates to track | Beam weapon (continuous DPS) |
| Tank | Heavy, slow pursuit | Stomp AoE attack |
| Bomber | Flying, dive-bombs | Explosion AoE on impact |
| Shielder | Support, stays near allies | Applies damage-absorbing shield to nearby enemies |

**Entity rendering** (entity_render.h/cpp): Builds dynamic mesh each frame
from all alive entities. Each type has a distinct visual (geometric shapes,
colors, animation states).

### Effects (effects.h/cpp)

Pool of 1024 particles. Each has position, velocity, color, size lerp,
lifetime, type. Updated every frame.

Effect types: muzzle flash, blood spray, hit sparks, explosions (fireball +
rings + debris + smoke), death effects.

### Shop System (shop.h/cpp)

**Physical shop room** between combat rooms. Player walks through a small room
with item stands:

```
Combat room → kill all enemies → exit door unlocks
→ Enter shop room (physical) → walk to stands, interact to buy
→ Exit shop door → next combat room (harder)
```

**Stands:**
- Weapon stand: buy new weapon or upgrade existing (gold-topped pedestal)
- Healthpack stand: restore 25% HP (green-topped pedestal)
- 2 empty stands: reserved for future upgrades

**Currency:** earned from killing enemies (1-5 gold per type).

### Game Flow

```
Start → Procedural room 1 → clear enemies → shop → room 2 (harder) → shop → ...
                                                          ↓
                                                    Death → restart
```

Difficulty scales per room: enemy HP, damage, speed multipliers increase.
Enemy budget (count) also increases. Weapon upgrades and currency persist
until death.

### Config & Keybinds

**Keybinds:** each action has 2 input slots. Supports keyboard scancodes,
mouse buttons, and scroll wheel. Rebindable in settings UI with click-to-bind.

**Config:** INI-style settings.ini. Saves/loads mouse, video, movement params,
keybinds. Auto-saves on exit, auto-loads on start.

### Level Loading (level_loader.h/cpp)

Loads .glb/.gltf via cgltf. Extracts all mesh primitives with world transforms.
Reads vertex colors or material base color as flat vertex color. Searches for
node named "spawn" for player spawn position. Extracts ladder volumes from
nodes with "ladder" in name. In-game level browser in debug menu.

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
