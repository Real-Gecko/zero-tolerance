// Options screen (0x10B11A) and password entry (0x58054).
#include "game.hpp"

#include <utility>

namespace game {

namespace {

U16 option_row{0xFF2704}; // 0 password, 1 players, 2 music, 3 voice, 4 sound test, 5 exit
U16 sound_test{0xFF2706};

// 0xC6AA4: two-row text in the 8x16 font (glyph table 0xC7196) at nametable address nt.
void DrawText16(uint16_t nt, uint32_t text, uint32_t glyphs = 0xC7196) {
    for (int row = 0; row < 2; row++, glyphs += 2, nt = uint16_t(nt + 0x80)) {
        vdp_write_addr(nt);
        for (uint32_t a = text; rd8(a); a++) vdp_data(rd16(glyphs + (rd8(a) - 0x20) * 4));
    }
}

// 0x1F810: one sprite table entry.
void SetSprite(uint16_t index, uint16_t y, uint16_t size_link, uint16_t x, uint16_t attr) {
    vdp_ctrl32((uint32_t(index) << 19) + U32{0xFF06F2});
    vdp_data(y);
    vdp_data(size_link);
    vdp_data(attr);
    vdp_data(x);
}

// 0x10B4C8: options cursor.
void sub_10B4C8(uint16_t row) {
    if (row == 5) row = 6;
    SetSprite(0, uint16_t(row * 16 + 0xB4), 0x400, 0xB8, 0x84AE);
}

// 0x10B570: sound test number in hex.
void sub_10B570() {
    uint8_t v = rd8(0xFF2707);
    wr8(0xFF0FFA, rd8(0x10B76C + (v >> 4)));
    wr8(0xFF0FFB, rd8(0x10B76C + (v & 0xF)));
    wr8(0xFF0FFC, 0);
    DrawText16(0xC72C, 0xFF0FFA);
}

const uint32_t text_off = 0x10B75E, text_on = 0x10B762;

// 0x10B4EE: option values.
void sub_10B4EE() {
    DrawText16(0xC328, password);
    DrawText16(0xC426, 0x10B73D); // "One"; two-console link play is not supported
    DrawText16(0xC522, (sound_options & 1) ? text_on : text_off);
    DrawText16(0xC622, (sound_options & 2) ? text_on : text_off);
    sub_10B570();
}

// 0x10B3D2
void sub_10B3D2() {
    ClearCram();
    ClearVram();
    sub_01F578();
    LoadPaletteLine(0, 0x10CBDC);
    LoadPaletteLine(3, 0x1CDAE);
    vdp_ctrl32(0x55C00002);
    for (uint32_t a = 0x10CB7C; a < 0x10CB7C + 64; a += 4) vdp_data32(rd32(a));
    vdp_ctrl32(0x56000002);
    for (uint32_t a = 0x125116; a < 0x125116 + 0x4B0 * 4; a += 4) vdp_data32(rd32(a));
    option_row = 5;
    sub_10B4C8(5);
    static const std::pair<uint16_t, uint32_t> labels[] = {
        {0xC314, 0x10B710}, {0xC414, 0x10B71A}, {0xC514, 0x10B723},
        {0xC614, 0x10B72A}, {0xC714, 0x10B731}, {0xC914, 0x10B766},
    };
    for (auto [nt, text] : labels) DrawText16(nt, text);
    sound_test = 0;
    U16{0xFF102C} = 0;
    sub_10B4EE();
}

// Auto-repeat of a held button: true on the press, then after 20 frames and every 8.
bool repeat(int bit, uint32_t counter) {
    if (!(pad_buttons >> bit & 1)) {
        wr8(counter, 0);
        return false;
    }
    if (!(rd8(0xFF0032) >> bit & 1)) return true;
    int8_t c = int8_t(rd8(counter));
    if (c <= 0) {
        wr8(counter, 0x14);
        return false;
    }
    wr8(counter, uint8_t(c - 1));
    if (c - 1) return false;
    wr8(counter, 8);
    return true;
}

bool newly(int bit) { return (pad_buttons >> bit & 1) && !(rd8(0xFF0032) >> bit & 1); }

// 0x10B5B2: A/B/C on an option.
void sub_10B5B2() {
    switch (uint16_t(option_row)) {
    case 0:
        sub_000BF2();
        sub_058054();
        sub_10B3D2();
        DrawText16(0xC328, password);
        break;
    case 1: break; // players: one only
    case 2: {
        uint16_t o = sound_options;
        if (o & 1) {
            sub_0C5DAC();
            o &= ~1;
        } else o |= 1;
        sound_options = o;
        DrawText16(0xC522, (o & 1) ? text_on : text_off);
        if (sound_options & 1) PlaySound(3);
        break;
    }
    case 3: {
        uint16_t o = uint16_t(sound_options ^ 2);
        sound_options = o;
        DrawText16(0xC622, (o & 2) ? text_on : text_off);
        break;
    }
    case 4: {
        sub_0C5DAC();
        uint16_t saved = sound_options;
        sound_options = 0xF;
        sub_0C615A(sound_test);
        sound_options = saved;
        break;
    }
    }
}

} // namespace

// 0xD760: PlaySound for the local console (the link variant is not supported).
void sub_00D760(uint16_t id) {
    int_mask = 7;
    sub_0C615A(id);
    int_mask = 5;
}

// 0x10B11A
void sub_10B11A() {
    sub_10B3D2();
    for (;;) {
        WaitVBlank();
        ReadPad();
        if (repeat(0, 0xFF102E)) {
            sub_0C615A(0x6B);
            option_row -= 1;
            if (int16_t(uint16_t(option_row)) < 0) option_row = 5;
            sub_10B4C8(option_row);
            continue;
        }
        if (repeat(1, 0xFF102F)) {
            sub_0C615A(0x6B);
            option_row += 1;
            if (int16_t(uint16_t(option_row)) > 5) option_row = 0;
            sub_10B4C8(option_row);
            continue;
        }
        if (newly(6) || newly(4) || newly(5)) {
            if (option_row == 5) break;
            if (option_row != 4) sub_0C615A(0x6B);
            sub_10B5B2();
            continue;
        }
        if (option_row == 4) {
            if (repeat(2, 0xFF102D)) {
                sub_0C615A(0x6B);
                sound_test -= 1;
                if (int16_t(uint16_t(sound_test)) < 0) sound_test = 0x99;
                sub_10B570();
                continue;
            }
            if (repeat(3, 0xFF102C)) {
                sub_0C615A(0x6B);
                sound_test += 1;
                if (int16_t(uint16_t(sound_test)) > 0x99) sound_test = 0;
                sub_10B570();
                continue;
            }
        } else wr8(0xFF102C, 0);
        if (newly(7) && option_row == 5) break;
    }
    sub_0C5DAC();
    if (sound_options & 1) PlaySound(3);
}

namespace {

// 0x57D2E: clears a row of plane A and centres one line of 8x16 text on it.
void DrawCentred(uint16_t nt, uint32_t text) {
    vdp_write_addr(nt);
    for (int i = 0; i < 0x80; i++) vdp_data(0x8000);
    uint16_t len = 0;
    while (rd8(text + len)) len++;
    nt = uint16_t(nt + (0x14 - (int16_t(len) >> 1)) * 2);
    DrawText16(nt, text, 0x57BC2);
}

// 0x575EE: clears a row of plane A.
void sub_0575EE(uint16_t nt) {
    vdp_write_addr(nt);
    for (int i = 0; i < 0x40; i++) vdp_data32(0);
}

uint32_t copy_str(uint32_t a0, uint32_t a1) {
    while (uint8_t c = rd8(a1++)) wr8(a0++, c);
    return a0;
}

uint32_t copy_name(uint32_t a0, uint32_t a1) {
    for (int i = 0; i < 9; i++) wr8(a0++, rd8(a1++));
    for (int i = 0; i < 4; i++) wr8(a0++, 0x20);
    return a0;
}

// 0x5761E: "<level name> : <password>" or the secured state of the current level.
uint32_t sub_05761E(uint32_t a0, uint16_t level) {
    a0 = copy_str(a0, 0x576AA + int16_t(uint16_t((level + cur_area * 16) * 0x19)));
    if (level != U16{0xFF272A}) return copy_name(a0, 0xFF273C + level * 9);
    uint32_t a1 = U16{0xFF272C} ? 0x5769D : 0x57690;
    for (int i = 0; i < 13; i++) wr8(a0++, rd8(a1++));
    return a0;
}

// 0x57B5A: the space station / high rise line.
uint32_t sub_057B5A(uint32_t a0) {
    a0 = copy_str(a0, cur_area == 1 ? 0x57B90 : 0x57BA9);
    return copy_name(a0, 0xFF2732);
}

} // namespace

// 0x5755E: map screen level lines for the levels before, at and after d1.
void sub_05755E(uint16_t level) {
    const uint32_t buf = 0xFF0FFA;
    if (int16_t(uint16_t(level - 1)) >= 0) {
        wr8(sub_05761E(buf, uint16_t(level - 1)), 0);
        DrawCentred(0xE080, buf);
    } else if (U8{0xFF2732}) {
        wr8(sub_057B5A(buf), 0);
        DrawCentred(0xE080, buf);
    } else
        sub_0575EE(0xE080);
    wr8(sub_05761E(buf, level), 0);
    DrawCentred(0xE180, buf);
    level = uint16_t(level + 1);
    if (level > U16{0xFF272A})
        sub_0575EE(0xE280);
    else {
        wr8(sub_05761E(buf, level), 0);
        DrawCentred(0xE280, buf);
    }
}

// 0x58054: password entry. The grid has 9 rows of 8 characters (0x585A2);
// row 8 holds DEL, CANCEL and DONE.
void sub_058054() {
    fb_dma_pending = 0;
    sub_000CAA();
    sub_000C5A();
    vdp_ctrl32(0x75A00000);
    for (uint32_t a = 0x125116; a < 0x125116 + 0x4B0 * 4; a += 4) vdp_data32(rd32(a));
    for (uint32_t a = 0x12EA86; a < 0x12EA86 + 32; a += 4) vdp_data32(rd32(a));
    vdp_ctrl32(0xC0400000);
    for (uint32_t a = 0x1CDAE; a < 0x1CDAE + 32; a += 4) vdp_data32(rd32(a));
    wr32(vdp_queue, 0xFFFFFFFF);
    vdp_queue_ptr = vdp_queue;
    U16{0xFF0042} = 1;
    U32{0xFF003E} = 0xFF0044;
    DrawCentred(0xE080, 0x584D1);
    for (int i = 0; i < 9; i++) DrawCentred(uint16_t(0xE200 + i * 0x100), 0x584E1 + i * 16);
    U16 col{0xFF06E2}, row{0xFF06E4}, len{0xFF06E6};
    col = 3;
    row = 8;
    len = 0;
    const uint32_t entry = 0xFF06E8;
    for (int i = 0; i < 9; i++) wr8(entry + i, rd8(password + i));
    wr8(entry + 9, 0);
    if (rd8(entry) != 0x5F) len = 9;
    DrawCentred(0xEB80, entry);
    U32{0xFF102C} = 0;

    for (;;) {
        WaitVBlank();
        sub_00079A();
        ReadPad();
        // Cursor: four corner sprites around the selected cell.
        U16 link{0xFF0042};
        link = 1;
        uint32_t a2 = 0xFF0044;
        uint16_t y = uint16_t(row * 16 + 0x9E), x = uint16_t(col * 16 + 0xE5);
        uint16_t y2 = uint16_t(y + 0xB), x2 = uint16_t(x + 6);
        if (row == 8) {
            x = rd16(0x58582 + col * 4);
            x2 = rd16(0x58584 + col * 4);
        }
        const struct { uint16_t y, attr, x; } corners[4] = {
            {y, 0xC243, x}, {y, 0xCA43, x2}, {y2, 0xD243, x}, {y2, 0xDA43, x2}};
        for (const auto& c : corners) {
            wr16(a2, c.y);
            wr16(a2 + 2, link);
            link += 1;
            wr16(a2 + 4, c.attr);
            wr16(a2 + 6, c.x);
            a2 += 8;
        }
        U32{0xFF003E} = a2;
        sub_001D18();
        wr32(vdp_queue_ptr, 0xFFFFFFFF);

        // Held directions repeat after 20 frames, then every 8 (counter
        // reaches 1, not 0, in this routine).
        auto rep = [](int bit, uint32_t counter) {
            if (!(pad_buttons >> bit & 1)) {
                wr8(counter, 0);
                return false;
            }
            if (!(rd8(0xFF0032) >> bit & 1)) {
                wr8(counter, 0x14);
                return true;
            }
            wr8(counter, uint8_t(rd8(counter) - 1));
            if (rd8(counter) != 1) return false;
            wr8(counter, 8);
            return true;
        };
        if (rep(2, 0xFF102D)) {
            if (row == 8) wr8(0xFF06E3, rd8(0x58572 + col));
            col = uint16_t((col - 1) & 7);
            sub_00D760(0x6B);
        }
        if (rep(3, 0xFF102C)) {
            if (row == 8) wr8(0xFF06E3, rd8(0x5857A + col));
            col = uint16_t((col + 1) & 7);
            sub_00D760(0x6B);
        }
        if (rep(0, 0xFF102E)) {
            row -= 1;
            if (int16_t(uint16_t(row)) < 0) row += 9;
            sub_00D760(0x6B);
        }
        if (rep(1, 0xFF102F)) {
            row += 1;
            if (row == 9) row = 0;
            sub_00D760(0x6B);
        }
        uint8_t pressed = pad_buttons & 0xF0;
        if (!pressed || (rd8(0xFF0032) & pressed) == pressed) continue;
        uint8_t c = rd8(0x585A2 + row * 8 + col);
        if (c == 0) return; // CANCEL
        if (c == 0x0D) { // DONE
            if (len == 0) {
                for (int i = 0; i < 9; i++) wr8(password + i, 0x5F);
                wr8(password + 9, 0);
                return;
            }
            if (len == 9 && sub_058A74(entry) != 0xFFFF) {
                for (int i = 0; i < 9; i++) wr8(password + i, rd8(entry + i));
                return;
            }
            DrawCentred(0xEB80, 0x584BE);
            for (int i = 0; i <= 0x28; i++) WaitVBlank();
            DrawCentred(0xEB80, entry);
            continue;
        }
        if (c == 0x7F) { // DEL
            if (!len) continue;
            len -= 1;
            wr8(entry + len, 0x5F);
        } else {
            if (len == 9) continue;
            wr8(entry + len, c);
            len += 1;
        }
        sub_00D760(0x69);
        DrawCentred(0xEB80, entry);
    }
}

} // namespace game
