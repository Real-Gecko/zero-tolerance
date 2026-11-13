// Scaled sprites in the 3D view: the column scaler (0xEDA0), map object
// sprites (DrawObjectSprite 0x113B0, handlers at 0x113F4) and multi-block
// sprites of enemies and large objects (0x1BD72).
#include "render.hpp"

#include "game.hpp"
#include "m68k.hpp"

namespace game {

namespace {

int16_t s16(uint16_t v) { return int16_t(v); }

U16 mirror{0xFF1088};
U16 col_step{0xFF108A}; // +0x20 / -0x20: next source column

// Row routines of table 0xF142: k rows, each reading its source row from
// the row map a2.
void rows(int k, uint32_t& a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    for (int i = 0; i < k; i++) {
        a0 += 4;
        uint8_t t = rd8(a1 + rd8(a2++));
        if (t) wr8(a0, rd8(a3 + t));
    }
}

// 0xF104: sprites taller than the view, 16.16 stepping through the column.
void tall_rows(uint32_t a0, uint32_t a1, uint32_t a3, uint16_t step) {
    int n = s16(U16{0xFF1090});
    uint16_t frac = U16{0xFF108E};
    uint8_t c = rd8(a1++);
    if (c) c = rd8(a3 + c);
    uint32_t count = uint32_t(uint16_t(n)) + 1; // dbra
    for (uint32_t i = 0; i < count; i++) {
        a0 += 4;
        uint32_t sum = uint32_t(frac) + step;
        frac = uint16_t(sum);
        if (sum > 0xFFFF) {
            c = rd8(a1++);
            if (c) {
                c = rd8(a3 + c);
                wr8(a0, c);
            }
        } else if (c) {
            wr8(a0, c);
        }
    }
}

} // namespace

// 0xEDA0: d0 height, d4 width (screen pixels), d1 left column, d2 top row,
// a1 sprite (16 columns of 32 texels). FF1094 scale for the depth test,
// FF1096 the shade tables, FF1088 mirrored.
void DrawSprite(int16_t height, int16_t width, int16_t col, int16_t row, uint32_t a1) {
    if (height <= 0 || width <= 0 || col >= 0x80 || row >= 0x50) return;
    if (int16_t(col + width - 1) < 0 || int16_t(row + height - 1) < 0) return;
    uint32_t shades = U32{0xFF1096};
    if (shades != 0xED50) {
        int16_t d3 = int16_t((s16(U16{0xFF1094}) >> 3) - 1);
        if (d3 < 0) return;
        d3 = int16_t(4 - d3);
        if (d3 < 0) d3 = 0;
        shades += uint32_t(d3 * 4);
    }
    uint32_t a3 = rd32(shades);
    U32{0xFF109A} = 0xFF6126 + uint16_t(col << 1);
    uint32_t a4 = rd32(0xF6E8 + uint16_t(height << 2));
    int16_t n = height;
    U16{0xFF1092} = uint16_t(row);
    if (row < 0) {
        n = int16_t(n + row);
        a4 -= uint32_t(int32_t(row));
        row = 0;
    }
    n = int16_t(n + row);
    if (uint16_t(n) > 0x50) n = 0x50;
    n = int16_t(n - row);
    col_step = 0x20;
    if (mirror) {
        a1 += 0x1E0;
        col_step = uint16_t(-0x20);
        a3 += 0xC00;
    }
    U16{0xFF1090} = uint16_t(n);
    int row_count = n;
    uint32_t a0 = 0xFF3922 + uint32_t(row * 4) + uint32_t((col & ~3) * 0x50) + uint32_t(col & 3);
    int16_t group = int16_t(4 - (col & 3));
    bool narrow = uint16_t(width) < 0x10;
    uint32_t offsets = 0;
    if (narrow) {
        offsets = 0x104D6 + uint32_t((width + (mirror ? 0x10 : 0)) << 5);
        if (width == 0) return;
    }
    int16_t d5 = 0, cols = int16_t(width - 1);
    auto next_source = [&]() {
        if (narrow) {
            a1 += uint32_t(int32_t(s16(rd16(offsets))));
            offsets += 2;
            return;
        }
        d5 = int16_t(d5 + 0x10);
        if (d5 >= width) {
            d5 = int16_t(d5 - width);
            a1 += uint32_t(int32_t(s16(col_step)));
        }
    };
    if (col < 0) {
        group = 4;
        a0 = 0xFF3922 + uint32_t(row * 4);
        U32{0xFF109A} = 0xFF6126;
        do {
            next_source();
            cols--;
            col++;
        } while (col < 0);
    }
    if (uint16_t(col + cols) > 0x7F) cols = int16_t(0x7F - col);
    bool tall = uint16_t(height) > 0x50;
    uint16_t step = U16{0xFF108C};
    if (tall) {
        U16{0xFF1090} -= 1;
        uint32_t q = 0x200000u / uint16_t(height);
        if (q <= 0xFFFF) U16{0xFF108C} = uint16_t(q);
        U16{0xFF108E} = 0;
        int16_t top = s16(U16{0xFF1092});
        if (top < 0) {
            uint32_t skip = uint32_t(uint16_t(-top)) << 5;
            a1 += uint16_t(skip / uint16_t(height));
            uint32_t rem = skip % uint16_t(height);
            U16{0xFF108E} = uint16_t((rem * U16{0xFF108C}) >> 5);
        }
        step = U16{0xFF108C};
    }
    int16_t scale = s16(U16{0xFF1094});
    bool lit = s16(U16{0xFF105E}) < 0 && s16(U16{0xFF1060}) >= 0;
    if (lit) {
        uint32_t a2 = U32{0xFF109A};
        if (a2 > U32{0xFF106A} && a2 <= U32{0xFF106E}) {
            U32{0xFF109E} = a3;
            a3 = 0x10D1BE + (mirror ? 0xC00 : 0);
        }
    }
    for (int c = 0; c <= cols; c++) {
        uint32_t a2 = U32{0xFF109A};
        if (lit) {
            if (a2 == U32{0xFF106A}) {
                U32{0xFF109E} = a3;
                a3 = 0x10D1BE + (mirror ? 0xC00 : 0);
            } else if (a2 == U32{0xFF106E}) {
                a3 = U32{0xFF109E};
            }
        }
        U32{0xFF109A} = a2 + 2;
        if (scale >= s16(rd16(a2))) {
            if (tall) tall_rows(a0, a1, a3, step);
            else {
                uint32_t a = a0;
                rows(row_count, a, a1, a4, a3);
            }
        }
        a0 += 1;
        if (--group == 0) {
            a0 += 0x13C;
            group = 4;
        }
        next_source();
    }
}

// 0x1BD72: multi-block sprite: a1 sprite set, anim (d0), frame (d7), part
// (d3); col, row, scale = d1, d2, d5.
void DrawSpriteSet(uint32_t a1, uint16_t anim, uint16_t frame, uint16_t part, int16_t col, int16_t row, uint16_t scale) {
    if (anim >= rd16(a1)) return;
    a1 += 2;
    uint32_t blocks = a1 + rd16(a1);
    a1 += uint32_t(int32_t(s16(rd16(a1 + 2 + anim * 2))));
    a1 += uint32_t(int32_t(s16(rd16(a1 + 2 + frame * 2))));
    a1 += 2;
    DrawSpriteBlocks(a1 + uint32_t(part) * 0x26, blocks, col, row, scale);
}

// 0x1BD9E / 0x1BCCE: the blocks of one sprite part a1 (block data at
// blocks), placed by its anchor bytes at (col, row).
void DrawSpriteBlocks(uint32_t a1, uint32_t blocks, int16_t col, int16_t row, uint16_t scale) {
    int16_t bw = int16_t((uint32_t(rd8(a1 + 2)) * scale) >> 5);
    int16_t bh = int16_t((uint32_t(rd8(a1 + 3)) * scale) >> 4);
    uint16_t w = uint16_t(rd8(a1 + 4) * uint16_t(bw));
    if (rd8(a1)) col = int16_t(col - (rd8(a1) == 1 ? int16_t(w) >> 1 : int16_t(w)));
    uint16_t h = uint16_t(rd8(a1 + 5) * uint16_t(bh));
    if (rd8(a1 + 1)) row = int16_t(row - (rd8(a1 + 1) == 1 ? int16_t(h) >> 1 : int16_t(h)));
    mirror = 0;
    uint32_t a3 = a1 + 6;
    for (int y = 0; y < rd8(a1 + 5); y++, row = int16_t(row + bh), a3 += 4) {
        int16_t c = col;
        uint32_t cell = a3;
        for (int x = 0; x < rd8(a1 + 4); x++, cell++, c = int16_t(c + bw)) {
            wr8(0xFF1089, rd8(cell));
            uint32_t block = blocks + (ext_l(uint16_t(rd8(cell + 0x10) << 1)) << 8);
            DrawSprite(bh, bw, c, row, block);
        }
    }
}

namespace {

// Height of the hanging objects (0x1198A, 0x119E8), from the view height.
int16_t eye_height(int16_t d0) {
    int16_t d3 = int16_t(0x20 + s16(U16{0xFF0E1A}) - s16(U16{0xFF116E}));
    return int16_t((int32_t(d0) * d3) >> 6);
}

} // namespace

// 0x113B0
void DrawObjectSprite(uint32_t& a3) {
    a3 -= 0xA;
    uint8_t cls = rd8(a3 + 5);
    int16_t d5 = s16(rd16(a3 + 6)), d1 = s16(rd16(a3 + 8));
    U16{0xFF1094} = uint16_t(d5);
    int16_t d3 = int16_t(s16(U16{0xFF0E1A}) - s16(U16{0xFF116E}) + 0x20);
    int16_t d2 = int16_t(((int32_t(d5) * d3) >> 6) + 0x28);
    switch (cls) {
    case 0x18: { // barrel-like animated object
        int16_t d4 = int16_t(d5 >> 1);
        d4 = int16_t(d4 + (d4 >> 2));
        mirror = U16{0xFF0EB6} & 1;
        DrawSprite(d5, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), 0x10E9BE);
        break;
    }
    case 0x19: case 0x82: { // 0x11A3A (0x82 reads one entry past the table)
        int16_t d0 = int16_t(d5 >> 1), d4 = int16_t(d0 >> 1);
        mirror = 0;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d0), cls == 0x82 ? 0x1161BE : 0x10EFBE);
        break;
    }
    case 0x1A: case 0x1B: case 0x1C: case 0x1E: case 0x1F: case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x24: case 0x25: case 0x36: { // 0x11A52
        static const uint32_t spr[] = {0x10F9BE, 0x10FDBE, 0x10FFBE, 0, 0x10F1BE, 0x1103BE, 0x1105BE, 0x10F7BE,
                                       0x10F3BE, 0x10F5BE, 0x1101BE, 0x110DBE};
        uint32_t a1 = cls == 0x36 ? 0x110BBE : spr[cls - 0x1A];
        int16_t d0 = int16_t(d5 >> 2);
        mirror = 0;
        DrawSprite(d0, d0, int16_t(d1 - (d0 >> 1)), int16_t(d2 - d0), a1);
        break;
    }
    case 0x1D: {
        int16_t d4 = int16_t(d5 >> 1), d0 = int16_t(d4 >> 1);
        mirror = 0;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d0), 0x1107BE);
        break;
    }
    case 0x26: {
        int16_t d0 = int16_t(d5 >> 1);
        d0 = int16_t(d0 - (d0 >> 2));
        int16_t d4 = int16_t(d0 >> 1);
        mirror = 0;
        uint32_t a1 = (rd8(0xFF0EB7) & 2) ? 0x1111BE : 0x110FBE;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), a1);
        break;
    }
    case 0x27: case 0x28: break;
    case 0x29: DrawSpriteSet(0x1B7B38, 0, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x2A: DrawSpriteSet(0x17BA80, 0, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x2B: DrawSpriteSet(0x1A7828, 0, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x2C: DrawSpriteSet(0x1B7B38, 0xB, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x35: DrawSpriteSet(0x1CB96A, 0, 0, 2, d1, d2, uint16_t(d5)); break;
    case 0x37: { // 0x11A0E
        int16_t d4 = int16_t(d5 >> 1);
        mirror = 0;
        DrawSprite(d5, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), 0x1145BE);
        break;
    }
    case 0x5C: {
        int16_t d4 = int16_t(d5 >> 1);
        mirror = 0;
        DrawSprite(d5, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), 0x1147BE);
        break;
    }
    case 0x60: case 0x61: case 0x76: { // 0x11A24
        uint32_t a1 = cls == 0x60 ? 0x114FBE : cls == 0x61 ? 0x1155BE : 0x1151BE;
        int16_t d4 = int16_t(d5 >> 2);
        mirror = 0;
        DrawSprite(d5, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), a1);
        break;
    }
    case 0x5D: {
        int16_t d0 = int16_t(d5 - (d5 >> 2));
        int16_t d4 = int16_t(d0 >> 1);
        mirror = 0;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d0), 0x1149BE);
        break;
    }
    case 0x5F: {
        int16_t d0 = int16_t(d5 >> 1), d4 = int16_t(d0 >> 2);
        mirror = 0;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d0), 0x1157BE);
        break;
    }
    case 0x5E: {
        int16_t h = int16_t(d5 >> 1);
        int16_t d0 = int16_t(h - (h >> 2));
        mirror = 0;
        DrawSprite(d0, h, int16_t(d1 - (h >> 1)), int16_t(d2 - d0), 0x1157BE);
        break;
    }
    case 0x63: case 0x62: {
        uint32_t a1 = 0x1153BE;
        if (cls == 0x62 && (rd8(0xFF0EB7) & 2)) a1 = 0x114BBE;
        int16_t d0 = int16_t(d5 >> 2), d4 = int16_t(d0 >> 1);
        mirror = 0;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), a1);
        break;
    }
    case 0x64: {
        int16_t d4 = int16_t(d5 >> 2), d0 = int16_t(d4 >> 1);
        mirror = 0;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), 0x1159BE);
        // 0x1198A: the top part.
        int16_t w = int16_t(d5 >> 1), half = int16_t(w >> 1);
        int16_t hh = eye_height(half);
        mirror = 0;
        DrawSprite(hh, w, int16_t(d1 - half), int16_t(d2 - (hh >> 1)), 0x114DBE);
        break;
    }
    case 0x75: {
        int16_t d4 = int16_t(d5 >> 1), d0 = int16_t(d4 >> 2);
        mirror = 0;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), 0x115BBE);
        break;
    }
    case 0x6C: DrawSpriteSet(0x16EC58, 4, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x6D: DrawSpriteSet(0x1A7828, 6, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x6E: {
        uint32_t r = Random();
        DrawSpriteSet(0x1ACC72, 9, 0, (r & 0x3F00) ? 1 : 0, d1, d2, uint16_t(d5));
        break;
    }
    case 0x6F: DrawSpriteSet(0x18D078, 7, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x70: DrawSpriteSet(0x183B94, 6, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x71: DrawSpriteSet(0x1784F2, 6, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x72: DrawSpriteSet(0x17BA80, 4, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x73: DrawSpriteSet(0x193F00, 9, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x74: DrawSpriteSet(0x19D50C, 4, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x78: {
        mirror = 0;
        uint32_t a1 = 0x115DBE;
        uint16_t t = U16{0xFF0EB6} & 3;
        if (t != 0 && t != 3) a1 += 0x200;
        if (t >= 2) mirror = 1;
        int16_t w = int16_t(d5 >> 1), half = int16_t(w >> 1);
        int16_t hh = eye_height(half);
        DrawSprite(hh, w, int16_t(d1 - half), int16_t(d2 - (hh >> 1)), a1);
        break;
    }
    default:
        break; // 0x13298: nothing
    }
}

} // namespace game
