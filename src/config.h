#pragma once

#include "camera.h"
#include "player.h"
#include "keybinds.h"
#include <string>

// Settings that get saved/loaded from settings.ini
struct Config {
    // Mouse
    float sensitivity = 0.00014f;
    bool  invert_x    = true;
    bool  invert_y    = false;

    // Video
    float fov = 90.0f;

    // Movement (exposed for tuning)
    float gravity              = 20.0f;
    float max_speed            = 8.0f;
    float air_wish_speed       = 0.76f;
    float ground_accel         = 10.0f;
    float air_accel            = 70.0f;
    float friction             = 6.0f;
    float jump_speed           = 7.2f;
    bool  auto_hop             = false;
    float crouch_speed         = 4.0f;
    float slide_friction       = 0.8f;
    float slide_boost          = 3.0f;
    float slide_min_speed      = 6.0f;
    float slide_stop_speed     = 3.0f;
    float slide_boost_cooldown = 2.0f;
    float slide_jump_boost     = 4.0f;
    float lurch_window         = 0.5f;
    float lurch_strength       = 0.5f;

    // Keybinds
    Keybinds keybinds;

    // Apply config to camera and player
    void apply(Camera& camera, Player& player) const;

    // Pull current values from camera and player into config
    void pull(const Camera& camera, const Player& player);

    // File I/O — path is relative to executable
    bool save(const std::string& path = "settings.ini") const;
    bool load(const std::string& path = "settings.ini");
};
