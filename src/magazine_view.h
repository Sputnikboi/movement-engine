#pragma once

struct GameState;

// Draw the Balatro-style magazine card view.
// Shows all rounds in the active weapon's magazine as cards in a fan arc.
// In shop: clicking a card selects it for individual modding.
// Call during ImGui frame.
struct ImFont;
void magazine_view_draw(GameState& gs, ImFont* font = nullptr);
