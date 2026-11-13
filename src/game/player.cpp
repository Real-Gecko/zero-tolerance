// Player movement, collision and cell interaction.
#include "game.hpp"

#include <initializer_list>

#include "m68k.hpp"
#include "render.hpp"

namespace game {

namespace {

U16 height{0xFF116E}; // view height offset on stairs
U16 lift{0xFF1170};   // 1 going down, -1 going up, 2 / -2 arrived

// 0xE634
void h_00E634() { U16{0xFF2722} = 1; }

// 0xE662: hazard cell: pushes the player towards the cell centre and hurts.
void h_00E662() {
    uint16_t d0 = uint16_t((player_x & 0xFF00) | 0x80) - player_x;
    uint16_t d1 = uint16_t((player_y & 0xFF00) | 0x80) - player_y;
    uint16_t d3 = d0, d4 = d1;
    d0 = Distance(int16_t(d0), int16_t(d1));
    bool sound = int16_t(uint16_t(U16{0xFF1066})) < 0;
    sub_00D7DC(d0, d3, d4);
    if (sound) sub_00D760(0x1B);
}

// Lift cells: the first ride on the level shows message 0x1E67A.
void lift_message() {
    if (!U16{0xFF272C} && U16{0xFF272A} == cur_level) sub_01E0C2(0x1E67A);
}

void lift_music() {
    if (last_sound == 0x88) return;
    sub_0C62E8();
    PlaySound(0x88);
}

// 0xE69E: lift down.
void h_00E69E() {
    lift_message();
    if (height) return;
    lift_music();
    lift = 1;
    sub_00D760(0x6F);
}

// 0xE6FE: lift up.
void h_00E6FE() {
    lift_message();
    if (height) return;
    lift_music();
    lift = 0xFFFF;
    sub_00D760(0x6F);
}

// 0xE75E: lift, keeps the direction of a ride going up.
void h_00E75E() {
    lift_message();
    if (height) return;
    lift_music();
    lift = int16_t(uint16_t(lift)) < 0 ? 0xFFFF : 1;
    sub_00D760(0x6F);
}

// 0xE7D6: lift stop.
void h_00E7D6() {
    if (last_sound == 0x88) {
        sub_0C62E8();
        PlaySound(cur_area);
    }
    if (lift) lift = int16_t(uint16_t(lift)) < 0 ? 0xFFFE : 2;
    height = 0;
}

// Picked-up cells outside the map buffer clear a dummy byte.
uint32_t pickup_cell(uint32_t a0) { return a0 >= 0xFF7CD6 && a0 < 0xFFBCD6 ? a0 : 0xFFCCD6; }

// 0x11B2A: item d0 (weapon or ammo) goes into the inventory if there is room.
void take_item(uint16_t d0, uint32_t a0) {
    if (sub_011084(d0)) return;
    a0 = pickup_cell(a0);
    wr8(a0, 0);
    sub_01C9A0(a0);
    sub_00D760(0x69);
    sub_01E0C2(rd32(0x1EF14 + d0 * 4));
}

// 0x11B7C: medikit.
void take_medikit(uint32_t a0) {
    if (U16{0xFF0DE4} == 0x64) return;
    a0 = pickup_cell(a0);
    wr8(a0, 0);
    sub_01C9A0(a0);
    sub_00D760(0x6A);
    sub_01E0C2(0x1E5C2);
    U16{0xFF0DE4} += U16{0xFF1030} == 2 ? 0x28 : 0x14;
    sub_00D98E();
}

// 0xA33A / 0xA38C: pulls the player towards the cell centre when closer than
// range.
void pull_to_centre(uint16_t range, uint16_t mul) {
    uint16_t cx = uint16_t((player_x & 0xFF00) | 0x80), cy = uint16_t((player_y & 0xFF00) | 0x80);
    uint16_t dx = uint16_t(player_x - cx), dy = uint16_t(player_y - cy);
    uint16_t d = Distance(int16_t(dx), int16_t(dy));
    if (d >= range) return;
    d++;
    player_x = uint16_t(lo(divs(ext_l(uint16_t(dx * mul)), d)) + cx);
    player_y = uint16_t(lo(divs(ext_l(uint16_t(dy * mul)), d)) + cy);
}

// 0xA3DE
void h_00A3DE() {
    U16{0xFF0E1A} = uint16_t(int16_t(uint16_t(U16{0xFF0E1A})) >> 1);
    U16{0xFF0E1C} = 0;
    U16{0xFF0E20} = 0;
}

// 0xA3EC: keeps the player away from the cell edges while the view is high.
void h_00A3EC() {
    int16_t h = int16_t(uint16_t(height));
    if ((h < 0 ? uint16_t(-h) : uint16_t(h)) < 0x18) return;
    bool moved = false;
    uint8_t x = uint8_t(player_x);
    if (x < 0x20) wr8(0xFF0DED, 0x20), moved = true;
    else if (x > 0xDF) wr8(0xFF0DED, 0xDF), moved = true;
    uint8_t y = uint8_t(player_y);
    if (y < 0x20) wr8(0xFF0DEF, 0x20), moved = true;
    else if (y > 0xDF) wr8(0xFF0DEF, 0xDF), moved = true;
    if (!moved) return;
    U16{0xFF0E82} = 0;
    U16{0xFF0E86} = 0;
    U16{0xFF0DF0} = 0;
    U16{0xFF0DF2} = 0;
}

// 0xA456 / 0xA472 / 0xA48E / 0xA4AA: on a stair cell, keeps the player on
// one half of the cell along the stairs (pos = low byte of player_y or _x).
void stair_half(uint32_t pos, bool high_half) {
    h_00A3DE();
    if (bool(rd8(pos) & 0x80) != high_half) wr8(pos, 0x80);
}

uint8_t cls_at(uint32_t a0) { return rd8(cell_class + rd8(a0)); }

// 0xA4C6 / 0xA4EC
void h_00A4C6(uint32_t a0) {
    if (cls_at(a0 + 1) == 0x17) stair_half(0xFF0DEF, true);
    else if (cls_at(a0 - 1) == 0x3F) stair_half(0xFF0DEF, false);
    else if (cls_at(a0 + 0x20) == 0x47) stair_half(0xFF0DED, false);
    else stair_half(0xFF0DED, true);
}

void h_00A4EC(uint32_t a0) {
    if (cls_at(a0 - 1) == 0x17) stair_half(0xFF0DEF, true);
    else if (cls_at(a0 + 1) == 0x3F) stair_half(0xFF0DEF, false);
    else if (cls_at(a0 - 0x20) == 0x47) stair_half(0xFF0DED, false);
    else stair_half(0xFF0DED, true);
}

// Door list FF0EC6..FF0FA6 (end pointer FF0EC2), 14-byte entries: +0 map
// cell, +4 opening 0..0x80, +6 cell value to restore, +8 / +A cell x / y,
// +C mode (0 closes when free, 1 stays, 2 opens fully), +D level.
U32 door_end{0xFF0EC2};

// 0xB30C: sends a door to the other console.
void sub_00B30C(uint32_t a3) {
    wr8(0xFF0FFA, 0x0A);
    wr8(0xFF0FFB, rd8(a3 + 0xD));
    wr8(0xFF0FFC, rd8(a3 + 8));
    wr8(0xFF0FFD, rd8(a3 + 0xA));
    wr32(0xFF0FFE, rd32(a3));
    wr8(0xFF1002, rd8(a3 + 5));
    sub_01C03A(0xFF0FFA);
}

// 0xB25E / 0xB29A: door cells; the cell becomes the open door FF62F3 / FF62F4.
void open_door(uint32_t a0, uint32_t open_cell) {
    uint32_t a3 = door_end;
    if (a3 == 0xFF0FA6) return;
    wr32(a3, a0);
    wr16(a3 + 4, 0x20);
    wr16(a3 + 6, rd8(a0));
    a0 = pickup_cell(a0);
    wr8(a0, rd8(open_cell));
    sub_01C9A0(a0);
    U16{0xFF0EB8} = 1;
    wr16(a3 + 8, player_x & 0xFF00);
    wr16(a3 + 0xA, player_y & 0xFF00);
    wr8(a3 + 0xC, 0);
    wr8(a3 + 0xD, rd8(0xFF1029));
    sub_00D760(0x67);
    door_end = a3 + 0xE;
    if (link_mode) sub_00B30C(a3);
}

void h_00B25E(uint32_t a0) { open_door(a0, 0xFF62F3); }
void h_00B29A(uint32_t a0) { open_door(a0, 0xFF62F4); }

// 0xB1C4 / 0xB202 (enemies) and 0xB130 / 0xB168 (the boss cell): door cell
// a0 opens; the entry gets the cell position (d0, d1), the player's level
// and, for the boss cell, kind 2 and no old cell byte. With the list full,
// 0xB202 still writes the second half of an entry past its end.
void other_open_door(uint32_t a0, uint32_t open_cell, uint16_t d0, uint16_t d1, bool when_full, uint8_t kind) {
    uint32_t a3 = door_end;
    if (a3 == 0xFF0FA6) {
        if (!when_full) return;
        a3 -= 8;
    } else {
        wr32(a3, a0);
        wr16(a3 + 4, 0x20);
        wr16(a3 + 6, kind ? 0 : rd8(a0));
        a0 = pickup_cell(a0);
        wr8(a0, rd8(open_cell));
        sub_01C9A0(a0);
    }
    U16{0xFF0EB8} = 1;
    wr16(a3 + 8, d0);
    wr16(a3 + 0xA, d1);
    wr8(a3 + 0xC, kind);
    wr8(a3 + 0xD, rd8(0xFF1029));
    door_end = a3 + 0xE;
    if (link_mode) sub_00B30C(a3);
}

} // namespace

void sub_00B1C4(uint32_t a0, uint16_t d0, uint16_t d1) { other_open_door(a0, 0xFF62F3, d0, d1, false, 0); }
void sub_00B202(uint32_t a0, uint16_t d0, uint16_t d1) { other_open_door(a0, 0xFF62F4, d0, d1, true, 0); }
void sub_00B130(uint32_t a0, uint16_t d0, uint16_t d1) { other_open_door(a0, 0xFF62F3, d0, d1, false, 2); }
void sub_00B168(uint32_t a0, uint16_t d0, uint16_t d1) { other_open_door(a0, 0xFF62F4, d0, d1, false, 2); }

// 0xA1D2: special cells under the player (tables 0xA20A / 0xA30A).
void loc_00A1D2() {
    uint32_t a0 = cur_map_ptr + uint32_t(int16_t(player_x) >> 8) + uint32_t(int16_t(player_y & 0xFF00) >> 3);
    uint8_t m = rd8(0xA20A + cls_at(a0));
    if (!m) return;
    switch (rd32(0xA30A + (m - 1) * 4)) {
    case 0xA3DE: h_00A3DE(); break;
    case 0xA33A: pull_to_centre(0x28, 48); break;
    case 0xA3EC: h_00A3EC(); break;
    case 0xA38C: pull_to_centre(0x58, 96); break;
    case 0xA456: stair_half(0xFF0DEF, true); break;
    case 0xA472: stair_half(0xFF0DEF, false); break;
    case 0xA48E: stair_half(0xFF0DED, true); break;
    case 0xA4AA: stair_half(0xFF0DED, false); break;
    case 0xA4C6: h_00A4C6(a0); break;
    case 0xA4EC: h_00A4EC(a0); break;
    case 0xB25E: h_00B25E(a0); break;
    case 0xB29A: h_00B29A(a0); break;
    }
}

// 0xA9CE: level n's variants of the map cells (table 0xAA0C, cells FF62C6).
void sub_00A9CE(uint16_t level) {
    uint32_t a0 = 0xFF7CD6 + uint32_t(int16_t(uint16_t(level << 10)));
    for (int i = 0; i < 0x400; i++, a0++) {
        uint8_t m = rd8(0xAA0C + rd8(cell_class + rd8(a0)));
        if (m != 0xFF) wr8(a0, rd8(0xFF62C6 + m));
    }
}

// 0xB33A: opening of the door at map cell a0, 0 when it is not open.
uint16_t sub_00B33A(uint32_t a0) {
    for (uint32_t a3 = 0xFF0EC6; a3 != door_end; a3 += 0xE)
        if (rd32(a3) == a0) return rd16(a3 + 4);
    return 0;
}

// 0xB35C: opens and closes the doors in the door list.
void sub_00B35C() {
    uint32_t a3 = 0xFF0EC6;
    while (a3 != door_end) {
        uint8_t mode = rd8(a3 + 0xC);
        bool remove = false;
        if (mode == 1) {
        } else if (mode == 2) {
            if (rd16(a3 + 4) == 0x80) remove = true;
            else {
                wr16(a3 + 4, uint16_t(rd16(a3 + 4) + 0x20));
                if (link_mode) sub_00B30C(a3);
            }
        } else {
            uint16_t x0 = rd16(a3 + 8), y0 = rd16(a3 + 0xA);
            uint16_t x1 = uint16_t(x0 + 0xFF), y1 = uint16_t(y0 + 0xFF);
            auto inside = [&](uint16_t x, uint16_t y) { return x0 <= x && x <= x1 && y0 <= y && y <= y1; };
            bool blocked = inside(player_x, player_y);
            if (!blocked && U16{0xFF24E8}) {
                uint32_t e = U32{0xFF24EA};
                for (uint16_t n = U16{0xFF24E8}; n; n--, e = rd32(e))
                    if ((rd16(e + 4) & 0x10) && inside(rd16(e + 0x20), rd16(e + 0x22))) {
                        blocked = true;
                        break;
                    }
            }
            if (blocked) {
                if (rd16(a3 + 4) != 0x80) {
                    wr16(a3 + 4, uint16_t(rd16(a3 + 4) + 0x20));
                    if (link_mode) sub_00B30C(a3);
                }
            } else {
                wr16(a3 + 4, uint16_t(rd16(a3 + 4) - 0x20));
                if (link_mode) sub_00B30C(a3);
                if (!rd16(a3 + 4)) {
                    sub_00D760(0x67);
                    remove = true;
                }
            }
        }
        if (!remove) {
            a3 += 0xE;
            continue;
        }
        U16{0xFF0EB8} = 1;
        uint32_t a0 = pickup_cell(rd32(a3));
        wr8(a0, rd8(a3 + 7));
        sub_01C9A0(a0);
        uint32_t a2 = a3;
        for (uint32_t a4 = a3 + 0xE; a4 != door_end; a4 += 0xE, a2 += 0xE)
            for (int i = 0; i < 0xE; i += 2) wr16(a2 + i, rd16(a4 + i));
        door_end = a2;
    }
}

// 0xB48A: puts every door that is not mode 1 back into the map and removes
// it from the list.
void loc_00B48A() {
    uint32_t a3 = 0xFF0EC6;
    while (a3 != door_end) {
        if (rd8(a3 + 0xC) == 1) {
            a3 += 0xE;
            continue;
        }
        uint32_t a0 = pickup_cell(rd32(a3));
        wr8(a0, rd8(a3 + 7));
        sub_01C9A0(a0);
        uint32_t a2 = a3;
        for (uint32_t a4 = a3 + 0xE; a4 != door_end; a4 += 0xE, a2 += 0xE)
            for (int i = 0; i < 0xE; i += 2) wr16(a2 + i, rd16(a4 + i));
        door_end = a2;
    }
}

// 0xD940: health warnings when the health (FF0DE4) drops below 0x33 or 0x10.
void sub_00D940() {
    if (U16{0xFF0DE6} > 0x32) {
        if (U16{0xFF0DE4} <= 0x32) sub_01E0C2(0x1E6F8);
    } else if (U16{0xFF0DE6} > 0xF) {
        if (U16{0xFF0DE4} <= 0xF) sub_01E0C2(0x1E726);
    }
}

// 0xD800: the player is hit from distance d0 by a shot from direction
// (d3, d4): damage (unless the armour FF1064 is active), the camera shake
// (FF0E20), the push (FF0DF0) and the health warnings.
void sub_00D800(uint16_t d0, uint16_t d3, uint16_t d4) {
    if (U16{0xFF0DE2}) return;
    uint16_t d1 = d0;
    int16_t jump_h = int16_t(U16{0xFF0E1A});
    if (jump_h > 0)
        d1 = uint16_t(d1 - (int16_t(d1) >> 2));
    else if (jump_h < 0)
        d1 = uint16_t(d1 + (int16_t(d1) >> 1));
    if (d1 >= 0x400) return;
    d1 = uint16_t(int16_t(d1) >> 6);
    bool armour = int16_t(U16{0xFF1064}) >= 0;
    if (!armour) {
        d1 = uint16_t(0x10 - d1);
        if (U16{0xFF1030} == 3) d1 = uint16_t(d1 - (int16_t(d1) >> 2));
        if (U16{0xFF1030} == 4) d1 = uint16_t(int16_t(d1) >> 1);
        U16{0xFF0DE6} = U16{0xFF0DE4};
        U16{0xFF0DE4} = uint16_t(U16{0xFF0DE4} - d1);
        d1 = uint16_t(0x10 - d1);
    }
    d1 = uint16_t((d1 << 2) & 0x3C);
    uint32_t a1 = 0xD900 + d1;
    if (!armour) {
        d1 = uint16_t(0xF - (d1 >> 3));
        if (d1 > U16{0xFF1072}) U16{0xFF1072} = d1;
    }
    if (!U16{0xFF0E20} && !U16{0xFF0E1A}) U16{0xFF0E20} = rd16(a1);
    int16_t k = int16_t(rd16(a1 + 2));
    uint16_t d = uint16_t(Distance(int16_t(d3), int16_t(d4)) + 1);
    d3 = uint16_t(divs(uint32_t(int32_t(int16_t(d3)) * k), d));
    d4 = uint16_t(divs(uint32_t(int32_t(int16_t(d4)) * k), d));
    while (Distance(int16_t(d3), int16_t(d4)) > 0x100) {
        d3 = uint16_t(int16_t(d3) >> 1);
        d4 = uint16_t(int16_t(d4) >> 1);
    }
    U16{0xFF0DF0} = d3;
    U16{0xFF0DF2} = d4;
    if (rd16(a1) >= 7) {
        U16{0xFF0E1C} = 0xFFF0;
        U16{0xFF0E24} = 0x14;
    }
    sub_00D940();
    if (!armour)
        sub_00D98E();
    else
        sub_010EAC();
}

// 0xD7DC: a hit on the player; the shield (FF1066) only loses charge.
void sub_00D7DC(uint16_t d0, uint16_t d3, uint16_t d4) {
    if (int16_t(U16{0xFF1066}) >= 0) return sub_010F98();
    uint16_t armour = U16{0xFF1064};
    U16{0xFF1064} = 0xFFFF;
    sub_00D800(d0, d3, d4);
    U16{0xFF1064} = armour;
}

namespace {

U16 dirx{0xFF0E00}, diry{0xFF0E02};
U16 push_x{0xFF0DF0}, push_y{0xFF0DF2}; // knock-back, decays by 1/4 per frame
U16 jump{0xFF0E1A}, jump_target{0xFF0E1C}, jump_speed{0xFF0E20}, crouch_time{0xFF0E24};
U16 turn{0xFF0E7E}, turn_target{0xFF0E80}, speed{0xFF0E82}, speed_target{0xFF0E84};
U16 strafe{0xFF0E86}, strafe_target{0xFF0E88};

int16_t s16(uint16_t v) { return int16_t(v); }
uint16_t asr(uint16_t v, int n) { return uint16_t(s16(v) >> n); }

// Moves cur towards target by diff >> shift.
void approach(const U16& cur, uint16_t target, int shift) {
    uint16_t d = uint16_t(target - cur);
    if (!d) return;
    if (s16(d) < 0) cur = uint16_t(cur + asr(d, shift));
    else cur = uint16_t(cur - asr(uint16_t(-d), shift));
}

uint16_t mul8(uint16_t a, uint16_t b) { return uint16_t((int32_t(s16(a)) * s16(b)) >> 8); }

// 0xE1B2: collision shape blocking point (x, y) of map a1, 0 when free.
uint8_t sub_00E1B2(uint32_t a1, uint16_t x, uint16_t y) {
    uint8_t cls = rd8(cell_class + rd8(a1 + uint32_t(int16_t(uint16_t((s16(x) >> 8) + (s16(uint16_t(y & 0xFF00)) >> 3))))));
    if (!cls) return 0;
    uint8_t shape = sub_00E238(cls);
    uint8_t px = uint8_t(x), py = uint8_t(y);
    switch (shape) {
    case 1: return 1;
    case 2: return px < py ? 0 : 2;
    case 3: return px < uint8_t(~py) ? 0 : 3;
    case 4: return px > py ? 0 : 4;
    case 5: return px > uint8_t(~py) ? 0 : 5;
    default: return 0;
    }
}

uint8_t shape_at(uint32_t a) { return sub_00E238(rd8(cell_class + rd8(a))); }

bool any(uint8_t v, std::initializer_list<uint8_t> l) {
    for (uint8_t x : l)
        if (v == x) return true;
    return false;
}

// 0xDE3E: keeps the player off the walls and diagonal walls around the cell.
void keep_off_walls(uint32_t a1) {
    uint8_t x = uint8_t(player_x), y = uint8_t(player_y);
    a1 += uint32_t(int16_t(uint16_t((s16(player_x) >> 8) + (s16(uint16_t(player_y & 0xFF00)) >> 3))));
    bool right = x & 0x80, down = y & 0x80;
    uint8_t own = shape_at(a1);
    uint8_t side = shape_at(a1 + (right ? 1 : -1));
    uint8_t vert = shape_at(a1 + (down ? 0x20 : -0x20));
    uint8_t diag = shape_at(a1 + (down ? 0x20 : -0x20) + (right ? 1 : -1));
    uint8_t t;
    if (right && down) {
        if (x >= 0xE0 && any(side, {1, 4, 5})) x = 0xDF;
        if (y >= 0xE0 && any(vert, {1, 2, 5})) y = 0xDF;
        if (x >= 0xE0 && y >= 0xE0 && any(diag, {1, 2, 4, 5})) x = y = 0xDF;
        if (own == 2) { t = uint8_t(y - 0x2D); if (x > t) x = t; }
        else if (own == 4) { t = uint8_t(x - 0x2D); if (y > t) y = t; }
        else if (own == 5) { t = uint8_t(0x2D - y); if (x < t) x = t; }
    } else if (right) {
        if (x >= 0xE0 && any(side, {1, 4, 5})) x = 0xDF;
        if (y <= 0x1F && any(vert, {1, 3, 4})) y = 0x20;
        if (x >= 0xE0 && y <= 0x1F && any(diag, {1, 3, 4, 5})) x = 0xDF, y = 0x20;
        if (own == 3) { t = uint8_t(0xD3 - y); if (x > t) x = t; }
        else if (own == 5) { t = uint8_t(0x2D - x); if (y < t) y = t; }
        else if (own == 4) { t = uint8_t(0x2D + y); if (x < t) x = t; }
    } else if (down) {
        if (x <= 0x1F && any(side, {1, 2, 3})) x = 0x20;
        if (y >= 0xE0 && any(vert, {1, 2, 5})) y = 0xDF;
        if (x <= 0x1F && y >= 0xE0 && any(diag, {1, 2, 3, 5})) x = 0x20, y = 0xDF;
        if (own == 3) { t = uint8_t(0xD3 - x); if (y > t) y = t; }
        else if (own == 5) { t = uint8_t(0x2D - y); if (x < t) x = t; }
        else if (own == 2) { t = uint8_t(y - 0x2D); if (x > t) x = t; }
    } else {
        if (x <= 0x1F && any(side, {1, 2, 3})) x = 0x20;
        if (y <= 0x1F && any(vert, {1, 3, 4})) y = 0x20;
        if (x <= 0x1F && y <= 0x1F && any(diag, {1, 2, 3, 4})) x = y = 0x20;
        if (own == 2) { t = uint8_t(x + 0x2D); if (y < t) y = t; }
        else if (own == 4) { t = uint8_t(y + 0x2D); if (x < t) x = t; }
        else if (own == 3) { t = uint8_t(0xD3 - x); if (y > t) y = t; }
    }
    wr8(0xFF0DED, x);
    wr8(0xFF0DEF, y);
}

// Moves the player by (dx, dy), sliding along walls. False when blocked.
bool move(uint32_t a1, uint16_t dx, uint16_t dy) {
    uint8_t r = sub_00E1B2(a1, uint16_t(player_x + dx), uint16_t(player_y + dy));
    if (r == 1) {
        if (!sub_00E1B2(a1, uint16_t(player_x + dx), player_y)) player_x = uint16_t(player_x + dx);
        else if (!sub_00E1B2(a1, player_x, uint16_t(player_y + dy))) player_y = uint16_t(player_y + dy);
        else return false;
        return true;
    }
    if (r) {
        // Diagonal wall: first along the wall, then the rest.
        uint16_t d6 = asr(uint16_t(dy - dx), 1), d5 = uint16_t(-d6);
        if (!sub_00E1B2(a1, uint16_t(player_x + d5), uint16_t(player_y + d6))) {
            dx = d5;
            dy = d6;
        } else {
            dx = uint16_t(dx - d5);
            dy = uint16_t(dy - d6);
            if (sub_00E1B2(a1, uint16_t(player_x + dx), uint16_t(player_y + dy))) return false;
        }
    }
    player_x = uint16_t(player_x + dx);
    player_y = uint16_t(player_y + dy);
    return true;
}

} // namespace

// 0xDAB8: player controls, turning, jumping, movement and collision.
void sub_00DAB8() {
    uint8_t pad = pad_buttons, prev = rd8(0xFF0032);
    if (U16{0xFF0DE2}) {
        speed_target = 0;
        turn_target = 0;
        strafe_target = 0;
        jump_target = 0xFFE4;
        crouch_time = 0x63;
    } else {
        if ((pad & 0x20) && !(prev & 0x20)) sub_01125E();
        turn_target = 0;
        speed_target = 0;
        strafe_target = 0;
        if (pad & 0x40) {
            if (s16(jump) <= 0) {
                if (pad & 4) strafe_target = 0xFFD8;
                if (pad & 8) strafe_target = 0x28;
            }
            if ((pad & 1) && s16(jump) <= 0) {
                if (s16(jump) < 0) jump_target = 0;
                else if (!((prev & 0x40) && (prev & 1)) && !(pad & 4) && !(pad & 8)) {
                    jump_speed = 9;
                    jump = 0;
                }
            }
            if ((pad & 2) && !(pad & 4) && !(pad & 8)) {
                jump_target = 0xFFF0;
                crouch_time = 0x1E;
            }
        } else {
            if (pad & 4) turn_target = 0xFFF0;
            if (pad & 8) turn_target = 0x10;
            if (pad & 1) speed_target = U16{0xFF1030} == 3 ? 0x2D : 0x28;
            if (pad & 2) speed_target = U16{0xFF1030} == 3 ? 0xFFDD : 0xFFEC;
        }
    }
    if (jump_target == 0xFFF0 && !(crouch_time = uint16_t(crouch_time - 1))) jump_target = 0;
    uint16_t d0 = jump_speed;
    if (d0 || s16(jump) > 0) {
        jump_speed = uint16_t(jump_speed - 2);
        jump = uint16_t(jump + d0);
        if (s16(jump) <= 0) {
            jump = 0;
            jump_speed = 0;
        }
    } else {
        d0 = jump;
        if (s16(d0) < 0) {
            strafe_target = asr(strafe_target, 1);
            speed_target = asr(speed_target, 1);
        }
        if (s16(d0) < s16(jump_target)) jump = uint16_t(jump + 4);
        else if (d0 != jump_target) jump = uint16_t(jump - 4);
    }
    if (s16(jump) <= 0) approach(turn, turn_target, turn_target ? 3 : 1);
    if (turn) {
        player_angle = uint16_t((player_angle + turn) & 0x1FF);
        dirx = rd16(0x8124 + player_angle * 4);
        diry = rd16(0x8124 + player_angle * 4 + 2);
    }
    if (s16(jump) <= 0) {
        approach(speed, speed_target, speed_target ? 4 : 1);
        approach(strafe, strafe_target, strafe_target ? 4 : 1);
    }
    uint32_t a1 = cur_map_ptr;
    uint16_t dx, dy;
    d0 = speed;
    if (!d0) d0 = push_x | push_y;
    if (!d0) {
        if (!strafe) return loc_00A1D2();
        dx = mul8(uint16_t(-diry), strafe);
        dy = mul8(dirx, strafe);
    } else {
        push_x = uint16_t(push_x - (s16(push_x) < 0 ? asr(push_x, 2) : uint16_t(-asr(uint16_t(-push_x), 2))));
        push_y = uint16_t(push_y - (s16(push_y) < 0 ? asr(push_y, 2) : uint16_t(-asr(uint16_t(-push_y), 2))));
        dx = mul8(dirx, d0);
        dy = mul8(diry, d0);
        if (strafe) {
            dx = uint16_t(dx + mul8(uint16_t(-diry), strafe));
            dy = uint16_t(dy + mul8(dirx, strafe));
        }
    }
    dx = uint16_t(dx + push_x);
    dy = uint16_t(dy + push_y);
    if (move(a1, dx, dy)) keep_off_walls(a1);
    loc_00A1D2();
}

// 0xEC1C: head bob and footstep sounds while walking.
void sub_00EC1C() {
    uint16_t v = uint16_t((s16(speed) < 0 ? -speed : speed) + (s16(strafe) < 0 ? -strafe : strafe));
    U16 bob{0xFF0E26};
    bob = uint16_t(bob + v);
    // 0xEBE0
    uint8_t step = rd8(0xEC0C + ((s16(bob) >> 5) & 0xF));
    if (step != rd8(0xFF2726)) {
        wr8(0xFF2726, step);
        sub_00D760(step);
    }
    if (v >= 0x28) {
        uint16_t i = uint16_t((s16(bob) >> 4) & 0x1C);
        uint16_t dh = rd16(0xECA0 + i), da = rd16(0xECA2 + i);
        if (da || dh) {
            U16{0xFF0E2A} = dh;
            U16{0xFF0E28} = da;
            U16{0xFF0E2C} = player_angle;
            U16{0xFF0E2E} = dirx;
            U16{0xFF0E30} = diry;
            U16{0xFF0E32} = jump;
            jump = uint16_t(jump + dh);
            player_angle = uint16_t((player_angle + da) & 0x1FF);
            dirx = rd16(0x8124 + player_angle * 4);
            diry = rd16(0x8124 + player_angle * 4 + 2);
            return;
        }
    }
    U32{0xFF0E28} = 0;
}

// 0x11AB4: pickup cells.
void PickupItem(uint32_t a0, uint8_t d3) {
    if (U16{0xFF0DE2}) return;
    if (d3 == 0x82) return take_item(0xD, a0);
    if (d3 == 0x36) return take_item(0xE, a0);
    uint16_t d0 = uint16_t(d3 - 0x18);
    if (d0 > 0xE) return;
    if (d0 >= 1 && d0 <= 0xC) take_item(d0, a0);
    else if (d0 == 0xD) take_medikit(a0);
}

// 0xE238: cell class -> collision shape (table 0xE23E). The 68000 code only
// replaces the low byte of d3.
uint8_t sub_00E238(uint16_t d3) { return rd8(0xE23E + int16_t(d3)); }

// Targets of the cell-use table 0xE420 (called by 0xE3C4 / 0xE3F8 with
// a0 = map cell, d3 = cell class).
void CellUseHandler(uint32_t addr, uint32_t a0, uint16_t d3) {
    switch (addr) {
    case 0xB25E: h_00B25E(a0); break;
    case 0xB29A: h_00B29A(a0); break;
    case 0xE63C: sub_001D46(0); break;
    case 0xE644: sub_001D46(1); break;
    case 0xE64E: sub_001D46(2); break;
    case 0xE658: sub_001D46(3); break;
    case 0xE662: h_00E662(); break;
    case 0x11AB4: PickupItem(a0, uint8_t(d3)); break;
    case 0x2682: h_002682(); break;
    case 0xE7D6: h_00E7D6(); break;
    case 0xE69E: h_00E69E(); break;
    case 0xE6FE: h_00E6FE(); break;
    case 0xE75E: h_00E75E(); break;
    case 0xE634: h_00E634(); break;
    case 0xE69C: case 0x13298: break;
    default: untranslated(addr);
    }
}

} // namespace game
