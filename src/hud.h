#pragma once

struct GameState;

struct HudContext {
    float display_fps;
    bool  near_exit_door;
    bool  exit_door_locked;
    int   enemy_count;
};

// Draw the main gameplay HUD (health, speed, weapon info, door status, vignette, crosshair).
// Call during ImGui frame, after NewFrame().
void hud_draw(GameState& gs, const HudContext& ctx);
