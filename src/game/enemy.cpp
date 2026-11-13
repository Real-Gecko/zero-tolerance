// Enemies: the per-frame update of the enemy list (0x131E8), timed items
// (0x13182) and the shared enemy helpers.
#include "enemy.hpp"
#include "m68k.hpp"
#include "render.hpp"

namespace game {

namespace {

U16 enemy_count{0xFF24E8};
U32 enemy_list{0xFF24EA};
constexpr uint32_t player_struct = 0xFF119C;
constexpr uint32_t area_maps = 0xFF7CD6; // 16 levels x 0x400

int16_t s16(uint16_t v) { return int16_t(v); }

// Map index of a position: (x >> 8) + (y & 0xFF00) >> 3.
int16_t cell_index(uint16_t x, uint16_t y) {
    return int16_t(uint16_t((s16(x) >> 8) + (s16(uint16_t(y & 0xFF00)) >> 3)));
}

// 0x14852: nonzero when cell class d3 blocks enemies.
uint8_t sub_014852(uint8_t cls) { return rd8(0x14858 + cls); }

// Diagonal wall shapes 2-5 (0xE23E): 1 when (xlo, ylo) is inside the wall half.
uint8_t diagonal_hit(uint8_t shape, uint8_t xlo, uint8_t ylo) {
    uint16_t inv = uint8_t(0xFF - ylo);
    switch (shape) {
    case 2: return xlo > ylo;
    case 3: return xlo > inv;
    case 4: return xlo < ylo;
    default: return xlo < inv;
    }
}

// 0x165D8 / 0x166E8: 1 when a wall lies on the line from (d3, d4) to (d0, d1),
// in the map at a1 (cur_map_ptr or the enemy's level).
uint16_t line_blocked(bool own_map, uint32_t a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4) {
    uint16_t d2 = uint16_t(Distance(s16(uint16_t(d0 - d3)), s16(uint16_t(d1 - d4))) >> 6);
    if (d2 == 0 || d2 == 1) return 0;
    int16_t sx = int16_t(uint16_t(divs(ext_l(uint16_t(d3 - d0)), d2)));
    int16_t sy = int16_t(uint16_t(divs(ext_l(uint16_t(d4 - d1)), d2)));
    for (int n = d2 - 1; n > 0; n--) {
        uint32_t a1 = own_map ? enemy_map(a0) : uint32_t(cur_map_ptr);
        d0 = uint16_t(d0 + sx);
        d1 = uint16_t(d1 + sy);
        uint8_t shape = sub_00E238(rd8(cell_class + rd8(a1 + cell_index(d0, d1))));
        if (!shape) continue;
        if (shape >= 6 || shape == 1) return 1;
        if (diagonal_hit(shape, uint8_t(d0), uint8_t(d1))) return 1;
    }
    return 0;
}

// 0x14454, 0x144FE, 0x14A56, 0x14B08: drops the item byte FF62C6[+0x47 or
// +0x46] on the first empty cell around the enemy and removes the enemy.
// With every cell taken, keep = true leaves the enemy alone; counted calls
// 0x57E0C first.
void drop_item(uint32_t a0, uint16_t item_ofs, bool keep, bool counted) {
    uint32_t a1 = enemy_map(a0) + cell_index(rd16(a0 + 0x20), rd16(a0 + 0x22));
    static const int16_t around[] = {0, -1, 1, -0x20, 0x20, 0x1F, 0x21, -0x1F, -0x21};
    bool found = false;
    for (int16_t d : around) {
        if (!rd8(a1 + d)) {
            a1 += d;
            found = true;
            break;
        }
    }
    if (found) {
        if (a1 < 0xFF7CD6 || a1 >= 0xFFBCD6) a1 = 0xFFCCD6;
        wr8(a1, rd8(0xFF62C6 + rd8(a0 + item_ofs)));
        sub_01C9A0(a1);
    } else if (keep)
        return;
    else if (counted)
        sub_057E0C();
    sub_013554(a0);
}

} // namespace

// 0x1657A: map of the enemy's level.
uint32_t enemy_map(uint32_t a0) { return area_maps + int16_t(uint16_t(rd8(a0 + 0x32) << 10)); }

// 0xD740: sound d0 when the enemy is on the player's level.
void sub_00D740(uint32_t a0, uint16_t id) {
    uint8_t level = rd8(a0 + 0x32);
    if (level == U8{0xFF1029}) sub_00D760(id);
    if (link_mode && level == U8{0xFF121A}) {
        wr8(0xFF0FFA, 8);
        wr8(0xFF0FFB, uint8_t(id));
        sub_01C03A(0xFF0FFA);
    }
}

// 0x145AA (margin 0x20) / 0x146FE (margin 0x40): moves the enemy by (d0, d1),
// sliding along blocking cells, keeps it margin away from blocking
// neighbours and 0x20 away from the player. Returns the movement done.
EnemyMove move_enemy(uint32_t a0, uint16_t d0, uint16_t d1, uint8_t margin) {
    uint16_t ox = rd16(a0 + 0x20), oy = rd16(a0 + 0x22);
    uint32_t a1 = enemy_map(a0);
    auto blocked = [&](int16_t i) { return sub_014852(rd8(cell_class + rd8(a1 + i))) != 0; };
    uint16_t x = ox, y = oy;
    if (!blocked(cell_index(uint16_t(x + d0), uint16_t(y + d1)))) {
        x = uint16_t(x + d0);
        y = uint16_t(y + d1);
    } else if (!blocked(cell_index(x, uint16_t(y + d1))))
        y = uint16_t(y + d1);
    else if (!blocked(cell_index(uint16_t(x + d0), y)))
        x = uint16_t(x + d0);
    wr16(a0 + 0x20, x);
    wr16(a0 + 0x22, y);
    a1 += cell_index(x, y);
    uint8_t hi = uint8_t(0xFF - margin);
    uint8_t xl = rd8(a0 + 0x21);
    if (xl < margin) {
        if (sub_014852(rd8(cell_class + rd8(a1 - 1)))) wr8(a0 + 0x21, margin);
    } else if (xl > hi) {
        if (sub_014852(rd8(cell_class + rd8(a1 + 1)))) wr8(a0 + 0x21, hi);
    }
    uint8_t yl = rd8(a0 + 0x23);
    if (yl < margin) {
        if (sub_014852(rd8(cell_class + rd8(a1 - 0x20)))) wr8(a0 + 0x23, margin);
    } else if (yl > hi) {
        if (sub_014852(rd8(cell_class + rd8(a1 + 0x20)))) wr8(a0 + 0x23, hi);
    }
    uint16_t dx = uint16_t(rd16(a0 + 0x20) - player_x), dy = uint16_t(rd16(a0 + 0x22) - player_y);
    uint16_t d = uint16_t(Distance(s16(dx), s16(dy)) + 1);
    if (d != 0 && d < 0x20) {
        wr16(a0 + 0x20, uint16_t(uint16_t(divs(ext_l(dx) << 5, d)) + player_x));
        wr16(a0 + 0x22, uint16_t(uint16_t(divs(ext_l(dy) << 5, d)) + player_y));
    }
    return {uint16_t(rd16(a0 + 0x20) - ox), uint16_t(rd16(a0 + 0x22) - oy)};
}

EnemyMove sub_0145AA(uint32_t a0, uint16_t d0, uint16_t d1) { return move_enemy(a0, d0, d1, 0x20); }
EnemyMove sub_0146FE(uint32_t a0, uint16_t d0, uint16_t d1) { return move_enemy(a0, d0, d1, 0x40); }

// 0x13D42 (map of the enemy's level) / 0x13D48 (current map): the collision
// shape at (d0, d1): 0 free, 1 wall, >= 6 the shape itself.
uint8_t sub_013D42(uint32_t a0, uint16_t d0, uint16_t d1) {
    uint8_t shape = sub_00E238(rd8(cell_class + rd8(enemy_map(a0) + cell_index(d0, d1))));
    if (shape == 0 || shape == 1 || shape >= 6) return shape;
    return diagonal_hit(shape, uint8_t(d0), uint8_t(d1));
}

// 0x13CBC: 1 when the cell at (d0, d1) of the enemy's level is one that
// shots pass over (per area).
uint16_t sub_013CBC(uint32_t a0, uint16_t d0, uint16_t d1) {
    uint8_t c = rd8(enemy_map(a0) + cell_index(d0, d1));
    if (cur_area == 0) return c == 1 || c == 0x6E || c == 0x6F || (c >= 0x91 && c <= 0x96);
    if (cur_area == 1) {
        if (c == 1 || c == 0x8F || c == 0xA3) return 1;
        if (s16(rd16(a0 + 0x24)) < 0) return 0;
        return c == 3 || c == 0xA2 || (c >= 6 && c <= 8);
    }
    return 0;
}

// 0x1642C: the enemy's target: the player when on the enemy's level, else 0
// (in a link game, the nearer of the two players).
uint32_t sub_01642C(uint32_t a0) {
    uint8_t level = rd8(a0 + 0x32);
    if (link_mode) {
        uint32_t a1 = enemy_list;
        if (rd8(a1 + 0x32) == level) {
            if (uint8_t(cur_level) != level) return a1;
            uint16_t other = Distance(s16(uint16_t(rd16(a1 + 0x20) - rd16(a0 + 0x20))), s16(uint16_t(rd16(a1 + 0x22) - rd16(a0 + 0x22))));
            uint16_t me = Distance(s16(uint16_t(player_x - rd16(a0 + 0x20))), s16(uint16_t(player_y - rd16(a0 + 0x22))));
            return other < me ? a1 : player_struct;
        }
    }
    return uint8_t(cur_level) == level ? player_struct : 0;
}

// 0x1649C: like 0x1642C, preferring a target in sight in a link game.
uint32_t sub_01649C(uint32_t a0) {
    uint8_t level = rd8(a0 + 0x32);
    if (link_mode) {
        uint32_t a1 = enemy_list;
        if (rd8(a1 + 0x32) == level) {
            if (uint8_t(cur_level) != level) return a1;
            uint16_t ex = rd16(a0 + 0x20), ey = rd16(a0 + 0x22);
            bool other_hidden = sub_0166A0(a0, rd16(a1 + 0x20), rd16(a1 + 0x22), ex, ey);
            bool me_hidden = sub_0166A0(a0, player_x, player_y, ex, ey);
            if (!other_hidden && me_hidden) return a1;
            if (other_hidden && !me_hidden) return player_struct;
            uint16_t other = Distance(s16(uint16_t(rd16(a1 + 0x20) - ex)), s16(uint16_t(rd16(a1 + 0x22) - ey)));
            uint16_t me = Distance(s16(uint16_t(player_x - ex)), s16(uint16_t(player_y - ey)));
            return other < me ? a1 : player_struct;
        }
    }
    return uint8_t(cur_level) == level ? player_struct : 0;
}

// 0x165D8 / 0x166E8 (see line_blocked).
uint16_t sub_0165D8(uint32_t a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4) { return line_blocked(false, a0, d0, d1, d3, d4); }
uint16_t sub_0166E8(uint32_t a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4) { return line_blocked(true, a0, d0, d1, d3, d4); }

// 0x13D48: sub_013D42 on the current map.
uint8_t sub_013D48(uint16_t d0, uint16_t d1) {
    uint8_t shape = sub_00E238(rd8(cell_class + rd8(cur_map_ptr + cell_index(d0, d1))));
    if (shape == 0 || shape == 1 || shape >= 6) return shape;
    return diagonal_hit(shape, uint8_t(d0), uint8_t(d1));
}

// 0x13C36: sub_013CBC on the current map, by the player's height.
uint16_t sub_013C36(uint16_t d0, uint16_t d1) {
    uint8_t c = rd8(cur_map_ptr + cell_index(d0, d1));
    if (cur_area == 0) return c == 1 || c == 0x6E || c == 0x6F || (c >= 0x91 && c <= 0x96);
    if (cur_area == 1) {
        if (c == 1 || c == 0x8F || c == 0xA3) return 1;
        if (s16(U16{0xFF0E1A}) < 0) return 0;
        return c == 3 || c == 0xA2 || (c >= 6 && c <= 8);
    }
    return 0;
}

// 0xA8B6: nonzero when the player's cell (FF0E12) is one of the classes of
// table 0xA8CC.
uint8_t sub_00A8B6() { return rd8(0xA8CC + rd8(cell_class + rd8(U32{0xFF0E12}))); }

// 0x16388: the nearest enemy to a0 (or the player for a0 = 0) on its level
// whose flags include all of d5; 0 for the player, 0xFFFFFFFF for none.
uint32_t sub_016388(uint32_t a0, uint16_t d5) {
    uint32_t a1 = 0xFFFFFFFF;
    uint16_t best = 0xFFFF, x = player_x, y = player_y;
    uint8_t level = uint8_t(cur_level);
    if (a0) {
        x = rd16(a0 + 0x20);
        y = rd16(a0 + 0x22);
        level = rd8(a0 + 0x32);
    }
    U16 left{0xFF0FF8};
    left = enemy_count;
    if (left) {
        uint32_t a3 = enemy_list;
        do {
            if (a3 != a0 && (rd16(a3 + 4) & d5) == d5 && rd8(a3 + 0x32) == level) {
                uint16_t d = Distance(s16(uint16_t(x - rd16(a3 + 0x20))), s16(uint16_t(y - rd16(a3 + 0x22))));
                if (d < best) {
                    best = d;
                    a1 = a3;
                }
            }
            a3 = rd32(a3);
            left = uint16_t(left - 1);
        } while (left);
    }
    if (a0 && (0xDA & d5) == d5 && level == U8{0xFF1029}) {
        if (Distance(s16(uint16_t(x - player_x)), s16(uint16_t(y - player_y))) < best) a1 = 0;
    }
    return a1;
}

// 0x167B0: the enemy in the aim cone (width d1, column d2) nearest to the
// view, nearer than the wall in the middle column (FF61A6); also lets a shot
// at the sky hit the flash (0x29BA).
TargetResult sub_0167B0(uint16_t d1, uint16_t d2) {
    TargetResult r{0, 0};
    uint16_t n = enemy_count;
    if (n) {
        uint32_t a0 = enemy_list;
        r.d0 = U16{0xFF61A6};
        uint16_t mask = 0x40;
        if (s16(U16{0xFF0E1A}) > 0)
            mask = 0x80;
        else if (s16(U16{0xFF0E1A}) < 0)
            mask = 0x08;
        U16{0xFF1068} = mask;
        for (; n; n--, a0 = rd32(a0)) {
            if (!(rd16(a0 + 4) & mask) || rd8(a0 + 0x32) != uint8_t(cur_level)) continue;
            int16_t ex = s16(uint16_t(rd16(a0 + 0x20) - player_x)), ey = s16(uint16_t(rd16(a0 + 0x22) - player_y));
            int16_t dx = s16(U16{0xFF0E00}), dy = s16(U16{0xFF0E02});
            int32_t along = (int32_t(ex) * dx + int32_t(ey) * dy) >> 6;
            int32_t side = int32_t(ey) * dx - int32_t(ex) * dy;
            if (along < 2) continue;
            uint16_t d5 = uint16_t(divs(0x10000, uint16_t(along)));
            if (d5 <= r.d0) continue;
            uint16_t d6 = uint16_t(divs(uint32_t(side), uint16_t(along)));
            if (s16(d6) < 0) d6 = uint16_t(-d6);
            d6 = uint16_t(d6 + 0x40 - d2);
            if (d6 > uint16_t((s16(d5) >> 3) + d1)) continue;
            r.a1 = a0;
            r.d0 = d5;
        }
    }
    if (cur_level || !U8{0xFF1074}) return r;
    uint8_t cls = rd8(cell_class + rd8(U32{0xFF0E12}));
    if (cls != 0x28 && !(s16(U16{0xFF0E1A}) >= -5 && cls == 0x27)) return r;
    int16_t d = int16_t(uint16_t((d2 << 1) + 0x9E - U16{0xFF107A}));
    if (d < 0) d = int16_t(-d);
    if (uint16_t(d) <= 3) {
        U8{0xFF1078} = 0x20;
        sub_0029BA();
    }
    return r;
}

// 0x16590 / 0x166A0: line of sight from (d3, d4) to (d0, d1) also checked
// from 0x1C to each side; true when any line is blocked.
bool sub_016590(uint32_t a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4) {
    return sub_0165D8(a0, d0, d1, d3, d4) || sub_0165D8(a0, d0, d1, uint16_t(d3 + 0x1C), d4) ||
           sub_0165D8(a0, d0, d1, uint16_t(d3 - 0x1C), d4) || sub_0165D8(a0, d0, d1, d3, uint16_t(d4 + 0x1C)) ||
           sub_0165D8(a0, d0, d1, d3, uint16_t(d4 - 0x1C));
}
bool sub_0166A0(uint32_t a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4) {
    return sub_0166E8(a0, d0, d1, d3, d4) || sub_0166E8(a0, d0, d1, uint16_t(d3 + 0x1C), d4) ||
           sub_0166E8(a0, d0, d1, uint16_t(d3 - 0x1C), d4) || sub_0166E8(a0, d0, d1, d3, uint16_t(d4 + 0x1C)) ||
           sub_0166E8(a0, d0, d1, d3, uint16_t(d4 - 0x1C));
}

// 0x1BA04: draws animation d0 of the enemy's sprite set (+0x42) at column d1,
// scale d5. The view the set shows depends on its mode word: 1 one view,
// 2 front/back, 4/6/8 directions from the angle between the enemy's heading
// (+0x2A) and the player. d2 >= 0 is the frame; d2 < 0 steps the walking
// frames by the distance moved (+0x3C).
void sub_01BA04(uint32_t a0, uint16_t d0, uint16_t d2, int16_t col, uint16_t scale) {
    uint32_t a1 = rd32(a0 + 0x42);
    if (d0 >= rd16(a1)) return;
    a1 += 2;
    uint32_t blocks = a1 + uint32_t(int32_t(s16(rd16(a1))));
    a1 += uint32_t(int32_t(s16(rd16(a1 + 2 + d0 * 2))));
    uint16_t mode = rd16(a1);
    int16_t vx = s16(rd16(a0 + 0x2A)), vy = s16(rd16(a0 + 0x2C));
    int16_t ex = s16(uint16_t(rd16(a0 + 0x20) - player_x)), ey = s16(uint16_t(rd16(a0 + 0x22) - player_y));
    uint16_t view = 2;
    if (mode != 1) {
        int32_t dot = int32_t(ex) * vx + int32_t(ey) * vy;
        if (mode == 2)
            view = dot < 0 ? 2 : 4;
        else {
            uint32_t d3 = divs(uint32_t(dot), uint16_t(Distance(ex, ey) + 1));
            d3 = divs(ext_l(uint16_t(d3)) << 8, uint16_t(Distance(vx, vy) + 1));
            int16_t c = int16_t(uint16_t(d3));
            bool left = int32_t(ey) * vx - int32_t(ex) * vy < 0;
            if (mode == 4) {
                view = c >= 0x9B ? 4 : c <= -0x9B ? 2 : left ? 6 : 8;
            } else if (mode == 6) {
                view = c >= 0x9B ? 4 : c <= -0xC8 ? 2 : c >= -0x5A ? (left ? 6 : 8) : (left ? 0xA : 0xC);
            } else {
                view = c >= 0xC8 ? 4 : c <= -0xC8 ? 2 : c < -0x5A ? (left ? 0xA : 0xC) : c > 0x5A ? (left ? 0xE : 0x10) : (left ? 6 : 8);
            }
        }
    }
    a1 += uint32_t(int32_t(s16(rd16(a1 + view))));
    uint16_t frames = rd16(a1);
    a1 += 2;
    uint16_t part = 0;
    if (s16(d2) >= 0)
        part = d2 <= frames ? d2 : 0;
    else if (frames) {
        uint16_t speed = Distance(vx, vy);
        if (speed >= 4) {
            wr16(a0 + 0x3C, uint16_t(rd16(a0 + 0x3C) + speed));
            part = rd8(0x1BBAA + uint16_t((frames - 1) << 5) + ((s16(rd16(a0 + 0x3C)) >> 3) & 0x1F));
        }
    }
    U16{0xFF1094} = scale;
    int16_t d3 = s16(uint16_t(U16{0xFF0E1A} - U16{0xFF116E} - rd16(a0 + 0x24)));
    int16_t row = int16_t(uint16_t(((int32_t(s16(scale)) * d3) >> 6) + 0x28));
    DrawSpriteBlocks(a1 + uint32_t(part) * 0x26, blocks, col, row, scale);
}

void h_014454(uint32_t a0) { drop_item(a0, 0x47, false, false); }
void h_0144FE(uint32_t a0) { drop_item(a0, 0x47, true, false); }
void h_014A56(uint32_t a0) { drop_item(a0, 0x46, false, true); }
void h_014B08(uint32_t a0) { drop_item(a0, 0x46, true, false); }

namespace {

void enemy_die(uint32_t a0) { CallEnemyHandler(rd32(a0 + 0x16), a0); }

uint16_t blink_sum(uint32_t a0) { return uint8_t(U8{0xFF0EB7} + rd8(a0 + 0x3E)); }

uint16_t target_distance(uint32_t a0, uint32_t a1) {
    return Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a1 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a1 + 0x22))));
}

// Keeps a low byte of a position at 0x40 / 0xC0 from the edge of a door cell
// (0x1615E-0x161A8) and plays the door sound when it moved.
void door_clamp(uint32_t a0, uint32_t at, uint8_t lo, uint8_t hi) {
    uint8_t v = rd8(at);
    if (int8_t(v) >= 0) {
        if (v <= lo) return;
        wr8(at, lo);
    } else {
        if (v >= hi) return;
        wr8(at, hi);
    }
    sub_00D740(a0, 0x36); // 0x1628A
}

// Door cells push towards their middle line: classes 6/0x83 act on y, 7/0x84 on x.
void door_cells(uint32_t a0, uint32_t y_byte, uint32_t x_byte, uint8_t lo, uint8_t hi) {
    uint8_t cls = rd8(cell_class + rd8(enemy_map(a0) + cell_index(rd16(a0 + 0x20), rd16(a0 + 0x22))));
    if (cls == 6 || cls == 0x83)
        door_clamp(a0, y_byte, lo, hi);
    else if (cls == 7 || cls == 0x84)
        door_clamp(a0, x_byte, lo, hi);
}

} // namespace

// 0x16084: every 8 frames picks the target (0x1642C); without one, or with
// it 0xA00 away, the enemy dies (+0x16).
void loc_016084(uint32_t a0) {
    if (!(blink_sum(a0) & 7)) {
        uint32_t a1 = sub_01642C(a0);
        wr32(a0 + 6, a1);
        if (!a1) return enemy_die(a0);
    }
    if (target_distance(a0, rd32(a0 + 6)) >= 0xA00) enemy_die(a0);
}

// 0x160C4: every 8 frames picks the target (0x1649C); returns it (a1).
uint32_t loc_0160C4(uint32_t a0) {
    if (!(blink_sum(a0) & 7)) {
        uint32_t a1 = sub_01649C(a0);
        wr32(a0 + 6, a1);
        if (!a1) {
            enemy_die(a0);
            return rd32(a0 + 0x16);
        }
    }
    return rd32(a0 + 6);
}

// 0x160E8: like 0x16084 every 4 frames with 0x600, then keeps the enemy out
// of the door frames.
void loc_0160E8(uint32_t a0) {
    if (!(blink_sum(a0) & 3)) {
        uint32_t a1 = sub_01642C(a0);
        wr32(a0 + 6, a1);
        if (!a1) return enemy_die(a0);
    }
    if (target_distance(a0, rd32(a0 + 6)) >= 0x600) return enemy_die(a0);
    door_cells(a0, a0 + 0x23, a0 + 0x21, 0x40, 0xC0);
}

// 0x161B2: like 0x160E8, but checks the door cells every 8 frames and then
// clamps the bytes at FF8023 / FF8021 (a6-relative in the original, where
// a0 was probably meant).
void loc_0161B2(uint32_t a0) {
    if (!(blink_sum(a0) & 3)) {
        uint32_t a1 = sub_01642C(a0);
        wr32(a0 + 6, a1);
        if (!a1) return enemy_die(a0);
    }
    if (target_distance(a0, rd32(a0 + 6)) >= 0x600) return enemy_die(a0);
    if (!(blink_sum(a0) & 7)) door_cells(a0, 0xFF8023, 0xFF8021, 0x20, 0xE0);
}

// Halves the enemy's velocity (rounding towards zero) and moves it
// (0x186B8-0x186F2 and its copies).
void slow_down(uint32_t a0) {
    auto half = [](uint16_t v) { return uint16_t(s16(v) < 0 ? -(-s16(v) >> 1) : s16(v) >> 1); };
    uint16_t vx = half(rd16(a0 + 0x2A)), vy = half(rd16(a0 + 0x2C));
    wr16(a0 + 0x2A, vx);
    wr16(a0 + 0x2C, vy);
    if (vx | vy) sub_0145AA(a0, vx, vy);
}

// Knock-back from a hit (d0 distance, d3/d4 from the enemy to the shooter):
// returns the unit direction away from the shooter * 256 (0x187DA-0x187F8).
EnemyMove knock_direction(uint16_t d3, uint16_t d4) {
    d3 = uint16_t(-d3);
    d4 = uint16_t(-d4);
    uint16_t d = uint16_t(Distance(s16(d3), s16(d4)) + 1);
    uint32_t x = ext_l(d3) << 8, y = ext_l(d4) << 8;
    if (d) {
        x = divs(x, d);
        y = divs(y, d);
    }
    return {uint16_t(x), uint16_t(y)};
}

// Sets the velocity to direction * strength >> 8.
void set_knock(uint32_t a0, EnemyMove dir, int16_t strength) {
    wr16(a0 + 0x2A, uint16_t((int32_t(s16(dir.d0)) * strength) >> 8));
    wr16(a0 + 0x2C, uint16_t((int32_t(s16(dir.d1)) * strength) >> 8));
}

// 0x1B8D8: the enemy is stunned: handlers 0x1B97A / 0x1B952 / 0x1B9BC /
// 0x14454 and sprite set 0x1CB96A.
void loc_01B8D8(uint32_t a0) {
    wr16(a0 + 4, rd16(a0 + 4) & 0xFF2F);
    wr32(a0 + 0x12, 0x1B97A);
    wr8(a0 + 0x1F, 0);
    wr32(a0 + 0xA, 0x1B9BC);
    wr32(a0 + 0x16, 0x14454);
    wr32(a0 + 0xE, 0x1B952);
    wr16(a0 + 0x2A, 0);
    wr16(a0 + 0x2C, 0);
    wr32(a0 + 0x42, 0x1CB96A);
    wr8(a0 + 0x47, 0x35);
    sub_057E0C();
    if (!link_mode) return;
    wr32(a0 + 0x1A, 0x182DA);
    wr8(0xFF0FFA, 0x12);
    wr8(0xFF0FFB, rd8(a0 + 0x3E));
    wr8(0xFF0FFC, uint8_t(rd8(a0 + 5) | 0x20));
    wr8(0xFF0FFD, rd8(a0 + 0x32));
    sub_01C03A(0xFF0FFA);
}

// 0x1B952
void h_01B952(uint32_t a0, uint16_t d5, uint16_t d1) {
    wr32(a0 + 0x42, 0x1CB96A);
    uint16_t d2 = (blink_sum(a0) >> 2) & 1;
    if (rd8(a0 + 0x35) & 1) d2 = uint16_t(d2 + 3);
    sub_01BA04(a0, 0, d2, s16(d1), d5);
}

// 0x1B97A
void h_01B97A(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    slow_down(a0);
    loc_0160E8(a0);
}

// 0x1B9BC (and its copies): pushed away by a hit, flips the frame.
void h_01B9BC(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    wr16(a0 + 0x2A, 0);
    wr16(a0 + 0x2C, 0);
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    set_knock(a0, dir, int16_t(f >> 4));
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) + 1));
}

// 0x157CA: blood: up to 5 particles for damage d0 >= 0x2F8, flying with
// (d3, d4) plus noise.
void sub_0157CA(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    int16_t n = int16_t(((s16(uint16_t(d0 - 0x338))) >> 6) + 1);
    if (n < 0 || n > 4) return;
    for (; n >= 0; n--) {
        if (U16{0xFF0FF6} == 0x10) return;
        uint32_t e = sub_013466();
        if (!e) return;
        U16{0xFF0FF6} = uint16_t(U16{0xFF0FF6} + 1);
        wr16(e + 0x20, rd16(a0 + 0x20));
        wr16(e + 0x22, rd16(a0 + 0x22));
        wr32(e + 0x12, 0x158CC);
        wr32(e + 0xE, 0x159FE);
        wr32(e + 0x16, 0x159EE);
        wr16(e + 0x2A, d3);
        wr16(e + 0x2C, d4);
        int32_t r = int32_t(Random());
        r >>= 5;
        wr16(e + 0x2A, uint16_t(rd16(e + 0x2A) + (r & 0x7F) - 0x40));
        r >>= 7;
        wr16(e + 0x2C, uint16_t(rd16(e + 0x2C) + (r & 0x7F) - 0x40));
        r >>= 7;
        wr16(e + 0x2E, uint16_t((r & 0xF) - 9));
        r >>= 4;
        wr16(e + 0x24, uint16_t((r & 0xF) - 8));
        wr8(e + 0x34, 0);
        if (link_mode) {
            wr32(e + 0x1A, 0x16ED8);
            wr8(0xFF0FFA, 4);
            wr8(0xFF0FFB, rd8(e + 0x3E));
            wr16(0xFF0FFC, rd16(e + 0x20));
            wr16(0xFF0FFE, rd16(e + 0x22));
            wr8(0xFF1000, rd8(e + 0x25));
            wr8(0xFF1001, uint8_t(rd8(e + 5) | 0x20));
            wr8(0xFF1002, rd8(e + 0x32));
            wr8(0xFF1003, 9);
            wr16(0xFF1004, rd16(e + 0x2A));
            wr16(0xFF1006, rd16(e + 0x2C));
            sub_01C03A(0xFF0FFA);
        }
    }
}

// 0x159EE: removes a blood particle.
void h_0159EE(uint32_t a0) {
    if (U16{0xFF0FF6}) U16{0xFF0FF6} = uint16_t(U16{0xFF0FF6} - 1);
    sub_013554(a0);
}

// 0x158CC: blood particle: flies in 16 substeps until it hits a wall (it
// then sticks, state 1) or falls to the floor (state 2), then fades.
void h_0158CC(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    if (rd8(a0 + 0x34)) {
        if (!(blink_sum(a0) & 7) && s16(rd16(a0 + 0x24)) > -0x10) wr16(a0 + 0x24, uint16_t(rd16(a0 + 0x24) - 1));
        uint16_t sum = uint16_t(rd8(a0 + 0x35) + uint8_t(uint16_t(s16(uint16_t(U16{0xFF0FF6} + 1)) >> 1)));
        wr8(a0 + 0x35, uint8_t(sum));
        if (sum > 0xFF) h_0159EE(a0);
        return;
    }
    if (rd16(a0 + 0x2A) | rd16(a0 + 0x2C)) {
        int32_t x = int32_t(s16(rd16(a0 + 0x20))) << 4, y = int32_t(s16(rd16(a0 + 0x22))) << 4;
        int32_t vx = s16(rd16(a0 + 0x2A)), vy = s16(rd16(a0 + 0x2C));
        for (int i = 0; i < 16; i++, x += vx, y += vy) {
            uint16_t nx = uint16_t((x + vx) >> 4), ny = uint16_t((y + vy) >> 4);
            uint8_t hit = sub_013D42(a0, nx, ny);
            if (!hit) continue;
            if (hit >= 6 || sub_013CBC(a0, nx, ny)) return h_0159EE(a0);
            wr32(a0 + 0x1A, 0x16F02);
            wr16(a0 + 0x20, uint16_t(x >> 4));
            wr16(a0 + 0x22, uint16_t(y >> 4));
            wr16(a0 + 0x2E, 0);
            wr16(a0 + 0x2A, 0);
            wr16(a0 + 0x2C, 0);
            wr8(a0 + 0x34, 1);
            wr8(a0 + 0x35, 0);
            return;
        }
        wr16(a0 + 0x20, uint16_t(x >> 4));
        wr16(a0 + 0x22, uint16_t(y >> 4));
        wr16(a0 + 0x2A, uint16_t(rd16(a0 + 0x2A) - (s16(rd16(a0 + 0x2A)) >> 2)));
        wr16(a0 + 0x2C, uint16_t(rd16(a0 + 0x2C) - (s16(rd16(a0 + 0x2C)) >> 2)));
    }
    if (rd16(a0 + 0x24) == 0xFFE0) return;
    uint16_t dz = rd16(a0 + 0x2E);
    wr16(a0 + 0x24, uint16_t(rd16(a0 + 0x24) + dz));
    if (s16(rd16(a0 + 0x24)) <= -0x20) {
        wr16(a0 + 0x24, 0xFFE0);
        wr16(a0 + 0x2A, 0);
        wr16(a0 + 0x2C, 0);
        wr32(a0 + 0x1A, 0x16F2C);
        wr8(a0 + 0x34, 2);
        wr8(a0 + 0x35, 0);
    } else
        wr16(a0 + 0x2E, uint16_t(dz - 2));
}

namespace {

// Screen row of the enemy's height for scale d5 (0x15A02-0x15A14 and copies).
int16_t sprite_row(uint32_t a0, uint16_t d5) {
    U16{0xFF1094} = d5;
    int16_t d3 = s16(uint16_t(U16{0xFF0E1A} - U16{0xFF116E} - rd16(a0 + 0x24)));
    return int16_t(uint16_t(((int32_t(s16(d5)) * d3) >> 6) + 0x28));
}

// One sprite d5 / 4 high, half as wide, centred on (d1, row).
void draw_small(uint32_t a1, uint16_t d5, int16_t col, int16_t row) {
    int16_t h = int16_t(s16(d5) >> 2), w = int16_t(h >> 1);
    DrawSprite(h, w, int16_t(col - (w >> 1)), int16_t(row - w), a1);
}

} // namespace

// 0x159FE: blood particle: a drop, a splash on a wall or a pool on the floor.
void h_0159FE(uint32_t a0, uint16_t d5, uint16_t d1) {
    int16_t row = sprite_row(a0, d5);
    uint16_t d7 = rd8(a0 + 0x35);
    U16{0xFF1088} = rd8(a0 + 0x3E) & 1;
    int16_t col = s16(d1);
    switch (rd8(a0 + 0x34)) {
    case 0: return draw_small(0x1163BE, d5, col, row);
    case 1: {
        int16_t h = int16_t(int16_t(uint16_t((uint32_t(d5) * uint16_t(d7 + 0x100)) >> 8)) >> 3);
        int16_t w = int16_t(int16_t(uint16_t((uint32_t(d5) * uint16_t(0x100 - d7)) >> 8)) >> 4);
        return DrawSprite(h, w, int16_t(col - (w >> 1)), row, 0x1165BE);
    }
    default: {
        int16_t w = int16_t(int16_t(uint16_t((uint32_t(d5) * uint16_t(d7 + 0x80)) >> 7)) >> 3);
        int16_t h = int16_t(int16_t(uint16_t((uint32_t(d5) * uint16_t(0x100 - d7)) >> 8)) >> 4);
        return DrawSprite(h, w, int16_t(col - (w >> 1)), int16_t(row - (h >> 1)), 0x1165BE);
    }
    }
}

// 0x15AC4: bullet spark: rises for 3 frames.
void h_015AC4(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    wr8(a0 + 0x34, uint8_t(rd8(a0 + 0x34) - 1));
    if (int8_t(rd8(a0 + 0x34)) < 0) return sub_013554(a0);
    wr16(a0 + 0x24, uint16_t(rd16(a0 + 0x24) + 2));
}

// 0x15AD6: bullet spark frames (0x1167BE).
void h_015AD6(uint32_t a0, uint16_t d5, uint16_t d1) {
    int16_t row = sprite_row(a0, d5);
    uint32_t a1 = 0x1167BE;
    uint8_t f = uint8_t(2 - rd8(a0 + 0x34));
    if (f) a1 += uint32_t(int32_t(int16_t(uint16_t(f << 9))));
    U16{0xFF1088} = 0;
    draw_small(a1, d5, s16(d1), row);
}

// 0x16040: a small blinking sprite (0x10EBBE).
void h_016040(uint32_t a0, uint16_t d5, uint16_t d1) {
    int16_t row = sprite_row(a0, d5);
    U16{0xFF1088} = blink_sum(a0) & 1;
    draw_small(0x10EBBE, d5, s16(d1), row);
}

// ---- Objects the weapons create (0x13AD2-0x1444A).

namespace {

void link_state(uint32_t a0, uint32_t handler) {
    if (!link_mode) return;
    wr32(a0 + 0x1A, handler);
    wr8(0xFF0FFA, 0x12);
    wr8(0xFF0FFB, rd8(a0 + 0x3E));
    wr8(0xFF0FFC, uint8_t(rd8(a0 + 5) | 0x20));
    wr8(0xFF0FFD, rd8(a0 + 0x32));
    sub_01C03A(0xFF0FFA);
}

void link_remove(uint32_t a0) {
    if (link_mode) {
        wr8(0xFF0FFA, 5);
        wr8(0xFF0FFB, rd8(a0 + 0x3E));
        sub_01C03A(0xFF0FFA);
    }
    sub_013526(a0);
}

void explosion_sound(uint32_t a0, uint16_t id, uint16_t time) {
    U16{0xFF2716} = 0;
    U16{0xFF2718} = 0;
    sub_00D740(a0, id);
    U16{0xFF2718} = time;
}

// 0x13DE8: turns the object into an explosion (0x14196 / 0x141E2).
void become_explosion(uint32_t a0) {
    wr32(a0 + 0xE, 0x141E2);
    wr32(a0 + 0x12, 0x14196);
    wr32(a0 + 0xA, 0x13298);
    wr16(a0 + 4, rd16(a0 + 4) & 0xFF37);
    wr8(a0 + 0x1F, 0);
    wr8(a0 + 0x1E, 0);
    link_state(a0, 0x16E34);
}

// 0x13DCC
void explode(uint32_t a0) {
    explosion_sound(a0, 0x8A, 0x1E);
    become_explosion(a0);
}

// 0x13E42 / 0x13EC0: explodes on the player at once (the flags are or-ed
// with 0xFF37 where the other paths and them).
void explode_on_player(uint32_t a0, uint16_t sound, bool shielded) {
    explosion_sound(a0, sound, 0x1E);
    uint16_t px = U16{0xFF0DF0}, py = U16{0xFF0DF2};
    uint16_t dx = uint16_t(rd16(a0 + 0x20) - player_x), dy = uint16_t(rd16(a0 + 0x22) - player_y);
    uint16_t d = Distance(s16(dx), s16(dy));
    if (shielded)
        sub_00D7DC(d, dx, dy);
    else
        sub_00D800(d, dx, dy);
    U16{0xFF0DF2} = py;
    U16{0xFF0DF0} = px;
    wr32(a0 + 0xE, 0x141E2);
    wr32(a0 + 0x12, 0x14196);
    wr32(a0 + 0x1A, 0x16E34);
    wr16(a0 + 4, rd16(a0 + 4) | 0xFF37);
    wr32(a0 + 0xA, 0x13298);
    wr8(a0 + 0x1F, 0);
    wr8(a0 + 0x1E, 2);
}

// Halves a velocity word, rounding towards zero.
uint16_t half(uint16_t v) { return uint16_t(s16(v) < 0 ? -(-s16(v) >> 1) : s16(v) >> 1); }

// neg.w then asr.w on memory: reverses and halves a velocity.
void bounce(uint32_t at) { wr16(at, uint16_t(s16(uint16_t(-rd16(at))) >> 1)); }

// Projectile sprites centred on column d1 at the object's height; the
// width is height >> wshift, the row moves up by height >> rshift.
void draw_shot(uint32_t a0, uint16_t d5, uint16_t d1, uint32_t a1, int16_t h, int wshift, int rshift) {
    int16_t row = sprite_row(a0, d5);
    int16_t w = int16_t(h >> wshift);
    DrawSprite(h, w, int16_t(s16(d1) - (w >> 1)), int16_t(row - (h >> rshift)), a1);
}

// 0x9F8E table index of the enemy start cell classes -> template (0xA09A).
// 0xA08E: spawns the enemy of start cell a1 (on the level in cur_level).
void sub_00A08E(uint16_t d3, uint32_t a1) {
    uint32_t t = rd32(0xA09A + (d3 - 1) * 4);
    U32{0xFF26F2} = t;
    if (a1 < 0xFF7CD6 || a1 >= 0xFFBCD6) a1 = 0xFFCCD6;
    wr8(a1, 0);
    sub_01C9A0(a1);
    uint32_t e = sub_013466();
    if (!e) return;
    wr8(e + 0x1F, 0);
    wr32(e + 0x12, rd32(t));
    wr32(e + 0xE, rd32(t + 4));
    wr32(e + 0xA, rd32(t + 8));
    wr32(e + 0x16, rd32(t + 0xC));
    wr16(e + 4, uint16_t(rd16(e + 4) | rd16(t + 0x10)));
    wr16(e + 0x36, rd16(t + 0x12));
    wr8(e + 0x32, U8{0xFF1029});
    uint16_t idx = uint16_t(a1 - enemy_map(e)) & 0x3FF;
    wr16(e + 0x20, uint16_t(((idx & 0x1F) << 8) + 0x80));
    wr16(e + 0x22, uint16_t(((idx << 3) & 0xFF00) | 0x80));
    wr16(e + 0x24, rd16(t + 0x1A));
    wr8(e + 0x34, 0);
    wr8(e + 0x35, 0x14);
    wr8(e + 0x41, 0);
    wr16(e + 0x38, player_x);
    wr16(e + 0x3A, player_y);
    wr32(e + 0x42, rd32(t + 0x14));
    if (cur_level && rd32(e + 0x42) == 0x16EC58 && ((rd16(e + 0x20) ^ rd16(e + 0x22)) & 0x100)) wr32(e + 0x42, 0x1C258A);
    wr8(e + 0x46, rd8(t + 0x18));
    wr8(e + 0x47, rd8(t + 0x19));
    wr32(e + 6, player_struct);
    sub_00D760(rd16(t + 0x24));
}

// 0x9E1E: spawns the enemies of the start cells in the 7x7 cells around
// cell a0, skipping rows and columns outside the map as seen from the
// player's position.
void sub_009E1E(uint32_t a0) {
    int16_t row = int16_t((s16(player_y) >> 8) - 4);
    uint32_t a1 = a0 - 0x63;
    for (int i = 0; i < 7; i++) {
        row++;
        if (row >= 0) {
            if (row >= 0x20) return;
            int16_t col = int16_t((s16(player_x) >> 8) - 4);
            for (int j = 0; j < 7; j++, a1++) {
                col++;
                if (col < 0 || col >= 0x20) continue;
                uint8_t n = rd8(0x9F8E + rd8(cell_class + rd8(a1)));
                if (n) sub_00A08E(n, a1);
            }
            a1 -= 7;
        }
        a1 += 0x20;
    }
}

// 0x16294: an explosion at (d0, d1) on the level of a0: wakes the enemy
// start cells around it, then hits every enemy and the player within 0x400
// in sight (FF26F0 set within 0x80).
void loc_016294(uint32_t a0, uint16_t d0, uint16_t d1) {
    uint8_t level = rd8(a0 + 0x32);
    uint16_t saved = cur_level;
    cur_level = level;
    sub_009E1E(enemy_map(a0) + cell_index(d0, d1));
    cur_level = saved;
    uint16_t n = enemy_count;
    for (uint32_t e = n ? uint32_t(enemy_list) : 0; n; n--) {
        uint32_t next = rd32(e);
        if (rd8(e + 0x32) == level && !sub_0166E8(e, d0, d1, rd16(e + 0x20), rd16(e + 0x22))) {
            uint16_t dx = uint16_t(d0 - rd16(e + 0x20)), dy = uint16_t(d1 - rd16(e + 0x22));
            uint16_t d = Distance(s16(dx), s16(dy));
            if (d < 0x400) {
                if (d <= 0x80) U8{0xFF26F0} = 1;
                CallEnemyHit(rd32(e + 0xA), e, d, dx, dy);
                U8{0xFF26F0} = 0;
            }
        }
        e = next;
    }
    if (level != U8{0xFF1029}) return;
    if (sub_0165D8(a0, d0, d1, player_x, player_y)) return;
    uint16_t dx = uint16_t(d0 - player_x), dy = uint16_t(d1 - player_y);
    uint16_t d = Distance(s16(dx), s16(dy));
    if (U16{0xFF1030} == 1) d = uint16_t(d << 1);
    if (d < 0x400) sub_00D800(d, dx, dy);
}

} // namespace

// 0xA6BC: a blast at (d0, d1) breaks the breakable cell there (classes
// 0x79, 0x7B, 0x7D, 0x7F) into the cell byte FF6340 + 2n.
void sub_00A6BC(uint16_t d0, uint16_t d1) {
    uint32_t a0 = cur_map_ptr + cell_index(d0, d1);
    uint8_t cls = rd8(cell_class + rd8(a0));
    if (cls != 0x79 && cls != 0x7B && cls != 0x7D && cls != 0x7F) return;
    if (a0 < 0xFF7CD6 || a0 >= 0xFFBCD6) a0 = 0xFFCCD6;
    wr8(a0, rd8(0xFF6340 + (cls - 0x79)));
    sub_01C9A0(a0);
    U16{0xFF2716} = 0;
    U16{0xFF2718} = 0;
    sub_00D760(0x3B);
    U16{0xFF2718} = 0x14;
}

// 0x13AD2: a thrown bomb explodes when hit nearer than 0x200.
void h_013AD2(uint32_t a0, uint16_t d0) {
    if (d0 < 0x200) explode(a0);
}

// 0x13ADC: thrown bomb: bounces off walls and the floor, then explodes,
// breaking the cells around it.
void h_013ADC(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    if (rd16(a0 + 0x2A) | rd16(a0 + 0x2C)) {
        uint16_t x = rd16(a0 + 0x20), y = rd16(a0 + 0x22);
        uint16_t nx = uint16_t(x + rd16(a0 + 0x2A)), ny = uint16_t(y + rd16(a0 + 0x2C));
        if (sub_013D42(a0, nx, ny)) {
            if (sub_013CBC(a0, nx, ny)) return sub_013554(a0);
            sub_00D740(a0, 0x6C);
            bool xwall = sub_013D42(a0, uint16_t(rd16(a0 + 0x20) + rd16(a0 + 0x2A)), rd16(a0 + 0x22));
            bool ywall = sub_013D42(a0, rd16(a0 + 0x20), uint16_t(rd16(a0 + 0x22) + rd16(a0 + 0x2C)));
            if (xwall == ywall) {
                bounce(a0 + 0x2C);
                bounce(a0 + 0x2A);
            } else if (xwall)
                bounce(a0 + 0x2A);
            else
                bounce(a0 + 0x2C);
        }
        wr16(a0 + 0x20, uint16_t(rd16(a0 + 0x20) + rd16(a0 + 0x2A)));
        wr16(a0 + 0x22, uint16_t(rd16(a0 + 0x22) + rd16(a0 + 0x2C)));
    }
    uint16_t vz = rd16(a0 + 0x2E);
    wr16(a0 + 0x24, uint16_t(rd16(a0 + 0x24) + vz));
    if (s16(rd16(a0 + 0x24)) > -0x18)
        wr16(a0 + 0x2E, uint16_t(vz - 3));
    else {
        wr16(a0 + 0x24, 0xFFE8);
        if (vz) {
            vz = uint16_t(s16(uint16_t(-(vz - 3))) >> 2);
            if (vz < 3) vz = 0;
            wr16(a0 + 0x2E, vz);
            sub_00D740(a0, 0x6C);
        }
        if (rd16(a0 + 0x24) == 0xFFE8) {
            wr16(a0 + 0x2A, half(rd16(a0 + 0x2A)));
            wr16(a0 + 0x2C, half(rd16(a0 + 0x2C)));
        }
    }
    wr8(a0 + 0x34, uint8_t(rd8(a0 + 0x34) - 1));
    if (rd8(a0 + 0x34)) return;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            sub_00A6BC(uint16_t(rd16(a0 + 0x20) - 0x100 + i * 0x100), uint16_t(rd16(a0 + 0x22) - 0x100 + j * 0x100));
    explode(a0);
}

void h_013E42(uint32_t a0) { explode_on_player(a0, 0x8A, false); }
void h_013EC0(uint32_t a0) { explode_on_player(a0, 0x6D, true); }

// 0x13F3E: thrown bomb sprite.
void h_013F3E(uint32_t a0, uint16_t d5, uint16_t d1) {
    U16{0xFF1088} = 0;
    draw_shot(a0, d5, d1, 0x1109BE, int16_t(s16(d5) >> 2), 1, 1);
}

// 0x13F7C: mine: every 20 frames picks the nearest enemy with flag 0x10 (or
// the player), and explodes when it comes within 0x100.
void h_013F7C(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    wr8(a0 + 0x1E, uint8_t(rd8(a0 + 0x1E) - 1));
    if (int8_t(rd8(a0 + 0x1E)) < 0) {
        wr8(a0 + 0x1E, 0x14);
        uint32_t a1 = sub_016388(a0, 0x10);
        wr32(a0 + 6, a1);
        if (int32_t(a1) < 0) return sub_013554(a0);
    }
    uint32_t a1 = rd32(a0 + 6);
    if (a1 == 0xFFFFFFFF) return;
    uint16_t tx, ty;
    if (a1 == 0 || a1 == player_struct) {
        if (U16{0xFF1030} == 1) return;
        tx = a1 ? rd16(a1 + 0x20) : uint16_t(player_x);
        ty = a1 ? rd16(a1 + 0x22) : uint16_t(player_y);
    } else {
        if (!(rd16(a0 + 4) & 1)) return sub_013554(a0);
        tx = rd16(a1 + 0x20);
        ty = rd16(a1 + 0x22);
    }
    if (Distance(s16(uint16_t(rd16(a0 + 0x20) - tx)), s16(uint16_t(rd16(a0 + 0x22) - ty))) < 0x100) explode(a0);
}

// 0x1402E: mine sprite, blinking.
void h_01402E(uint32_t a0, uint16_t d5, uint16_t d1) {
    U16{0xFF1088} = 0;
    uint32_t a1 = (U8{0xFF0EB7} & 2) ? 0x10FBBE : 0x10F9BE;
    draw_shot(a0, d5, d1, a1, int16_t(s16(d5) >> 2), 0, 0);
}

// 0x14076: grenade: flies until it hits a wall or its time runs out, then
// explodes (an open sky cell takes it as a lightning bolt instead).
void h_014076(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    wr8(a0 + 0x34, uint8_t(rd8(a0 + 0x34) - 1));
    uint16_t nx = uint16_t(rd16(a0 + 0x20) + rd16(a0 + 0x2A)), ny = uint16_t(rd16(a0 + 0x22) + rd16(a0 + 0x2C));
    if (rd8(a0 + 0x34) && !sub_013D42(a0, nx, ny)) {
        wr16(a0 + 0x20, nx);
        wr16(a0 + 0x22, ny);
        return;
    }
    if (nx >= 0x1F00) {
        uint8_t cls = rd8(cell_class + rd8(enemy_map(a0) + cell_index(rd16(a0 + 0x20), rd16(a0 + 0x22))));
        if (cls == 0x28 || (cls == 0x27 && s16(rd16(a0 + 0x24)) > 0)) {
            sub_0028B4(0x120);
            return sub_013554(a0);
        }
    }
    if (sub_013CBC(a0, nx, ny)) return sub_013554(a0);
    sub_00A6BC(nx, ny);
    wr16(a0 + 0x2A, 0);
    wr16(a0 + 0x2C, 0);
    explosion_sound(a0, 0x18, 0x32);
    become_explosion(a0);
}

// 0x14150: grenade and fire ball sprite.
void h_014150(uint32_t a0, uint16_t d5, uint16_t d1) {
    U16{0xFF1088} = U16{0xFF0EB6} & 1;
    draw_shot(a0, d5, d1, 0x10EBBE, int16_t(s16(d5) >> 1), 1, 1);
}

// 0x14196: explosion: hits what is around at step 2, ends at step 6.
void h_014196(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    wr8(a0 + 0x1E, uint8_t(rd8(a0 + 0x1E) + 1));
    if (rd8(a0 + 0x1E) == 6) return link_remove(a0);
    if (rd8(a0 + 0x1E) == 2) loc_016294(a0, rd16(a0 + 0x20), rd16(a0 + 0x22));
}

// 0x141E2: explosion sprite, sized by step (table 0x14234).
void h_0141E2(uint32_t a0, uint16_t d5, uint16_t d1) {
    U16{0xFF1088} = U16{0xFF0EB6} & 1;
    uint8_t shift = rd8(0x14234 + rd8(a0 + 0x1E));
    draw_shot(a0, d5, d1, 0x10EBBE, int16_t(s16(uint16_t(d5 + d5)) >> shift), 1, 1);
}

// 0x1423A: fire on the floor, burns for 16 frames.
void h_01423A(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    wr8(a0 + 0x1E, uint8_t(rd8(a0 + 0x1E) + 1));
    if (rd8(a0 + 0x1E) == 0x10) link_remove(a0);
}

// 0x14272: the fire shrinks as it burns down.
void h_014272(uint32_t a0, uint16_t d5, uint16_t d1) {
    U16{0xFF1094} = d5;
    int16_t d3 = s16(uint16_t(U16{0xFF0E1A} - U16{0xFF116E} + 0x20));
    int16_t row = int16_t(uint16_t(((int32_t(s16(d5)) * d3) >> 6) + 0x28));
    U16{0xFF1088} = U16{0xFF0EB6} & 1;
    int16_t h = int16_t(uint16_t((int32_t(s16(d5)) * int16_t(0x10 - rd8(a0 + 0x1E))) >> 4));
    int16_t w = int16_t(h >> 1);
    DrawSprite(h, w, int16_t(s16(d1) - (w >> 1)), int16_t(row - h), 0x10E9BE);
}

// 0x142C6: fire ball: arcs until it lands, hits a wall or its time runs out,
// then sets fire to what is within 0x100 (the player within 0x30).
void h_0142C6(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    wr8(a0 + 0x34, uint8_t(rd8(a0 + 0x34) - 1));
    if (rd8(a0 + 0x34)) {
        wr16(a0 + 0x24, uint16_t(rd16(a0 + 0x24) - rd16(a0 + 0x2E)));
        wr16(a0 + 0x2E, uint16_t(rd16(a0 + 0x2E) + 1));
        if (s16(rd16(a0 + 0x24)) >= -0x1C) {
            uint16_t nx = uint16_t(rd16(a0 + 0x20) + rd16(a0 + 0x2A)), ny = uint16_t(rd16(a0 + 0x22) + rd16(a0 + 0x2C));
            if (!sub_013D42(a0, nx, ny)) {
                wr16(a0 + 0x20, nx);
                wr16(a0 + 0x22, ny);
                return;
            }
        }
    }
    U8{0xFF26F0} = 1;
    U8{0xFF26F1} = 1;
    uint16_t d0 = rd16(a0 + 0x20), d1 = rd16(a0 + 0x22);
    uint8_t level = rd8(a0 + 0x32);
    // The sound below reads the level of a0 as the loop left it.
    uint32_t last = a0;
    uint16_t n = enemy_count;
    if (n) {
        uint32_t e = enemy_list;
        for (; n; n--) {
            uint32_t next = rd32(e);
            if ((rd16(e + 4) & 0xC8) && rd8(e + 0x32) == level && !sub_0166E8(e, d0, d1, rd16(e + 0x20), rd16(e + 0x22))) {
                uint16_t dx = uint16_t(d0 - rd16(e + 0x20)), dy = uint16_t(d1 - rd16(e + 0x22));
                uint16_t d = Distance(s16(dx), s16(dy));
                if (d < 0x100) {
                    uint16_t blood = U16{0xFF0FF6};
                    U16{0xFF0FF6} = 0x10;
                    CallEnemyHit(rd32(e + 0xA), e, d, dx, dy);
                    U16{0xFF0FF6} = blood;
                    wr16(e + 0x2A, 0);
                    wr16(e + 0x2C, 0);
                }
            }
            e = next;
        }
        last = e;
    }
    if (cur_level == level && !sub_0165D8(last, d0, d1, player_x, player_y)) {
        uint16_t dx = uint16_t(d0 - player_x), dy = uint16_t(d1 - player_y);
        uint16_t d = Distance(s16(dx), s16(dy));
        if (d < 0x30) sub_00D7DC(d, dx, dy);
    }
    U8{0xFF26F0} = 0;
    U8{0xFF26F1} = 0;
    sub_00D740(last, 0x6D);
    wr8(a0 + 0x1F, 0);
    wr8(a0 + 0x1E, 0);
    wr32(a0 + 0x12, 0x1423A);
    wr32(a0 + 0xE, 0x14272);
    wr16(a0 + 0x24, 0xFFE0);
    wr16(a0 + 4, rd16(a0 + 4) & 0xFF37);
    link_state(a0, 0x16E88);
}

// 0x15F38: makes a0 a 0x15F6E shot at (d0, d1) with velocity (d3, d4),
// height d5 and vertical speed d6.
void sub_015F38(uint32_t a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4, uint16_t d5, uint16_t d6) {
    wr16(a0 + 0x20, d0);
    wr16(a0 + 0x22, d1);
    wr16(a0 + 0x2A, d3);
    wr16(a0 + 0x2C, d4);
    wr16(a0 + 0x24, d5);
    wr16(a0 + 0x2E, d6);
    wr8(a0 + 0x1F, 0);
    wr32(a0 + 0x12, 0x15F6E);
    wr32(a0 + 0xE, 0x16040);
}

// 0x15F6E: enemy shot: flies until a wall, hits the target within 0x40
// unless it passes over (the target jumping) or under (crouching) it.
void h_015F6E(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    int16_t h = s16(uint16_t(rd16(a0 + 0x24) + rd16(a0 + 0x2E)));
    if (h < -0x1C) h = -0x1C;
    else if (h > 0x1C) h = 0x1C;
    wr16(a0 + 0x24, uint16_t(h));
    uint16_t nx = uint16_t(rd16(a0 + 0x20) + rd16(a0 + 0x2A)), ny = uint16_t(rd16(a0 + 0x22) + rd16(a0 + 0x2C));
    if (sub_013D42(a0, nx, ny)) return sub_013554(a0);
    wr16(a0 + 0x20, nx);
    wr16(a0 + 0x22, ny);
    uint32_t a3 = sub_01642C(a0);
    if (!a3) return sub_013554(a0);
    if (Distance(s16(uint16_t(rd16(a3 + 0x20) - nx)), s16(uint16_t(rd16(a3 + 0x22) - ny))) > 0x40) return;
    int16_t jump = s16(U16{0xFF0E1A});
    if (h < -8) {
        if (jump > 0) return;
    } else if (h > 8 && jump < 0)
        return;
    CallEnemyHit(rd32(a3 + 0xA), a3, 0x1F4, uint16_t(nx - rd16(a3 + 0x20)), uint16_t(ny - rd16(a3 + 0x22)));
    U16{0xFF2716} = 0;
    U16{0xFF2718} = 0;
    sub_00D740(a0, 0x1B);
    U16{0xFF2718} = 0xF;
    sub_013554(a0);
}

// ---- The boss start cell (class 0x26, spawned by 0xA796): AI 0x15B1E, draw
// 0x15D84, hit 0x15E4E, death 0x15ED2. Once the target is near and in sight
// it counts down (state 1), then wakes the enemy start cells and opens the
// doors in the 11x11 cells around it (state 2).

namespace {

// The cells of the 11x11 square around cell a0 (the cell of a2) that lie on
// the map: fn(cell, column, row).
template <class F> void around_11(uint32_t a0, uint32_t a2, F fn) {
    int16_t row = int16_t((s16(rd16(a2 + 0x22)) >> 8) - 6);
    uint32_t a1 = a0 - 0xA5;
    for (int i = 0; i < 11; i++) {
        row++;
        if (row >= 0) {
            if (row >= 0x20) return;
            int16_t col = int16_t((s16(rd16(a2 + 0x20)) >> 8) - 6);
            for (int j = 0; j < 11; j++, a1++) {
                col++;
                if (col >= 0 && col < 0x20) fn(a1, col, row);
            }
            a1 -= 0xB;
        }
        a1 += 0x20;
    }
}

uint32_t own_cell(uint32_t a0) { return enemy_map(a0) + cell_index(rd16(a0 + 0x20), rd16(a0 + 0x22)); }

// 0x15D18: spawns the enemies of the start cells around (on a2's level).
void sub_015D18(uint32_t a0, uint32_t a2) {
    around_11(a0, a2, [&](uint32_t a1, int16_t, int16_t) {
        uint8_t n = rd8(0x9F8E + rd8(cell_class + rd8(a1)));
        if (!n) return;
        uint16_t saved = cur_level;
        U8{0xFF1029} = rd8(a2 + 0x32);
        sub_00A08E(n, a1);
        cur_level = saved;
    });
}

// 0x15C74: opens the doors around.
void sub_015C74(uint32_t a0, uint32_t a2) {
    around_11(a0, a2, [&](uint32_t a1, int16_t col, int16_t row) {
        uint8_t cls = rd8(cell_class + rd8(a1));
        if (cls == 6 || cls == 0x83)
            sub_00B130(a1, uint16_t(col << 8), uint16_t(row << 8));
        else if (cls == 7 || cls == 0x84)
            sub_00B168(a1, uint16_t(col << 8), uint16_t(row << 8));
    });
    sub_00D760(0x67);
}

// The target (0x1642C) and its distance; false when there is none within
// 0xC00 (the cell then goes back to the map).
bool boss_target(uint32_t a0, uint32_t& a1, uint16_t& d) {
    a1 = sub_01642C(a0);
    if (!a1) return false;
    d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a1 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a1 + 0x22))));
    return d <= 0xC00;
}

} // namespace

void h_015B1E(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    uint32_t a1;
    uint16_t d;
    uint8_t state = rd8(a0 + 0x34);
    if (state == 0) {
        if (!boss_target(a0, a1, d)) return loc_015ED2(a0);
        bool hard = U16{0xFF1030} == 4;
        if (a1 == player_struct && hard && d > 0x180) return;
        if (d > 0x300) return;
        if (sub_0166A0(a0, rd16(a1 + 0x20), rd16(a1 + 0x22), rd16(a0 + 0x20), rd16(a0 + 0x22))) return;
        wr8(a0 + 0x35, hard ? 0x31 : 0x21);
        wr8(a0 + 0x34, 1);
        return;
    }
    if (state != 1) {
        if (!boss_target(a0, a1, d)) loc_015ED2(a0);
        return;
    }
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
    if (rd8(a0 + 0x35)) {
        if (!(rd8(a0 + 0x35) & 7)) sub_00D740(a0, 0x71);
        return;
    }
    wr8(a0 + 0x34, 2);
    sub_015D18(own_cell(a0), a0);
    sub_015C74(own_cell(a0), a0);
    sub_00D740(a0, 0x70);
}

// 0x15D84: the cell sprite, blinking.
void h_015D84(uint32_t, uint16_t d5, uint16_t d1) {
    U16{0xFF1094} = d5;
    int16_t d3 = s16(uint16_t(U16{0xFF0E1A} - U16{0xFF116E} - 0x20));
    int16_t row = int16_t(uint16_t(((int32_t(s16(d5)) * d3) >> 6) + 0x28));
    int16_t h = int16_t(s16(d5) >> 1);
    h = int16_t(h - (h >> 2));
    int16_t w = int16_t(h >> 1);
    U16{0xFF1088} = 0;
    DrawSprite(h, w, int16_t(s16(d1) - (w >> 1)), row, (U8{0xFF0EB7} & 2) ? 0x1111BE : 0x110FBE);
}

// 0x15E4E: a hit destroys it: it flies up as an explosion (0x15DD0).
void h_015E4E(uint32_t a0) {
    wr32(a0 + 0x12, 0x15DD0);
    wr32(a0 + 0xE, 0x15DFC);
    wr16(a0 + 4, rd16(a0 + 4) & 0xFF37);
    wr32(a0 + 0x16, 0x13298);
    wr32(a0 + 0xA, 0x13298);
    wr8(a0 + 0x35, 0);
    wr16(a0 + 0x24, 0x10);
    wr16(a0 + 0x2E, 0);
    explosion_sound(a0, 0x8A, 0x1E);
    link_state(a0, 0x16E5E);
}

// 0x15DD0
void h_015DD0(uint32_t a0) {
    uint16_t v = rd16(a0 + 0x2E);
    wr16(a0 + 0x24, uint16_t(rd16(a0 + 0x24) - v));
    wr16(a0 + 0x2E, uint16_t(v + 1));
    if (s16(rd16(a0 + 0x24)) < -0x20) wr16(a0 + 0x24, 0xFFE0);
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) + 1));
    if (rd8(a0 + 0x35) == 0xA) sub_013554(a0);
}

// 0x15DFC: the explosion, sized by step (table 0x15E48).
void h_015DFC(uint32_t a0, uint16_t d5, uint16_t d1) {
    int16_t row = sprite_row(a0, d5);
    int16_t h = int16_t(s16(d5) >> rd8(0x15E48 + (rd8(a0 + 0x35) >> 1)));
    int16_t w = int16_t(h >> 1);
    U16{0xFF1088} = U16{0xFF0EB6} & 1;
    DrawSprite(h, w, int16_t(s16(d1) - (w >> 1)), int16_t(row - w), 0x10EBBE);
}

// 0x15ED2: puts the cell (FF62EC) back on the map and removes the object.
void loc_015ED2(uint32_t a0) {
    uint32_t a1 = own_cell(a0);
    if (a1 < 0xFF7CD6 || a1 >= 0xFFBCD6) a1 = 0xFFCCD6;
    wr8(a1, U8{0xFF62EC});
    sub_01C9A0(a1);
    link_remove(a0);
}

// 0x13182: timed item charges, used every 128 frames.
void sub_013182() {
    uint16_t d0 = U16{0xFF0EB6} & 0x7F;
    if (!d0 && int16_t(uint16_t(U16{0xFF105E})) >= 0) d0 = sub_010BD8();
    if (int16_t(uint16_t(U16{0xFF105E})) < 0 && int16_t(uint16_t(U16{0xFF1060})) >= 0) {
        uint32_t d1 = 0xFF6196 + int16_t(uint16_t(U16{0xFF0E28} << 2));
        U32{0xFF106A} = d1;
        U32{0xFF106E} = d1 + 0x20;
        if (!((d0 + 0x10) & 0x7F)) sub_010CC6();
    }
    if (!((U16{0xFF0EB6} + 0x30) & 0x7F) && int16_t(uint16_t(U16{0xFF1062})) >= 0) sub_010DC0();
}

// 0x131E8: copies the player into the player's enemy struct (FF119C), then
// removes enemies that left the map and runs the AI handler (+0x12) of the
// others when their delay (+0x1F) runs out.
void sub_0131E8() {
    player_x = player_x & 0x1FFF;
    player_y = player_y & 0x1FFF;
    const uint32_t p = 0xFF119C;
    wr16(p + 0x24, U16{0xFF0E1A});
    wr16(p + 0x20, player_x);
    wr16(p + 0x22, player_y);
    wr16(p + 0x2A, U16{0xFF0E00});
    wr16(p + 0x2C, U16{0xFF0E02});
    wr8(p + 0x32, U8{0xFF1029});
    wr32(p + 0xA, 0xD800);
    uint16_t n = enemy_count;
    if (!n) return;
    for (uint32_t a0 = enemy_list; n; n--) {
        uint32_t next = rd32(a0);
        uint16_t x = rd16(a0 + 0x20), y = rd16(a0 + 0x22);
        wr16(a0 + 0x26, x);
        wr16(a0 + 0x28, y);
        uint8_t level = rd8(a0 + 0x32);
        bool off_map = x >= 0x2000 || y >= 0x2000 || level > 0xF;
        if (off_map && !(rd16(a0 + 4) & 0x20))
            sub_013554(a0);
        else {
            wr8(a0 + 0x1F, uint8_t(rd8(a0 + 0x1F) - 1));
            if (int8_t(rd8(a0 + 0x1F)) < 0) CallEnemyHandler(rd32(a0 + 0x12), a0);
        }
        a0 = next;
        if (!a0) return;
    }
}

// 0x149DE: drops the item FF62F2 on the first empty cell around the player.
void sub_0149DE() {
    uint32_t a1 = cur_map_ptr + cell_index(player_x, player_y);
    static const int16_t around[] = {0, -1, 1, -0x20, 0x20, 0x1F, 0x21, -0x1F, -0x21};
    for (int16_t d : around) {
        if (rd8(a1 + d)) continue;
        a1 += d;
        if (a1 < 0xFF7CD6 || a1 >= 0xFFBCD6) a1 = 0xFFCCD6;
        wr8(a1, U8{0xFF62F2});
        sub_01C9A0(a1);
        return;
    }
}

// 0x168AC: runs the death handler of every enemy without flag 0x20, then
// resets the doors (0xB48A).
void sub_0168AC() {
    uint16_t n = enemy_count;
    for (uint32_t a0 = enemy_list; n; n--) {
        uint32_t next = rd32(a0);
        if (!(rd16(a0 + 4) & 0x20)) enemy_die(a0);
        a0 = next;
    }
    loc_00B48A();
}

// 0x133A4: empties the enemy slots.
void sub_0133A4() {
    for (uint32_t a = 0xFF11E8; a < 0xFF11E8 + 0x980 * 2; a += 2) wr16(a, 0);
    enemy_count = 0;
    enemy_list = 0;
    U16{0xFF0FF6} = 0;
}

// 0x13554: removes an enemy (in a link game, the other console as well).
void sub_013554(uint32_t a0) {
    if (link_mode) {
        wr8(0xFF0FFA, 5);
        wr8(0xFF0FFB, rd8(a0 + 0x3E));
        sub_01C03A(0xFF0FFA);
    }
    sub_013526(a0);
}

// Handlers stored in the enemy structs: AI (+0x12) and death (+0x16) take
// the enemy, drawing (+0xE) also the scale d5 and column d1, hits (+0xA) the
// distance d0 and the direction d3/d4 to the shooter.
void CallEnemyHandler(uint32_t addr, uint32_t a0) {
    switch (addr) {
    case 0x13298: break;
    case 0x13526: sub_013526(a0); break;
    case 0x13554: sub_013554(a0); break;
    case 0x13ADC: h_013ADC(a0); break;
    case 0x13E42: h_013E42(a0); break;
    case 0x13EC0: h_013EC0(a0); break;
    case 0x13F7C: h_013F7C(a0); break;
    case 0x14076: h_014076(a0); break;
    case 0x14196: h_014196(a0); break;
    case 0x1423A: h_01423A(a0); break;
    case 0x142C6: h_0142C6(a0); break;
    case 0x14454: h_014454(a0); break;
    case 0x144FE: h_0144FE(a0); break;
    case 0x14A56: h_014A56(a0); break;
    case 0x14B08: h_014B08(a0); break;
    case 0x158CC: h_0158CC(a0); break;
    case 0x159EE: h_0159EE(a0); break;
    case 0x15AC4: h_015AC4(a0); break;
    case 0x182FC: h_0182FC(a0); break;
    case 0x186B8: h_0186B8(a0); break;
    case 0x1887A: h_01887A(a0); break;
    case 0x18BC4: h_018BC4(a0); break;
    case 0x19060: h_019060(a0); break;
    case 0x19540: h_019540(a0); break;
    case 0x19958: h_019958(a0); break;
    case 0x19A48: h_019A48(a0); break;
    case 0x1A324: h_01A324(a0); break;
    case 0x1A92A: h_01A92A(a0); break;
    case 0x1AEB4: h_01AEB4(a0); break;
    case 0x1B6A2: // same code
    case 0x19FD2: h_019FD2(a0); break;
    case 0x1B078: h_01B078(a0); break;
    case 0x14C2A: h_014C2A(a0); break;
    case 0x14C40: h_014C40(a0); break;
    case 0x15F6E: h_015F6E(a0); break;
    case 0x15B1E: h_015B1E(a0); break;
    case 0x15DD0: h_015DD0(a0); break;
    case 0x15ED2: loc_015ED2(a0); break;
    case 0x1943C: // same code
    case 0x1A808:
    case 0x18F5C: h_018F5C(a0); break;
    case 0x18AD0: // same code
    case 0x156D2:
    case 0x1B97A: h_01B97A(a0); break;
    default: untranslated(addr);
    }
}

void CallEnemyDraw(uint32_t addr, uint32_t a0, uint16_t d5, uint16_t d1) {
    switch (addr) {
    case 0x13298: break;
    case 0x13F3E: h_013F3E(a0, d5, d1); break;
    case 0x1402E: h_01402E(a0, d5, d1); break;
    case 0x14150: h_014150(a0, d5, d1); break;
    case 0x141E2: h_0141E2(a0, d5, d1); break;
    case 0x14272: h_014272(a0, d5, d1); break;
    case 0x159FE: h_0159FE(a0, d5, d1); break;
    case 0x15AD6: h_015AD6(a0, d5, d1); break;
    case 0x16040: h_016040(a0, d5, d1); break;
    case 0x1856C: h_01856C(a0, d5, d1); break;
    case 0x18660: h_018660(a0, d5, d1); break;
    case 0x18A66: h_018A66(a0, d5, d1); break;
    case 0x18AC8: body_frame(a0, 6, d5, d1); break;
    case 0x18EE8: h_018EE8(a0, d5, d1); break;
    case 0x18F50: body_walk(a0, 4, d5, d1); break;
    case 0x193C8: h_0193C8(a0, d5, d1); break;
    case 0x19430: body_walk(a0, 6, d5, d1); break;
    case 0x198C4: h_0198C4(a0, d5, d1); break;
    case 0x1994C: body_walk(a0, 6, d5, d1); break;
    case 0x19ECA: h_019ECA(a0, d5, d1); break;
    case 0x19FCA: body_frame(a0, 4, d5, d1); break;
    case 0x1A722: h_01A722(a0, d5, d1); break;
    case 0x1A7FC: body_walk(a0, 9, d5, d1); break;
    case 0x1AD9A: h_01AD9A(a0, d5, d1); break;
    case 0x1AE9E: body_frame(a0, 7, d5, d1); break;
    case 0x1B562: h_01B562(a0, d5, d1); break;
    case 0x1539E: h_01539E(a0, d5, d1); break;
    case 0x15618: h_015618(a0, d5, d1); break;
    case 0x15D84: h_015D84(a0, d5, d1); break;
    case 0x15DFC: h_015DFC(a0, d5, d1); break;
    case 0x1B68C: body_frame(a0, 0xB, d5, d1); break;
    case 0x1B952: h_01B952(a0, d5, d1); break;
    default: untranslated(addr);
    }
}

void CallEnemyHit(uint32_t addr, uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    switch (addr) {
    case 0x13298: break;
    case 0xD800: sub_00D800(d0, d3, d4); break;
    case 0x13AD2: h_013AD2(a0, d0); break;
    case 0x1999A: h_01999A(a0, d0, d3, d4); break;
    case 0x1A154: h_01A154(a0, d0, d3, d4); break;
    case 0x1A84A: h_01A84A(a0, d0, d3, d4); break;
    case 0x1AFC6: h_01AFC6(a0, d0, d3, d4); break;
    case 0x1B824: h_01B824(a0, d0, d3, d4); break;
    case 0x1570C: h_01570C(a0, d0, d3, d4); break;
    case 0x15E4E: h_015E4E(a0); break;
    case 0x1575C: h_01575C(a0, d0, d3, d4); break;
    case 0x1947E: // same code
    case 0x18F9E: h_018F9E(a0, d0, d3, d4); break;
    case 0x1901C: // same code
    case 0x194FC:
    case 0x19A04:
    case 0x1A8C8: h_01901C(a0, d0, d3, d4); break;
    case 0x18B12: // same code
    case 0x187C8: h_0187C8(a0, d0, d3, d4); break;
    case 0x18832: // same code
    case 0x18B7C:
    case 0x1A1CC:
    case 0x1B030:
    case 0x1B890:
    case 0x1B9BC: h_01B9BC(a0, d0, d3, d4); break;
    default: untranslated(addr);
    }
}

} // namespace game
