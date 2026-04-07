#pragma once

#include <string>
#include <functional>

struct GameState;

// Callback type for loading a .glb level by path.
// Returns true on success.
using LoadLevelFn = std::function<bool(const std::string& path)>;

// Draw the debug/settings window (level browser, weapon tuning, enemy tuning,
// movement params, keybinds, video, etc.).
// Call during ImGui frame, after NewFrame().
// load_level_fn is called when user picks a level from the browser.
void debug_menu_draw(GameState& gs, const LoadLevelFn& load_level_fn);
