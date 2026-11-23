// Real saves replacing the original passwords.
#include "savegame.hpp"

#include "display.hpp"
#include "game.hpp"

#include "../host.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace game {

namespace {

int load_slot = -1;

constexpr uint32_t kMagic = 0x3153545A; // "ZTS1"
constexpr uint32_t kVersion = 4; // v4: no ROM addresses in the State struct (older saves are rejected)

bool read_header(std::ifstream& f) {
    uint32_t magic = 0, version = 0;
    f.read(reinterpret_cast<char*>(&magic), 4);
    f.read(reinterpret_cast<char*>(&version), 4);
    return bool(f) && magic == kMagic && version == kVersion;
}

// "1 A2 L3 14.11 02:58" (area and level of the snapshot, file time) or "1 EMPTY".
std::string slot_label(int slot) {
    std::string n = std::to_string(slot + 1);
    if (!has_save(slot)) return n + " EMPTY";
    std::ifstream f(host::save_path(slot), std::ios::binary);
    static State saved;
    read_header(f);
    f.read(reinterpret_cast<char*>(&saved), sizeof saved);
    SDL_PathInfo info{};
    SDL_DateTime t{};
    SDL_GetPathInfo(host::save_path(slot).c_str(), &info);
    SDL_TimeToDateTime(info.modify_time, &t, true);
    char s[32];
    std::snprintf(s, sizeof s, "%d A%d L%d %02d.%02d %02d:%02d", slot + 1, saved.cur_area + 1,
                  saved.cur_level + 1, t.day, t.month, t.hour, t.minute);
    return s;
}

std::vector<std::string> slot_labels() {
    std::vector<std::string> r;
    for (int i = 0; i < kSaveSlots; i++) r.push_back(slot_label(i));
    return r;
}

uint32_t saved_slots() {
    uint32_t m = 0;
    for (int i = 0; i < kSaveSlots; i++) m |= uint32_t(has_save(i)) << i;
    return m;
}

// A host menu: Up/Down move, A/B/C/Start pick an enabled item, Esc (or the
// gamepad's Start in gameplay) backs out (-1).
int choose(const char* title, const std::vector<std::string>& items, uint32_t enabled, int sel) {
    int n = int(items.size());
    host::menu_show(title, items, enabled, sel);
    auto newly = [](int bit) { return (pad_buttons >> bit & 1) && !(st.pad_prev >> bit & 1); };
    for (;;) {
        host::frame();
        ReadPad();
        if (host::take_menu_request() || (start_is_menu && action_pressed(512))) {
            host::menu_hide();
            return -1;
        }
        if (newly(0) || newly(1)) {
            sel = (sel + (newly(0) ? n - 1 : 1)) % n;
            GemsPlaySound(0x6B);
            host::menu_show(title, items, enabled, sel);
        }
        uint8_t pressed = pad_buttons & 0xF0;
        if (pressed && (st.pad_prev & pressed) != pressed && (enabled >> sel & 1)) {
            host::menu_hide();
            return sel;
        }
    }
}

int first(uint32_t mask) {
    for (int i = 0; i < kSaveSlots; i++)
        if (mask >> i & 1) return i;
    return 0;
}

} // namespace

bool has_save(int slot) {
    std::ifstream f(host::save_path(slot), std::ios::binary);
    if (!f) return false;
    if (!read_header(f)) return false;
    // Full snapshot present: RAM, display (palette, tiles, planes, mode) and int mask.
    f.seekg(0, std::ios::end);
    return f.tellg() == std::streampos(8 + sizeof(State) + 64 * 2 + sizeof display::tiles +
                                       sizeof display::planeA + sizeof display::planeB + 2 + 2 + 4 * 4 +
                                       sizeof display::hscrollA + sizeof display::hscrollB + 4 + 4 + 2 + 4);
}

bool any_save() { return saved_slots() != 0; }

bool save_snapshot(int slot) {
    std::ofstream f(host::save_path(slot), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    uint32_t magic = kMagic, version = kVersion;
    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(&version), 4);
    f.write(reinterpret_cast<const char*>(&st), sizeof st);
    f.write(reinterpret_cast<const char*>(display::pal), sizeof display::pal);
    f.write(reinterpret_cast<const char*>(display::tiles), sizeof display::tiles);
    f.write(reinterpret_cast<const char*>(display::planeA), sizeof display::planeA);
    f.write(reinterpret_cast<const char*>(display::planeB), sizeof display::planeB);
    f.write(reinterpret_cast<const char*>(&display::ntA), 2);
    f.write(reinterpret_cast<const char*>(&display::ntB), 2);
    f.write(reinterpret_cast<const char*>(&display::pwA), 4);
    f.write(reinterpret_cast<const char*>(&display::phA), 4);
    f.write(reinterpret_cast<const char*>(&display::pwB), 4);
    f.write(reinterpret_cast<const char*>(&display::phB), 4);
    f.write(reinterpret_cast<const char*>(display::hscrollA), sizeof display::hscrollA);
    f.write(reinterpret_cast<const char*>(display::hscrollB), sizeof display::hscrollB);
    f.write(reinterpret_cast<const char*>(&display::hmode), 4);
    f.write(reinterpret_cast<const char*>(&display::vscrollA), 2);
    f.write(reinterpret_cast<const char*>(&display::vscrollB), 2);
    f.write(reinterpret_cast<const char*>(&display::win_h), 2);
    int32_t mask = host::int_mask;
    f.write(reinterpret_cast<const char*>(&mask), 4);
    return bool(f);
}

bool load_snapshot(int slot) {
    std::ifstream f(host::save_path(slot), std::ios::binary);
    if (!f || !read_header(f)) return false;
    static State loaded;
    f.read(reinterpret_cast<char*>(&loaded), sizeof loaded);
    f.read(reinterpret_cast<char*>(display::pal), sizeof display::pal);
    f.read(reinterpret_cast<char*>(display::tiles), sizeof display::tiles);
    f.read(reinterpret_cast<char*>(display::planeA), sizeof display::planeA);
    f.read(reinterpret_cast<char*>(display::planeB), sizeof display::planeB);
    f.read(reinterpret_cast<char*>(&display::ntA), 2);
    f.read(reinterpret_cast<char*>(&display::ntB), 2);
    f.read(reinterpret_cast<char*>(&display::pwA), 4);
    f.read(reinterpret_cast<char*>(&display::phA), 4);
    f.read(reinterpret_cast<char*>(&display::pwB), 4);
    f.read(reinterpret_cast<char*>(&display::phB), 4);
    f.read(reinterpret_cast<char*>(display::hscrollA), sizeof display::hscrollA);
    f.read(reinterpret_cast<char*>(display::hscrollB), sizeof display::hscrollB);
    f.read(reinterpret_cast<char*>(&display::hmode), 4);
    f.read(reinterpret_cast<char*>(&display::vscrollA), 2);
    f.read(reinterpret_cast<char*>(&display::vscrollB), 2);
    f.read(reinterpret_cast<char*>(&display::win_h), 2);
    int32_t mask = 5;
    f.read(reinterpret_cast<char*>(&mask), 4);
    if (!f) return false;
    st = loaded;
    host::int_mask = int(mask);
    drawn_count = 0;
    StopAllSound();
    PlaySound(cur_area);
    return true;
}

void request_load(int slot) { load_slot = slot; }

int take_load() {
    int r = load_slot;
    load_slot = -1;
    return r;
}

int run_pause_menu() {
    int sel = 0;
    for (;;) {
        sel = choose("PAUSED", {"SAVE", "LOAD", "QUIT TO TITLE"}, any_save() ? 7 : 5, sel);
        if (sel < 0) break;
        if (sel == 2) return 1;
        uint32_t enabled = sel == 0 ? 15 : saved_slots();
        int slot = choose(sel == 0 ? "SAVE TO" : "LOAD FROM", slot_labels(), enabled, sel == 0 ? 0 : first(enabled));
        if (slot < 0) continue;
        if (sel == 0 && save_snapshot(slot)) {
            PlaySoundLocal(0x69);
            queue_saved_message();
            break;
        }
        if (sel == 1 && load_snapshot(slot)) break;
    }
    host::capture_mouse();
    return 0;
}

int choose_load_slot() {
    host::take_menu_request(); // drop an Esc pressed on the title
    uint32_t enabled = saved_slots();
    return choose("LOAD FROM", slot_labels(), enabled, first(enabled));
}

} // namespace game
