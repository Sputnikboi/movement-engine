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
| Ground contact system | ✅ | ✅ | Multi-ray (5-point) ground probe + iterative depenetration |
| Step climbing | ✅ | ✅ | Source-style up→forward→down step move |
| Landing grace | ✅ | ✅ | Single-tick grace from new engine — feels good enough |
| Coyote time | ✅ | ❌ | Low priority — grace period for jumping after leaving edge |

## Camera

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| FPS mouselook | ✅ | ✅ | |
| FOV setting | ✅ | ✅ | Slider in settings |
| Sensitivity + inversion | ✅ | ✅ | Per-axis inversion, saved in config |
| ADS FOV scaling | ✅ | ❌ | **Soon** — FOV zoom for aim down sights |
| View bobbing | ✅ | ❌ | Low priority |
| Camera smoothing | ✅ | ❌ | Low priority |
| Crouch camera lerp | ✅ | ❌ | Low priority |
| Screen shake | ✅ | ❌ | Low priority |

## Weapons — HIGH PRIORITY

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Hitscan shooting | ✅ | 🟡 | Engine: left click, 10 damage, no feedback |
| Weapon viewmodel | ✅ | ❌ | **Next** — first-person gun model rendering |
| ADS (aim down sights) | ✅ | ❌ | **Next** — FOV zoom + position offset |
| Reload animation | ✅ | ❌ | **Next** — magazine drop, procedural animation |
| Magazine / ammo system | ✅ | ❌ | **Next** — ammo count, reload mechanic |
| Recoil (procedural) | ✅ | ❌ | **Next** — visual + spread |
| Weapon switching | ✅ | ❌ | After core weapon loop works |
| Muzzle flash | ✅ | ❌ | Particle + light flash |
| Tracers | ✅ | ❌ | Pooled tracer projectiles |
| Damage numbers | ✅ | ❌ | Billboard floating text with crits |
| Critical hit zones | ✅ | ❌ | Per-collider crit multiplier |
| Crosshair | ✅ | ✅ | Simple white cross via ImGui |

## Enemies — HIGH PRIORITY

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Drone (chase/circle/attack) | ✅ | ✅ | AI states match Unity version — **needs work** |
| Drone hover + bob | ✅ | ✅ | Raycast-based hover, noise bob |
| Drone projectiles | ✅ | ✅ | Fired at player, wall collision |
| Drone death animation | ✅ | 🟡 | **Needs work** — falls then explodes, needs polish |
| Drone collision | ✅ | 🟡 | **Needs work** — collision issues |
| Drone death explosion | ✅ | 🟡 | Engine has particles, Unity had fancier effects |
| Rusher Drone | ✅ | ❌ | **Soon** — melee-charge enemy with dash + stun |
| Enemy health bars | ✅ | ❌ | Billboard UI above enemies |
| EnemyManager (waves) | ✅ | ❌ | Spawn waves, track alive enemies |
| Object pooling | ✅ | ❌ | Engine uses direct allocation |
| Boss enemy | ✅ | ❌ | Low priority — Unity version was placeholder |

## Player Health & Feedback

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Player health | ❌ | ❌ | Projectile collision detected but no health system |
| Health HUD | ✅ | ❌ | |
| Damage taken feedback | ✅ | ❌ | Screen flash, directional indicator |
| Death / respawn | ❌ | 🟡 | Engine respawns on void-out only |

## Audio — HIGH PRIORITY

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Sound manager | ✅ | ❌ | SDL3 audio API |
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
| Transparent / additive rendering | ✅ | ✅ | Particle pipeline |
| Distance fog | ✅ | ✅ | In mesh.frag |
| Textures | ✅ | ❌ | Art style TBD |
| Normal maps | ✅ | ❌ | Art style TBD |
| Shadows | ✅ | ❌ | Art style TBD |
| Post-processing | ✅ | ❌ | Art style TBD |
| Anti-aliasing | ✅ | ❌ | Art style TBD |

## UI

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Settings menu | ✅ | ✅ | ImGui-based, all params tunable |
| Key rebinding | ✅ | ✅ | 2 slots per action, mouse/wheel support |
| HUD (FPS, speed, state) | ✅ | ✅ | |
| Pause menu | ✅ | ✅ | Escape toggles |
| Level browser | ❌ | ✅ | Engine-only: in-game level selector |
| Weapon mod UI | ✅ | ❌ | Low priority — mod system TBD |

## Level / World

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Level loading (glTF) | N/A | ✅ | Blender → GLB → engine |
| Spawn points | ✅ | ✅ | Empty named "spawn" in Blender |
| Built-in test level | ✅ | ✅ | Hardcoded geometry |
| Hot-reload levels | ❌ | ✅ | Engine-only: reload without restart |
| Trigger volumes | ✅ | ❌ | Trivial to add when needed |
| Scene transitions | ✅ | ❌ | |

---

## Current Focus

### 🔴 Now — Weapons + Enemies
1. **Weapon viewmodel** — load glTF gun model, render in first person
2. **Shooting / reloading / ADS** — match Unity version's feel
3. **Fix drone AI** — collision and behavior issues
4. **Fix drone death animation** — polish the ragdoll/explosion
5. **Rusher drone** — second enemy type

### 🟡 Soon — Audio + Polish
6. **Audio system** — SDL3 audio, footsteps, gunshots, explosions
7. **ADS FOV scaling** — camera zoom when aiming
8. **Enemy health bars** — billboard UI
9. **Player health + damage feedback**

### 🟢 Later
10. **Coyote time**
11. **Camera polish** (bobbing, smoothing, crouch lerp)
12. **Wave spawner**
13. **Weapon switching / multiple weapons**
14. **Rendering upgrades** (pending art style decision)
15. **Mod system** (TBD)

## Shop System (Planned)
- Opens between rooms at exit door (before entering next room, or dedicated shop phase)
- Currency earned from killing enemies (per type: drone=1, rusher=1, turret=3, tank=5, bomber=3, shielder=4)
- **Buy weapons** — unlock new weapons to add to loadout
- **Weapon upgrades** — damage, mag size, fire rate, reload speed (per weapon, stacking)
- **Passive upgrades** — max HP, movement speed, slide boost, jump height
- UI: ImGui window with categories, costs, descriptions
- Upgrades persist across rooms until run ends (death resets)
