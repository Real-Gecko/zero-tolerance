// Radar: the 10x10 cell map around the player, the player and enemy dots
// (0xE816-0xEBDE) and the cell-use calls made when the player enters a cell.
#include "game.hpp"

#include "../host.hpp"

namespace game {

namespace {

int16_t s16(uint16_t v) { return int16_t(v); }

U16 radar_x{0xFF0DE8}, radar_y{0xFF0DEA};
U16 player_dirx{0xFF0E00}, player_diry{0xFF0E02};
U16 spr_link{0xFF0042};

uint32_t put_sprite(uint32_t a2, uint16_t y, uint16_t attr, uint16_t x) {
    wr16(a2, y);
    wr16(a2 + 2, spr_link);
    spr_link = uint16_t(spr_link + 1);
    wr16(a2 + 4, attr);
    wr16(a2 + 6, x);
    return a2 + 8;
}

uint16_t clamp_radar(uint16_t v) {
    if (v < 0x500) return uint16_t(0x500 | (v & 0xFF));
    if (v >= 0x1B00) return uint16_t(0x1B00 | (v & 0xFF));
    return v;
}

uint32_t player_cell() {
    return cur_map_ptr + (player_x >> 8) + ((player_y >> 8) << 5);
}

uint32_t radar_origin() {
    return cur_map_ptr + int16_t(uint16_t((s16(radar_x) >> 8) - 5)) + int16_t(uint16_t(((s16(radar_y) >> 8) - 5) << 5));
}

uint16_t radar_tile(uint32_t a1) {
    return uint16_t(0xE347 + sub_00E238(rd8(cell_class + rd8(a1))));
}

} // namespace

// 0xE3C4: runs the cell-use handler of the cell at a0 if it is a pickup.
void sub_00E3C4(uint32_t a0) {
    U16{0xFF2728} = 0;
    uint8_t cls = rd8(cell_class + rd8(a0));
    if (cls <= 5 || cls > 0x82) return;
    uint32_t addr = rd32(0xE420 + cls * 4);
    if (addr == 0x11AB4) CellUseHandler(addr, a0, cls);
}

// 0xE3F8: runs the cell-use handler of the cell at a0.
void sub_00E3F8(uint32_t a0) {
    uint8_t cls = rd8(cell_class + rd8(a0));
    if (cls <= 5 || cls > 0x82) return;
    CellUseHandler(rd32(0xE420 + cls * 4), a0, cls);
}

// 0xE966: the player dot and, every other frame, three dots ahead of it.
uint32_t sub_00E966(uint32_t a2, uint16_t d5) {
    uint16_t y = uint16_t((s16(uint16_t(player_y - (radar_y & 0xFF00))) >> 5) + 0x12E);
    uint16_t x = uint16_t((s16(uint16_t(player_x - (radar_x & 0xFF00))) >> 5) + 0x10E);
    a2 = put_sprite(a2, y, d5, x);
    if (!(U8{0xFF0EB7} & 1)) return a2;
    a2 = put_sprite(a2, uint16_t(y + (s16(player_diry) >> 6)), 0xE34F, uint16_t(x + (s16(player_dirx) >> 6)));
    a2 = put_sprite(a2, uint16_t(y + (s16(player_diry) >> 5)), 0xE34F, uint16_t(x + (s16(player_dirx) >> 5)));
    int16_t dy = int16_t(s16(player_diry) >> 5), dx = int16_t(s16(player_dirx) >> 5);
    return put_sprite(a2, uint16_t(y + dy + (dy >> 1)), 0xE34F, uint16_t(x + dx + (dx >> 1)));
}

// 0xEB5A: dots for the enemies of this level inside the radar.
uint32_t sub_00EB5A(uint32_t a2, uint16_t d5) {
    uint16_t n = U16{0xFF24E8};
    if (!n) return a2;
    uint32_t a0 = U32{0xFF24EA};
    int16_t left = int16_t(uint16_t((radar_x & 0xFF00) - 0x500)), right = int16_t(uint16_t((radar_x & 0xFF00) + 0x500));
    int16_t top = int16_t(uint16_t((radar_y & 0xFF00) - 0x500)), bottom = int16_t(uint16_t((radar_y & 0xFF00) + 0x500));
    for (; n; n--, a0 = rd32(a0)) {
        if (!(rd16(a0 + 4) & 0x10) || rd8(a0 + 0x32) != uint8_t(cur_level)) continue;
        int16_t x = int16_t(rd16(a0 + 0x20)), y = int16_t(rd16(a0 + 0x22));
        if (x >= right || x < left || y >= bottom || y < top) continue;
        a2 = put_sprite(a2, uint16_t((int16_t(uint16_t(y - top)) >> 5) + 0x106), d5,
                        uint16_t((int16_t(uint16_t(x - left)) >> 5) + 0xE6));
    }
    return a2;
}

// 0xE816: radar. With FF1062 >= 0 it also marks the 0x29-0x2B and
// 0x65-0x6B cells with dots and redraws the cells every frame.
void DrawRadar() {
    radar_x = clamp_radar(player_x);
    radar_y = clamp_radar(player_y);
    while (fb_dma_pending) host::frame();
    uint16_t blink = U16{0xFF0EB6} & 7;
    if (s16(U16{0xFF1062}) < 0) {
        uint32_t a0 = player_cell();
        U32{0xFF0E12} = a0;
        bool redraw = true;
        if (a0 == U32{0xFF0E16}) {
            if (U16{0xFF2728}) sub_00E3C4(a0);
            redraw = U16{0xFF0EB8} != 0;
        } else {
            U32{0xFF0E16} = a0;
            sub_00E3F8(a0);
        }
        if (redraw) {
            U16{0xFF0EB8} = 0;
            uint16_t nt = 0xC81A;
            uint32_t row = radar_origin();
            for (int y = 0; y < 10; y++, row += 0x20) {
                nt = uint16_t(nt + 0x80);
                vdp_write_addr(nt);
                for (int x = 0; x < 10; x++) vdp_data(radar_tile(row + x));
            }
        }
        uint32_t a2 = U32{0xFF003E};
        uint16_t d5 = rd16(0xE956 + blink * 2);
        a2 = sub_00E966(a2, d5);
        U32{0xFF003E} = sub_00EB5A(a2, uint16_t(d5 + 5));
        return;
    }
    uint32_t a2 = U32{0xFF003E};
    uint16_t d5 = rd16(0xEB4A + blink * 2);
    uint32_t a0 = player_cell();
    U32{0xFF0E12} = a0;
    if (U16{0xFF2728}) sub_00E3C4(a0);
    if (a0 != U32{0xFF0E16}) {
        U32{0xFF0E16} = a0;
        sub_00E3F8(a0);
    }
    uint16_t nt = 0xC81A, sy = 0x10A;
    uint32_t row = radar_origin();
    for (int y = 0; y < 10; y++, row += 0x20, sy = uint16_t(sy + 8)) {
        nt = uint16_t(nt + 0x80);
        vdp_write_addr(nt);
        uint16_t sx = 0xEA;
        for (int x = 0; x < 10; x++, sx = uint16_t(sx + 8)) {
            uint8_t cls = rd8(cell_class + rd8(row + x));
            if ((cls >= 0x29 && cls <= 0x2B) || (cls >= 0x65 && cls <= 0x6B))
                a2 = put_sprite(a2, sy, uint16_t(d5 + 5), sx);
            vdp_data(radar_tile(row + x));
        }
    }
    a2 = sub_00E966(a2, d5);
    U32{0xFF003E} = sub_00EB5A(a2, uint16_t(d5 + 5));
}

} // namespace game
