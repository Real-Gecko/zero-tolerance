// Native 2D display, see display.hpp.
#include "display.hpp"

#include "game.hpp"

#include <cstring>

namespace game::display {

uint16_t pal[64];
uint8_t tiles[2048][64];
uint16_t planeA[128 * 64], planeB[128 * 64];
int pwA = 64, phA = 32, pwB = 64, phB = 32;
uint16_t ntA = 0xC000, ntB = 0xE000;
uint16_t hscrollA[H], hscrollB[H];
int hmode = 0;
uint16_t vscrollA = 0, vscrollB = 0;
uint8_t win_h = 0, win_v = 0;

namespace {

// Parsed sprite list (VDP attribute records from game RAM).
struct Sprite {
    int y, x, w, h; // cells w/h, screen x/y
    uint16_t entry; // tile entry (palette, flip, base index)
    uint8_t link;   // next sprite index
};
Sprite sprites[80];
int sprite_count = 0;

uint32_t color(uint16_t c) {
    static const uint8_t lv[8] = {0, 0x34, 0x57, 0x74, 0x90, 0xAC, 0xCE, 0xFF};
    return 0xFF000000u | uint32_t(lv[(c >> 1) & 7]) << 16 | uint32_t(lv[(c >> 5) & 7]) << 8 | lv[(c >> 9) & 7];
}

bool window_covers(int line, int x) {
    int wx = (win_h & 0x1F) * 16, wy = (win_v & 0x1F) * 8;
    bool in_x = (win_h & 0x80) ? x >= wx : x < wx;
    bool in_y = (win_v & 0x80) ? line >= wy : line < wy;
    if ((win_h & 0x1F) == 0 && !(win_h & 0x80)) in_x = false;
    if ((win_v & 0x1F) == 0 && !(win_v & 0x80)) in_y = false;
    return in_x || in_y;
}

// Indexed pixel of a plane cell, with scroll; bit 7 set when opaque.
uint8_t plane_pixel(const uint16_t* plane, int pw, int ph, int x, int line, const uint16_t* hscroll,
                    uint16_t vscroll) {
    int sl = line < 0 ? 0 : line > H - 1 ? H - 1 : line;
    int hline = hmode == 0 ? 0 : hmode == 2 ? (sl & ~7) : sl;
    int px = (x - (hscroll[hline] & 0x3FF)) & (pw * 8 - 1);
    int py = (line + vscroll) & (ph * 8 - 1);
    uint16_t e = plane[(py >> 3) * pw + (px >> 3)];
    int c = tile_pixel(e, px & 7, py & 7);
    return c ? uint8_t(c | (e & 0x8000 ? 0x40 : 0) | 0x80) : 0;
}

} // namespace

void reset() {
    std::memset(pal, 0, sizeof pal);
    clear_vram();
}

void clear_vram() {
    std::memset(tiles, 0, sizeof tiles);
    std::memset(planeA, 0, sizeof planeA);
    std::memset(planeB, 0, sizeof planeB);
    std::memset(hscrollA, 0, sizeof hscrollA);
    std::memset(hscrollB, 0, sizeof hscrollB);
    vscrollA = vscrollB = 0;
    sprite_count = 0;
}

void fill_a(uint16_t e, int n) {
    for (int i = 0; i < n && i < pwA * phA; i++) planeA[i] = e;
}

void fill_b(uint16_t e, int n) {
    for (int i = 0; i < n && i < pwB * phB; i++) planeB[i] = e;
}

// Plane and cell index of a nametable VRAM address.
static bool locate(uint16_t nt, uint16_t*& plane, int& pw, int& ph, uint32_t& cell) {
    for (int p = 0; p < 2; p++) {
        uint16_t base = p ? ntB : ntA;
        int w = p ? pwB : pwA, h = p ? phB : phA;
        if (nt >= base && nt < base + uint32_t(w * h * 2)) {
            plane = p ? planeB : planeA;
            pw = w;
            ph = h;
            cell = uint32_t(nt - base) / 2;
            return true;
        }
    }
    return false;
}

void write_cell(uint16_t nt, uint16_t e) {
    uint16_t* plane;
    int pw, ph;
    uint32_t cell;
    if (locate(nt, plane, pw, ph, cell)) plane[cell % uint32_t(pw * ph)] = e;
}

void fill_cells(uint16_t nt, int n, uint16_t e) {
    uint16_t* plane;
    int pw, ph;
    uint32_t cell;
    if (!locate(nt, plane, pw, ph, cell)) return;
    for (int i = 0; i < n; i++) plane[(cell + uint32_t(i)) % uint32_t(pw * ph)] = e;
}

void set_sprite(int i, uint16_t y, uint8_t size, uint8_t link, uint16_t entry, uint16_t x) {
    if (i < 0 || i >= 80) return;
    Sprite& sp = sprites[i];
    sp.y = (int16_t(y) & 0x3FF) - 128;
    sp.entry = entry;
    sp.x = (int16_t(x) & 0x1FF) - 128;
    sp.w = ((size >> 2) & 3) + 1;
    sp.h = (size & 3) + 1;
    sp.link = link;
    if (i >= sprite_count) sprite_count = i + 1;
}

void clear_sprites() { sprite_count = 0; }

uint32_t argb(int i) { return color(pal[i & 63]); }

int tile_pixel(uint16_t e, int x, int y) {
    x &= 7;
    y &= 7;
    if (e & 0x0800) x = 7 - x;
    if (e & 0x1000) y = 7 - y;
    int c = tiles[e & 0x7FF][y * 8 + x];
    return c ? ((e >> 13) & 3) * 16 + c : 0;
}

void upload_tiles(uint16_t tile, uint16_t n, const uint8_t* src) {
    for (uint32_t t = 0; t < n; t++) {
        uint8_t* d = tiles[(tile + t) & 0x7FF];
        for (int i = 0; i < 16; i++, src += 2) {
            uint16_t w = uint16_t(src[0] << 8 | src[1]);
            d[i * 4] = uint8_t(w >> 12 & 15);
            d[i * 4 + 1] = uint8_t(w >> 8 & 15);
            d[i * 4 + 2] = uint8_t(w >> 4 & 15);
            d[i * 4 + 3] = uint8_t(w & 15);
        }
    }
}

void tile_word(uint16_t tile, int i, uint16_t w) {
    uint8_t* d = tiles[tile & 0x7FF];
    d[i * 4] = uint8_t(w >> 12 & 15);
    d[i * 4 + 1] = uint8_t(w >> 8 & 15);
    d[i * 4 + 2] = uint8_t(w >> 4 & 15);
    d[i * 4 + 3] = uint8_t(w & 15);
}

void tile_long(uint16_t dest, uint32_t w) {
    uint8_t* d = tiles[(dest >> 5) & 0x7FF];
    int row = (dest >> 2) & 7;
    for (int i = 0; i < 8; i++) d[row * 8 + i] = uint8_t(w >> (28 - i * 4) & 15);
}
void blit_linear(uint16_t nt, const uint16_t* src, int w, int h, int stride_cells, uint16_t attr) {
    uint16_t* plane;
    int pw, ph;
    uint32_t cell;
    if (!locate(nt, plane, pw, ph, cell)) return;
    uint32_t size = uint32_t(pw * ph);
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++, src++) {
            uint32_t c = cell + uint32_t(y * stride_cells + x);
            if (c < size) plane[c] = uint16_t(*src + attr);
        }
}

void upload_sprites(const SatEntry* list, int n) {
    sprite_count = 0;
    for (const SatEntry* s = list; s < list + n && sprite_count < 80; s++) {
        Sprite& sp = sprites[sprite_count++];
        sp.y = (int16_t(s->y) & 0x3FF) - 128;
        uint8_t size = uint8_t(s->link >> 8);
        uint8_t link = uint8_t(s->link);
        sp.entry = s->attr;
        sp.x = (int16_t(s->x) & 0x1FF) - 128;
        sp.w = ((size >> 2) & 3) + 1;
        sp.h = (size & 3) + 1;
        sp.link = link;
        if (!link) break;
    }
}

void compose(uint32_t* out, int pitch, int x0, int w, const Layers& layers) {
    uint32_t cmap[64];
    for (int i = 0; i < 64; i++) cmap[i] = color(pal[i]);
    static uint8_t a[1024], b[1024], s[1024];
    for (int line = 0; line < H; line++) {
        uint32_t* o = out + line * pitch;
        std::memset(a, 0, size_t(w));
        std::memset(b, 0, size_t(w));
        std::memset(s, 0, size_t(w));
        if (layers.planes) {
            for (int x = 0; x < w; x++) {
                int sx = x0 + x;
                b[x] = plane_pixel(planeB, pwB, phB, sx, line, hscrollB, vscrollB);
                if (window_covers(line, sx)) {
                    uint16_t e = planeA[(line >> 3) * pwA + ((sx >> 3) & (pwA - 1))];
                    int c = tile_pixel(e, sx & 7, line & 7);
                    a[x] = c ? uint8_t(c | (e & 0x8000 ? 0x40 : 0) | 0x80)
                             : uint8_t(0x80 | (e & 0x8000 ? 0x40 : 0));
                } else
                    a[x] = plane_pixel(planeA, pwA, phA, sx, line, hscrollA, vscrollA);
            }
        }
        int count = 0, n = 0;
        for (int steps = 0; steps < 80 && n < sprite_count; steps++) {
            const Sprite& sp = sprites[n];
            n = sp.link;
            if (layers.sprite && !layers.sprite(sp.entry)) continue;
            if (line < sp.y || line >= sp.y + sp.h * 8) continue;
            if (++count > 20) break;
            int row = line - sp.y;
            if (sp.entry & 0x1000) row = sp.h * 8 - 1 - row;
            for (int cx = 0; cx < sp.w * 8; cx++) {
                int sx = sp.x + cx - x0;
                if (sx < 0 || sx >= w) continue;
                int col = (sp.entry & 0x0800) ? sp.w * 8 - 1 - cx : cx;
                // col/row are already flipped; keep the flip bits out of the tile lookup.
                int c = tile_pixel(uint16_t((sp.entry & ~0x1800) + (col >> 3) * sp.h + (row >> 3)), col & 7, row & 7);
                if (c && !(s[sx] & 0x80)) s[sx] = uint8_t(c | (sp.entry & 0x8000 ? 0x40 : 0) | 0x80);
            }
        }
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
            o[x] = p ? cmap[p & 0x3F] : layers.transparent ? 0 : cmap[0];
        }
    }
}

} // namespace game::display
