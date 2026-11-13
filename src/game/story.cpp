// Story, briefing, game-over and ending screens (0xCAA4E): a picture on
// plane B with text scrolling up on plane A.
#include "game.hpp"

namespace game {

namespace {

constexpr uint32_t target_pal = 0xFF0776; // 4 palette lines; line 3 is the text colours
constexpr uint32_t text_pal = 0xCD47A;

U32 text_ptr{0xFF2700};
U16 scroll{0xFF2706};

struct Picture {
    uint32_t tiles;
    uint16_t ntiles;
    uint32_t map, palette;
    int palette_lines;
};

const Picture pic_a{0xCD49A, 0x401, 0xD54BA, 0xD5D7A, 1};
const Picture pic_b{0xD5DFA, 0x3C2, 0xDD63A, 0xDDEFA, 1};
const Picture pic_c{0xDDF7A, 0x441, 0xE679A, 0xE705A, 1};
const Picture pic_d{0xE70DA, 0x411, 0xEF2FA, 0xEFBBA, 2};
const Picture pic_ending{0x10201A, 0x43E, 0x10A7DA, 0x10B09A, 2};
const Picture pic_basement{0xF8E1A, 0x446, 0x1016DA, 0x101F9A, 1};

// 0x1F84E: copies one palette line.
void copy_line(uint32_t src, uint32_t dest) {
    for (int i = 0; i < 8; i++) wr32(dest + i * 4, rd32(src + i * 4));
}

void load_picture(const Picture& p) {
    LoadTilesAt(1, p.ntiles, p.tiles);
    LoadMap(p.map, 0xE000, 0x28, 0x1C, 0x80, 1);
}

void show_picture(const Picture& p) {
    load_picture(p);
    for (int i = 0; i < p.palette_lines; i++) copy_line(p.palette + i * 0x20, target_pal + i * 0x20);
    copy_line(text_pal, 0xFF07D6);
    FadeIn(target_pal, 4);
}

// 0xCB2F6: one line of text in two tile rows (glyph table 0xCD30E). Returns
// the address after the second row's string.
uint32_t draw_line(uint16_t nt, uint32_t text) {
    uint32_t glyphs = 0xCD30E, a0 = text;
    for (int row = 0; row < 2; row++, glyphs += 2, nt = uint16_t(nt + 0x100)) {
        vdp_write_addr(nt);
        a0 = text;
        for (uint8_t c; (c = rd8(a0++)) != 0;) vdp_data(rd16(glyphs + (c - 0x20) * 4));
    }
    return a0;
}

bool newly(int bit) { return (pad_buttons >> bit & 1) && !(rd8(0xFF0032) >> bit & 1); }

// 0xCB1E4: scrolls the text (records of 0x25 bytes, two strings each) up.
// Up speeds it up, Down or A pause it, Start skips.
void scroll_text(uint32_t text) {
    text_ptr = text;
    FillPlaneA(0);
    scroll = 0;
    SetVScrollA(0);
    for (;;) {
        int frames = (pad_buttons & 1) ? 1 : 4;
        for (int i = 0; i < frames; i++) WaitVBlank();
        for (;;) {
            ReadPad();
            if (newly(7)) return;
            if (pad_buttons & 0x42) break;
            uint16_t s = scroll;
            if (!(s & 0xF)) {
                uint32_t a0 = draw_line(uint16_t((((s & 0xFF) << 5) + 4) | 0xC000), text_ptr);
                if (!rd8(a0)) {
                    if (a0 == 0xCD30D) return;
                    text_ptr = 0xCD0BD; // blank lines until the text has scrolled off
                    continue;
                }
                text_ptr += 0x25;
            }
            scroll += 1;
            SetVScrollA(scroll);
            break;
        }
    }
}

void fade_out() { FadeOut(target_pal, 4); }

// 0xCB0BE: the last picture of the game-over sequence, flashing in from white.
void final_picture() {
    LoadTilesAt(1, 0x445, 0xEFC3A);
    LoadMap(0xF84DA, 0xE000, 0x28, 0x1C, 0x80, 1);
    FillPlaneA(0);
    for (int i = 0; i <= 0x10; i++) wr32(target_pal + i * 4, 0);
    FadeToWhite(target_pal, 1);
    sub_0C615A(0x98);
    copy_line(0xF8D9A, target_pal);
    copy_line(text_pal, 0xFF07D6);
    FadeFromWhite(target_pal, 4);
    LoadPaletteLine(0, target_pal);
    LoadPaletteLine(3, target_pal + 0x20);
    sub_0C615A(0x98);
    WaitFrames(10);
    sub_0C615A(0x98);
    for (int i = 0; i <= 0x384; i++) {
        WaitVBlank();
        ReadPad();
        if (newly(7)) break;
    }
}

} // namespace

// d0 = story: 0 opening, 1 high-rise, 2 basement, 3 attract mode (returns 1
// if Start was pressed), 4 ending, 5 game over.
uint16_t sub_0CAA4E(uint16_t story, uint32_t) {
    ClearCram();
    ClearVram();
    sub_01F578();
    vdp_ctrl(link_sr == 0x2100 ? 0x8B0B : 0x8B03);
    vdp_ctrl(0x9003);
    // 0xCAAC2: line scroll hides lines 0-24 and 201-255 of the text plane.
    for (int line = 0xFF; line >= 0; line--) {
        vdp_write_addr(uint16_t(0xBC00 + line * 4));
        vdp_data(line <= 0x18 || line > 0xC8 ? 0x140 : 0);
    }
    vdp_ctrl32(0x56000002);
    for (uint32_t a = 0x125116; a < 0x125116 + 0x4B0 * 4; a += 4) vdp_data32(rd32(a));

    switch (story & 0xFF) {
    case 0:
        PlaySound(3);
        show_picture(pic_a);
        scroll_text(0xCB3A8);
        if (!(pad_buttons & 0x80)) {
            fade_out();
            show_picture(pic_b);
            scroll_text(0xCB824);
        }
        fade_out();
        sub_0C62E8();
        return 0;
    case 1:
        PlaySound(3);
        show_picture(pic_b);
        scroll_text(0xCBB2F);
        if (!(pad_buttons & 0x80)) {
            fade_out();
            show_picture(pic_c);
            scroll_text(0xCBCC7);
        }
        fade_out();
        sub_0C62E8();
        return 0;
    case 2:
        PlaySound(3);
        show_picture(pic_d);
        scroll_text(0xCBECF);
        fade_out();
        sub_0C62E8();
        return 0;
    case 3: {
        show_picture(pic_a);
        scroll_text(0xCB3A8);
        uint16_t skipped = (pad_buttons & 0x80) ? 1 : 0;
        fade_out();
        return skipped;
    }
    case 4:
        PlaySound(0x89);
        show_picture(pic_ending);
        scroll_text(0xCBFD3);
        fade_out();
        sub_0C62E8();
        return 0;
    case 5:
        PlaySound(0x87);
        if (cur_area == 1 || cur_area == 2) {
            show_picture(cur_area == 1 ? pic_c : pic_basement);
            scroll_text(0xCBDF0);
            fade_out();
            final_picture();
        } else {
            show_picture(pic_b);
            scroll_text(0xCB9E1);
        }
        fade_out();
        sub_0C62E8();
        return 0;
    }
    untranslated(0x0CAA4E);
}

} // namespace game
