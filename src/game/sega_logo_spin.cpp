// Boot logo: the SEGA wall that swings into view (0x5711E).
#include "display.hpp"
#include "game.hpp"
#include "m68k.hpp"

#include "../host.hpp"

#include <algorithm>
#include <iterator>

namespace game {

// 0x1A34
void WaitVBlank() {
    uint16_t c = vblank_count;
    while (vblank_count == c) host::frame();
}

namespace {

// The 128x80 view the logo is drawn into, as the bytes of tiles 1-320
// (FF3926), and the wall height drawn in each column (FF6126).
uint8_t fb[320 * 32];
uint16_t depth[0x80];

// Height and screen column of a wall end (+C / +E of the edge records FF0E3C / FF0E4C).
struct Edge {
    uint16_t height, column;
};

// Palette cycle of colours 8-19 (22 steps).
void cycle_palette() {
    auto& step = st.frame;
    uint16_t d0 = uint16_t(step + 1);
    if (d0 >= 0x16) d0 = 0;
    step = d0;
    for (int i = 0; i < 12; i++) display::pal[8 + i] = kLogoCycle[d0 + i];
}

// Screen column and height of a wall end at the given angle.
void project_edge(Edge& e, uint16_t angle) {
    const uint16_t* dir = kDirVectors[angle & 0x1FF];
    uint16_t d3 = uint16_t(dir[1] + st.logo_dist);
    uint32_t d0 = ext_l(divs(ext_l(dir[0]) << 8, 0xB5)) << 6;
    d0 = divs(d0, d3);
    e.column = uint16_t(lo(d0) + 0x40);
    e.height = lo(divs(0x8000, d3));
}

// 0x5710E: the logo wall's face, 4 strips of (top, bottom) wall blocks.
const uint16_t kLogoStrips[8] = {0xF7, 0xFB, 0xF8, 0xFC, 0xF9, 0xFD, 0xFA, 0xFE};

// The pixels of wall block n (16 byte columns of 32 rows).
const uint8_t* wall_block(uint16_t n) { return assets::blocks("gfx/wallblocks.png", 16).data() + n * 0x200; }

// DrawWallSpan (0xCCB6) as the logo uses it: the strips of kLogoStrips from u 0
// to 0xFF, unshaded (table 0x4436), the view level and no door, over the
// background FF6226, which is all zero after ClearRam.
void draw_wall(const Edge& left, const Edge& right) {
    uint16_t h0 = left.height, h1 = right.height, x1 = right.column, x0 = left.column;
    if (int16_t(h0) < 0 || int16_t(h1) < 0 || int16_t(x1) < 0) return;
    if (int16_t(x0) < 0) x0 = 0;
    if (int16_t(x0) >= 0x80) return;
    if (int16_t(x1) >= 0x80) x1 = 0x7F;

    int strip = 0;
    const uint8_t *a1 = wall_block(kLogoStrips[strip]), *a2 = wall_block(kLogoStrips[strip + 1]);
    uint16_t x = x0;
    uint32_t a0 = (x & 3) + (x >> 2) * 0x140;
    uint16_t d0 = uint16_t(3 - (x & 3));
    int16_t d5 = int16_t(uint16_t(x1) - uint16_t(x0) + 1);
    if (d5 <= 0) return;
    uint32_t d2 = ext_l(h0) << 8;
    uint32_t d1 = ext_l(divs((ext_l(h1) << 8) - d2, uint16_t(d5)));
    uint32_t d6 = divs(ext_l(uint16_t(0xFF << 6)), uint16_t(d5));
    uint16_t tex_col = 0; // FF0E60: a whole strip in the high byte
    uint16_t tex_u = lo(d6); // FF0E5C: a texture column in the high byte

    // Moves to the next texture strip when the column counter overflows.
    auto next_strip = [&]() {
        if (!(tex_col >> 8)) return;
        strip += uint8_t((tex_col >> 8) << 2) / 2;
        tex_col &= 0xFF;
        uint16_t c = uint16_t(tex_col << 1);
        a1 = wall_block(kLogoStrips[strip]) + c;
        a2 = wall_block(kLogoStrips[strip + 1]) + c;
    };
    // Steps the texture by the column counter's high byte.
    auto next_column = [&]() {
        uint16_t u = uint16_t(int16_t(uint16_t(tex_u & 0xFF00)) >> 3);
        tex_u &= 0xFF;
        a1 += int16_t(u);
        a2 += int16_t(u);
        tex_col = uint16_t(tex_col + uint16_t(int16_t(u) >> 1));
        next_strip();
    };
    if (tex_u > 0xFF) next_column();

    do {
        uint16_t h = uint16_t(int32_t(d2) >> 8);
        if (h < depth[x])
            a0 += 0x140;
        else {
            depth[x] = h;
            if (h <= 0x50) {
                // 0x4436: 40 - i rows of background, i texels of each block, background.
                int i = h >> 1;
                for (int r = 0; r < 40 - i; r++, a0 += 4) fb[a0] = 0;
                for (int j = 0; j < i; j++, a0 += 4) fb[a0] = a1[(2 * j + 1) * 16 / i];
                for (int j = 0; j < i; j++, a0 += 4) fb[a0] = a2[(2 * j + 1) * 16 / i];
                for (int r = 0; r < 40 - i; r++, a0 += 4) fb[a0] = 0;
            } else {
                // Taller than the view: 40 rows up and 40 down from the middle.
                a0 += 0xA0;
                uint32_t up = a0;
                const uint8_t *top = a1 + 0x20, *bottom = a2;
                int16_t d3 = int16_t(h);
                uint8_t c5 = *--top, c6 = *bottom++;
                for (int n = 0; n < 40; n++) {
                    up -= 4;
                    fb[up] = c5;
                    fb[a0] = c6;
                    a0 += 4;
                    d3 = int16_t(d3 - 0x40);
                    if (d3 < 0) {
                        c5 = *--top;
                        c6 = *bottom++;
                        d3 = int16_t(d3 + int16_t(h));
                    }
                }
            }
        }
        // 0xCEB4: the next screen column and texture column.
        d2 += d1;
        x++;
        tex_u = uint16_t(tex_u + lo(d6));
        if (tex_u >> 8) next_column();
        a0 -= 0x13F;
        if (int16_t(--d0) < 0) {
            d0 = 3;
            a0 += 0x13C;
        }
    } while (--d5 != 0);
}

void draw_logo_wall(const Edge& a, const Edge& b) {
    st.walls_shaded = 0;
    st.backdrop_image = 0;
    st.sprite_shades = 0;
    st.nv_slot = 0;
    st.light_slot = 0;
    draw_wall(a, b);
}

void next_frame() {
    fb_dma_pending = 2;
    WaitVBlank();
    cycle_palette();
}

void clear_view() {
    std::fill(std::begin(depth), std::end(depth), 0);
    while (fb_dma_pending) host::frame();
    std::fill(std::begin(fb), std::end(fb), 0);
}

// Shows the wall drawn into the framebuffer: its bytes are VRAM tile bytes
// for tiles 1-320 (the VBlank DMAs of the original).
void show_fb() {
    for (int t = 0; t < 320; t++)
        for (int i = 0; i < 32; i++) {
            uint8_t b = fb[t * 32 + i];
            display::tiles[1 + t][i * 2] = uint8_t(b >> 4);
            display::tiles[1 + t][i * 2 + 1] = uint8_t(b & 15);
        }
}

} // namespace

// 0x5711E
void SpinningSegaLogo() {
    fb_dma_pending = 0;
    ClearPlaneA();
    ClearPlaneB();
    for (int i = 0; i < 16; i++) display::pal[i] = assets::palette("palettes/sega_logo_spin.png")[i];
    for (int t = 1; t <= 320; t++)
        for (int i = 0; i < 64; i++) display::tiles[t][i] = 0;
    std::fill(std::begin(fb), std::end(fb), 0);
    // Plane B: 32 x 10 tiles from tile 1, column-major, high priority.
    uint16_t tile = 0x8001;
    for (uint32_t col = 0; col < 32; col++) {
        uint16_t a = uint16_t(0xC488 + col * 2);
        for (int row = 0; row < 10; row++, a = uint16_t(a + 0x80)) display::write_cell(a, tile++);
    }
    st.dma_count = 0;
    auto& angle = st.logo_angle;
    auto& dist = st.logo_dist;
    angle = 0;
    dist = 0xE00;
    Edge a2, a3;
    // PC: any press skips each boot cinematic (here and in intro.cpp).
    host::any_pressed();
    auto skip = [] {
        if (!host::any_pressed()) return false;
        while (fb_dma_pending) host::frame();
        return true;
    };
    do {
        if (skip()) return;
        next_frame();
        angle -= 8;
        uint16_t d0 = uint16_t(int16_t(uint16_t(dist) - 0x2B5) >> 4);
        if (d0 < 0x18) d0 = 0x18;
        d0 = uint16_t(dist - d0);
        if (d0 < 0x2B5) d0 = 0x2B5;
        dist = d0;
        clear_view();
        angle = uint16_t(((angle + 0xC0) & 0x7F) - 0xC0);
        project_edge(a2, angle);
        project_edge(a3, uint16_t(angle + 0x80));
        draw_logo_wall(a2, a3);
        project_edge(a2, uint16_t(angle + 0x80));
        project_edge(a3, uint16_t(angle + 0x100));
        draw_logo_wall(a2, a3);
        show_fb();
    } while (!(dist == 0x2B5 && angle == 0xFF40));
    // Hold the logo while the palette cycle finishes.
    int16_t d7 = 0xF;
    do {
        if (skip()) return;
        next_frame();
        clear_view();
        a2 = {0x40, 0x20};
        a3 = {0x40, 0x5F};
        draw_logo_wall(a2, a3);
        show_fb();
    } while (--d7 >= 0 || st.frame != 0);
    for (int i = 0; i < 0x1F && !skip(); i++) WaitVBlank();
}

} // namespace game
