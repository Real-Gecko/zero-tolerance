// Host side: window, input, audio, frame pacing, and the test plan runner.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace host {

struct Options {
    std::string data = "data";
    int scale = 3;
    std::string plan;   // test plan, same format as tools/mame/harness.lua
    bool headless = false; // no window, no audio, no frame pacing
    int64_t seed = -1;  // replaces the random seed the game takes from the HV counter
};

extern Options opt;
extern uint32_t frame_count;

bool init(const Options& o);
// One VBlank: runs the game's VBlank handler, shows the frame, reads input and
// waits for the next 1/60 s. Translated code calls this wherever the original
// waits for the VBlank interrupt.
void frame();
void set_vblank_handler(void (*handler)());
// The 68000 interrupt mask the game code sets: frame() runs the VBlank
// handler only while it is below 6.
extern int int_mask;
// The pad buttons held, active high: bit 7 Start, 6 A, 5 C, 4 B, 3 Right,
// 2 Left, 1 Down, 0 Up.
uint8_t pad();
// PC controls read by the player code (0xDAB8): the mouse turn since the
// last call in player angle units (512 per turn).
int mouse_turn();
// Held PC action keys, apart from the pad: bit 0 jump, 1 crouch toggle,
// 2 fire, 3 next item, 4 map, 5 forward, 6 back, 7 strafe left, 8 strafe right,
// 9 gamepad Start.
uint16_t action_keys();
// A key or button went down since the last call; skips the boot cinematics.
bool any_pressed();
// Set by a screen that can be shown wider than 320 pixels: called before the
// wide render with the extra columns per side; VRAM is restored after it.
extern void (*wide_screen)(int extra);
// Menus of the PC additions (pause menu, save slots): drawn by the host over
// whatever screen is shown, so VRAM is untouched. Items whose bit in enabled
// is clear are dimmed.
void menu_show(const std::string& title, const std::vector<std::string>& items, uint32_t enabled,
               int selected);
void menu_hide();
// File of save slot 0-3.
std::string save_path(int slot);
// Esc was pressed since the last call (cleared on read). Set only for key
// events; the game polls it where a menu makes sense.
bool take_menu_request();
// Keyboard text entry (the password field): while on, keys type instead of
// working the pad; take_text() returns what was typed since the last call,
// printable ASCII plus '\b' (Backspace), '\r' (Enter) and '\x1b' (Esc).
void text_input(bool on);
std::string take_text();
// Recapture the mouse after the pause menu (Esc releases it).
void capture_mouse();

} // namespace host
