// Boot logo: the "TECHNOPOP" wall that swings into view (0x5711E).
#include "game.hpp"
#include "m68k.hpp"

#include "../host.hpp"

namespace game {

// 0x1A34
void WaitVBlank() {
    uint16_t c = vblank_count;
    while (vblank_count == c) host::frame();
}

namespace {

void clear_longs(uint32_t a, int n) {
    for (int i = 0; i < n; i++, a += 4) wr32(a, 0);
}

// Palette cycle of colours 4-15 (22 steps from 0x570CA).
void cycle_palette() {
    vdp_ctrl32(0xC0080000);
    U16 step{0xFF0EB6};
    uint16_t d0 = uint16_t(step + 1);
    if (d0 >= 0x16) d0 = 0;
    step = d0;
    uint32_t a0 = 0x570CA + d0 * 2;
    for (int i = 0; i < 6; i++, a0 += 4) vdp_data32(rd32(a0));
}

// Screen column and height of a wall end at the given angle.
void project_edge(uint32_t rec, uint16_t angle) {
    uint32_t a0 = 0x8124 + (angle & 0x1FF) * 4;
    uint16_t d3 = uint16_t(rd16(a0 + 2) + U16{0xFF0876});
    uint32_t d0 = ext_l(divs(ext_l(rd16(a0)) << 8, 0xB5)) << 6;
    d0 = divs(d0, d3);
    wr16(rec + 0xE, uint16_t(lo(d0) + 0x40));
    wr16(rec + 0xC, lo(divs(0x8000, d3)));
}

void draw_logo_wall(uint32_t a2, uint32_t a3) {
    U16{0xFF0E5C} = 0;
    U16{0xFF0E5E} = 0xFF;
    U32{0xFF0EAA} = 0x4436;
    U32{0xFF0EAE} = 0x14EE66;
    U32{0xFF1096} = 0xED50;
    U32{0xFF1174} = 0;
    U32{0xFF1178} = 0;
    U16{0xFF1172} = 0;
    U16{0xFF105E} = 0;
    U16{0xFF1060} = 0;
    U32{0xFF0E70} = 0x5710E;
    DrawWallSpan(a2, a3);
}

void next_frame() {
    fb_dma_pending = 2;
    WaitVBlank();
    cycle_palette();
}

void clear_view() {
    clear_longs(0xFF6126, 0x40);
    while (fb_dma_pending) host::frame();
    clear_longs(framebuffer, 0xA00);
}

} // namespace

void sub_05711E() {
    fb_dma_pending = 0;
    sub_000CAA();
    sub_000C5A();
    vdp_ctrl32(0xC0000000);
    for (uint32_t a = 0x570C2; a < 0x570C2 + 32; a += 4) vdp_data32(rd32(a));
    vdp_ctrl32(0x40200000);
    for (int i = 0; i < 0xA00; i++) vdp_data32(0);
    clear_longs(framebuffer, 0xA00);
    // Plane B: 32 x 10 tiles from tile 1, column-major, high priority.
    uint16_t tile = 0x8001;
    for (uint32_t col = 0; col < 32; col++) {
        uint32_t a = 0xC488 + col * 2;
        for (int row = 0; row < 10; row++, a += 0x80) {
            vdp_ctrl32(((a & 0x3FFF) | 0x4000) << 16 | (a >> 14));
            vdp_data(tile++);
        }
    }
    U16{0xFF0E4A} = 0x20;
    U16{0xFF0E48} = 0x40;
    U16{0xFF0E5A} = 0x5F;
    U16{0xFF0E58} = 0x40;
    wr32(vdp_queue, 0xFFFFFFFF);
    vdp_queue_ptr = vdp_queue;
    U16 angle{0xFF0874}, dist{0xFF0876};
    angle = 0;
    dist = 0xE00;
    const uint32_t a2 = 0xFF0E3C, a3 = 0xFF0E4C;
    do {
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
    } while (!(dist == 0x2B5 && angle == 0xFF40));
    // Hold the logo while the palette cycle finishes.
    int16_t d7 = 0xF;
    do {
        next_frame();
        clear_view();
        wr16(a2 + 0xE, 0x20);
        wr16(a2 + 0xC, 0x40);
        wr16(a3 + 0xE, 0x5F);
        wr16(a3 + 0xC, 0x40);
        draw_logo_wall(a2, a3);
    } while (--d7 >= 0 || U16{0xFF0EB6} != 0);
    for (int i = 0; i < 0x1F; i++) WaitVBlank();
}

} // namespace game
