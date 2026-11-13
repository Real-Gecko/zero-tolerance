// Character select (0xC6362). Up/Down scroll between two nametable pages
// (0xE082 / 0xF082 on a 64x64 plane); FF1030 is the chosen character.
#include "game.hpp"

namespace game {

namespace {

U16 character{0xFF1030};
U16 page{0xFF2704};
U16 scroll{0xFF2706};
constexpr uint32_t portraits = 0xC634E;   // 5 image resources
constexpr uint32_t portrait_vram = 0xFF26F6; // VRAM address of each portrait's tiles
constexpr uint32_t alive = 0xFF28EC;      // 1 = character still available

void draw_text(uint16_t nt, uint32_t text) {
    for (int row = 0; row < 2; row++, nt = uint16_t(nt + 0x80)) {
        vdp_write_addr(nt);
        for (uint32_t a = text; rd8(a); a++) vdp_data(rd16(0xC7196 + row * 2 + (rd8(a) - 0x20) * 4));
    }
}

void copy_line(uint32_t src, uint32_t dest) {
    for (int i = 0; i < 8; i++) wr32(dest + i * 4, rd32(src + i * 4));
}

// 0x1F7DA: w x h pixels of tiles picked by the index list idx from src.
void load_tile_list(uint16_t tile, uint8_t w, uint8_t h, uint32_t src, uint32_t idx) {
    int n = (int8_t(w) >> 3) * (int8_t(h) >> 3);
    for (int i = 0; i < n; i++, idx += 2) LoadTilesAt(tile++, 1, src + (uint32_t(rd16(idx)) << 5));
}

// 0xC6686: the frame sprites, moved with the scroll.
void sub_0C6686() {
    uint16_t d5 = uint16_t(0x1FF - (scroll & 0x1FF));
    static const struct { uint16_t size_link, x, y, attr; } s[14] = {
        {0xB01, 0x1A0, 0x0A8, 0xA546}, {0xB02, 0x1A0, 0x110, 0xB546}, {0xB03, 0x1A0, 0x1A8, 0xA546},
        {0xB04, 0x1A0, 0x210, 0xB546}, {0xF05, 0x19C, 0x0DC, 0xA552}, {0xF06, 0x19C, 0x1DC, 0xA552},
        {0x007, 0x088, 0x089, 0xE562}, {0x008, 0x088, 0x0E1, 0xF562}, {0x009, 0x100, 0x089, 0xED62},
        {0x00A, 0x100, 0x0E1, 0xFD62}, {0x00B, 0x088, 0x189, 0xE562}, {0x00C, 0x088, 0x1E1, 0xF562},
        {0x00D, 0x100, 0x189, 0xED62}, {0x000, 0x100, 0x1E1, 0xFD62},
    };
    for (int i = 0; i < 14; i++) {
        vdp_ctrl32((uint32_t(i) << 19) + U32{0xFF06F2});
        vdp_data(uint16_t((s[i].y + d5) & 0x1FF));
        vdp_data(s[i].size_link);
        vdp_data(s[i].attr);
        vdp_data(s[i].x);
    }
}

// 0xC6520
void sub_0C6520() {
    copy_line(0xC7322, 0xFF0776);
    copy_line(0xC7E72, 0xFF0796);
    copy_line(0xCA972, 0xFF07B6);
    copy_line(0xC7302, 0xFF07D6);
    uint16_t vram = 0x20;
    for (int i = 0; i < 5; i++) {
        uint32_t res = rd32(portraits + i * 4);
        wr16(portrait_vram + i * 2, vram);
        uint16_t next = uint16_t(vram + (rd16(res) << 5));
        LoadTiles(vram, res);
        vram = next;
    }
    vdp_ctrl32(0x56000002);
    for (uint32_t a = 0x125116; a < 0x125116 + 0x4B0 * 4; a += 4) vdp_data32(rd32(a));
    vdp_ctrl32(0x74400001);
    for (uint32_t a = 0xC7EF2; a < 0xC7EF2 + 0x870 * 4; a += 4) vdp_data32(rd32(a));
    load_tile_list(0x546, 0x18, 0x20, 0xC7342, 0xC76A2);
    load_tile_list(0x552, 0x20, 0x20, 0xC7342, 0xC76F2);
    vdp_ctrl32(0x6C400002);
    for (uint32_t a = 0xCA9F2; a < 0xCA9F2 + 32; a += 4) vdp_data32(rd32(a));
    sub_0C6686();
    SetVScrollA(0);
    SetVScrollB(0);
    draw_text(0xEE00, 0xC716C);
    draw_text(0xFE00, 0xC716C);
}

void draw_portrait(uint16_t nt) {
    sub_002022(rd16(portrait_vram + character * 2), nt, 0, rd32(portraits + character * 4));
}

// 0xC6934: name and description of the character.
void sub_0C6934() {
    uint32_t a0 = 0xC6B36 + character * 0x13E;
    uint16_t nt = page == 0xE082 ? 0xE0A4 : 0xF0A4;
    for (int i = 0; i < 6; i++, a0 += 0x12, nt = uint16_t(nt + 0x100)) draw_text(nt, a0);
    nt = page == 0xE082 ? 0xE682 : 0xF682;
    for (int i = 0; i < 6; i++, a0 += 0x23, nt = uint16_t(nt + 0x100)) draw_text(nt, a0);
}

// 0xC6A16: overlay on a character who has been killed.
void sub_0C6A16() {
    LoadMap(0xCA0B2, page == 0xE082 ? 0xC000 : 0xD000, 0x28, 0x1C, 0x40, 0x43A2);
}

// 0xC6A50: clears the page scrolled away from. For page 0xF082 the original
// computes the address from leftover registers, which always gives 0xC004.
void sub_0C6A50() {
    vdp_write_addr(page == 0xE082 ? 0xD000 : 0xC004);
    for (int i = 0; i < 0x800; i++) vdp_data(0);
}

// 0xC683C / 0xC6896: draws the new character on the other page and scrolls to it.
void change_character(int16_t step) {
    page = page == 0xE082 ? 0xF082 : 0xE082;
    draw_portrait(page);
    sub_0C6934();
    if (rd8(alive + character) != 1) sub_0C6A16();
    for (int i = 0; i < 0x20; i++) {
        WaitVBlank();
        scroll += uint16_t(step);
        SetVScrollB(scroll);
        SetVScrollA(scroll);
        sub_0C6686();
    }
    sub_0C6A50();
}

bool newly(int bit) { return (pad_buttons >> bit & 1) && !(rd8(0xFF0032) >> bit & 1); }

} // namespace

void sub_0C6362(uint32_t) {
    ClearCram();
    ClearVram();
    sub_01F578();
    vdp_ctrl(0x9011);
    scroll = 0;
    sub_0C6520();
    page = 0xE082;
    draw_portrait(0xE082);
    sub_0C6934();
    if (rd8(alive + character) != 1) sub_0C6A16();
    // 0xC69B8
    draw_text(page == 0xE082 ? 0xEB82 : 0xFB82, 0xC6B36 + character * 0x13E + 0x11B);
    FadeIn(0xFF0776, 4);
    for (;;) {
        WaitVBlank();
        ReadPad();
        if (newly(0)) {
            sub_0C615A(0x6B);
            character += 1;
            if (character >= 5) character = 0;
            change_character(8);
            continue;
        }
        if (newly(1)) {
            sub_0C615A(0x6B);
            character -= 1;
            if (int16_t(uint16_t(character)) < 0) character = 4;
            change_character(-8);
            continue;
        }
        if (!(newly(6) || newly(4) || newly(5) || newly(7))) continue;
        if (rd8(alive + character) != 1) {
            sub_0C615A(0x69);
            continue;
        }
        sub_0C615A(0x6B);
        FadeOut(0xFF0776, 4);
        return;
    }
}

} // namespace game
