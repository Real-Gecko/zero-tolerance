#include "input.hpp"

namespace {
uint16_t key_button(SDL_Scancode k) {
    switch (k) {
    case SDL_SCANCODE_UP: return PAD_UP;
    case SDL_SCANCODE_DOWN: return PAD_DOWN;
    case SDL_SCANCODE_LEFT: return PAD_LEFT;
    case SDL_SCANCODE_RIGHT: return PAD_RIGHT;
    case SDL_SCANCODE_Z: return PAD_A;
    case SDL_SCANCODE_X: return PAD_B;
    case SDL_SCANCODE_C: return PAD_C;
    case SDL_SCANCODE_RETURN: return SDL_GetModState() & SDL_KMOD_ALT ? 0 : PAD_START; // Alt+Enter: fullscreen
    default: return 0;
    }
}

uint16_t key_action(SDL_Scancode k) {
    switch (k) {
    case SDL_SCANCODE_W: return ACTION_FORWARD;
    case SDL_SCANCODE_S: return ACTION_BACK;
    case SDL_SCANCODE_A: return ACTION_STRAFE_LEFT;
    case SDL_SCANCODE_D: return ACTION_STRAFE_RIGHT;
    case SDL_SCANCODE_SPACE: return ACTION_JUMP;
    case SDL_SCANCODE_LCTRL: return ACTION_CROUCH;
    case SDL_SCANCODE_TAB: return ACTION_MAP;
    default: return 0;
    }
}
} // namespace

void Input::handle_event(const SDL_Event& e) {
    if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) {
        taps_ |= key_button(e.key.scancode);
        action_taps_ |= key_action(e.key.scancode);
    }
    if (e.type == SDL_EVENT_MOUSE_MOTION)
        mouse_x_ += e.motion.xrel;
    uint8_t mb = 0;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP)
        mb = e.button.button == SDL_BUTTON_LEFT ? ACTION_FIRE : e.button.button == SDL_BUTTON_RIGHT ? ACTION_ITEM : 0;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        action_taps_ |= mb;
        mouse_held_ |= mb;
    }
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP) mouse_held_ &= uint8_t(~mb);
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
    actions_ = action_taps_ | mouse_held_;
    action_taps_ = 0;
    int n;
    const bool* k = SDL_GetKeyboardState(&n);
    for (int i = 0; i < n; i++) {
        if (!k[i]) continue;
        held_ |= key_button(SDL_Scancode(i));
        actions_ |= key_action(SDL_Scancode(i));
    }
    if (pad_) {
        auto b = [&](SDL_GamepadButton btn) { return SDL_GetGamepadButton(pad_, btn); };
        auto axis = [&](SDL_GamepadAxis a) { return SDL_GetGamepadAxis(pad_, a); };
        if (b(SDL_GAMEPAD_BUTTON_DPAD_UP)) held_ |= PAD_UP;
        if (b(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) held_ |= PAD_DOWN;
        if (b(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) held_ |= PAD_LEFT;
        if (b(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) held_ |= PAD_RIGHT;
        if (b(SDL_GAMEPAD_BUTTON_WEST)) held_ |= PAD_B;
        if (b(SDL_GAMEPAD_BUTTON_SOUTH)) held_ |= PAD_A;
        if (b(SDL_GAMEPAD_BUTTON_EAST)) held_ |= PAD_C;
        // The left stick works like WASD, the right stick like the mouse.
        Sint16 lx = axis(SDL_GAMEPAD_AXIS_LEFTX), ly = axis(SDL_GAMEPAD_AXIS_LEFTY);
        if (ly < -16000) actions_ |= ACTION_FORWARD;
        if (ly > 16000) actions_ |= ACTION_BACK;
        if (lx < -16000) actions_ |= ACTION_STRAFE_LEFT;
        if (lx > 16000) actions_ |= ACTION_STRAFE_RIGHT;
        Sint16 rx = axis(SDL_GAMEPAD_AXIS_RIGHTX);
        if (rx < -8000 || rx > 8000) mouse_x_ += float(rx) / 32767 * 20;
        if (b(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)) actions_ |= ACTION_JUMP;
        if (b(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) actions_ |= ACTION_CROUCH;
        if (axis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000) actions_ |= ACTION_ITEM;
        if (axis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000) actions_ |= ACTION_FIRE;
        if (b(SDL_GAMEPAD_BUTTON_BACK)) actions_ |= ACTION_MAP;
        if (b(SDL_GAMEPAD_BUTTON_START)) actions_ |= ACTION_START;
    }
}

float Input::take_mouse_x() {
    float x = mouse_x_;
    mouse_x_ = 0;
    return x;
}
