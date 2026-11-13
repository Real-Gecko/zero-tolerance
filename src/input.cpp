#include "input.hpp"

namespace {
uint16_t key_button(SDL_Scancode k) {
    switch (k) {
    case SDL_SCANCODE_UP: case SDL_SCANCODE_W: return PAD_UP;
    case SDL_SCANCODE_DOWN: case SDL_SCANCODE_S: return PAD_DOWN;
    case SDL_SCANCODE_LEFT: case SDL_SCANCODE_A: return PAD_LEFT;
    case SDL_SCANCODE_RIGHT: case SDL_SCANCODE_D: return PAD_RIGHT;
    case SDL_SCANCODE_Z: return PAD_A;
    case SDL_SCANCODE_X: return PAD_B;
    case SDL_SCANCODE_C: return PAD_C;
    case SDL_SCANCODE_RETURN: return PAD_START;
    default: return 0;
    }
}
} // namespace

void Input::handle_event(const SDL_Event& e) {
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat)
        taps_ |= key_button(e.key.scancode);
    if (e.type == SDL_EVENT_GAMEPAD_ADDED && !pad_)
        pad_ = SDL_OpenGamepad(e.gdevice.which);
    if (e.type == SDL_EVENT_GAMEPAD_REMOVED && pad_ && SDL_GetGamepadID(pad_) == e.gdevice.which) {
        SDL_CloseGamepad(pad_);
        pad_ = nullptr;
    }
}

void Input::update() {
    // A tap counts as released last update, so it shows up as a press.
    prev_ = held_ & ~taps_;
    held_ = taps_;
    taps_ = 0;
    int n;
    const bool* k = SDL_GetKeyboardState(&n);
    for (int i = 0; i < n; i++)
        if (k[i])
            held_ |= key_button(SDL_Scancode(i));
    if (pad_) {
        auto b = [&](SDL_GamepadButton btn) { return SDL_GetGamepadButton(pad_, btn); };
        Sint16 lx = SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_LEFTX);
        Sint16 ly = SDL_GetGamepadAxis(pad_, SDL_GAMEPAD_AXIS_LEFTY);
        if (b(SDL_GAMEPAD_BUTTON_DPAD_UP) || ly < -16000) held_ |= PAD_UP;
        if (b(SDL_GAMEPAD_BUTTON_DPAD_DOWN) || ly > 16000) held_ |= PAD_DOWN;
        if (b(SDL_GAMEPAD_BUTTON_DPAD_LEFT) || lx < -16000) held_ |= PAD_LEFT;
        if (b(SDL_GAMEPAD_BUTTON_DPAD_RIGHT) || lx > 16000) held_ |= PAD_RIGHT;
        if (b(SDL_GAMEPAD_BUTTON_WEST)) held_ |= PAD_A;
        if (b(SDL_GAMEPAD_BUTTON_SOUTH)) held_ |= PAD_B;
        if (b(SDL_GAMEPAD_BUTTON_EAST)) held_ |= PAD_C;
        if (b(SDL_GAMEPAD_BUTTON_START)) held_ |= PAD_START;
    }
}
