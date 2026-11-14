// Real saves replacing the original passwords: a snapshot of the emulated
// machine taken in the gameplay frame loop, restorable there or from the
// title LOAD option. Slots 0-3 at host::save_path(slot).
#pragma once

namespace game {

constexpr int kSaveSlots = 4;

// Save file present and readable.
bool has_save(int slot);
bool any_save();
// Snapshot RAM + VDP state to the save file.
bool save_snapshot(int slot);
// Restore the save file into RAM + VDP (restarts the area music, resets the
// interpolation buffers). False when there is no valid save.
bool load_snapshot(int slot);

// Title LOAD handling: set when the title menu picks a slot, consumed by the
// boot main loop to skip the new-game setup. take_load: the slot or -1.
void request_load(int slot);
int take_load();

// In-game Esc menu: SAVE / LOAD / QUIT TO TITLE, SAVE and LOAD then pick a
// slot. Returns 0 = resume, 1 = quit to title. LOAD restores the save and
// resumes.
int run_pause_menu();
// Title LOAD: the slot to load, -1 = cancelled.
int choose_load_slot();

} // namespace game
