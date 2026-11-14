// Options screen (0x10B11A) and the map screen's level lines (0x5755E).
#include "display.hpp"
#include "game.hpp"

#include "../host.hpp"

#include <utility>

namespace game {

namespace {

auto& option_row = st.menu_row; // 0 password, 1 players, 2 music, 3 voice, 4 sound test, 5 exit
auto& sound_test = st.menu_value;

// 0xC6AA4: two-row text in an 8x16 font at nametable address nt.
void DrawText16(uint16_t nt, const uint8_t* text, const uint16_t (*glyphs)[2] = kFont16) {
    for (int row = 0; row < 2; row++, nt = uint16_t(nt + 0x80)) {
        uint32_t x = 0;
        for (const uint8_t* a = text; *a; a++, x += 2) display::write_cell(uint16_t(nt + x), glyphs[*a - 0x20][row]);
    }
}
void DrawText16(uint16_t nt, const char* text) { DrawText16(nt, reinterpret_cast<const uint8_t*>(text)); }

// 0x1F810: one sprite table entry.
void SetSprite(uint16_t index, uint16_t y, uint16_t size_link, uint16_t x, uint16_t attr) {
    display::set_sprite(index, y, uint8_t(size_link >> 8), uint8_t(size_link), attr, x);
}

// 0x10B4C8: options cursor.
void SetOptionsCursor(uint16_t row) {
    if (row == 5) row = 6;
    SetSprite(0, uint16_t(row * 16 + 0xB4), 0x400, 0xB8, 0x84AE);
}

// 0x10B570: sound test number in hex.
void DrawSoundTestNumber() {
    uint8_t v = uint8_t(sound_test);
    const char text[3] = {"0123456789ABCDEF"[v >> 4], "0123456789ABCDEF"[v & 0xF], 0};
    DrawText16(0xC72C, text);
}

// 0x10B4EE: option values.
void DrawOptionValues() {
    DrawText16(0xC328, st.password);
    DrawText16(0xC426, assets::text("options.one")); // two-console link play is not supported
    DrawText16(0xC522, assets::text((sound_options & 1) ? "options.on" : "options.off"));
    DrawText16(0xC622, assets::text((sound_options & 2) ? "options.on" : "options.off"));
    DrawSoundTestNumber();
}

// 0x10B3D2
void InitOptionsScreen() {
    ClearCram();
    ClearVram();
    SetIntroVdpRegs();
    LoadPaletteLine(0, assets::palette("palettes/options.png"));
    LoadPaletteLine(3, assets::palette("palettes/menu_font.png"));
    display::upload_tiles(0x4AE, 2, assets::tiles("screens/tiles/options_cursor.png").data());
    display::upload_tiles(0x4B0, 0x4B0 / 8, Font16Tiles().data());
    option_row = 5;
    SetOptionsCursor(5);
    static const std::pair<uint16_t, const char*> labels[] = {
        {0xC314, "options.password"}, {0xC414, "options.players"}, {0xC514, "options.music"},
        {0xC614, "options.voice"}, {0xC714, "options.sound_test"}, {0xC914, "options.exit"},
    };
    for (auto [nt, text] : labels) DrawText16(nt, assets::text(text));
    sound_test = 0;
    st.repeat[0] = st.repeat[1] = 0;
    DrawOptionValues();
}

// Auto-repeat of a held button: true on the press, then after 20 frames and every 8.
bool repeat(int bit, uint8_t& counter) {
    if (!(pad_buttons >> bit & 1)) {
        counter = 0;
        return false;
    }
    if (!(st.pad_prev >> bit & 1)) return true;
    int8_t c = int8_t(counter);
    if (c <= 0) {
        counter = 0x14;
        return false;
    }
    counter = uint8_t(c - 1);
    if (c - 1) return false;
    counter = 8;
    return true;
}

bool newly(int bit) { return (pad_buttons >> bit & 1) && !(st.pad_prev >> bit & 1); }

// PC: the password entry (0x58054) as a keyboard field in place of the
// value, instead of the original's character grid. Enter is its DONE: an
// empty field clears the password, nine characters the game accepts
// (0x58A74) become it, anything else shows "INCORRECT PASSWORD"; Esc is
// its CANCEL.
void PasswordEntry() {
    uint8_t entry[19]; // the field, padded with spaces over the message
    for (int i = 0; i < 18; i++) entry[i] = i < 9 ? st.password[i] : ' ';
    entry[18] = 0;
    int len = entry[0] != 0x5F ? 9 : 0;
    DrawText16(0xC328, entry);
    host::text_input(true);
    for (bool done = false; !done;) {
        WaitVBlank();
        ReadPad();
        for (char c : host::take_text()) {
            if (c == '\x1b') {
                done = true;
                break;
            }
            if (c == '\r') {
                if (len == 0) {
                    for (int i = 0; i < 9; i++) st.password[i] = 0x5F;
                    done = true;
                    break;
                }
                if (len == 9 && DecodePassword(entry) != 0xFFFF) {
                    for (int i = 0; i < 9; i++) st.password[i] = entry[i];
                    done = true;
                    break;
                }
                DrawText16(0xC328, assets::text("password.incorrect"));
                for (int i = 0; i <= 0x28; i++) WaitVBlank();
                DrawText16(0xC328, entry);
                continue;
            }
            if (c == '\b') {
                if (!len) continue;
                entry[--len] = 0x5F;
            } else {
                if (len == 9) continue;
                entry[len++] = uint8_t(c);
            }
            PlaySoundLocal(0x69);
            DrawText16(0xC328, entry);
        }
    }
    host::text_input(false);
    for (int i = 0; i < 18; i++) entry[i] = i < 9 ? st.password[i] : ' ';
    DrawText16(0xC328, entry);
}

// 0x10B5B2: A/B/C on an option.
void ChangeOption() {
    switch (uint16_t(option_row)) {
    case 0: PasswordEntry(); break;
    case 1: break; // players: one only
    case 2: {
        uint16_t o = sound_options;
        if (o & 1) {
            StopAllSound();
            o &= ~1;
        } else o |= 1;
        sound_options = o;
        DrawText16(0xC522, assets::text((o & 1) ? "options.on" : "options.off"));
        if (sound_options & 1) PlaySound(3);
        break;
    }
    case 3: {
        uint16_t o = uint16_t(sound_options ^ 2);
        sound_options = o;
        DrawText16(0xC622, assets::text((o & 2) ? "options.on" : "options.off"));
        break;
    }
    case 4: {
        StopAllSound();
        uint16_t saved = sound_options;
        sound_options = 0xF;
        GemsPlaySound(sound_test);
        sound_options = saved;
        break;
    }
    }
}

} // namespace

// 0xD760: PlaySound for the local console (the link variant is not supported).
void PlaySoundLocal(uint16_t id) {
    int_mask = 7;
    GemsPlaySound(id);
    int_mask = 5;
}

// 0x10B11A
void OptionsScreen() {
    InitOptionsScreen();
    for (;;) {
        WaitVBlank();
        ReadPad();
        if (repeat(0, st.repeat[2])) {
            GemsPlaySound(0x6B);
            option_row -= 1;
            if (int16_t(uint16_t(option_row)) < 0) option_row = 5;
            SetOptionsCursor(option_row);
            continue;
        }
        if (repeat(1, st.repeat[3])) {
            GemsPlaySound(0x6B);
            option_row += 1;
            if (int16_t(uint16_t(option_row)) > 5) option_row = 0;
            SetOptionsCursor(option_row);
            continue;
        }
        if (newly(6) || newly(4) || newly(5) || (newly(7) && option_row == 0)) { // Start (Enter) opens the password too
            if (option_row == 5) break;
            if (option_row != 4) GemsPlaySound(0x6B);
            ChangeOption();
            continue;
        }
        if (option_row == 4) {
            if (repeat(2, st.repeat[1])) {
                GemsPlaySound(0x6B);
                sound_test -= 1;
                if (int16_t(uint16_t(sound_test)) < 0) sound_test = 0x99;
                DrawSoundTestNumber();
                continue;
            }
            if (repeat(3, st.repeat[0])) {
                GemsPlaySound(0x6B);
                sound_test += 1;
                if (int16_t(uint16_t(sound_test)) > 0x99) sound_test = 0;
                DrawSoundTestNumber();
                continue;
            }
        } else st.repeat[0] = 0;
        if (newly(7) && option_row == 5) break;
    }
    StopAllSound();
    if (sound_options & 1) PlaySound(3);
}

namespace {

// 0x57D2E: clears a row of plane A and centres one line of 8x16 text on it.
void DrawCentred(uint16_t nt, const uint8_t* text) {
    display::fill_cells(nt, 0x80, 0x8000);
    uint16_t len = 0;
    while (text[len]) len++;
    nt = uint16_t(nt + (0x14 - (int16_t(len) >> 1)) * 2);
    DrawText16(nt, text, kMapFont16);
}

// 0x575EE: clears a row of plane A.
void ClearPlaneRow(uint16_t nt) { display::fill_cells(nt, 0x80, 0); }

uint8_t* copy_str(uint8_t* a0, const char* a1) {
    while (char c = *a1++) *a0++ = uint8_t(c);
    return a0;
}

uint8_t* copy_name(uint8_t* a0, const uint8_t* a1) {
    for (int i = 0; i < 9; i++) *a0++ = *a1++;
    for (int i = 0; i < 4; i++) *a0++ = 0x20;
    return a0;
}

// 0x5761E: "<level name> : <password>" or the secured state of the current level.
uint8_t* FormatLevelLine(uint8_t* a0, uint16_t level) {
    a0 = copy_str(a0, assets::list("text/mapnames.txt")[level + cur_area * 16].c_str());
    if (level != st.pw_level) return copy_name(a0, st.level_passwords + level * 9);
    const char* a1 = assets::text(st.cleared_timer ? "map.secured" : "map.not_secured");
    for (int i = 0; i < 13; i++) *a0++ = *a1++;
    return a0;
}

// 0x57B5A: the space station / high rise line.
uint8_t* FormatAreaLine(uint8_t* a0) {
    a0 = copy_str(a0, assets::text(cur_area == 1 ? "map.space_station" : "map.high_rise"));
    return copy_name(a0, st.area_password);
}

} // namespace

// 0x5755E: map screen level lines for the levels before, at and after d1.
void DrawMapLevelLines(uint16_t level) {
    uint8_t buf[64];
    if (int16_t(uint16_t(level - 1)) >= 0) {
        *FormatLevelLine(buf, uint16_t(level - 1)) = 0;
        DrawCentred(0xE080, buf);
    } else if (st.area_password[0]) {
        *FormatAreaLine(buf) = 0;
        DrawCentred(0xE080, buf);
    } else
        ClearPlaneRow(0xE080);
    *FormatLevelLine(buf, level) = 0;
    DrawCentred(0xE180, buf);
    level = uint16_t(level + 1);
    if (level > st.pw_level)
        ClearPlaneRow(0xE280);
    else {
        *FormatLevelLine(buf, level) = 0;
        DrawCentred(0xE280, buf);
    }
}

} // namespace game
