#pragma once

#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_mouse.h>
#include <string>
#include <cstdint>

// All rebindable actions
enum class Action : uint8_t {
    MoveForward,
    MoveBack,
    MoveLeft,
    MoveRight,
    Jump,
    Crouch,
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
        case Action::MoveLeft:    return "Move Right";
        case Action::MoveRight:   return "Move Left";
        case Action::Jump:        return "Jump";
        case Action::Crouch:      return "Crouch";
        case Action::Descend:     return "Descend";
        case Action::Sprint:      return "Sprint";
        case Action::Noclip:      return "Noclip";
        case Action::ToggleHUD:   return "Toggle HUD";
        default:                  return "???";
    }
}

// ============================================================
//  Input codes: real SDL scancodes (0-511) plus virtual codes
//  for mouse inputs. 0xFFFF = unbound.
// ============================================================

using InputCode = uint16_t;

static constexpr InputCode INPUT_NONE              = 0xFFFF;
static constexpr InputCode INPUT_MOUSE_LEFT        = 512;
static constexpr InputCode INPUT_MOUSE_RIGHT       = 513;
static constexpr InputCode INPUT_MOUSE_MIDDLE      = 514;
static constexpr InputCode INPUT_MOUSE_WHEEL_UP    = 515;
static constexpr InputCode INPUT_MOUSE_WHEEL_DOWN  = 516;

inline bool is_scancode(InputCode c)     { return c < 512; }
inline bool is_mouse_button(InputCode c) { return c >= 512 && c <= 514; }
inline bool is_mouse_wheel(InputCode c)  { return c == INPUT_MOUSE_WHEEL_UP || c == INPUT_MOUSE_WHEEL_DOWN; }

inline const char* input_code_name(InputCode c) {
    if (c == INPUT_NONE)              return "---";
    if (c == INPUT_MOUSE_LEFT)        return "Mouse Left";
    if (c == INPUT_MOUSE_RIGHT)       return "Mouse Right";
    if (c == INPUT_MOUSE_MIDDLE)      return "Mouse Middle";
    if (c == INPUT_MOUSE_WHEEL_UP)    return "Wheel Up";
    if (c == INPUT_MOUSE_WHEEL_DOWN)  return "Wheel Down";
    if (is_scancode(c)) {
        const char* name = SDL_GetScancodeName(static_cast<SDL_Scancode>(c));
        return (name && name[0]) ? name : "???";
    }
    return "???";
}

// ============================================================
//  Keybinds: 2 slots per action
// ============================================================

static constexpr int SLOTS = 2;

struct Keybinds {
    InputCode binds[ACTION_COUNT][SLOTS];

    Keybinds() { reset_defaults(); }

    void reset_defaults() {
        for (int i = 0; i < ACTION_COUNT; i++)
            for (int s = 0; s < SLOTS; s++)
                binds[i][s] = INPUT_NONE;

        binds[static_cast<int>(Action::MoveForward)][0] = SDL_SCANCODE_W;
        binds[static_cast<int>(Action::MoveBack)][0]    = SDL_SCANCODE_S;
        binds[static_cast<int>(Action::MoveLeft)][0]    = SDL_SCANCODE_A;
        binds[static_cast<int>(Action::MoveRight)][0]   = SDL_SCANCODE_D;
        binds[static_cast<int>(Action::Jump)][0]        = SDL_SCANCODE_SPACE;
        binds[static_cast<int>(Action::Jump)][1]        = INPUT_MOUSE_WHEEL_DOWN;
        binds[static_cast<int>(Action::Crouch)][0]      = SDL_SCANCODE_LCTRL;
        binds[static_cast<int>(Action::Descend)][0]     = SDL_SCANCODE_LSHIFT;
        binds[static_cast<int>(Action::Sprint)][0]      = SDL_SCANCODE_LCTRL;
        binds[static_cast<int>(Action::Noclip)][0]      = SDL_SCANCODE_V;
        binds[static_cast<int>(Action::ToggleHUD)][0]   = SDL_SCANCODE_H;
    }

    InputCode get(Action a, int slot) const {
        return binds[static_cast<int>(a)][slot];
    }

    void set(Action a, int slot, InputCode code) {
        binds[static_cast<int>(a)][slot] = code;
    }

    // Check if any slot for this action is held (keyboard or mouse button)
    bool held(Action a, const bool* keyboard_state) const {
        for (int s = 0; s < SLOTS; s++) {
            InputCode c = binds[static_cast<int>(a)][s];
            if (c == INPUT_NONE) continue;

            if (is_scancode(c) && keyboard_state[c])
                return true;

            if (is_mouse_button(c)) {
                SDL_MouseButtonFlags buttons = SDL_GetMouseState(nullptr, nullptr);
                if (c == INPUT_MOUSE_LEFT   && (buttons & SDL_BUTTON_LMASK)) return true;
                if (c == INPUT_MOUSE_RIGHT  && (buttons & SDL_BUTTON_RMASK)) return true;
                if (c == INPUT_MOUSE_MIDDLE && (buttons & SDL_BUTTON_MMASK)) return true;
            }
        }
        return false;
    }

    // Check if a scancode matches any slot of an action
    bool matches_scancode(Action a, SDL_Scancode sc) const {
        InputCode code = static_cast<InputCode>(sc);
        for (int s = 0; s < SLOTS; s++)
            if (binds[static_cast<int>(a)][s] == code)
                return true;
        return false;
    }

    // Check if a mouse wheel code matches any slot of an action
    bool matches_wheel(Action a, InputCode wheel_code) const {
        for (int s = 0; s < SLOTS; s++)
            if (binds[static_cast<int>(a)][s] == wheel_code)
                return true;
        return false;
    }

    // Check if a mouse button code matches any slot of an action
    bool matches_mouse_button(Action a, InputCode btn_code) const {
        for (int s = 0; s < SLOTS; s++)
            if (binds[static_cast<int>(a)][s] == btn_code)
                return true;
        return false;
    }
};
