#pragma once

struct GameState;
struct ImFont;

struct HudContext {
    float display_fps;
    bool  near_exit_door;
    bool  exit_door_locked;
    int   enemy_count;
};

// Clean in-game HUD using raw draw calls (Daydream pixel font).
// HP bar top-center, ammo bottom-left, gold/room top-right, door prompts.
void hud_draw_game(GameState& gs, const HudContext& ctx, ImFont* font, ImFont* font_large);

// Debug HUD (ImGui window) — toggled by show_hud / H key.
void hud_draw_debug(GameState& gs, const HudContext& ctx);

// Damage vignette + crosshair — always drawn (unless dead).
void hud_draw_overlay(GameState& gs);
