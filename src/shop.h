#pragma once

#include "mesh.h"

struct GameState;

// Called when player interacts with exit door in a combat room.
// Generates the shop room, teleports the player in.
void shop_enter(GameState& gs);

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
