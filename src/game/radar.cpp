// Radar: the 10x10 cell map around the player, the player and enemy dots
// (0xE816-0xEBDE) and the cell-use calls made when the player enters a cell.
#include "game.hpp"
#include "display.hpp"

#include "../host.hpp"

namespace game {

namespace {

int16_t s16(uint16_t v) { return int16_t(v); }

auto& radar_x = st.radar_x;
auto& radar_y = st.radar_y;
auto& player_dirx = st.dir_x;
auto& player_diry = st.dir_y;
auto& spr_link = st.sprite_link;

uint16_t put_sprite(uint16_t a2, uint16_t y, uint16_t attr, uint16_t x) {
    st.sprites[a2] = {y, spr_link, attr, x};
    spr_link = uint16_t(spr_link + 1);
    return uint16_t(a2 + 1);
}

uint16_t clamp_radar(uint16_t v) {
    if (v < 0x500) return uint16_t(0x500 | (v & 0xFF));
    if (v >= 0x1B00) return uint16_t(0x1B00 | (v & 0xFF));
    return v;
}

Cell player_cell() {
    return cur_map + (player_x >> 8) + ((player_y >> 8) << 5);
}

Cell radar_origin() {
    return cur_map + int16_t(uint16_t((s16(radar_x) >> 8) - 5)) + int16_t(uint16_t(((s16(radar_y) >> 8) - 5) << 5));
}

uint16_t radar_tile(Cell a1) {
    return uint16_t(0xE347 + CollisionShape(st.cell_class[map_cell(a1)]));
}

} // namespace

// 0xE3C4: runs the cell-use handler of the cell at a0 if it is a pickup.
void UsePickupCell(Cell a0) {
    st.pickup_check = 0;
    uint8_t cls = st.cell_class[map_cell(a0)];
    if (cls <= 5 || cls > 0x82) return;
    if (kCellUse[cls] == CellUse::Pickup) CellUseHandler(CellUse::Pickup, a0, cls);
}

// 0xE3F8: runs the cell-use handler of the cell at a0.
void UseCell(Cell a0) {
    uint8_t cls = st.cell_class[map_cell(a0)];
    if (cls <= 5 || cls > 0x82) return;
    CellUseHandler(kCellUse[cls], a0, cls);
}

// 0xE966: the player dot and, every other frame, three dots ahead of it.
uint16_t RadarPlayerDots(uint16_t a2, uint16_t d5) {
    uint16_t y = uint16_t((s16(uint16_t(player_y - (radar_y & 0xFF00))) >> 5) + 0x12E);
    uint16_t x = uint16_t((s16(uint16_t(player_x - (radar_x & 0xFF00))) >> 5) + 0x10E);
    a2 = put_sprite(a2, y, d5, x);
    if (!(uint8_t(st.frame) & 1)) return a2;
    a2 = put_sprite(a2, uint16_t(y + (s16(player_diry) >> 6)), 0xE34F, uint16_t(x + (s16(player_dirx) >> 6)));
    a2 = put_sprite(a2, uint16_t(y + (s16(player_diry) >> 5)), 0xE34F, uint16_t(x + (s16(player_dirx) >> 5)));
    int16_t dy = int16_t(s16(player_diry) >> 5), dx = int16_t(s16(player_dirx) >> 5);
    return put_sprite(a2, uint16_t(y + dy + (dy >> 1)), 0xE34F, uint16_t(x + dx + (dx >> 1)));
}

// 0xEB5A: dots for the enemies of this level inside the radar.
uint16_t RadarEnemyDots(uint16_t a2, uint16_t d5) {
    uint16_t n = st.object_count;
    if (!n) return a2;
    Enemy* a0 = first_object();
    int16_t left = int16_t(uint16_t((radar_x & 0xFF00) - 0x500)), right = int16_t(uint16_t((radar_x & 0xFF00) + 0x500));
    int16_t top = int16_t(uint16_t((radar_y & 0xFF00) - 0x500)), bottom = int16_t(uint16_t((radar_y & 0xFF00) + 0x500));
    for (; n; n--, a0 = next_object(a0)) {
        if (!(a0->flags & 0x10) || a0->level != uint8_t(cur_level)) continue;
        int16_t x = int16_t(a0->x), y = int16_t(a0->y);
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
    uint16_t blink = st.frame & 7;
    if (s16(st.scanner_slot) < 0) {
        Cell a0 = player_cell();
        st.player_cell = a0;
        bool redraw = true;
        if (a0 == st.last_cell) {
            if (st.pickup_check) UsePickupCell(a0);
            redraw = st.radar_redraw != 0;
        } else {
            st.last_cell = a0;
            UseCell(a0);
        }
        if (redraw) {
            st.radar_redraw = 0;
            uint16_t nt = 0xC81A;
            Cell row = radar_origin();
            for (int y = 0; y < 10; y++, row += 0x20) {
                nt = uint16_t(nt + 0x80);
                for (int x = 0; x < 10; x++) display::write_cell(uint16_t(nt + x * 2), radar_tile(row + x));
            }
        }
        uint16_t a2 = st.sprite_end;
        uint16_t d5 = kRadarBlinkA[blink];
        a2 = RadarPlayerDots(a2, d5);
        st.sprite_end = RadarEnemyDots(a2, uint16_t(d5 + 5));
        return;
    }
    uint16_t a2 = st.sprite_end;
    uint16_t d5 = kRadarBlinkB[blink];
    Cell a0 = player_cell();
    st.player_cell = a0;
    if (st.pickup_check) UsePickupCell(a0);
    if (a0 != st.last_cell) {
        st.last_cell = a0;
        UseCell(a0);
    }
    uint16_t nt = 0xC81A, sy = 0x10A;
    Cell row = radar_origin();
    for (int y = 0; y < 10; y++, row += 0x20, sy = uint16_t(sy + 8)) {
        nt = uint16_t(nt + 0x80);
        uint16_t sx = 0xEA;
        for (int x = 0; x < 10; x++, sx = uint16_t(sx + 8)) {
            uint8_t cls = st.cell_class[map_cell(row + x)];
            if ((cls >= 0x29 && cls <= 0x2B) || (cls >= 0x65 && cls <= 0x6B))
                a2 = put_sprite(a2, sy, uint16_t(d5 + 5), sx);
            display::write_cell(uint16_t(nt + x * 2), radar_tile(row + x));
        }
    }
    a2 = RadarPlayerDots(a2, d5);
    st.sprite_end = RadarEnemyDots(a2, uint16_t(d5 + 5));
}

} // namespace game
