// Maps keyboard and gamepads to a Mega Drive style 3-button pad.
#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

enum PadButton : uint16_t {
    PAD_UP = 1, PAD_DOWN = 2, PAD_LEFT = 4, PAD_RIGHT = 8,
    PAD_B = 16, PAD_C = 32, PAD_A = 64, PAD_START = 128,
};

class Input {
public:
    void handle_event(const SDL_Event& e);
    // Call once per game tick: updates held/pressed state.
    void update();
    uint16_t held() const { return held_; }
    uint16_t pressed() const { return held_ & ~prev_; }
    // PAD_LEFT / PAD_RIGHT bits held only through A / D.
    uint16_t strafe() const { return strafe_; }
    // Horizontal mouse motion since the last call, in pixels.
    float take_mouse_x();

private:
    uint16_t held_ = 0, prev_ = 0, strafe_ = 0;
    uint16_t taps_ = 0; // keys pressed since the last update, so short taps are not lost
    uint16_t mouse_held_ = 0;
    float mouse_x_ = 0;
    SDL_Gamepad* pad_ = nullptr;
};
