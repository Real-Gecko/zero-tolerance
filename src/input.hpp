// Maps keyboard and gamepads to a Mega Drive style 3-button pad.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

enum PadButton : uint16_t {
    PAD_UP = 1, PAD_DOWN = 2, PAD_LEFT = 4, PAD_RIGHT = 8,
    PAD_B = 16, PAD_C = 32, PAD_A = 64, PAD_START = 128,
};

// PC-only actions with no pad button of their own.
enum Action : uint16_t {
    ACTION_JUMP = 1, ACTION_CROUCH = 2, ACTION_FIRE = 4, ACTION_ITEM = 8, ACTION_MAP = 16,
    ACTION_FORWARD = 32, ACTION_BACK = 64, ACTION_STRAFE_LEFT = 128, ACTION_STRAFE_RIGHT = 256,
    ACTION_START = 512, // gamepad Start: the pause menu in gameplay, the pad's Start elsewhere
};

class Input {
public:
    void handle_event(const SDL_Event& e);
    // Call once per game tick: updates held/pressed state.
    void update();
    uint16_t held() const { return held_; }
    uint16_t pressed() const { return held_ & ~prev_; }
    // ACTION_* bits held through their own keys.
    uint16_t actions() const { return actions_; }
    // Horizontal mouse motion since the last call, in pixels.
    float take_mouse_x();

private:
    uint16_t held_ = 0, prev_ = 0;
    uint16_t taps_ = 0; // keys pressed since the last update, so short taps are not lost
    uint16_t actions_ = 0, action_taps_ = 0;
    uint8_t mouse_held_ = 0;
    float mouse_x_ = 0;
    SDL_Gamepad* pad_ = nullptr;
};
