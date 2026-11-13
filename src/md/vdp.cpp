#include "vdp.hpp"

#include "mem.hpp"

#include <cstring>

namespace md::vdp {

uint8_t vram[0x10000];
uint16_t cram[64];
uint16_t vsram[40];
uint8_t reg[32];

static bool pending = false;
static uint8_t code = 0;
static uint16_t addr = 0;
static bool fill_pending = false;
static bool in_vblank = false;
static uint16_t hv_counter = 0;

void reset() {
    std::memset(vram, 0, sizeof vram);
    std::memset(cram, 0, sizeof cram);
    std::memset(vsram, 0, sizeof vsram);
    std::memset(reg, 0, sizeof reg);
    pending = fill_pending = false;
    code = 0;
    addr = 0;
    hv_counter = 0;
}

int width() { return (reg[12] & 0x81) ? 320 : 256; }
int height() { return (reg[1] & 0x08) ? 240 : 224; }
bool vint_enabled() { return reg[1] & 0x20; }
void set_vblank(bool on) { in_vblank = on; }

static void write_target(uint16_t v) {
    switch (code & 0x0F) {
    case 1: {
        uint16_t a = addr;
        if (a & 1) v = uint16_t(v << 8 | v >> 8);
        vram[a & 0xFFFE] = uint8_t(v >> 8);
        vram[(a & 0xFFFE) + 1] = uint8_t(v);
        break;
    }
    case 3: cram[(addr >> 1) & 0x3F] = v & 0x0EEE; break;
    case 5:
        if (((addr >> 1) & 0x3F) < 40) vsram[(addr >> 1) & 0x3F] = v & 0x07FF;
        break;
    }
    addr = uint16_t(addr + reg[15]);
}

static uint32_t dma_length() {
    uint32_t len = reg[19] | reg[20] << 8;
    return len ? len : 0x10000;
}

static void dma_68k() {
    uint32_t src = (uint32_t(reg[23] & 0x7F) << 17) | (reg[22] << 9) | (reg[21] << 1);
    uint32_t len = dma_length();
    for (uint32_t i = 0; i < len; i++) {
        write_target(rd16(src));
        src = (src & 0xFE0000) | ((src + 2) & 0x1FFFF);
    }
    reg[19] = reg[20] = 0;
    reg[21] = uint8_t(src >> 1);
    reg[22] = uint8_t(src >> 9);
    reg[23] = uint8_t((reg[23] & 0x80) | ((src >> 17) & 0x7F));
}

static void dma_copy() {
    uint16_t src = uint16_t(reg[21] | reg[22] << 8);
    uint32_t len = dma_length();
    for (uint32_t i = 0; i < len; i++) {
        vram[addr ^ 1] = vram[src ^ 1];
        src++;
        addr = uint16_t(addr + reg[15]);
    }
    reg[19] = reg[20] = 0;
}

void write_ctrl(uint16_t v) {
    if (!pending) {
        if ((v & 0xC000) == 0x8000) {
            reg[(v >> 8) & 0x1F] = uint8_t(v);
            code = 0;
            return;
        }
        code = uint8_t((code & 0x3C) | (v >> 14));
        addr = uint16_t((addr & 0xC000) | (v & 0x3FFF));
        pending = true;
        return;
    }
    pending = false;
    code = uint8_t((code & 0x03) | ((v >> 2) & 0x3C));
    addr = uint16_t((addr & 0x3FFF) | ((v & 3) << 14));
    if ((code & 0x20) && (reg[1] & 0x10)) {
        switch (reg[23] >> 6) {
        case 0:
        case 1: dma_68k(); break;
        case 2: fill_pending = true; break;
        case 3: dma_copy(); break;
        }
    }
}

void write_data(uint16_t v) {
    pending = false;
    if (fill_pending) {
        fill_pending = false;
        write_target(v);
        uint32_t len = dma_length();
        for (uint32_t i = 0; i < len; i++) {
            vram[addr ^ 1] = uint8_t(v >> 8);
            addr = uint16_t(addr + reg[15]);
        }
        reg[19] = reg[20] = 0;
        return;
    }
    write_target(v);
}

uint16_t read_data() {
    pending = false;
    uint16_t v = 0;
    switch (code & 0x0F) {
    case 0: v = uint16_t(vram[addr & 0xFFFE] << 8 | vram[(addr & 0xFFFE) + 1]); break;
    case 8: v = cram[(addr >> 1) & 0x3F]; break;
    case 4: v = vsram[((addr >> 1) & 0x3F) % 40]; break;
    }
    addr = uint16_t(addr + reg[15]);
    return v;
}

uint16_t read_ctrl() {
    pending = false;
    return uint16_t(0x3600 | (in_vblank ? 0x0008 : 0));
}

uint16_t read_hv() {
    hv_counter = uint16_t(hv_counter * 0x6255 + 0x3619);
    return hv_counter;
}

// ---- renderer ----

static uint32_t color(uint16_t c) {
    static const uint8_t lv[8] = {0, 0x34, 0x57, 0x74, 0x90, 0xAC, 0xCE, 0xFF};
    return 0xFF000000u | uint32_t(lv[(c >> 1) & 7]) << 16 | uint32_t(lv[(c >> 5) & 7]) << 8 | lv[(c >> 9) & 7];
}

// Pixel of a tile: returns colour index 0-15 (0 = transparent).
static int tile_pixel(uint16_t entry, int x, int y, int tile_h) {
    if (entry & 0x0800) x = 7 - x;
    if (entry & 0x1000) y = tile_h - 1 - y;
    uint32_t base = uint32_t(entry & 0x07FF) * 32;
    uint8_t b = vram[(base + y * 4 + (x >> 1)) & 0xFFFF];
    return (x & 1) ? (b & 15) : (b >> 4);
}

static uint16_t vram16(uint32_t a) {
    a &= 0xFFFE;
    return uint16_t(vram[a] << 8 | vram[a + 1]);
}

// Output per pixel: bits 0-5 colour (line*16+index), bit 6 priority, 0 = transparent.
static void render_plane(int line, bool plane_a, uint8_t* out, int w) {
    static const int sizes[4] = {32, 64, 32, 128};
    int pw = sizes[reg[16] & 3], ph = sizes[(reg[16] >> 4) & 3];
    uint32_t nt = plane_a ? (reg[2] & 0x38) << 10 : (reg[4] & 7) << 13;
    uint32_t hs_base = (reg[13] & 0x3F) << 10;
    int hmode = reg[11] & 3;
    int hline = hmode == 0 ? 0 : hmode == 2 ? (line & ~7) : line;
    int hscroll = vram16(hs_base + hline * 4 + (plane_a ? 0 : 2)) & 0x3FF;
    bool vcol = reg[11] & 4;
    for (int x = 0; x < w; x++) {
        int vs = vcol ? vsram[((x >> 4) * 2 + (plane_a ? 0 : 1)) % 40] : vsram[plane_a ? 0 : 1];
        int px = (x - hscroll) & (pw * 8 - 1);
        int py = (line + vs) & (ph * 8 - 1);
        uint16_t e = vram16(nt + ((py >> 3) * pw + (px >> 3)) * 2);
        int c = tile_pixel(e, px & 7, py & 7, 8);
        out[x] = c ? uint8_t((((e >> 13) & 3) * 16 + c) | ((e & 0x8000) ? 0x40 : 0) | 0x80) : 0;
    }
}

static bool window_covers(int line, int x) {
    int wh = reg[17], wv = reg[18];
    int wx = (wh & 0x1F) * 16, wy = (wv & 0x1F) * 8;
    bool in_x = (wh & 0x80) ? x >= wx : x < wx;
    bool in_y = (wv & 0x80) ? line >= wy : line < wy;
    if ((wh & 0x1F) == 0 && !(wh & 0x80)) in_x = false;
    if ((wv & 0x1F) == 0 && !(wv & 0x80)) in_y = false;
    return in_x || in_y;
}

static void render_window(int line, uint8_t* out, int w) {
    uint32_t nt = (reg[3] & (w == 320 ? 0x3C : 0x3E)) << 10;
    int pw = w == 320 ? 64 : 32;
    for (int x = 0; x < w; x++) {
        if (!window_covers(line, x)) continue;
        uint16_t e = vram16(nt + ((line >> 3) * pw + (x >> 3)) * 2);
        int c = tile_pixel(e, x & 7, line & 7, 8);
        out[x] = c ? uint8_t((((e >> 13) & 3) * 16 + c) | ((e & 0x8000) ? 0x40 : 0) | 0x80) : 0x80 | ((e & 0x8000) ? 0x40 : 0);
    }
}

static void render_sprites(int line, uint8_t* out, int w) {
    uint32_t sat = (reg[5] & (w == 320 ? 0x7E : 0x7F)) << 9;
    int max_sprites = w == 320 ? 80 : 64, max_line = w == 320 ? 20 : 16, max_px = w;
    int idx = 0, count = 0, pixels = 0;
    for (int n = 0; n < max_sprites; n++) {
        uint32_t s = sat + idx * 8;
        int y = (vram16(s) & 0x3FF) - 128;
        int size = vram[(s + 2) & 0xFFFF];
        int link = vram[(s + 3) & 0xFFFF] & 0x7F;
        uint16_t e = vram16(s + 4);
        int x = (vram16(s + 6) & 0x1FF) - 128;
        int sw = ((size >> 2) & 3) + 1, sh = (size & 3) + 1;
        if (line >= y && line < y + sh * 8) {
            if (++count > max_line) break;
            int row = line - y;
            if (e & 0x1000) row = sh * 8 - 1 - row;
            for (int cx = 0; cx < sw * 8; cx++) {
                if (pixels++ >= max_px) break;
                int sx = x + cx;
                if (sx < 0 || sx >= w) continue;
                int col = (e & 0x0800) ? sw * 8 - 1 - cx : cx;
                int tile = (e & 0x07FF) + (col >> 3) * sh + (row >> 3);
                uint32_t base = uint32_t(tile & 0x7FF) * 32;
                uint8_t b = vram[(base + (row & 7) * 4 + ((col & 7) >> 1)) & 0xFFFF];
                int c = (col & 1) ? (b & 15) : (b >> 4);
                if (c && !(out[sx] & 0x80))
                    out[sx] = uint8_t((((e >> 13) & 3) * 16 + c) | ((e & 0x8000) ? 0x40 : 0) | 0x80);
            }
        }
        if (!link) break;
        idx = link;
    }
}

void render(uint32_t* out, int pitch) {
    int w = width(), h = height();
    uint32_t pal[64];
    for (int i = 0; i < 64; i++) pal[i] = color(cram[i]);
    uint32_t bg = pal[reg[7] & 0x3F];
    uint8_t a[320], b[320], s[320];
    for (int line = 0; line < h; line++) {
        uint32_t* o = out + line * pitch;
        if (!(reg[1] & 0x40)) {
            for (int x = 0; x < w; x++) o[x] = 0xFF000000u;
            continue;
        }
        std::memset(a, 0, sizeof a);
        std::memset(s, 0, sizeof s);
        render_plane(line, false, b, w);
        render_plane(line, true, a, w);
        render_window(line, a, w);
        render_sprites(line, s, w);
        for (int x = 0; x < w; x++) {
            uint8_t p = 0;
            auto take = [&](uint8_t v, bool prio) {
                if (!p && (v & 0x80) && (v & 15) && bool(v & 0x40) == prio) p = v;
            };
            take(s[x], true);
            take(a[x], true);
            take(b[x], true);
            take(s[x], false);
            take(a[x], false);
            take(b[x], false);
            o[x] = p ? pal[p & 0x3F] : bg;
        }
    }
}

} // namespace md::vdp
