# Missing Features — Unity Version vs Custom Engine

Comparison of Movement-Gaming (Unity) against movement-engine (custom).
Organized by priority/impact.

## Legend
- ✅ Implemented (functional parity or better)
- 🟡 Partially implemented (basic version exists)
- ❌ Not implemented

---

## Player Movement

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Ground movement (accel/friction) | ✅ | ✅ | Engine uses Quake model, Unity uses custom |
| Air strafing | ✅ | ✅ | Engine's is cleaner (pure Quake math) |
| Bunny hopping | ✅ | ✅ | No landing-tick friction, edge-triggered jump |
| Crouch | ✅ | ✅ | Height change, headroom check |
| Slide (power slide + boost) | ✅ | ✅ | Speed boost, cooldown, low friction |
| Slide-jump boost | ✅ | ✅ | Extra speed when jumping out of slide |
| Lurch (momentum redirect) | ✅ | ✅ | Fires on input change during post-jump window |
| Slope movement | ✅ | ✅ | Multi-ray ground check, slope sticking, no more ramp-edge bumps |
| Wall running | ❌ | ❌ | Not in Unity version either |
| Coyote time | ✅ | ❌ | Grace period for jumping after leaving edge |
| Landing grace | ✅ | ❌ | Unity had a speed preservation window on land |
| Ground contact system | ✅ | ✅ | Multi-ray (5-point) ground probe + iterative depenetration |
| Step climbing | ✅ | ✅ | Source-style up→forward→down step move |

## Camera

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| FPS mouselook | ✅ | ✅ | |
| FOV setting | ✅ | ✅ | Slider in settings |
| Sensitivity + inversion | ✅ | ✅ | Per-axis inversion, saved in config |
| View bobbing | ✅ | ❌ | Procedural head bob while walking |
| Camera smoothing | ✅ | ❌ | Unity had SmoothDamp on some transitions |
| Crouch camera lerp | ✅ | ❌ | Smooth eye height transition when crouching |
| Screen shake | ✅ | ❌ | On damage, explosions |

## Weapons

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Hitscan shooting | ✅ | 🟡 | Engine: left click, 10 damage, no feedback |
| Weapon switching | ✅ | ❌ | Unity had pistol + wingman + gun handler |
| ADS (aim down sights) | ✅ | ❌ | FOV zoom + position offset |
| Recoil (procedural) | ✅ | ❌ | ProceduralWeaponAnimator: visual + spread |
| Reload animation | ✅ | ❌ | Magazine drop, procedural animation |
| Magazine / ammo system | ✅ | ❌ | Ammo count, reload mechanic |
| Weapon modifications | ✅ | ❌ | ScriptableObject mod system with rarities |
| Muzzle flash | ✅ | ❌ | Particle + light flash |
| Tracers | ✅ | ❌ | Pooled tracer projectiles |
| Damage numbers | ✅ | ❌ | Billboard floating text with crits |
| Critical hit zones | ✅ | ❌ | Per-collider crit multiplier |
| Crosshair | ✅ | ✅ | Simple white cross via ImGui |
| Weapon viewmodel | ✅ | ❌ | First-person gun model rendering |

## Enemies

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Drone (chase/circle/attack) | ✅ | ✅ | AI states match Unity version |
| Drone hover + bob | ✅ | ✅ | Raycast-based hover, noise bob |
| Drone projectiles | ✅ | ✅ | Fired at player, wall collision |
| Drone death ragdoll | ✅ | ✅ | Falls with gravity + knockback, then explodes |
| Drone death explosion | ✅ | 🟡 | Engine has particles, Unity had fancier shader effects |
| Rusher Drone | ✅ | ❌ | Melee-charge enemy with dash + stun |
| Boss enemy | ✅ | ❌ | Phase system, charge attack, bullet patterns |
| Enemy health bars | ✅ | ❌ | Billboard UI above enemies |
| IDamageable interface | ✅ | ❌ | Engine uses direct health field |
| EnemyManager (waves) | ✅ | ❌ | Spawn waves, track alive enemies |
| Object pooling | ✅ | ❌ | Unity had generic ObjectPooler |

## Player Health & Feedback

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Player health | ❌ | ❌ | Projectile collision detected but no health system |
| Health HUD | ✅ | ❌ | |
| Damage taken feedback | ✅ | ❌ | Screen flash, directional indicator |
| Death / respawn | ❌ | 🟡 | Engine respawns on void-out only |

## Audio

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Sound manager | ✅ | ❌ | Unity had SoundManager singleton |
| Footsteps | ✅ | ❌ | |
| Gunshot sounds | ✅ | ❌ | |
| Jump / land sounds | ✅ | ❌ | |
| Enemy sounds | ✅ | ❌ | Drone hover hum, attack cues |
| Explosion sounds | ✅ | ❌ | |
| Music | ❌ | ❌ | Neither version had music |

## Rendering

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| 3D mesh rendering | ✅ | ✅ | Lit, depth-buffered |
| Material colors from glTF | ✅ | ✅ | Base color as vertex color |
| Textures | ✅ | ❌ | Engine has flat colors only |
| Normal maps | ✅ | ❌ | |
| Shadows | ✅ | ❌ | |
| Post-processing | ✅ | ❌ | Unity had bloom, color grading |
| Transparent / additive rendering | ✅ | ✅ | Particle pipeline |
| Distance fog | ✅ | ✅ | In mesh.frag |
| Anti-aliasing | ✅ | ❌ | MSAA or FXAA |

## UI

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Settings menu | ✅ | ✅ | ImGui-based, all params tunable |
| Key rebinding | ✅ | ✅ | 2 slots per action, mouse/wheel support |
| HUD (FPS, speed, state) | ✅ | ✅ | |
| Pause menu | ✅ | ✅ | Escape toggles |
| Level browser | ❌ | ✅ | Engine-only: in-game level selector |
| Mod display UI | ✅ | ❌ | Weapon mod slots |
| Settings validation | ✅ | ❌ | Unity had SettingsValidator |

## Level / World

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Level loading (glTF) | N/A | ✅ | Blender → GLB → engine |
| Spawn points | ✅ | ✅ | Empty named "spawn" in Blender |
| Built-in test level | ✅ | ✅ | Hardcoded geometry |
| Hot-reload levels | ❌ | ✅ | Engine-only: reload without restart |
| Trigger volumes | ✅ | ❌ | Unity Trigger.cs for zone events |
| Scene transitions | ✅ | ❌ | |

---

## Priority Recommendations

### High Impact (would make the game feel like a game)
1. **Audio** — footsteps, gunshots, explosions via SDL3 audio API
2. **Player health + damage feedback** — screen flash, respawn on death
3. **Weapon viewmodel** — first-person gun rendering (load glTF model)
4. **Damage numbers** — billboard text on enemy hits

### Medium Impact (polish)
5. **Coyote time** — small grace period for jump after leaving edge
6. **Crouch camera lerp** — smooth eye height transition
7. **Muzzle flash** — particle + brief light on shoot
8. **Enemy health bars** — billboard above drones
9. **Textures** — load from glTF materials instead of flat colors

### Lower Priority (content expansion)
10. **Rusher Drone** — second enemy type
11. **Weapon system** — multiple guns, ammo, reload
12. **Boss enemy** — phase system
13. **Shadows** — shadow mapping
14. **Wave spawner** — enemy wave management
