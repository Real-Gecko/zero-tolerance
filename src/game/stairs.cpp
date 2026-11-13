// Stairs and lifts under the player (0xB4E4-0xB8FC), level changes, and
// the depth-sorted drawing of enemies and map objects (0x1329A).
#include "render.hpp"

#include "game.hpp"
#include "m68k.hpp"

namespace game {

namespace {

U16 height{0xFF116E}; // view height offset on stairs: -0x40..0x40
U16 lift{0xFF1170};   // 1 going down, -1 going up, 2 / -2 arrived

int16_t s16(uint16_t v) { return int16_t(v); }
uint16_t frac(uint16_t v) { return v & 0xFF; }

void level_down();
void level_up();

// Stair cells: the view height follows the position in the cell.
void stairs(uint8_t c) {
    switch (c) {
    case 0x12: height = uint16_t(s16(uint16_t(frac(player_y) - 0xFF)) >> 2); break;
    case 0x13: case 0x3D: case 0x45: case 0x4D: height = 0; break;
    case 0x14: height = uint16_t(s16(uint16_t(-frac(player_y) + 0xFF)) >> 2); break;
    case 0x15: height = 0xFFC0; break;
    case 0x16: height = 0x40; break;
    case 0x3C: height = uint16_t(s16(uint16_t(-frac(player_y))) >> 2); break;
    case 0x3E: height = uint16_t(s16(frac(player_y)) >> 2); break;
    case 0x44: height = uint16_t(s16(uint16_t(-frac(player_x))) >> 2); break;
    case 0x46: height = uint16_t(s16(frac(player_x)) >> 2); break;
    case 0x4C: height = uint16_t(s16(uint16_t(frac(player_x) - 0xFF)) >> 2); break;
    case 0x4E: height = uint16_t(s16(uint16_t(-frac(player_x) + 0xFF)) >> 2); break;
    // Top / bottom cells of a staircase: crossing the middle changes level.
    case 0x17: case 0x3F: case 0x47: case 0x4F: {
        bool x_axis = c == 0x17 || c == 0x3F;
        int8_t pos = int8_t(uint8_t(x_axis ? player_x : player_y));
        bool down_side = (c == 0x17 || c == 0x47) ? pos < 0 : pos >= 0;
        if (height != 0x40) {
            if (down_side) {
                height = 0x40;
                level_down();
            }
        } else if (!down_side) {
            height = 0xFFC0;
            level_up();
        }
        break;
    }
    }
}

// 0xB7EC: one level down (messages for the basement).
void level_down() {
    if (cur_area == 2) sub_01E0C2(0x1E5F0);
    uint16_t lvl = uint16_t(cur_level - 1);
    if (cur_area != 2) sub_01E0C2(rd32(0x1EE94 + ((cur_area << 4) + lvl) * 4));
    sub_00B8FC(lvl);
}

// 0xB862: one level up.
void level_up() {
    if (cur_area == 2) sub_01E0C2(0x1E61E);
    uint16_t lvl = uint16_t(cur_level + 1);
    if (cur_area != 2) sub_01E0C2(rd32(0x1EE94 + ((cur_area << 4) + lvl) * 4));
    sub_00B8FC(lvl);
}

} // namespace

// 0xB4E4: the player stands on a special cell of class cls.
void sub_00B4E4(uint8_t cls) {
    if (cls < 0x12) return;
    if (cls <= 0x4F) stairs(cls);
    if (lift == 1) {
        // 0xB768: lift going down.
        if (cls == 0x33 || cls == 0x52 || cls == 0x56 || cls == 0x5A) {
            height -= 4;
            if (s16(height) < 0) {
                lift = 2;
                height = 0;
            }
            return;
        }
        height -= 4;
        if (s16(height) >= -0x40) return;
        height = 0x40;
        level_down();
    } else if (lift == 0xFFFF) {
        if (cls == 0x32 || cls == 0x51 || cls == 0x55 || cls == 0x59) {
            height += 4;
            if (s16(height) >= 0) {
                lift = 0xFFFE;
                height = 0;
            }
            return;
        }
        height += 4;
        if (s16(height) <= 0x40) return;
        height = 0xFFC0;
        level_up();
    }
}

// 0xB8FC: switches to level n of the area.
void sub_00B8FC(uint16_t level) {
    sub_057E0C();
    sub_057EC4();
    cur_level = level;
    cur_map_ptr = 0xFF7CD6 + (ext_l(uint16_t(level << 10)));
    // Floor number shown in the HUD (FF6506): "165 - level" as three digits.
    uint16_t n = uint16_t(0xA5 - cur_level);
    uint16_t d1 = 0x2E;
    uint32_t digits = 0xB994;
    if (n >= 0x64) {
        uint32_t a1 = digits;
        do {
            n = uint16_t(n - 0x64);
            d1 = rd16(a1);
            a1 += 2;
        } while (n >= 0x64);
    }
    wr16(0xFF6506, d1);
    d1 = 0x2E;
    if (n >= 0xA) {
        uint32_t a1 = digits;
        do {
            n = uint16_t(n - 0xA);
            d1 = rd16(a1);
            a1 += 2;
        } while (n >= 0xA);
    }
    wr16(0xFF650A, d1);
    wr16(0xFF650E, rd16(0xB992 + n * 2));
    sub_001D46(rd8(0xFF7CC6 + cur_level));
}

// 0x13466: a free enemy slot (0x4C bytes at FF11E8), linked into the list.
uint32_t sub_013466() {
    if (U16{0xFF24E8} == 0x40) return 0;
    U16{0xFF24E8} += 1;
    uint32_t a0 = 0xFF11E8;
    if (rd16(a0 + 4)) {
        uint32_t a1;
        do {
            a1 = a0;
            a0 += 0x4C;
        } while (rd16(a0 + 4));
        wr32(a0, rd32(a1));
        wr32(a1, a0);
    } else {
        wr32(a0, U32{0xFF24EA});
        U32{0xFF24EA} = a0;
    }
    wr32(a0 + 0x12, 0x13298);
    wr32(a0 + 0xA, 0x13298);
    wr32(a0 + 0x1A, 0x13298);
    wr32(a0 + 0x16, 0x13526);
    wr32(a0 + 0xE, 0x13298);
    wr8(a0 + 0x30, 0);
    wr8(a0 + 0x1F, 0);
    wr8(a0 + 0x32, rd8(0xFF1029));
    wr8(a0 + 0x3E, uint8_t(divu(a0 - 0xFF11E8, 0x4C) + 1));
    wr16(a0 + 4, 1);
    return a0;
}

// 0x13526: removes an enemy from the list.
void sub_013526(uint32_t a0) {
    U16{0xFF24E8} -= 1;
    if (a0 == U32{0xFF24EA}) {
        U32{0xFF24EA} = rd32(a0);
    } else {
        uint32_t a1 = U32{0xFF24EA};
        while (rd32(a1) != a0) a1 = rd32(a1);
        wr32(a1, rd32(a0));
    }
    wr16(a0 + 4, 0);
    wr32(a0, 0);
}

namespace {

constexpr uint32_t heap = 0xFF24F0; // (u16 scale, u16 column, u32 enemy) by scale
U16 heap_n{0xFF24EE};

// 0x133C0: inserts scale d5 into the heap; returns the free entry.
uint32_t heap_push(int16_t d5) {
    uint16_t i = heap_n;
    heap_n += 1;
    while (i) {
        uint16_t p = uint16_t(int16_t(i - 1) >> 1);
        if (d5 >= s16(rd16(heap + p * 8))) break;
        wr32(heap + i * 8, rd32(heap + p * 8));
        wr32(heap + i * 8 + 4, rd32(heap + p * 8 + 4));
        i = p;
    }
    return heap + i * 8;
}

// 0x133FA: removes the top of the heap.
void heap_pop() {
    heap_n -= 1;
    uint16_t n = heap_n;
    int16_t last = s16(rd16(heap + n * 8));
    uint16_t hole = 0, d0 = 0;
    for (;;) {
        uint16_t c = uint16_t(d0 * 2 + 1);
        if (s16(c) >= s16(n)) break;
        d0 = c;
        if (s16(uint16_t(c + 1)) < s16(n) && s16(rd16(heap + c * 8 + 8)) < s16(rd16(heap + c * 8))) d0 = uint16_t(c + 1);
        if (last <= s16(rd16(heap + d0 * 8))) break;
        wr32(heap + hole * 8, rd32(heap + d0 * 8));
        wr32(heap + hole * 8 + 4, rd32(heap + d0 * 8 + 4));
        hole = d0;
    }
    wr32(heap + hole * 8, rd32(heap + n * 8));
    wr32(heap + hole * 8 + 4, rd32(heap + n * 8 + 4));
}

void draw_top_enemy() {
    uint16_t d5 = rd16(heap), d1 = rd16(heap + 2);
    uint32_t e = rd32(heap + 4);
    CallEnemyDraw(rd32(e + 0xE), e, d5, d1);
    heap_pop();
}

} // namespace

// 0x1329A: enemies on the level in view, sorted by scale, drawn back to front
// together with the map objects of FF10A2.
void DrawEnemiesAndObjects() {
    uint16_t n = U16{0xFF24E8};
    heap_n = 0;
    uint32_t a0 = U32{0xFF24EA};
    for (int i = 0; i < n; i++, a0 = rd32(a0)) {
        if (rd8(a0 + 0x32) != rd8(0xFF1029)) continue;
        int16_t dx = int16_t(rd16(a0 + 0x20) - player_x), dy = int16_t(rd16(a0 + 0x22) - player_y);
        if (Distance(dx, dy) > 0xC00) continue;
        int16_t cx = s16(U16{0xFF0E00}), cy = s16(U16{0xFF0E02});
        int32_t depth = (int32_t(dx) * cx + int32_t(dy) * cy) >> 6;
        int32_t lat = int32_t(dy) * cx - int32_t(dx) * cy;
        if (depth < 2) continue;
        int32_t q = lat / int16_t(depth);
        if (int16_t(depth) == 0 || q < -32768 || q > 32767) continue;
        int16_t col = int16_t(q + 0x40);
        q = 0x10000 / int16_t(depth);
        if (q < -32768 || q > 32767) continue;
        int16_t scale = int16_t(q);
        if (uint16_t(scale) > 0x12C) scale = 0x12C;
        uint32_t slot = heap_push(scale);
        wr16(slot, uint16_t(scale));
        wr16(slot + 2, uint16_t(col));
        wr32(slot + 4, a0);
    }
    uint32_t objects = U32{0xFF116A}; // walked backwards, nearest last
    if (objects == 0xFF10A2) {
        while (heap_n) draw_top_enemy();
        return;
    }
    if (!heap_n) {
        do DrawObjectSprite(objects); while (objects != 0xFF10A2);
        return;
    }
    for (;;) {
        if (s16(rd16(heap)) >= s16(rd16(objects - 4))) {
            DrawObjectSprite(objects);
            if (objects == 0xFF10A2) {
                while (heap_n) draw_top_enemy();
                return;
            }
        } else {
            draw_top_enemy();
            if (!heap_n) {
                do DrawObjectSprite(objects); while (objects != 0xFF10A2);
                return;
            }
        }
    }
}

} // namespace game
