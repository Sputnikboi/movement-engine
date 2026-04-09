#pragma once

#include "mesh.h"

struct GameState;
struct LevelData; // defined in level_loader.h

// Spawn enemies from a generated level's enemy_spawns list, applying difficulty scaling.
void spawn_enemies_from_level(GameState& gs, const LevelData& pld);

// Called when player interacts with exit door in a combat room.
// Generates the shop room, teleports the player in.
void shop_enter(GameState& gs);
void start_next_room(GameState& gs); // skip shop, go straight to next combat room

// Called every frame while in_shop_room == true.
// Handles stand proximity, buying, and exiting to next combat room.
// Returns true if the player exited the shop this frame.
bool shop_tick(GameState& gs, float dt, bool interact_pressed);

// Append spinning display models on shop stands to the entity mesh.
// Call each frame during entity mesh building (before draw).
void shop_build_display_meshes(GameState& gs, Mesh& out, float time);

// Draw shop room HUD (gold counter, stand prompts, exit prompt).
// Call during ImGui frame, after NewFrame().
void shop_draw_hud(GameState& gs);
