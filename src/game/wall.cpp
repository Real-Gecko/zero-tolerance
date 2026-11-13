// 3D view wall drawing: DrawWallSpan (0xCCB6) and the compiled column
// scalers it calls.
#include "game.hpp"
#include "m68k.hpp"
#include "render.hpp"

#include "../host.hpp"


namespace game {

namespace {

constexpr uint32_t fill_end = 0x48A6; // rts after the unrolled background copy at 0x4766

// The scalers of tables 0x4436 and 0x4392 (one routine per wall height 2i,
// i = 0-40) all do the same thing, checked against every routine by
// /tmp/opencode/zt_tr/scalers.py: copy background bytes from a5 for the top,
// i texels from the top block column a1 and i from the bottom block column
// a2 (rows (2j+1)*16/i), skip 2i background bytes, copy background for the
// bottom. Each output row is 4 bytes below the last. Routines below i = 38
// take the two fill lengths from their stack arguments (entry points into
// the unrolled copy); the others have them built in. Table 0x4392 maps the
// texels of heights up to 40 through the shade tables at FF0E8A-FF0EA6.
void scale_column(uint32_t table, int i, uint32_t top_fill, uint32_t bottom_fill, uint32_t& a0, uint32_t a1,
                  uint32_t a2, uint32_t& a5) {
    if (table != 0x4436 && table != 0x4392) untranslated(table);
    int top = i < 38 ? int(fill_end - top_fill) / 4 : 40 - i;
    int bottom = i < 38 ? int(fill_end - bottom_fill) / 4 : 40 - i;
    uint32_t shade = 0;
    if (table == 0x4392 && i >= 1 && i <= 20) {
        static const uint16_t ptr[21] = {0, 0xEA6, 0xEA6, 0xEA6, 0xEA6, 0xEA2, 0xEA2, 0xEA2, 0xE9E, 0xE9E, 0xE9E,
                                         0xE9A, 0xE9A, 0xE96, 0xE96, 0xE92, 0xE92, 0xE8E, 0xE8E, 0xE8A, 0xE8A};
        shade = rd32(0xFF0000 | ptr[i]);
    }
    auto texel = [&](uint32_t a) {
        uint8_t t = rd8(a);
        return shade ? rd8(shade + t) : t;
    };
    for (int r = 0; r < top; r++, a0 += 4) wr8(a0, rd8(a5++));
    for (int j = 0; j < i; j++, a0 += 4) wr8(a0, texel(a1 + (2 * j + 1) * 16 / i));
    for (int j = 0; j < i; j++, a0 += 4) wr8(a0, texel(a2 + (2 * j + 1) * 16 / i));
    a5 += 2 * i;
    for (int r = 0; r < bottom; r++, a0 += 4) wr8(a0, rd8(a5++));
}

// Texture block address of strip word w (block number) at a3.
uint32_t block_addr(uint32_t a3) { return 0x12EF26 + (ext_l(rd16(a3)) << 9); }

} // namespace

// a2/a3: left and right edge records (+0xC height, +0xE screen column).
void DrawWallSpan(uint32_t left, uint32_t right) {
    U16 x0{0xFF0E64}, h0{0xFF0E66}, x1{0xFF0E68}, h1{0xFF0E6A};
    U16 tex_u{0xFF0E5C}, tex_col{0xFF0E60};
    U32 strip{0xFF0E70};
    h0 = rd16(left + 0xC);
    if (int16_t(uint16_t(h0)) < 0) return;
    h1 = rd16(right + 0xC);
    if (int16_t(uint16_t(h1)) < 0) return;
    x1 = rd16(right + 0xE);
    if (int16_t(uint16_t(x1)) < 0) return;
    x0 = rd16(left + 0xE);
    if (int16_t(uint16_t(x0)) < 0) x0 = 0;
    if (int16_t(uint16_t(x0)) >= 0x80) return;
    if (int16_t(uint16_t(x1)) >= 0x80) x1 = 0x7F;
    if (fb_dma_pending && uint16_t(x1) >= 0x40)
        while (fb_dma_pending) host::frame();

    uint32_t a3 = strip;
    uint32_t a1 = block_addr(a3), a2 = block_addr(a3 + 2);
    uint16_t x = x0;
    uint32_t a4 = 0xFF6126 + 2 * ext_l(x);
    uint32_t a5 = 0xFF6226;
    if (!(x & 1)) a5 += 0x50;
    uint32_t a0 = framebuffer + (x & 3) + (x >> 2) * 0x140;
    uint16_t d0 = uint16_t(3 - (x & 3));
    int16_t d5 = int16_t(uint16_t(x1) - uint16_t(x0) + 1);
    if (d5 <= 0) return;
    U16{0xFF0E76} = x1;
    uint32_t d2 = ext_l(h0) << 8;
    uint32_t d1 = ext_l(divs((ext_l(h1) << 8) - d2, uint16_t(d5)));
    uint32_t d6 = divs(ext_l(uint16_t((uint16_t(U16{0xFF0E5E}) - uint16_t(tex_u)) << 6)), uint16_t(d5));
    cr.d6 = uint16_t(d6); // left in d6 for the caller
    tex_col = 0;
    uint16_t d4 = tex_u;
    U16{0xFF0E62} = d4;
    d4 = uint16_t((d4 << 6) + lo(d6));
    tex_u = d4;

    // Moves to the next texture strip when the column counter overflows.
    auto next_strip = [&]() {
        if (!rd8(0xFF0E60)) return;
        uint32_t n = uint8_t(rd8(0xFF0E60) << 2);
        wr8(0xFF0E60, 0);
        strip += n;
        a3 = strip;
        a1 = block_addr(a3);
        a2 = block_addr(a3 + 2);
        uint16_t c = uint16_t(uint16_t(tex_col) << 1);
        a1 += ext_l(c);
        a2 += ext_l(c);
    };

    if (d4 > 0xFF) {
        wr8(0xFF0E5C, 0);
        d4 = uint16_t(int16_t(d4) >> 3) & 0xFFE0;
        a1 += ext_l(d4);
        a2 += ext_l(d4);
        tex_col += uint16_t(int16_t(d4) >> 1);
        next_strip();
    }

    bool lit_range = int16_t(uint16_t(U16{0xFF105E})) < 0 && int16_t(uint16_t(U16{0xFF1060})) >= 0;
    U32 table{0xFF0EAA}, saved_table{0xFF109E};
    U32 range_start{0xFF106A}, range_end{0xFF106E};
    if (lit_range) {
        saved_table = uint32_t(table);
        if (a4 > range_start && a4 <= range_end) table = 0x4436;
    }
    auto range_check = [&]() {
        if (!lit_range) return;
        if (a4 == range_start) table = 0x4436;
        else if (a4 == range_end) table = uint32_t(saved_table);
    };
    // Steps to the next screen column and texture column (0xCEB4 / 0xD41E).
    auto advance = [&]() {
        d2 += d1;
        a4 += 2;
        tex_u += lo(d6);
        if (rd8(0xFF0E5C)) {
            uint16_t u = uint16_t(tex_u) & 0xFF00;
            wr8(0xFF0E5C, 0);
            u = uint16_t(int16_t(u) >> 3);
            a1 += ext_l(u);
            a2 += ext_l(u);
            tex_col += uint16_t(int16_t(u) >> 1);
            next_strip();
        }
        a0 -= 0x13F;
        if (int16_t(--d0) < 0) {
            d0 = 3;
            a0 += 0x13C;
        }
    };
    auto background = [&](uint32_t src, int n) {
        for (int k = 0; k < n; k++, a0 += 4) wr8(a0, rd8(src++));
    };
    // Texels of one block column a stretched over n rows: the 68000 loop
    // steps the source with an error term (d7) against the half height d6.
    auto stretch = [&](uint32_t& src, int n, int16_t& err, int16_t half) {
        uint8_t c = rd8(src++);
        uint32_t count = uint32_t(uint16_t(n - 1)) + 1; // dbra: 0 runs 65536 times
        for (uint32_t k = 0; k < count; k++) {
            wr8(a0, c);
            a0 += 4;
            err = int16_t(err + 0x20);
            if (err >= 0) {
                do {
                    c = rd8(src++);
                    err = int16_t(err - half);
                } while (err >= 0);
            }
        }
    };
    // 0xD202: a column shifted up or down by the view tilt (FF0E1A) and the
    // door offset (FF117C).
    auto tilted_column = [&]() {
        uint16_t h = uint16_t(int32_t(d2) >> 8);
        if (h < rd16(a4)) {
            a5 += 0x50;
            a0 += 0x140;
            return;
        }
        wr16(a4, h);
        int32_t off32 = int32_t(int16_t(h)) * int16_t(uint16_t(U16{0xFF0E1A})) + int32_t(int16_t(uint16_t(U16{0xFF117C})));
        int16_t off = int16_t(off32 >> 6);
        bool tall = h > 0x50;
        if (!tall) {
            int16_t d4 = int16_t(int16_t(h) >> 1);
            d4 = off < 0 ? int16_t(d4 - off) : int16_t(d4 + off);
            tall = d4 > 0x28;
        }
        if (!tall) {
            int i = h >> 1;
            uint32_t t = table;
            uint32_t base = 0x46C2 + uint32_t(i * 4);
            uint32_t bottom = rd32(base + uint32_t(int32_t(int16_t(off << 2))));
            uint32_t top = rd32(base + uint32_t(int32_t(int16_t(-(off << 2)))));
            scale_column(t, i, top, bottom, a0, a1, a2, a5);
            return;
        }
        int16_t half = int16_t(int16_t(h) >> 1);
        if (int16_t(half - off) <= -0x28 || int16_t(half + off) <= -0x28) {
            background(a5, 0x50);
            a5 += 0x50;
            return;
        }
        uint32_t ta = a1, tb = a2;
        int16_t rows = 0, err;
        if (off < 0x28 && off > -0x28) {
            int16_t d0x = int16_t(off + 0x28);
            int16_t n = half;
            int16_t pre = int16_t(d0x - half);
            if (pre > 0) {
                background(a5, pre);
                rows = int16_t(rows + pre);
                err = 0;
            } else {
                n = int16_t(n + pre);
                uint32_t skip = uint32_t(uint32_t(-pre) << 5);
                ta += skip / uint16_t(half);
                err = int16_t(skip % uint16_t(half));
            }
            rows = int16_t(rows + n);
            err = int16_t(err - half);
            stretch(ta, n, err, half);
            int16_t m = int16_t(0x50 - rows);
            if (m > half) m = half;
            rows = int16_t(rows + m);
            err = int16_t(-half);
            stretch(tb, m, err, half);
            if (uint16_t(rows) < 0x50) background(a5 + 0x50 - uint32_t(0x50 - rows), 0x50 - rows);
        } else if (off >= 0x28) {
            int16_t d0x = int16_t(off + 0x28);
            int16_t n = half;
            int16_t pre = int16_t(d0x - half);
            if (pre > 0) {
                background(a5, pre);
                rows = int16_t(rows + pre);
                err = 0;
            } else {
                n = int16_t(n + pre);
                uint32_t skip = uint32_t(uint32_t(-pre) << 5);
                ta += skip / uint16_t(half);
                err = int16_t(skip % uint16_t(half));
            }
            int16_t left = int16_t(0x50 - rows);
            if (n > left) n = left;
            err = int16_t(err - half);
            stretch(ta, n, err, half);
        } else {
            int16_t d0x = int16_t(off + 0x28);
            int16_t n = int16_t(half + d0x);
            uint32_t skip = uint32_t(uint32_t(-d0x) << 5);
            tb += skip / uint16_t(half);
            err = int16_t(skip % uint16_t(half));
            if (n > 0x50) n = 0x50;
            rows = int16_t(rows + n);
            err = int16_t(err - half);
            stretch(tb, n, err, half);
            if (uint16_t(rows) < 0x50) background(a5 + 0x50 - uint32_t(0x50 - rows), 0x50 - rows);
        }
        a5 += 0x50;
    };

    if (U16{0xFF1172}) {
        // 0xD0C4: door faces, shifted by the door offsets FF1172/FF1173.
        int8_t b0 = int8_t(rd8(0xFF1172)), b1 = int8_t(rd8(0xFF1173));
        int32_t d7 = int32_t(int16_t(int8_t(uint8_t(b1 - b0)))) * int16_t(uint16_t(U16{0xFF0E62}));
        d7 = int32_t((uint32_t(d7) & 0xFFFF0000) | uint16_t(uint16_t(d7) + (uint8_t(b0) << 8)));
        d7 = (int32_t(int16_t(d7)) * int16_t(uint16_t(h0))) >> 8;
        int32_t d4 = int32_t(int16_t(int8_t(uint8_t(b0 - b1)))) * int16_t(uint16_t(0xFF - U16{0xFF0E5E}));
        d4 = int32_t((uint32_t(d4) & 0xFFFF0000) | uint16_t(uint16_t(d4) + (uint8_t(b1) << 8)));
        d4 = (int32_t(int16_t(d4)) * int16_t(uint16_t(h1))) >> 8;
        d4 -= d7;
        U16{0xFF117C} = uint16_t(d7);
        U16{0xFF117E} = lo(divs(uint32_t(d4), uint16_t(d5)));
        do {
            range_check();
            U16{0xFF117C} += U16{0xFF117E};
            a5 = 0xD074;
            tilted_column();
            advance();
        } while (--d5 != 0);
    } else if (U16{0xFF0E1A}) {
        // 0xD186: the view is tilted.
        U16{0xFF117C} = 0;
        do {
            range_check();
            if (a5 - 0xA0 == 0xFF6226) a5 = 0xFF6226;
            tilted_column();
            advance();
        } while (--d5 != 0);
    } else {
        do {
            range_check();
            if (a5 - 0xA0 == 0xFF6226) a5 = 0xFF6226;
            uint16_t h = uint16_t(int32_t(d2) >> 8);
            if (h < rd16(a4)) {
                a5 += 0x50;
                a0 += 0x140;
            } else {
                wr16(a4, h);
                if (h <= 0x50) {
                    int i = h >> 1;
                    uint32_t t = table;
                    uint32_t fill = rd32(t + i * 4 + 0x148);
                    scale_column(t, i, fill, fill, a0, a1, a2, a5);
                } else {
                    // Taller than the view: 40 rows up and 40 down from the middle.
                    a0 += 0xA0;
                    uint32_t up = a0, top = a1 + 0x20, bottom = a2;
                    int16_t d3 = int16_t(h);
                    uint8_t c5 = rd8(--top), c6 = rd8(bottom++);
                    for (int n = 0; n < 40; n++) {
                        up -= 4;
                        wr8(up, c5);
                        wr8(a0, c6);
                        a0 += 4;
                        d3 = int16_t(d3 - 0x40);
                        if (d3 < 0) {
                            c5 = rd8(--top);
                            c6 = rd8(bottom++);
                            d3 = int16_t(d3 + int16_t(h));
                        }
                    }
                    a5 += 0x50;
                }
            }
            advance();
        } while (--d5 != 0);
    }
    if (lit_range) table = uint32_t(saved_table);
}

// 0xD5CE: a portal: the background shows through above and below the
// opening, which is as high as a wall at that distance.
void DrawPortalSpan(uint32_t left, uint32_t right) {
    U16 x0{0xFF0E64}, h0{0xFF0E66}, x1{0xFF0E68}, h1{0xFF0E6A};
    h0 = rd16(left + 0xC);
    if (int16_t(uint16_t(h0)) < 0) return;
    h1 = rd16(right + 0xC);
    if (int16_t(uint16_t(h1)) < 0) return;
    x1 = rd16(right + 0xE);
    if (int16_t(uint16_t(x1)) < 0) return;
    x0 = rd16(left + 0xE);
    if (int16_t(uint16_t(x0)) < 0) x0 = 0;
    if (int16_t(uint16_t(x0)) >= 0x80) return;
    if (int16_t(uint16_t(x1)) >= 0x80) x1 = 0x7F;
    if (fb_dma_pending && uint16_t(x1) >= 0x40)
        while (fb_dma_pending) host::frame();
    uint16_t d6 = U16{0xFF119A};
    uint16_t x = x0;
    uint32_t a4 = 0xFF6126 + 2 * ext_l(x);
    uint32_t a5 = 0xD074;
    if (d6) {
        d6 = 0x50;
        a5 = 0xFF6226;
        if (!(x & 1)) a5 += 0x50;
    }
    cr.d6 = d6;
    uint32_t a0 = framebuffer + (x & 3) + (x >> 2) * 0x140;
    uint16_t d0 = uint16_t(3 - (x & 3));
    int16_t d5 = int16_t(uint16_t(x1) - uint16_t(x0) + 1);
    if (d5 <= 0) return;
    uint32_t d2 = ext_l(h0) << 8;
    uint32_t d1 = ext_l(divs((ext_l(h1) << 8) - d2, uint16_t(d5)));
    auto copy = [&](uint32_t dst, uint32_t src, int n) {
        for (int k = 0; k < n; k++, dst += 4) wr8(dst, rd8(src++));
    };
    do {
        if (a5 - 0xA0 == 0xFF6226) a5 = 0xFF6226;
        uint16_t h = uint16_t(int32_t(d2) >> 8);
        uint16_t depth = rd16(a4);
        a4 += 2;
        if (h >= depth) {
            int16_t d3 = int16_t((int32_t(int16_t(h)) * int16_t(uint16_t(U16{0xFF0E1A}))) >> 6);
            int16_t d4 = int16_t(int16_t(int16_t(0x50 - h) >> 1) + d3);
            if (d4 > 0) {
                if (uint16_t(d4) > 0x50) d4 = 0x50;
                copy(a0, a5, d4);
            }
            d4 = int16_t(int16_t(int16_t(0x50 - h) >> 1) - d3);
            if (d4 > 0) {
                if (uint16_t(d4) > 0x50) d4 = 0x50;
                int16_t skip = int16_t(0x50 - d4);
                copy(a0 + uint32_t(skip * 4), a5 + uint32_t(skip), d4);
            }
        }
        a5 += d6;
        a0 += 0x140;
        d2 += d1;
        a0 -= 0x13F;
        if (int16_t(--d0) < 0) {
            d0 = 3;
            a0 += 0x13C;
        }
    } while (--d5 != 0);
}

} // namespace game
