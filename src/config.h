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
