#pragma once

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keyboard.h>
#include <string>
#include <cstdint>

// All rebindable actions
enum class Action : uint8_t {
    MoveForward,
    MoveBack,
    MoveLeft,
    MoveRight,
    Jump,
    Descend,    // noclip down
    Sprint,     // noclip sprint
    Noclip,
    ToggleHUD,
    COUNT
};

static constexpr int ACTION_COUNT = static_cast<int>(Action::COUNT);

// Human-readable names for the settings UI
inline const char* action_name(Action a) {
    switch (a) {
        case Action::MoveForward: return "Move Forward";
        case Action::MoveBack:    return "Move Back";
        case Action::MoveLeft:    return "Move Left";
        case Action::MoveRight:   return "Move Right";
        case Action::Jump:        return "Jump";
        case Action::Descend:     return "Descend";
        case Action::Sprint:      return "Sprint";
        case Action::Noclip:      return "Noclip";
        case Action::ToggleHUD:   return "Toggle HUD";
        default:                  return "???";
    }
}

struct Keybinds {
    SDL_Scancode binds[ACTION_COUNT];

    Keybinds() { reset_defaults(); }

    void reset_defaults() {
        binds[static_cast<int>(Action::MoveForward)] = SDL_SCANCODE_W;
        binds[static_cast<int>(Action::MoveBack)]    = SDL_SCANCODE_S;
        binds[static_cast<int>(Action::MoveLeft)]    = SDL_SCANCODE_A;
        binds[static_cast<int>(Action::MoveRight)]   = SDL_SCANCODE_D;
        binds[static_cast<int>(Action::Jump)]        = SDL_SCANCODE_SPACE;
        binds[static_cast<int>(Action::Descend)]     = SDL_SCANCODE_LSHIFT;
        binds[static_cast<int>(Action::Sprint)]      = SDL_SCANCODE_LCTRL;
        binds[static_cast<int>(Action::Noclip)]      = SDL_SCANCODE_V;
        binds[static_cast<int>(Action::ToggleHUD)]   = SDL_SCANCODE_H;
    }

    SDL_Scancode get(Action a) const {
        return binds[static_cast<int>(a)];
    }

    void set(Action a, SDL_Scancode sc) {
        binds[static_cast<int>(a)] = sc;
    }

    // Check if a scancode is currently held
    bool held(Action a, const bool* keyboard_state) const {
        return keyboard_state[get(a)];
    }

    // Convert scancode to display name
    static const char* key_name(SDL_Scancode sc) {
        const char* name = SDL_GetScancodeName(sc);
        return (name && name[0]) ? name : "???";
    }
};
