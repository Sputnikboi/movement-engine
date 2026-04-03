#include "config.h"
#include <SDL3/SDL_scancode.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

// Action name as it appears in the ini file
static const char* action_ini_key(int i) {
    switch (static_cast<Action>(i)) {
        case Action::MoveForward: return "move_forward";
        case Action::MoveBack:    return "move_back";
        case Action::MoveLeft:    return "move_left";
        case Action::MoveRight:   return "move_right";
        case Action::Jump:        return "jump";
        case Action::Crouch:      return "crouch";
        case Action::Descend:     return "descend";
        case Action::Sprint:      return "sprint";
        case Action::Noclip:      return "noclip";
        case Action::ToggleHUD:   return "toggle_hud";
        default:                  return nullptr;
    }
}

void Config::apply(Camera& camera, Player& player) const {
    camera.sensitivity = sensitivity;
    camera.invert_x    = invert_x ? -1.0f : 1.0f;
    camera.invert_y    = invert_y ? -1.0f : 1.0f;
    camera.fov         = fov;

    player.gravity        = gravity;
    player.max_speed      = max_speed;
    player.air_wish_speed = air_wish_speed;
    player.ground_accel   = ground_accel;
    player.air_accel      = air_accel;
    player.friction       = friction;
    player.jump_speed           = jump_speed;
    player.auto_hop             = auto_hop;
    player.crouch_speed         = crouch_speed;
    player.slide_friction       = slide_friction;
    player.slide_boost          = slide_boost;
    player.slide_min_speed      = slide_min_speed;
    player.slide_stop_speed     = slide_stop_speed;
    player.slide_boost_cooldown = slide_boost_cooldown;
    player.slide_jump_boost     = slide_jump_boost;
    player.lurch_window         = lurch_window;
    player.lurch_strength       = lurch_strength;
}

void Config::pull(const Camera& camera, const Player& player) {
    sensitivity = camera.sensitivity;
    invert_x    = camera.invert_x < 0.0f;
    invert_y    = camera.invert_y < 0.0f;
    fov         = camera.fov;

    gravity        = player.gravity;
    max_speed      = player.max_speed;
    air_wish_speed = player.air_wish_speed;
    ground_accel   = player.ground_accel;
    air_accel      = player.air_accel;
    friction       = player.friction;
    jump_speed           = player.jump_speed;
    auto_hop             = player.auto_hop;
    crouch_speed         = player.crouch_speed;
    slide_friction       = player.slide_friction;
    slide_boost          = player.slide_boost;
    slide_min_speed      = player.slide_min_speed;
    slide_stop_speed     = player.slide_stop_speed;
    slide_boost_cooldown = player.slide_boost_cooldown;
    slide_jump_boost     = player.slide_jump_boost;
    lurch_window         = player.lurch_window;
    lurch_strength       = player.lurch_strength;
}

bool Config::save(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        fprintf(stderr, "Failed to save config: %s\n", path.c_str());
        return false;
    }

    fprintf(f, "[mouse]\n");
    fprintf(f, "sensitivity = %.8f\n", sensitivity);
    fprintf(f, "invert_x = %d\n", invert_x ? 1 : 0);
    fprintf(f, "invert_y = %d\n", invert_y ? 1 : 0);
    fprintf(f, "\n");

    fprintf(f, "[video]\n");
    fprintf(f, "fov = %.1f\n", fov);
    fprintf(f, "\n");

    fprintf(f, "[movement]\n");
    fprintf(f, "gravity = %.2f\n", gravity);
    fprintf(f, "max_speed = %.2f\n", max_speed);
    fprintf(f, "air_wish_speed = %.4f\n", air_wish_speed);
    fprintf(f, "ground_accel = %.2f\n", ground_accel);
    fprintf(f, "air_accel = %.2f\n", air_accel);
    fprintf(f, "friction = %.2f\n", friction);
    fprintf(f, "jump_speed = %.2f\n", jump_speed);
    fprintf(f, "auto_hop = %d\n", auto_hop ? 1 : 0);
    fprintf(f, "crouch_speed = %.2f\n", crouch_speed);
    fprintf(f, "slide_friction = %.2f\n", slide_friction);
    fprintf(f, "slide_boost = %.2f\n", slide_boost);
    fprintf(f, "slide_min_speed = %.2f\n", slide_min_speed);
    fprintf(f, "slide_stop_speed = %.2f\n", slide_stop_speed);
    fprintf(f, "slide_boost_cooldown = %.2f\n", slide_boost_cooldown);
    fprintf(f, "slide_jump_boost = %.2f\n", slide_jump_boost);
    fprintf(f, "lurch_window = %.2f\n", lurch_window);
    fprintf(f, "lurch_strength = %.2f\n", lurch_strength);
    fprintf(f, "\n");

    fprintf(f, "[keybinds]\n");
    for (int i = 0; i < ACTION_COUNT; i++) {
        const char* k = action_ini_key(i);
        if (k) fprintf(f, "%s = %d,%d\n", k,
                        static_cast<int>(keybinds.binds[i][0]),
                        static_cast<int>(keybinds.binds[i][1]));
    }

    fclose(f);
    printf("Config saved to %s\n", path.c_str());
    return true;
}

bool Config::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        printf("No config file found at %s, using defaults\n", path.c_str());
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and section headers
        if (line.empty() || line[0] == '#' || line[0] == '[')
            continue;

        // Parse "key = value"
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        // Trim whitespace
        auto trim = [](std::string& s) {
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            while (!s.empty() && s.back() == ' ')  s.pop_back();
        };
        trim(key);
        trim(val);

        // Match keys
        if      (key == "sensitivity")    sensitivity    = std::stof(val);
        else if (key == "invert_x")       invert_x       = std::stoi(val) != 0;
        else if (key == "invert_y")       invert_y       = std::stoi(val) != 0;
        else if (key == "fov")            fov             = std::stof(val);
        else if (key == "gravity")        gravity         = std::stof(val);
        else if (key == "max_speed")      max_speed       = std::stof(val);
        else if (key == "air_wish_speed") air_wish_speed  = std::stof(val);
        else if (key == "ground_accel")   ground_accel    = std::stof(val);
        else if (key == "air_accel")      air_accel       = std::stof(val);
        else if (key == "friction")       friction        = std::stof(val);
        else if (key == "jump_speed")     jump_speed      = std::stof(val);
        else if (key == "auto_hop")       auto_hop        = std::stoi(val) != 0;
        else if (key == "crouch_speed")   crouch_speed    = std::stof(val);
        else if (key == "slide_friction") slide_friction   = std::stof(val);
        else if (key == "slide_boost")    slide_boost      = std::stof(val);
        else if (key == "slide_min_speed") slide_min_speed = std::stof(val);
        else if (key == "slide_stop_speed") slide_stop_speed = std::stof(val);
        else if (key == "slide_boost_cooldown") slide_boost_cooldown = std::stof(val);
        else if (key == "slide_jump_boost") slide_jump_boost = std::stof(val);
        else if (key == "lurch_window")   lurch_window    = std::stof(val);
        else if (key == "lurch_strength") lurch_strength  = std::stof(val);
        else {
            // Try keybinds (format: "action = code0,code1")
            for (int i = 0; i < ACTION_COUNT; i++) {
                const char* k = action_ini_key(i);
                if (k && key == k) {
                    auto comma = val.find(',');
                    if (comma != std::string::npos) {
                        keybinds.binds[i][0] = static_cast<InputCode>(std::stoi(val.substr(0, comma)));
                        keybinds.binds[i][1] = static_cast<InputCode>(std::stoi(val.substr(comma + 1)));
                    } else {
                        // Legacy: single value
                        keybinds.binds[i][0] = static_cast<InputCode>(std::stoi(val));
                        keybinds.binds[i][1] = INPUT_NONE;
                    }
                    break;
                }
            }
        }
    }

    printf("Config loaded from %s\n", path.c_str());
    return true;
}
