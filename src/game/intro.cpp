// Boot screens: logo 2, the SEGA logo and the intro animation (0x1CDEE,
// 0x1EF50, 0x52C7E), and the VDP helpers they share.
#include "game.hpp"
#include "m68k.hpp"

#include "../host.hpp"

namespace game {

void vdp_write_addr(uint16_t a) { vdp_ctrl32(uint32_t((a & 0x3FFF) | 0x4000) << 16 | (a >> 14)); }

// 0x1F72: image resource (u16 ntiles, u16 tile offset, u16 w, u16 h, map)
// with tiles at VRAM vram and the map at nametable address nt (64 cells per
// row); attr is added to every map entry. Returns the VRAM address after the
// tiles.
uint16_t LoadImageRes(uint16_t vram, uint16_t nt, uint16_t attr, uint32_t res) {
    sub_002022(vram, nt, attr, res);
    return LoadTiles(vram, res);
}

// 0x1FC0
uint16_t LoadTiles(uint16_t vram, uint32_t& res) {
    vdp_write_addr(vram);
    uint16_t n = rd16(res);
    vram = uint16_t(vram + (n << 5));
    res += ext_l(rd16(res + 2));
    for (int i = 0; i < n * 8; i++, res += 4) vdp_data32(rd32(res));
    return vram;
}

// 0x2022: only the map of an image resource.
void sub_002022(uint16_t vram, uint16_t nt, uint16_t attr, uint32_t res) {
    uint32_t map = res + 8;
    uint16_t base = uint16_t((vram >> 5) + attr);
    uint16_t w = rd16(res + 4), h = rd16(res + 6);
    for (int y = 0; y < h; y++, nt = uint16_t(nt + 0x80)) {
        vdp_write_addr(nt);
        for (int x = 0; x < w; x++, map += 2) vdp_data(uint16_t(rd16(map) + base));
    }
}

// 0x1CC04: steps one colour component of the count colours at FF06F6 by 2
// towards the palette at FF07F8, every other call. FF07F6 counts the calls
// since the last change, up to 20.
void sub_01CC04(int count) {
    bool changed = false;
    U16 idle{0xFF07F6};
    uint8_t toggle = rd8(0xFF07FD);
    wr8(0xFF07FD, toggle ^ 2);
    if (!(toggle & 2) && idle != 0x14) {
        uint32_t cur = 0xFF06F6, target = rd32(0xFF07F8);
        uint8_t comp = uint8_t(rd8(0xFF07FC) + 1);
        if (comp == 3) comp = 0;
        wr8(0xFF07FC, comp);
        // Component 0 = bits 1-3, 1 = bits 5-7, 2 = bits 9-11; the other two are kept.
        static const int shift[3] = {0, 4, 8};
        static const uint16_t keep[3] = {0xEE0, 0xE0E, 0x0EE};
        int k = comp == 1 ? 1 : comp == 2 ? 2 : 0;
        int s = shift[k];
        for (int i = 0; i < count; i++, cur += 2, target += 2) {
            uint16_t c = rd16(cur);
            int16_t t = int16_t((rd16(target) >> s) & 0xE), v = int16_t((c >> s) & 0xE);
            if (v != t) {
                changed = true;
                v = int16_t(v > t ? v - 2 : v + 2);
            }
            wr16(cur, uint16_t((c & keep[k]) | (uint16_t(v) << s)));
        }
    }
    if (changed) idle = 0;
    else if (idle != 0x14) idle += 1;
}

// 0x1CDEE: second boot logo, fading in and out, then the SEGA logo and intro.
void sub_01CDEE() {
    WaitVBlank();
    vdp_ctrl32(0x7C000002);
    vdp_data32(0);
    vdp_ctrl32(0x40000010);
    vdp_data32(0);
    sub_000BF2();
    vdp_ctrl32(0xC0000000);
    for (int i = 0; i < 0x20; i++) vdp_data32(0);
    vdp_ctrl32(0x60000003);
    for (int i = 0; i < 0x800; i++) vdp_data32(0);
    vdp_ctrl32(0x40000003);
    for (int i = 0; i < 0x800; i++) vdp_data32(0);
    LoadImageRes(0, 0xE000, 0x8000, 0x116DBE);
    for (uint32_t a = 0xFF06F6; a < 0xFF0716; a += 4) wr32(a, 0);
    auto fade = [](uint32_t palette, uint16_t frames) {
        U32{0xFF07F8} = palette;
        wr8(0xFF07FC, 0);
        U16{0xFF07F6} = 0;
        U16 left{0xFF07FE};
        left = frames;
        do {
            WaitVBlank();
            if (!U16{0xFF07F6}) {
                vdp_ctrl32(0xC0000000);
                for (uint32_t a = 0xFF06F6; a < 0xFF0716; a += 4) vdp_data32(rd32(a));
            }
            sub_01CC04(16);
            left -= 1;
        } while (left);
    };
    fade(0x1CD8E, 0xB4);
    fade(0x1CCEE, 0x3C);
    sub_01EF50();
}

// 0x1F578: VDP registers for the intro screens.
void sub_01F578() {
    for (uint16_t r : {0x8004, 0x8164, 0x8230, 0x8330, 0x8407, 0x855C, 0x8700, 0x8A00}) vdp_ctrl(r);
    vdp_ctrl(link_sr == 0x2100 ? 0x8B08 : 0x8B00);
    for (uint16_t r : {0x8C81, 0x8D2F, 0x8F02, 0x9011, 0x9100, 0x9200}) vdp_ctrl(r);
    U32{0xFF06F2} = 0x78000002; // 0x1F5D6: VRAM write command for the sprite table at 0xB800
}

// 0x1F5FE: waits n frames.
void WaitFrames(uint16_t n) {
    uint16_t target = uint16_t(vblank_count + n);
    while (int16_t(target) > int16_t(uint16_t(vblank_count))) host::frame();
}

// 0x1F610
void ClearCram() {
    vdp_ctrl32(0xC0000000);
    for (int i = 0; i < 0x40; i++) vdp_data(0);
}

// 0x1F62C
void ClearVram() {
    vdp_ctrl32(0x40000000);
    for (int i = 0; i < 0x8000; i++) vdp_data(0);
}

// 0x1F648: 16 colours from src to palette line.
void LoadPaletteLine(int line, uint32_t src) {
    vdp_ctrl32(uint32_t(((line & 3) << 5) | 0xC000) << 16);
    for (int i = 0; i < 16; i++, src += 2) vdp_data(rd16(src));
}

// 0x1F66C: n tiles from src to VRAM tile index tile.
void LoadTilesAt(uint16_t tile, uint16_t n, uint32_t src) {
    vdp_write_addr(uint16_t(tile << 5));
    for (int i = 0; i < n * 8; i++, src += 4) vdp_data32(rd32(src));
}

// 0x1F698 / 0x1F6AA: vertical scroll of plane A / B.
void SetVScrollA(uint16_t v) { vdp_ctrl32(0x40000010); vdp_data(v); }
void SetVScrollB(uint16_t v) { vdp_ctrl32(0x40020010); vdp_data(v); }

// 0x1F70A: w x h map from src to VRAM address dest, pitch cells per row.
void LoadMap(uint32_t src, uint16_t dest, uint16_t w, uint16_t h, uint16_t pitch, uint16_t attr) {
    for (int y = 0; y < h; y++, dest = uint16_t(dest + pitch * 2)) {
        vdp_write_addr(dest);
        for (int x = 0; x < w; x++, src += 2) vdp_data(uint16_t(rd16(src) + attr));
    }
}

// 0x1F870: fades the colours at FF06F6 from black up to the palette at src
// (count colours), one step every delay frames. Like the original it returns
// before showing the final step.
void FadeIn(uint32_t src, uint16_t delay, int count) {
    LoadPaletteLine(0, 0x1FBFE);
    for (uint32_t a = 0xFF06F6; a < 0xFF0776; a += 4) wr32(a, 0);
    for (;;) {
        WaitFrames(delay);
        bool done = true;
        for (int i = 0; i < count; i++) {
            uint32_t c = 0xFF06F6 + i * 2;
            uint16_t t = rd16(src + i * 2);
            for (uint16_t m : {0xE00, 0x0E0, 0x00E}) {
                if ((t & m) != (rd16(c) & m)) wr16(c, uint16_t(rd16(c) + (m & -m)));
            }
            if (rd16(c) != t) done = false;
        }
        if (done) return;
        int left = count - 1;
        for (int line = 0;; line++) {
            LoadPaletteLine(line, 0xFF06F6 + line * 0x20);
            left -= 0x10;
            if (left < 0) break;
        }
    }
}

// 0x1F93A: shows the palette at src, then fades it to black.
void FadeOut(uint32_t src, uint16_t delay, int count) {
    LoadPaletteLine(0, src);
    for (int i = 0; i < 0x20; i++) wr32(0xFF06F6 + i * 4, rd32(src + i * 4));
    for (;;) {
        WaitFrames(delay);
        bool done = true;
        for (int i = 0; i < count; i++) {
            uint32_t c = 0xFF06F6 + i * 2;
            for (uint16_t m : {0xE00, 0x0E0, 0x00E}) {
                if (rd16(c) & m) wr16(c, uint16_t(rd16(c) - (m & -m)));
            }
            if (rd16(c)) done = false;
        }
        if (done) {
            ClearCram();
            return;
        }
        int left = count - 1;
        for (int line = 0;; line++) {
            LoadPaletteLine(line, 0xFF06F6 + line * 0x20);
            left -= 0x10;
            if (left < 0) break;
        }
    }
}

// Loads all four palette lines from the 64 colours at buf.
static void load_palette_lines(uint32_t buf, int count) {
    int left = count - 1;
    for (int line = 0;; line++) {
        LoadPaletteLine(line, buf + line * 0x20);
        left -= 0x10;
        if (left < 0) break;
    }
}

// 0x1F9F6: fades from white to the palette at src.
void FadeFromWhite(uint32_t src, uint16_t delay, int count) {
    const uint32_t buf = 0xFF86F6;
    LoadPaletteLine(0, 0x1FC1E);
    for (int i = 0; i < 0x20; i++) wr32(buf + i * 4, 0x0EEE0EEE);
    for (;;) {
        WaitFrames(delay);
        bool done = true;
        for (int i = 0; i < count; i++) {
            uint32_t c = buf + i * 2;
            uint16_t t = rd16(src + i * 2);
            for (uint16_t m : {0xE00, 0x0E0, 0x00E})
                if ((t & m) != (rd16(c) & m)) wr16(c, uint16_t(rd16(c) - (m & -m)));
            if (rd16(c) != t) done = false;
        }
        if (done) return;
        load_palette_lines(buf, count);
    }
}

// 0x1FAC8: fades the palette at src to white.
void FadeToWhite(uint32_t src, uint16_t delay, int count) {
    const uint32_t buf = 0xFF86F6;
    LoadPaletteLine(0, src);
    for (int i = 0; i < 0x20; i++) wr32(buf + i * 4, rd32(src + i * 4));
    for (;;) {
        WaitFrames(delay);
        bool done = true;
        for (int i = 0; i < count; i++) {
            uint32_t c = buf + i * 2;
            for (uint16_t m : {0xE00, 0x0E0, 0x00E})
                if ((rd16(c) & m) != m) wr16(c, uint16_t(rd16(c) + (m & -m)));
            if (rd16(c) != 0xEEE) done = false;
        }
        if (done) return;
        load_palette_lines(buf, count);
    }
}

// 0x1F75E: fills the plane A nametable with v.
void FillPlaneA(uint16_t v) {
    vdp_write_addr(0xC000);
    for (int i = 0; i < 0x1000; i++) vdp_data(v);
}

namespace {

void sub_01EF76() {
    ClearCram();
    ClearVram();
    sub_01F578();
}

// One frame of the VDP status VBlank flag going high (0x52D66).
void wait_vblank_flag() { host::frame(); }

// 0x52C7E: SEGA logo, 15 animation frames of 5 VBlanks.
void sega_logo(uint16_t vscroll) {
    vdp_ctrl(0x8174);
    vdp_ctrl(0x8F02);
    vdp_ctrl32(0x40000010);
    for (int i = 0; i < 40; i++) vdp_data(vscroll);
    vdp_ctrl32(0x40000000);
    for (int i = 0; i < 0x8000; i++) vdp_data(0);
    vdp_ctrl32(0xC0000000);
    wait_vblank_flag();
    for (uint32_t a = 0x52D80; a < 0x52DA0; a += 4) vdp_data32(rd32(a));
    for (uint16_t r : {0x8004, 0x8174, 0x8220, 0x8405, 0x857E, 0x8700, 0x8B00, 0x8C81, 0x8D1F, 0x9001, 0x9100})
        vdp_ctrl(r);
    vdp_ctrl32(0x40000000);
    for (uint32_t a = 0x53DA0; a < 0x53DA0 + 0xCC8 * 4; a += 4) vdp_data32(rd32(a));
    wait_vblank_flag();
    for (uint32_t frame = 0; frame < 15; frame++) {
        uint32_t src = 0x52DA0 + frame * 256;
        uint32_t cmd = 0x45080002;
        for (int row = 0; row < 4; row++, cmd += 0x800000) {
            vdp_ctrl32(cmd);
            for (int i = 0; i < 32; i++, src += 2) vdp_data(uint16_t(rd8(src + 1) << 8 | rd8(src)));
        }
        for (int i = 0; i < 5; i++) wait_vblank_flag();
    }
    for (int i = 0; i < 61; i++) wait_vblank_flag();
}

enum Op { Wait, Tiles, Map };
struct Step {
    Op op;
    uint16_t a, b;
    uint32_t addr;
};

// 0x1F016-0x1F576: the intro frames, double-buffered at tiles 1 and 0x2BC.
const Step intro_steps[] = {
    {Wait, 15, 0, 0},
    {Wait, 6, 0, 0},
    {Wait, 6, 0, 0},
    {Tiles, 700, 213, 0x2295E},
    {Map, 700, 0, 0x2209E},
    {Wait, 6, 0, 0},
    {Tiles, 1, 216, 0x24CBE},
    {Map, 1, 0, 0x243FE},
    {Wait, 6, 0, 0},
    {Tiles, 700, 213, 0x2707E},
    {Map, 700, 0, 0x267BE},
    {Wait, 6, 0, 0},
    {Tiles, 1, 235, 0x293DE},
    {Map, 1, 0, 0x28B1E},
    {Wait, 6, 0, 0},
    {Tiles, 700, 253, 0x2B9FE},
    {Map, 700, 0, 0x2B13E},
    {Wait, 6, 0, 0},
    {Tiles, 1, 230, 0x2E25E},
    {Map, 1, 0, 0x2D99E},
    {Wait, 6, 0, 0},
    {Tiles, 700, 218, 0x307DE},
    {Map, 700, 0, 0x2FF1E},
    {Wait, 6, 0, 0},
    {Tiles, 1, 216, 0x32BDE},
    {Map, 1, 0, 0x3231E},
    {Wait, 6, 0, 0},
    {Tiles, 700, 222, 0x34F9E},
    {Map, 700, 0, 0x346DE},
    {Wait, 6, 0, 0},
    {Tiles, 1, 231, 0x3741E},
    {Map, 1, 0, 0x36B5E},
    {Wait, 6, 0, 0},
    {Tiles, 700, 226, 0x399BE},
    {Map, 700, 0, 0x390FE},
    {Wait, 6, 0, 0},
    {Tiles, 1, 226, 0x3BEBE},
    {Map, 1, 0, 0x3B5FE},
    {Wait, 6, 0, 0},
    {Tiles, 700, 223, 0x3E3BE},
    {Map, 700, 0, 0x3DAFE},
    {Wait, 6, 0, 0},
    {Tiles, 1, 218, 0x4085E},
    {Map, 1, 0, 0x3FF9E},
    {Wait, 6, 0, 0},
    {Wait, 6, 0, 0},
    {Wait, 6, 0, 0},
    {Wait, 6, 0, 0},
    {Wait, 6, 0, 0},
    {Wait, 6, 0, 0},
    {Tiles, 700, 213, 0x42C5E},
    {Map, 700, 0, 0x4239E},
    {Wait, 6, 0, 0},
    {Tiles, 1, 240, 0x44FBE},
    {Map, 1, 0, 0x446FE},
    {Wait, 6, 0, 0},
    {Tiles, 700, 249, 0x4767E},
    {Map, 700, 0, 0x46DBE},
    {Wait, 6, 0, 0},
    {Tiles, 1, 251, 0x49E5E},
    {Map, 1, 0, 0x4959E},
    {Wait, 6, 0, 0},
    {Tiles, 700, 246, 0x4C67E},
    {Map, 700, 0, 0x4BDBE},
    {Wait, 6, 0, 0},
    {Tiles, 1, 227, 0x4EDFE},
    {Map, 1, 0, 0x4E53E},
    {Wait, 6, 0, 0},
    {Tiles, 700, 203, 0x5131E},
    {Map, 700, 0, 0x50A5E},
    {Wait, 6, 0, 0},
    {Wait, 6, 0, 0},

};

// 0x1EF94
void intro() {
    ClearCram();
    ClearVram();
    SetVScrollA(0xFFF0);
    // 0x1EFD2
    LoadTilesAt(1, 0xD9, 0x2057E);
    LoadMap(0x1FCBE, 0xC000, 0x28, 0x20, 0x40, 1);
    FadeIn(0x1FC3E, 5);
    for (const Step& s : intro_steps) {
        switch (s.op) {
        case Wait: WaitFrames(s.a); break;
        case Tiles: LoadTilesAt(s.a, s.b, s.addr); break;
        case Map: LoadMap(s.addr, 0xC000, 0x28, 0x20, 0x40, s.a); break;
        }
    }
    WaitFrames(0x78);
    FadeOut(0x1FC3E, 5);
}

} // namespace

// 0x1EF50
void sub_01EF50() {
    sub_01EF76();
    sega_logo(0x0002); // d0 still holds the low word of the command from 0x1F5D6
    sub_01EF76();
    intro();
    ClearVram();
}

} // namespace game
