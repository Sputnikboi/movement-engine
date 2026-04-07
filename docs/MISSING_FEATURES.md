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
| Slope movement | ✅ | ✅ | Multi-ray ground check, slope sticking |
| Ground contact system | ✅ | ✅ | Multi-ray (5-point) ground probe + iterative depenetration |
| Step climbing | ✅ | ✅ | Source-style up→forward→down step move |
| Wall jump | ✅ | ✅ | Air-jump off walls |
| Ladder climbing | ✅ | ✅ | Volume-based detection, directional movement |
| Coyote time | ✅ | ❌ | Low priority — grace period for jumping after leaving edge |
| View bobbing | ✅ | ❌ | Low priority |
| Camera smoothing | ✅ | ❌ | Low priority |
| Crouch camera lerp | ✅ | ❌ | Low priority — eye height snaps currently |

## Camera

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| FPS mouselook | ✅ | ✅ | |
| FOV setting | ✅ | ✅ | Slider in settings |
| Sensitivity + inversion | ✅ | ✅ | Per-axis inversion, saved in config |
| ADS FOV scaling | ✅ | ✅ | Per-weapon FOV multiplier + sens scaling |
| Screen shake | ✅ | ❌ | Low priority |

## Weapons

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Hitscan shooting | ✅ | ✅ | Per-weapon damage, range, fire rate |
| Weapon viewmodel | ✅ | ✅ | GLB models, depth-separated rendering |
| ADS (aim down sights) | ✅ | ✅ | FOV zoom + position offset + sens scaling |
| Reload animation | ✅ | ✅ | 3-phase procedural (mag out/swap/gun up) |
| Magazine / ammo system | ✅ | ✅ | Per-weapon mag size, reload mechanic |
| Recoil (procedural) | ✅ | ✅ | Kick + pitch + roll + side + recovery |
| Weapon switching | ✅ | ✅ | Swap animation with raise/lower |
| Multiple weapons | ✅ | ✅ | 3 weapons (Glock, Wingman, Knife) |
| Weapon upgrades | ✅ | ✅ | Per-weapon level, bought in shop |
| Muzzle flash | ✅ | ✅ | Particle effect on fire |
| Crosshair | ✅ | ✅ | White cross via ImGui foreground draw |
| Tracers | ✅ | ❌ | Pooled tracer projectiles |
| Damage numbers | ✅ | ❌ | Billboard floating text with crits |
| Critical hit zones | ✅ | ❌ | Per-collider crit multiplier |

## Enemies

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Drone (chase/circle/attack) | ✅ | ✅ | Projectile attacks, hover/bob, death ragdoll |
| Rusher (melee charge) | ✅ | ✅ | Ground pursuit, dash attack with cooldown |
| Turret (stationary beam) | N/A | ✅ | Engine-only: tracks player, beam DPS |
| Tank (heavy melee) | N/A | ✅ | Engine-only: slow pursuit, stomp AoE |
| Bomber (dive bomb) | N/A | ✅ | Engine-only: flying, explosion AoE |
| Shielder (support) | N/A | ✅ | Engine-only: shields nearby allies |
| Enemy death effects | ✅ | ✅ | Particles, ragdoll fall, explosions |
| Enemy health bars | ✅ | ❌ | Billboard UI above enemies |
| Boss enemy | ✅ | ❌ | Low priority — Unity version was placeholder |

## Procedural Generation & Game Loop

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Procedural rooms | ❌ | ✅ | Engine-only: boxes, pillars, hills, doors |
| Room progression | ❌ | ✅ | Engine-only: clear enemies → shop → next room |
| Difficulty scaling | ❌ | ✅ | Engine-only: HP/dmg/speed/count per room |
| Physical shop room | ❌ | ✅ | Engine-only: walk to stands, interact to buy |
| Shop currency | ❌ | ✅ | Engine-only: gold from kills, spend in shop |
| Door lock mechanic | ❌ | ✅ | Engine-only: exit locked until room cleared |
| Death / restart | ❌ | ✅ | Resets run on death |

## Player Health & Feedback

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Player health system | ✅ | ✅ | HP with max, damage accumulator |
| Health HUD | ✅ | ✅ | Color-coded progress bar |
| Damage vignette | ✅ | ✅ | Red screen edges on damage |
| Healthpack (shop) | ❌ | ✅ | Engine-only: buy +25% HP in shop |
| Directional damage indicator | ✅ | ❌ | Shows which direction damage came from |

## Audio — HIGH PRIORITY

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Sound manager | ✅ | ❌ | SDL3 audio API available |
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
| Viewmodel depth separation | ✅ | ✅ | Own near/far to prevent wall clipping |
| Textures | ✅ | ❌ | Art style TBD |
| Normal maps | ✅ | ❌ | Art style TBD |
| Shadows | ✅ | ❌ | Art style TBD |
| Post-processing | ✅ | ❌ | Art style TBD |
| Anti-aliasing | ✅ | ❌ | Art style TBD |

## UI

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Settings menu | ✅ | ✅ | ImGui debug menu — placeholder, will be replaced |
| Key rebinding | ✅ | ✅ | 2 slots per action, mouse/wheel support |
| HUD (HP, ammo, speed, state) | ✅ | ✅ | ImGui overlay — placeholder, will be replaced |
| Pause menu | ✅ | ✅ | Escape toggles settings |
| Level browser | ❌ | ✅ | Engine-only: debug menu level selector |
| Shop UI (physical room) | ❌ | ✅ | Engine-only: walk-up interaction prompts |
| Weapon mod UI | ✅ | ❌ | Low priority — mod system TBD |

## Level / World

| Feature | Unity | Engine | Notes |
|---------|-------|--------|-------|
| Level loading (glTF) | N/A | ✅ | Blender → GLB → engine |
| Spawn points | ✅ | ✅ | Empty named "spawn" in Blender |
| Built-in test level | ✅ | ✅ | Hardcoded geometry |
| Hot-reload levels | ❌ | ✅ | Engine-only: reload without restart |
| Ladder volumes | ✅ | ✅ | Auto-extracted from "ladder" nodes in glTF |
| Trigger volumes | ✅ | ❌ | Trivial to add when needed |

---

## Current Focus

### 🔴 Now — Audio + Polish
1. **Audio system** — SDL3 audio, footsteps, gunshots, explosions
2. **Enemy health bars** — billboard UI above enemies
3. **Damage numbers** — floating text on hit
4. **Directional damage indicator** — show damage source direction

### 🟡 Soon — Content + Feel
5. **More shop upgrades** — passive upgrades (max HP, move speed, slide boost)
6. **Tracers** — visual bullet trails
7. **Critical hit zones** — per-collider crit multiplier
8. **Coyote time** — jump grace period after leaving edge

### 🟢 Later — UI Rewrite + Visual
9. **Complete UI rewrite** — replace ImGui debug menu with proper game UI
10. **Camera polish** (bobbing, smoothing, crouch lerp, screen shake)
11. **Boss enemy**
12. **Rendering upgrades** (textures, shadows, post-processing — pending art style)
13. **Mod system** (TBD)
