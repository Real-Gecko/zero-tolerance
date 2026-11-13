// Ray caster of the 3D view: Render3DView (0xB9D2), CastRay (0xBC90), the
// wall-end projection (0xCA4C) and the special cell handlers called through
// CellSpecialDispatch (0x9226, table 0x923A).
//
// The caster keeps its state in 68000 registers that the cell handlers read
// and change, so it is translated with an explicit register set (cr).
#include "render.hpp"

#include "game.hpp"
#include "m68k.hpp"

#include "../host.hpp"

#include <utility>

namespace game {

CastRegs cr;
bool skip_object_pass = false;

namespace {

constexpr uint32_t faces = 0xFF63C6, cell_faces = 0xFF73C6;
constexpr uint32_t edge_a = 0xFF0E3C, edge_b = 0xFF0E4C;

int16_t s16(uint16_t v) { return int16_t(v); }

// divs.w with the overflow test the projection uses (bvs).
bool divs_ok(int32_t n, int16_t d, int16_t& q) {
    if (d == 0) return false;
    int32_t r = n / d;
    if (r < -32768 || r > 32767) return false;
    q = int16_t(r);
    return true;
}

// ---- edge records (0xC9A8, 0xCA04) ----

// 0xC9A8: left end of a wall at (d0, d1), reusing the last right end when it
// is the same point.
void edge_left(uint16_t x, uint16_t y) {
    if (x == U16{0xFF0E4C} && y == U16{0xFF0E4E}) {
        wr8(0xFF0E3C, 0xFF);
        U32{0xFF0E40} = U32{0xFF0E50};
        U32{0xFF0E44} = U32{0xFF0E54};
        U16{0xFF0E48} = U16{0xFF0E58};
        U16{0xFF0E4A} = U16{0xFF0E5A};
        return;
    }
    U16{0xFF0E3C} = 0;
    int16_t dx = int16_t(x - player_x), dy = int16_t(y - player_y);
    int16_t cx = s16(U16{0xFF0E00}), cy = s16(U16{0xFF0E02});
    U32{0xFF0E40} = uint32_t(int32_t(dx) * cx + int32_t(dy) * cy);
    U32{0xFF0E44} = uint32_t(int32_t(dy) * cx - int32_t(dx) * cy);
}

// 0xCA04: right end.
void edge_right(uint16_t x, uint16_t y) {
    U16{0xFF0E4C} = x;
    U16{0xFF0E4E} = y;
    int16_t dx = int16_t(x - player_x), dy = int16_t(y - player_y);
    int16_t cx = s16(U16{0xFF0E00}), cy = s16(U16{0xFF0E02});
    U32{0xFF0E50} = uint32_t(int32_t(dx) * cx + int32_t(dy) * cy);
    U32{0xFF0E54} = uint32_t(int32_t(dy) * cx - int32_t(dx) * cy);
}

enum class Tail { Wall, Portal, None };

// Screen column and height of one edge from its depth and lateral offset.
bool project(uint32_t rec) {
    int32_t depth = int32_t(rd32(rec + 4)), lat = int32_t(rd32(rec + 8));
    int16_t d0 = int16_t(depth >> 6), q;
    if (!d0 || !divs_ok(lat, d0, q)) return false;
    wr16(rec + 0xE, uint16_t(q + 0x40));
    if (!divs_ok(0x10000, d0, q)) return false;
    wr16(rec + 0xC, uint16_t(q));
    return true;
}

// Height of an edge clipped to the screen side at texture step d6.
bool clipped_height(int16_t d6, uint32_t rec) {
    int32_t da = int32_t(rd32(edge_a + 4));
    int32_t d0 = int32_t(rd32(edge_b + 4)) - da;
    d0 >>= 3;
    d0 = int32_t(int16_t(d0)) * d6;
    d0 >>= 5;
    d0 += da;
    d0 >>= 6;
    int16_t q;
    if (!int16_t(d0) || !divs_ok(0x10000, int16_t(d0), q)) return false;
    wr16(rec + 0xC, uint16_t(q));
    return true;
}

// 0xCA4C: projects the two edges, clipping them to the view sides, then
// draws the span.
void project_and_draw(Tail tail) {
    int32_t a_depth = int32_t(rd32(edge_a + 4)), a_lat = int32_t(rd32(edge_a + 8));
    int32_t b_depth = int32_t(rd32(edge_b + 4)), b_lat = int32_t(rd32(edge_b + 8));
    U16 u0{0xFF0E5C}, u1{0xFF0E5E};
    int16_t q;
    if (-a_lat < a_depth) {
        if (b_lat < b_depth) {
            if (!U16{0xFF0E3C} && !project(edge_a)) return;
            if (!project(edge_b)) return;
            u0 = 0;
            u1 = 0xFF;
        } else {
            // Right end past the right side of the view.
            wr32(edge_b, 0xFFFFFFFF);
            int32_t d6 = a_depth - a_lat;
            int32_t d0 = (d6 - (b_depth - b_lat)) >> 8;
            if (!int16_t(d0) || !divs_ok(d6, int16_t(d0), q)) return;
            int16_t step = q;
            if (!U16{0xFF0E3C} && !project(edge_a)) return;
            wr16(edge_b + 0xE, 0x7F);
            if (!clipped_height(step, edge_b)) return;
            u0 = 0;
            u1 = uint16_t(step);
        }
    } else {
        if (b_lat < b_depth) {
            // Left end past the left side.
            int32_t d6 = a_depth + a_lat;
            int32_t d0 = (d6 - (b_depth + b_lat)) >> 8;
            if (!int16_t(d0) || !divs_ok(d6, int16_t(d0), q)) return;
            int16_t step = q;
            wr16(edge_a + 0xE, 0);
            if (!clipped_height(step, edge_a)) return;
            if (!project(edge_b)) return;
            u1 = 0xFF;
            u0 = uint16_t(step);
        } else {
            wr32(edge_b, 0xFFFFFFFF);
            int32_t d6 = a_depth + a_lat;
            int32_t d0 = (d6 - (b_depth + b_lat)) >> 8;
            if (!int16_t(d0) || !divs_ok(d6, int16_t(d0), q)) return;
            int16_t s6 = q;
            int32_t d5 = a_depth - a_lat;
            d0 = (d5 - (b_depth - b_lat)) >> 8;
            if (!int16_t(d0) || !divs_ok(d5, int16_t(d0), q)) return;
            int16_t s5 = q;
            wr16(edge_a + 0xE, 0);
            if (!clipped_height(s6, edge_a)) return;
            wr16(edge_b + 0xE, 0x7F);
            if (!clipped_height(s5, edge_b)) return;
            u0 = uint16_t(s6);
            u1 = uint16_t(s5);
        }
    }
    if (tail == Tail::Wall) DrawWallSpan(edge_a, edge_b);
    else if (tail == Tail::Portal) DrawPortalSpan(edge_a, edge_b);
}

// ---- wall emitters (0xC70E-0xC9A4) ----

void set_face(int word) { U32{0xFF0E70} = faces + ext_l(uint16_t(rd16(U32{0xFF0E6C} + word * 2) << 4)); }

void emit_C97C();
void emit_C842();
void emit_C8A8();
void emit_C910();

// South face (y = top of cell, viewed from +y).
void emit_C70E() {
    uint8_t c = uint8_t(cr.d3);
    if (c == 3) return emit_C97C();
    if (c == 4) return emit_C842();
    U16{0xFF1172} = U16{0xFF1174};
    set_face(0);
    edge_right(cr.d0, cr.d1);
    cr.d0 = uint16_t(cr.d0 + 0x100);
    edge_left(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

void emit_C748() {
    uint8_t c = uint8_t(cr.d3);
    if (c == 2) return emit_C8A8();
    if (c == 5) return emit_C910();
    U16{0xFF1172} = U16{0xFF1176};
    set_face(3);
    cr.d1 = uint16_t(cr.d1 + 0x100);
    edge_left(cr.d0, cr.d1);
    cr.d0 = uint16_t(cr.d0 + 0x100);
    edge_right(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

void emit_C788() {
    uint8_t c = uint8_t(cr.d3);
    if (c == 2) return emit_C8A8();
    if (c == 3) return emit_C97C();
    U16{0xFF1172} = U16{0xFF117A};
    set_face(1);
    edge_left(cr.d0, cr.d1);
    cr.d1 = uint16_t(cr.d1 + 0x100);
    edge_right(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

void emit_C7C4() {
    uint8_t c = uint8_t(cr.d3);
    if (c == 5) return emit_C910();
    if (c == 4) return emit_C842();
    U16{0xFF1172} = U16{0xFF1178};
    set_face(2);
    cr.d0 = uint16_t(cr.d0 + 0x100);
    edge_right(cr.d0, cr.d1);
    cr.d1 = uint16_t(cr.d1 + 0x100);
    edge_left(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

// Diagonal walls.
void emit_C842() {
    set_face(0);
    edge_right(cr.d0, cr.d1);
    cr.d0 = uint16_t(cr.d0 + 0x100);
    cr.d1 = uint16_t(cr.d1 + 0x100);
    edge_left(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

void emit_C8A8() {
    set_face(3);
    edge_left(cr.d0, cr.d1);
    cr.d0 = uint16_t(cr.d0 + 0x100);
    cr.d1 = uint16_t(cr.d1 + 0x100);
    edge_right(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

void emit_C910() {
    set_face(3);
    cr.d1 = uint16_t(cr.d1 + 0x100);
    edge_left(cr.d0, cr.d1);
    cr.d1 = uint16_t(cr.d1 - 0x100);
    cr.d0 = uint16_t(cr.d0 + 0x100);
    edge_right(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

void emit_C97C() {
    set_face(0);
    cr.d1 = uint16_t(cr.d1 + 0x100);
    edge_right(cr.d0, cr.d1);
    cr.d1 = uint16_t(cr.d1 - 0x100);
    cr.d0 = uint16_t(cr.d0 + 0x100);
    edge_left(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

// Common start of every hit: remember the cell, its face list and its corner.
bool hit_cell() {
    if (cr.a0 == U32{0xFF0E78}) return false;
    U32{0xFF0E78} = cr.a0;
    U32{0xFF0E6C} = cell_faces + uint32_t(rd8(cr.a0)) * 8;
    cr.d0 = uint16_t((cr.d0 + U16{0xFF0E06}) << 8);
    cr.d1 = uint16_t((cr.d1 + U16{0xFF0E08}) << 8);
    return true;
}

// A diagonal wall in the player's own cell (0xC804, 0xC86A, 0xC8D2, 0xC93E):
// the rays it covers are skipped.
void own_diagonal(void (*emit)()) {
    if (cr.a0 == U32{0xFF0E78}) return;
    U32{0xFF0E78} = cr.a0;
    U32{0xFF0E6C} = cell_faces + uint32_t(rd8(cr.a0)) * 8;
    cr.d0 = uint16_t((cr.d0 + U16{0xFF0E06}) << 8);
    cr.d1 = uint16_t((cr.d1 + U16{0xFF0E08}) << 8);
    U16{0xFF0E76} = 0xFFFF;
    emit();
    if (s16(U16{0xFF0E76}) > s16(U16{0xFF0E74})) U16{0xFF0E74} = U16{0xFF0E76};
}

uint8_t cls(uint8_t v) { return rd8(cell_class + v); }

// ---- axis-aligned rays ----

struct Axis {
    uint8_t own_a, own_b;      // diagonal classes that block in the own cell
    int16_t step_a0, step_x, step_y;
    uint32_t bound;            // FF0E34/36/38/3A
    bool bound_min;            // stop below the bound instead of above it
    uint8_t ca, cb;            // classes with their own emitter
    void (*ea)(), (*eb)(), (*e)();
};

void cast_axis(const Axis& ax) {
    cr.d0 = cr.d1 = 0;
    cr.a0 = U32{0xFF0E12};
    auto in_bound = [&]() {
        int16_t v = s16(ax.step_x ? cr.d0 : cr.d1), b = s16(U16{ax.bound});
        return ax.bound_min ? v >= b : v <= b;
    };
    auto step = [&]() {
        cr.a0 += uint32_t(int32_t(ax.step_a0));
        cr.d0 = uint16_t(cr.d0 + ax.step_x);
        cr.d1 = uint16_t(cr.d1 + ax.step_y);
    };
    bool hit = false;
    cr.d3 = uint16_t((cr.d3 & 0xFF00) | rd8(cr.a0));
    if (uint8_t(cr.d3)) {
        cr.d3 = uint16_t((cr.d3 & 0xFF00) | cls(uint8_t(cr.d3)));
        while (uint8_t(cr.d3)) {
            uint8_t c = uint8_t(cr.d3);
            if (c == ax.own_a || c == ax.own_b) {
                hit = true;
                break;
            }
            if (c < 6 || !CellSpecialDispatch()) break;
        }
    }
    if (!hit) {
        step();
        if (!in_bound()) return;
        for (;;) {
            cr.d3 = uint16_t((cr.d3 & 0xFF00) | rd8(cr.a0));
            if (uint8_t(cr.d3)) {
                cr.d3 = uint16_t((cr.d3 & 0xFF00) | cls(uint8_t(cr.d3)));
                if (uint8_t(cr.d3) && (uint8_t(cr.d3) < 6 || CellSpecialDispatch())) break;
            }
            step();
            if (!in_bound()) return;
        }
    }
    if (!hit_cell()) return;
    uint8_t c = uint8_t(cr.d3);
    if (c == ax.ca) ax.ea();
    else if (c == ax.cb) ax.eb();
    else ax.e();
}

// ---- diagonal rays: two DDA walkers, A crossing x lines, B crossing y ----

uint16_t slope_a() { return rd16(cr.a4); }
uint16_t slope_b() { return rd16(cr.a4 + 2); }

// Class test of a cell met by walker A (0xC014 etc.). diag is the diagonal
// class that only blocks when the ray actually crosses it.
bool check_a(uint8_t diag) {
    cr.d3 = uint16_t((cr.d3 & 0xFF00) | cls(uint8_t(cr.d3)));
    if (!uint8_t(cr.d3)) return false;
    for (;;) {
        uint8_t c = uint8_t(cr.d3);
        if (c >= 6) {
            if (!CellSpecialDispatch()) return false;
            continue;
        }
        if (c != diag) return true;
        if (uint16_t(cr.d2 + slope_a()) > 0xFF) {
            cr.d3 = 0;
            return false;
        }
        cr.d3 = diag;
        return true;
    }
}

void swap_ab() {
    std::swap(cr.d0, cr.d4);
    std::swap(cr.d1, cr.d5);
    std::swap(cr.d2, cr.d6);
    std::swap(cr.a0, cr.a1);
}

bool check_b(uint8_t diag) {
    cr.d3 = uint16_t((cr.d3 & 0xFF00) | cls(uint8_t(cr.d3)));
    if (!uint8_t(cr.d3)) return false;
    for (;;) {
        uint8_t c = uint8_t(cr.d3);
        if (c >= 6) {
            swap_ab();
            CellSpecialDispatch();
            swap_ab();
            if (!uint8_t(cr.d3)) return false;
            continue;
        }
        if (c != diag) return true;
        if (uint16_t(cr.d6 + slope_b()) > 0xFF) {
            cr.d3 = 0;
            return false;
        }
        cr.d3 = diag;
        return true;
    }
}

struct Quadrant {
    int sx, sy;                 // +1 / -1 per axis
    uint8_t own_diag;           // own-cell diagonal handled at once
    uint32_t fa_mul, fa_add;    // walker A start fraction: (FFxxxx * slope) >> 8 + FFyyyy
    uint8_t diag_a;             // own-cell diagonal hit by A
    void (*own_a)();
    uint32_t fb_mul, fb_add;
    uint8_t diag_b;
    void (*own_b)();
    void (*own_d)();
    uint8_t check_a, check_b;   // diagonal classes tested by the walkers
    void (*emit_a)();           // A hit (crossing an x line)
    void (*emit_b)();           // B hit (crossing a y line)
    bool neighbour_on_a;        // which hit also draws the other walker's face
    int neighbour;              // offset of the cell tested for that
    uint8_t skip[4];            // neighbour classes for which one face is enough
};

void cast_quadrant(const Quadrant& q, uint16_t angle) {
    cr.a4 = 0x8924 + uint32_t(angle) * 4;
    cr.d7 = 0xFF;
    cr.d0 = cr.d1 = 0;
    cr.a0 = U32{0xFF0E12};
    cr.d3 = cls(rd8(cr.a0));
    if (uint8_t(cr.d3) == q.own_diag) return own_diagonal(q.own_d);
    cr.d2 = uint16_t(((uint32_t(U16{q.fa_mul}) * slope_a()) >> 8) + U16{q.fa_add});
    if (cr.d2 <= 0xFF && uint8_t(cr.d3) == q.diag_a) return own_diagonal(q.own_a);
    cr.d6 = uint16_t(((uint32_t(U16{q.fb_mul}) * slope_b()) >> 8) + U16{q.fb_add});
    if (cr.d6 <= 0xFF && uint8_t(cr.d3) == q.diag_b) return own_diagonal(q.own_b);
    cr.d4 = cr.d0;
    cr.d5 = cr.d1;
    cr.a1 = cr.a0;
    cr.a0 += uint32_t(q.sx);
    cr.d0 = uint16_t(cr.d0 + q.sx);
    cr.a1 += uint32_t(q.sy * 0x20);
    cr.d5 = uint16_t(cr.d5 + q.sy);
    auto carry_a = [&]() {
        if (cr.d2 > 0xFF) {
            uint16_t n = cr.d2 >> 8;
            cr.d3 = cr.d2;
            cr.d2 &= 0xFF;
            cr.d1 = uint16_t(cr.d1 + q.sy * n);
            cr.a0 += uint32_t(q.sy * int(n << 5));
        }
    };
    auto carry_b = [&]() {
        if (cr.d6 > 0xFF) {
            uint16_t n = cr.d6 >> 8;
            cr.d3 = cr.d6;
            cr.d6 &= 0xFF;
            cr.d4 = uint16_t(cr.d4 + q.sx * n);
            cr.a1 += uint32_t(q.sx * int(n));
        }
    };
    carry_a();
    carry_b();
    uint32_t bx = q.sx > 0 ? 0xFF0E38 : 0xFF0E3A, by = q.sy > 0 ? 0xFF0E36 : 0xFF0E34;
    auto inside = [&](uint16_t x, uint16_t y) {
        bool okx = q.sx > 0 ? s16(x) <= s16(U16{bx}) : s16(x) >= s16(U16{bx});
        bool oky = q.sy > 0 ? s16(y) <= s16(U16{by}) : s16(y) >= s16(U16{by});
        return okx && oky;
    };
    for (;;) {
        // Walker A goes first while it is nearer along x (ties broken on y).
        int16_t ax_ = s16(cr.d0), bx_ = s16(cr.d4);
        bool a_first = q.sx > 0 ? (ax_ < bx_ || (ax_ == bx_ && (q.sy > 0 ? s16(cr.d1) < s16(cr.d5) : s16(cr.d1) > s16(cr.d5))))
                                : (ax_ > bx_ || (ax_ == bx_ && (q.sy > 0 ? s16(cr.d1) < s16(cr.d5) : s16(cr.d1) > s16(cr.d5))));
        if (a_first) {
            if (!inside(cr.d0, cr.d1)) return;
            cr.d3 = uint16_t((cr.d3 & 0xFF00) | rd8(cr.a0));
            if (uint8_t(cr.d3) && check_a(q.check_a)) {
                if (!hit_cell()) return;
                if (q.neighbour_on_a) {
                    uint8_t n = cls(rd8(cr.a0 + uint32_t(q.neighbour)));
                    bool only_a = false;
                    for (uint8_t s : q.skip) only_a |= n == s;
                    if (!only_a) {
                        uint16_t d0 = cr.d0, d1 = cr.d1, d3 = cr.d3;
                        q.emit_a();
                        cr.d0 = d0;
                        cr.d1 = d1;
                        cr.d3 = d3;
                        return q.emit_b();
                    }
                }
                return q.emit_a();
            }
            cr.a0 += uint32_t(q.sx);
            cr.d0 = uint16_t(cr.d0 + q.sx);
            cr.d2 = uint16_t(cr.d2 + slope_a());
            carry_a();
        } else {
            if (!inside(cr.d4, cr.d5)) return;
            cr.d3 = uint16_t((cr.d3 & 0xFF00) | rd8(cr.a1));
            if (uint8_t(cr.d3) && check_b(q.check_b)) {
                if (cr.a1 == U32{0xFF0E78}) return;
                cr.d0 = cr.d4;
                cr.d1 = cr.d5;
                cr.d2 = cr.d6;
                cr.a0 = cr.a1;
                if (!hit_cell()) return;
                if (!q.neighbour_on_a) {
                    uint8_t n = cls(rd8(cr.a0 + uint32_t(q.neighbour)));
                    bool only_b = false;
                    for (uint8_t s : q.skip) only_b |= n == s;
                    if (!only_b) {
                        uint16_t d0 = cr.d0, d1 = cr.d1, d3 = cr.d3;
                        q.emit_b();
                        cr.d0 = d0;
                        cr.d1 = d1;
                        cr.d3 = d3;
                        return q.emit_a();
                    }
                }
                return q.emit_b();
            }
            cr.a1 += uint32_t(q.sy * 0x20);
            cr.d5 = uint16_t(cr.d5 + q.sy);
            cr.d6 = uint16_t(cr.d6 + slope_b());
            carry_b();
        }
    }
}

} // namespace

// The tie rule of the loops differs per quadrant; the table entries below
// follow 0xBF1E (x+ y+), 0xC11A (x+ y-), 0xC316 (x- y-) and 0xC512 (x- y+).
void CastRay(uint16_t angle) {
    U32{0xFF1174} = 0;
    U32{0xFF1178} = 0;
    U16{0xFF1172} = 0;
    U16{0xFF0E7C} = 0;
    cr.a5 = cell_class;
    cr.d3 = 0;
    static const Axis north{2, 5, -0x20, 0, -1, 0xFF0E34, true, 2, 5, emit_C8A8, emit_C910, emit_C748};
    static const Axis south{3, 4, 0x20, 0, 1, 0xFF0E36, false, 3, 4, emit_C97C, emit_C842, emit_C70E};
    static const Axis east{2, 3, 1, 1, 0, 0xFF0E38, false, 2, 3, emit_C8A8, emit_C97C, emit_C788};
    static const Axis west{4, 5, -1, -1, 0, 0xFF0E3A, true, 4, 5, emit_C842, emit_C910, emit_C7C4};
    static const Quadrant q_se{1, 1, 3, 0xFF0E0A, 0xFF0E10, 2, emit_C8A8, 0xFF0E0C, 0xFF0E0E, 4, emit_C842,
                               emit_C97C, 2, 4, emit_C788, emit_C70E, false, -1, {1, 2, 3, 5}};
    static const Quadrant q_ne{1, -1, 2, 0xFF0E0A, 0xFF0E0C, 3, emit_C97C, 0xFF0E10, 0xFF0E0E, 5, emit_C910,
                               emit_C8A8, 3, 5, emit_C788, emit_C748, true, 0x20, {1, 2, 4, 5}};
    static const Quadrant q_nw{-1, -1, 5, 0xFF0E0E, 0xFF0E0C, 4, emit_C842, 0xFF0E10, 0xFF0E0A, 2, emit_C8A8,
                               emit_C910, 4, 2, emit_C7C4, emit_C748, false, 1, {1, 3, 4, 5}};
    static const Quadrant q_sw{-1, 1, 4, 0xFF0E0E, 0xFF0E10, 5, emit_C910, 0xFF0E0C, 0xFF0E0A, 3, emit_C97C,
                               emit_C842, 5, 3, emit_C7C4, emit_C70E, true, -0x20, {1, 2, 3, 4}};
    if (angle == 0) cast_axis(north);
    else if (angle == 0x100) cast_axis(south);
    else if (angle == 0x80) cast_axis(east);
    else if (angle == 0x180) cast_axis(west);
    else if (angle < 0x80) cast_quadrant(q_ne, angle);
    else if (angle < 0x100) cast_quadrant(q_se, angle);
    else if (angle < 0x180) cast_quadrant(q_sw, angle);
    else cast_quadrant(q_nw, angle);
}

// 0x9D42 / 0x9D4A: records a portal edge (d3 = two packed cell corners) once.
static void add_portal(uint32_t d3, uint16_t flag) {
    if (flag) flag = uint16_t((cr.d6 & 0xFF00) | 0xFF); // st.b d6 keeps d6's high byte
    uint32_t a3 = 0xFF1182;
    for (uint16_t n = U16{0xFF1180}; n; n--, a3 += 6)
        if (rd32(a3) == d3) return;
    wr32(a3, d3);
    wr16(a3 + 4, flag);
    U16{0xFF1180} += 1;
}

// 0x9D72: draws the recorded portals.
void sub_009D72() {
    uint32_t a3 = 0xFF1182;
    while (U16{0xFF1180}) {
        auto corner = [&](uint16_t& x, uint16_t& y) {
            x = uint16_t((int16_t(int8_t(rd8(a3))) + U16{0xFF0E06}) << 8);
            y = uint16_t((int16_t(int8_t(rd8(a3 + 1))) + U16{0xFF0E08}) << 8);
            a3 += 2;
        };
        uint16_t x, y;
        corner(x, y);
        edge_left(x, y);
        corner(x, y);
        U16{0xFF119A} = rd16(a3);
        a3 += 2;
        edge_right(x, y);
        project_and_draw(Tail::Portal);
        U16{0xFF1180} -= 1;
    }
}

namespace {

// Door faces (FF1174 / FF1178): four bytes per pair of sides, chosen by the
// player's height offset FF116E.
void doors(uint32_t up, uint32_t up2, uint32_t down, uint32_t down2) {
    if (s16(U16{0xFF116E}) > 0) {
        U32{0xFF1174} = up;
        U32{0xFF1178} = up2;
    } else {
        U32{0xFF1174} = down;
        U32{0xFF1178} = down2;
    }
    cr.d3 = 1;
}

void doors3(uint32_t up, uint32_t up2, uint32_t down, uint32_t down2, uint32_t level, uint32_t level2) {
    int16_t h = s16(U16{0xFF116E});
    if (h > 0) U32{0xFF1174} = up, U32{0xFF1178} = up2;
    else if (h < 0) U32{0xFF1174} = down, U32{0xFF1178} = down2;
    else U32{0xFF1174} = level, U32{0xFF1178} = level2;
    cr.d3 = 1;
}

// Portal cells: d3 = packed corners (x, y) of the edge, added when the ray
// comes from the given side.
uint32_t pack(int dx0, int dy0, int dx1, int dy1) {
    auto b = [](uint16_t v, int add) { return uint8_t(uint8_t(v) + add); };
    return uint32_t(b(cr.d0, dx0)) << 24 | uint32_t(b(cr.d1, dy0)) << 16 | uint32_t(b(cr.d0, dx1)) << 8 | b(cr.d1, dy1);
}

// Enemy start cells: spawn the enemy from template FF26F2 once it is near.
void spawn_from_template() {
    uint16_t d0 = cr.d0, d1 = cr.d1, d2 = cr.d2;
    int16_t x = int16_t(((d0 + U16{0xFF0E06}) << 8) + 0x80 - player_x);
    int16_t y = int16_t(((d1 + U16{0xFF0E08}) << 8) + 0x80 - player_y);
    if (Distance(x, y) >= 0x800) {
        AddVisibleObject();
        cr.d3 = 0;
        return;
    }
    uint32_t cell = cr.a0;
    if (cell < 0xFF7CD6 || cell >= 0xFFBCD6) cell = 0xFFCCD6;
    wr8(cell, 0);
    uint16_t sx = cr.d0;
    uint32_t e = sub_013466();
    if (e) {
        uint32_t t = U32{0xFF26F2};
        wr8(e + 0x1F, 0);
        wr32(e + 0x12, rd32(t));
        wr32(e + 0xE, rd32(t + 4));
        wr32(e + 0xA, rd32(t + 8));
        wr32(e + 0x16, rd32(t + 0xC));
        wr16(e + 4, uint16_t(rd16(e + 4) | rd16(t + 0x10)));
        wr16(e + 0x36, rd16(t + 0x12));
        wr16(e + 0x20, uint16_t(((sx + U16{0xFF0E06}) << 8) + 0x80));
        wr16(e + 0x22, uint16_t(((cr.d1 + U16{0xFF0E08}) << 8) + 0x80));
        wr16(e + 0x24, rd16(t + 0x1A));
        wr8(e + 0x32, rd8(0xFF1029));
        wr8(e + 0x34, 0);
        wr8(e + 0x35, 0x14);
        wr8(e + 0x41, 0);
        wr16(e + 0x38, player_x);
        wr16(e + 0x3A, player_y);
        wr32(e + 0x42, rd32(t + 0x14));
        if (rd32(e + 0x42) == 0x16EC58) {
            // The original uses d1 as scratch here and never restores it.
            d1 = rd16(e + 0x22);
            if ((rd16(e + 0x20) ^ d1) & 0x100) wr32(e + 0x42, 0x1C258A);
        }
        wr8(e + 0x46, rd8(t + 0x18));
        wr8(e + 0x47, rd8(t + 0x19));
        wr32(e + 6, 0xFF119C);
        sub_00D760(rd16(t + 0x24));
    }
    cr.d0 = d0;
    cr.d1 = d1;
    cr.d2 = d2;
    cr.d3 = 0;
}

void spawn(uint32_t tmpl) {
    if (U16{0xFF0E7C}) {
        cr.d3 = 0;
        return;
    }
    U32{0xFF26F2} = tmpl;
    spawn_from_template();
}

// 0xA796: the boss start cell (class 0x26), with fixed handlers.
void spawn_A796() {
    if (U16{0xFF0E7C}) {
        cr.d3 = 0;
        return;
    }
    uint16_t d0 = cr.d0, d1 = cr.d1;
    int16_t x = int16_t(((d0 + U16{0xFF0E06}) << 8) + 0x80 - player_x);
    int16_t y = int16_t(((d1 + U16{0xFF0E08}) << 8) + 0x80 - player_y);
    if (Distance(x, y) >= 0xA00) {
        AddVisibleObject();
        cr.d3 = 0;
        return;
    }
    uint32_t cell = cr.a0;
    if (cell < 0xFF7CD6 || cell >= 0xFFBCD6) cell = 0xFFCCD6;
    wr8(cell, 0);
    uint32_t e = sub_013466();
    if (!e) {
        AddVisibleObject();
        cr.d3 = 0;
        return;
    }
    wr8(e + 0x1F, 0);
    wr32(e + 0x12, 0x15B1E);
    wr32(e + 0xE, 0x15D84);
    wr32(e + 0xA, 0x15E4E);
    wr32(e + 0x16, 0x15ED2);
    wr16(e + 4, uint16_t(rd16(e + 4) | 0x84));
    wr16(e + 0x36, 0x64);
    wr16(e + 0x20, uint16_t(((d0 + U16{0xFF0E06}) << 8) + 0x80));
    wr16(e + 0x22, uint16_t(((d1 + U16{0xFF0E08}) << 8) + 0x80));
    wr16(e + 0x24, 0);
    wr8(e + 0x34, 0);
    wr8(e + 0x32, rd8(0xFF1029));
    cr.d3 = 0;
}

// Doors closed across the middle of a cell (0xAD8C x, 0xAF5E y) and sliding
// doors (0xAE2E, 0xB000) with their opening from the door list.
bool seen(uint32_t list, uint32_t end_ptr) {
    for (uint32_t a = list; a != U32{end_ptr}; a += 4)
        if (rd32(a) == cr.a0) return true;
    wr32(U32{end_ptr}, cr.a0);
    U32{end_ptr} += 4;
    return false;
}

uint16_t door_opening() {
    for (uint32_t a3 = 0xFF0EC6; a3 != U32{0xFF0EC2}; a3 += 0xE)
        if (rd32(a3) == cr.a0) return rd16(a3 + 4);
    return 0;
}

void door_x() {
    wr8(0xFF0E7C, 0xFF);
    if (seen(0xFF0FCE, 0xFF0EBE)) {
        cr.d3 = 0;
        return;
    }
    U32{0xFF1174} = 0;
    U32{0xFF1178} = 0;
    U16{0xFF1172} = 0;
    CastRegs saved = cr;
    uint16_t x = uint16_t(((cr.d0 + U16{0xFF0E06}) << 8) + 0x80);
    uint16_t y = uint16_t((cr.d1 + U16{0xFF0E08}) << 8);
    bool from_left = s16(cr.d0) < 0 || (cr.d0 == 0 && U16{0xFF0E0E} >= 0x80);
    if (from_left) {
        edge_right(x, y);
        edge_left(x, uint16_t(y + 0x100));
    } else {
        edge_left(x, y);
        edge_right(x, uint16_t(y + 0x100));
    }
    U32{0xFF0E70} = 0xFF64E6;
    project_and_draw(Tail::Wall);
    cr = saved;
    cr.d3 = 0;
}

void door_y() {
    wr8(0xFF0E7C, 0xFF);
    if (seen(0xFF0FA6, 0xFF0EBA)) {
        cr.d3 = 0;
        return;
    }
    U32{0xFF1174} = 0;
    U32{0xFF1178} = 0;
    U16{0xFF1172} = 0;
    CastRegs saved = cr;
    uint16_t x = uint16_t((cr.d0 + U16{0xFF0E06}) << 8);
    uint16_t y = uint16_t(((cr.d1 + U16{0xFF0E08}) << 8) + 0x80);
    bool from_top = s16(cr.d1) < 0 || (cr.d1 == 0 && U16{0xFF0E10} >= 0x80);
    if (from_top) {
        edge_left(x, y);
        edge_right(uint16_t(x + 0x100), y);
    } else {
        edge_right(x, y);
        edge_left(uint16_t(x + 0x100), y);
    }
    U32{0xFF0E70} = 0xFF64E6;
    project_and_draw(Tail::Wall);
    cr = saved;
    cr.d3 = 0;
}

void half(uint32_t face) {
    U32{0xFF0E70} = face;
    project_and_draw(Tail::None);
    U16{0xFF0E5C} = uint16_t(s16(U16{0xFF0E5C}) >> 1);
    U16{0xFF0E5E} = uint16_t(s16(U16{0xFF0E5E}) >> 1);
    DrawWallSpan(edge_a, edge_b);
}

void sliding_door_x() {
    if (seen(0xFF0FCE, 0xFF0EBE)) {
        cr.d3 = 0;
        return;
    }
    uint16_t open = door_opening();
    if (open == 0x80) {
        cr.d3 = 0;
        return;
    }
    U32{0xFF1174} = 0;
    U32{0xFF1178} = 0;
    U16{0xFF1172} = 0;
    CastRegs saved = cr;
    uint16_t x = uint16_t(((cr.d0 + U16{0xFF0E06}) << 8) + 0x80);
    uint16_t y = uint16_t(((cr.d1 + U16{0xFF0E08}) << 8) - open);
    bool from_left = s16(cr.d0) < 0 || (cr.d0 == 0 && U16{0xFF0E0E} >= 0x80);
    uint16_t y2 = uint16_t(y + 0x80 + 2 * open);
    if (from_left) {
        edge_right(x, y);
        edge_left(x, uint16_t(y + 0x80));
        half(0xFF64EE);
        edge_right(x, y2);
        edge_left(x, uint16_t(y2 + 0x80));
        half(0xFF64E6);
    } else {
        edge_left(x, y);
        edge_right(x, uint16_t(y + 0x80));
        half(0xFF64E6);
        edge_left(x, y2);
        edge_right(x, uint16_t(y2 + 0x80));
        half(0xFF64EE);
    }
    cr = saved;
    cr.d3 = 0;
}

void sliding_door_y() {
    if (seen(0xFF0FA6, 0xFF0EBA)) {
        cr.d3 = 0;
        return;
    }
    U32{0xFF1174} = 0;
    U32{0xFF1178} = 0;
    U16{0xFF1172} = 0;
    uint16_t open = door_opening();
    if (open == 0x80) {
        cr.d3 = 0;
        return;
    }
    CastRegs saved = cr;
    uint16_t x = uint16_t(((cr.d0 + U16{0xFF0E06}) << 8) - open);
    uint16_t y = uint16_t(((cr.d1 + U16{0xFF0E08}) << 8) + 0x80);
    bool from_top = s16(cr.d1) < 0 || (cr.d1 == 0 && U16{0xFF0E10} >= 0x80);
    uint16_t x2 = uint16_t(x + 0x80 + 2 * open);
    if (from_top) {
        edge_left(x, y);
        edge_right(uint16_t(x + 0x80), y);
        half(0xFF64E6);
        edge_left(x2, y);
        edge_right(uint16_t(x2 + 0x80), y);
        half(0xFF64EE);
    } else {
        edge_right(x, y);
        edge_left(uint16_t(x + 0x80), y);
        half(0xFF64EE);
        edge_right(x2, y);
        edge_left(uint16_t(x2 + 0x80), y);
        half(0xFF64E6);
    }
    cr = saved;
    cr.d3 = 0;
}

} // namespace

// 0x9226: d3 = class 6-0x84. Returns false when the cell lets the ray pass.
bool CellSpecialDispatch() {
    uint8_t c = uint8_t(cr.d3);
    cr.d3 = c;
    switch (c) {
    case 0x06: case 0x83: door_y(); break;
    case 0x07: case 0x84: door_x(); break;
    case 0x0C: doors(0x40407F7F, 0x7F40407F, 0xC0C00000, 0x00C0C000); break;
    case 0x0D: doors(0x40404040, 0x40404040, 0xC0C0C0C0, 0xC0C0C0C0); break;
    case 0x0E: {
        int16_t h = s16(U16{0xFF116E});
        bool up = h > 0 || (h == 0 && s16(cr.d0) < 0);
        U32{0xFF1174} = up ? 0x40404040 : 0xC0C0C0C0;
        U32{0xFF1178} = up ? 0x40404040 : 0xC0C0C0C0;
        cr.d3 = 1;
        break;
    }
    case 0x0F:
        if (s16(U16{0xFF116E}) >= 0) U32{0xFF1174} = 0x40404040, U32{0xFF1178} = 0x40404040;
        else U32{0xFF1174} = 0xC0C0C0C0, U32{0xFF1178} = 0xC0C0C0C0;
        cr.d3 = 1;
        break;
    case 0x10:
        if (s16(U16{0xFF116E}) >= 0) U32{0xFF1174} = 0x40400000, U32{0xFF1178} = 0x00404000;
        else U32{0xFF1174} = 0xC0C08080, U32{0xFF1178} = 0x80C0C080;
        cr.d3 = 1;
        break;
    case 0x11: doors3(0x40407F00, 0x0040407F, 0xC0C00080, 0x80C0C000, 0x40C00000, 0x0040C000); break;
    case 0x38: doors(0x7F7F4040, 0x407F7F40, 0x0000C0C0, 0xC00000C0); break;
    case 0x39: {
        int16_t h = s16(U16{0xFF116E});
        bool up = h > 0 || (h == 0 && s16(cr.d0) >= 0);
        U32{0xFF1174} = up ? 0x40404040 : 0xC0C0C0C0;
        U32{0xFF1178} = up ? 0x40404040 : 0xC0C0C0C0;
        cr.d3 = 1;
        break;
    }
    case 0x3A:
        if (s16(U16{0xFF116E}) >= 0) U32{0xFF1174} = 0x00004040, U32{0xFF1178} = 0x40000040;
        else U32{0xFF1174} = 0x8080C0C0, U32{0xFF1178} = 0xC08080C0;
        cr.d3 = 1;
        break;
    case 0x3B: doors3(0x7F004040, 0x407F0040, 0x0080C0C0, 0xC00080C0, 0x000040C0, 0xC0000040); break;
    case 0x40: doors(0x407F7F40, 0x40407F7F, 0xC00000C0, 0xC0C00000); break;
    case 0x41: {
        int16_t h = s16(U16{0xFF116E});
        bool up = h > 0 || (h == 0 && s16(cr.d1) < 0);
        U32{0xFF1174} = up ? 0x40404040 : 0xC0C0C0C0;
        U32{0xFF1178} = up ? 0x40404040 : 0xC0C0C0C0;
        cr.d3 = 1;
        break;
    }
    case 0x42:
        if (s16(U16{0xFF116E}) >= 0) U32{0xFF1174} = 0x40000040, U32{0xFF1178} = 0x40400000;
        else U32{0xFF1174} = 0xC08080C0, U32{0xFF1178} = 0xC0C08080;
        cr.d3 = 1;
        break;
    case 0x43: doors3(0x407F0040, 0x40407F00, 0xC00080C0, 0xC0C00080, 0xC0000040, 0x40C00000); break;
    case 0x48: doors(0x7F40407F, 0x7F7F4040, 0x00C0C000, 0x0000C0C0); break;
    case 0x49: {
        int16_t h = s16(U16{0xFF116E});
        bool up = h > 0 || (h == 0 && s16(cr.d1) >= 0);
        U32{0xFF1174} = up ? 0x40404040 : 0xC0C0C0C0;
        U32{0xFF1178} = up ? 0x40404040 : 0xC0C0C0C0;
        cr.d3 = 1;
        break;
    }
    case 0x4A:
        if (s16(U16{0xFF116E}) >= 0) U32{0xFF1174} = 0x00404000, U32{0xFF1178} = 0x00004040;
        else U32{0xFF1174} = 0x80C0C080, U32{0xFF1178} = 0x8080C0C0;
        cr.d3 = 1;
        break;
    case 0x4B: doors3(0x0040407F, 0x7F004040, 0x80C0C000, 0x0080C0C0, 0x0040C000, 0x000040C0); break;
    case 0x30: {
        // 0x9862: lift platform at height FF116F.
        uint8_t h = uint8_t(rd8(0xFF116F) | 1);
        uint16_t w = uint16_t(h << 8 | h);
        U16{0xFF1174} = w;
        U16{0xFF1176} = w;
        U16{0xFF1178} = w;
        U16{0xFF117A} = w;
        cr.d3 = 1;
        break;
    }
    // Portals (stair openings): the edge seen from one side is recorded.
    case 0x12: case 0x14:
        if (s16(cr.d1) < 0) add_portal(pack(0, 1, 1, 1), 0xFFFF);
        cr.d3 = 0;
        break;
    case 0x3C: case 0x3E:
        if (s16(cr.d1) > 0) add_portal(pack(1, 0, 0, 0), 0xFFFF);
        cr.d3 = 0;
        break;
    case 0x44: case 0x46:
        if (s16(cr.d0) > 0) add_portal(pack(0, 0, 0, 1), 0xFFFF);
        cr.d3 = 0;
        break;
    case 0x4C: case 0x4E:
        if (s16(cr.d0) < 0) add_portal(pack(1, 1, 1, 0), 0xFFFF);
        cr.d3 = 0;
        break;
    case 0x13:
        if (s16(cr.d1) > 0) add_portal(pack(1, 0, 0, 0), 0);
        cr.d3 = 0;
        break;
    case 0x3D:
        if (s16(cr.d1) < 0) add_portal(pack(0, 1, 1, 1), 0);
        cr.d3 = 0;
        break;
    case 0x45:
        if (s16(cr.d0) < 0) add_portal(pack(1, 1, 1, 0), 0);
        cr.d3 = 0;
        break;
    case 0x4D:
        if (s16(cr.d0) > 0) add_portal(pack(0, 0, 0, 1), 0);
        cr.d3 = 0;
        break;
    case 0x32: case 0x33: case 0x34:
        if (s16(cr.d0) > 0) add_portal(pack(0, 0, 0, 1), 0xFFFF);
        cr.d3 = 0;
        break;
    case 0x31:
        if (s16(cr.d0) < 0) add_portal(pack(1, 1, 1, 0), 0);
        cr.d3 = 0;
        break;
    case 0x51: case 0x52: case 0x53:
        if (s16(cr.d0) < 0) add_portal(pack(1, 1, 1, 0), 0xFFFF);
        cr.d3 = 0;
        break;
    case 0x50:
        if (s16(cr.d0) > 0) add_portal(pack(0, 0, 0, 1), 0);
        cr.d3 = 0;
        break;
    case 0x55: case 0x56: case 0x57:
        if (s16(cr.d1) < 0) add_portal(pack(0, 1, 1, 1), 0xFFFF);
        cr.d3 = 0;
        break;
    case 0x54:
        if (s16(cr.d1) > 0) add_portal(pack(1, 0, 0, 0), 0);
        cr.d3 = 0;
        break;
    case 0x59: case 0x5A: case 0x5B:
        if (s16(cr.d1) > 0) add_portal(pack(1, 0, 0, 0), 0xFFFF);
        cr.d3 = 0;
        break;
    case 0x58:
        if (s16(cr.d1) < 0) add_portal(pack(0, 1, 1, 1), 0);
        cr.d3 = 0;
        break;
    case 0x26: spawn_A796(); break;
    case 0x29: spawn(0xAB0C); break;
    case 0x2A: spawn(0xAB32); break;
    case 0x2B: spawn(0xAB58); break;
    case 0x65: spawn(0xAB7E); break;
    case 0x66: spawn(0xABA4); break;
    case 0x67: spawn(0xABCA); break;
    case 0x68: spawn(0xABF0); break;
    case 0x69: spawn(0xAC16); break;
    case 0x6A: spawn(0xAC3C); break;
    case 0x6B: spawn(0xAC62); break;
    case 0x2D: sliding_door_y(); break;
    case 0x2E: sliding_door_x(); break;
    case 0x79: case 0x7A: case 0x7B: case 0x7C: case 0x7D: case 0x7E: case 0x7F: case 0x80: cr.d3 = 1; break;
    default:
        if ((c >= 0x18 && c <= 0x25) || c == 0x2C || (c >= 0x35 && c <= 0x37) || (c >= 0x5C && c <= 0x64) ||
            (c >= 0x6C && c <= 0x76) || c == 0x78 || c == 0x82)
            AddVisibleObject();
        cr.d3 = 0;
        break;
    }
    return uint16_t(cr.d3) != 0;
}

// 0x1131E: a map object seen by a ray (cell d0, d1 relative to the player,
// class d3) is added to the object list FF10A2 with its scale and column.
void AddVisibleObject() {
    uint32_t key = uint32_t(cr.d1) << 16 | cr.d0;
    uint32_t a3 = 0xFF10A2;
    for (; a3 != U32{0xFF116A}; a3 += 0xA)
        if (rd32(a3) == key) return;
    if (a3 == 0xFF116A) return;
    wr32(a3, key);
    wr8(a3 + 4, 0);
    wr8(a3 + 5, uint8_t(cr.d3));
    int16_t x = int16_t(((cr.d0 + U16{0xFF0E06}) << 8) + 0x80);
    int16_t y = int16_t(((cr.d1 + U16{0xFF0E08}) << 8) + 0x80);
    int16_t dx = int16_t(x - player_x), dy = int16_t(y - player_y);
    int16_t cx = s16(U16{0xFF0E00}), cy = s16(U16{0xFF0E02});
    int32_t depth = (int32_t(dx) * cx + int32_t(dy) * cy) >> 6;
    if (depth < 2) return;
    uint32_t lat = uint32_t(int32_t(dy) * cx - int32_t(dx) * cy);
    uint16_t col = uint16_t(divs(lat, uint16_t(depth)) + 0x40);
    uint16_t scale = uint16_t(divs(0x10000, uint16_t(depth)));
    wr16(a3 + 6, scale);
    wr16(a3 + 8, col);
    U32{0xFF116A} = a3 + 0xA;
}

// 0xD7C0: distance estimate max + (max + min) / 2 ... as (|x| + |y| + max) / 2.
uint16_t Distance(int16_t x, int16_t y) {
    uint16_t ax = uint16_t(x < 0 ? -x : x), ay = uint16_t(y < 0 ? -y : y);
    uint16_t m = ax >= ay ? ax : ay;
    return uint16_t(int16_t(uint16_t(ax + ay + m)) >> 1);
}

// 0x7F92: background (sky and floor colours FF6226) in columns no wall covered.
void sub_007F92() {
    uint32_t a0 = framebuffer, a4 = 0xFF6126, a5 = 0xFF6226;
    auto fill = [&]() {
        for (int r = 0; r < 0x50; r++, a0 += 4) wr8(a0, rd8(a5++));
        a5 -= 0x50;
        a0 -= 0x140;
    };
    for (int col = 0; col < 0x20; col++) {
        for (int k = 0; k < 4; k++) {
            a5 = uint32_t(a5 + ((k & 1) ? -0x50 : 0x50));
            if (!rd16(a4)) fill();
            a4 += 2;
            if (k < 3) a0 += 1;
        }
        a0 += 0x13D;
    }
}

// 0xB9D2
void Render3DView() {
    U16{0xFF1180} = 0;
    U32{0xFF0E4C} = 0xFFFFFFFF;
    U32{0xFF0EBA} = 0xFF0FA6;
    U32{0xFF0EBE} = 0xFF0FCE;
    U32{0xFF116A} = 0xFF10A2;
    for (uint32_t a = 0xFF6126; a < 0xFF6226; a += 4) wr32(a, 0);
    U32{0xFF0E78} = 0;
    int16_t cx = int16_t(player_x) >> 8, cy = int16_t(player_y) >> 8;
    U16{0xFF0E06} = uint16_t(cx);
    U16{0xFF0E08} = uint16_t(cy);
    U16{0xFF0E3A} = uint16_t(-cx);
    U16{0xFF0E38} = uint16_t(-cx + 0x1F);
    U16{0xFF0E34} = uint16_t(-cy);
    U16{0xFF0E36} = uint16_t(-cy + 0x1F);
    int16_t vis = s16(U16{0xFF0EB4});
    if (-vis > s16(U16{0xFF0E3A})) U16{0xFF0E3A} = uint16_t(-vis);
    if (-vis > s16(U16{0xFF0E34})) U16{0xFF0E34} = uint16_t(-vis);
    if (vis < s16(U16{0xFF0E38})) U16{0xFF0E38} = uint16_t(vis);
    if (vis < s16(U16{0xFF0E36})) U16{0xFF0E36} = uint16_t(vis);
    U16{0xFF0E0E} = player_x & 0xFF;
    U16{0xFF0E0A} = uint16_t(0xFF - (player_x & 0xFF));
    U16{0xFF0E10} = player_y & 0xFF;
    U16{0xFF0E0C} = uint16_t(0xFF - (player_y & 0xFF));
    uint32_t a0 = cur_map_ptr + uint32_t(cx) + uint32_t(int32_t(cy) << 5);
    U32{0xFF0E12} = a0;
    U16{0xFF0E74} = 0;
    uint8_t c = rd8(cell_class + rd8(a0));
    bool every_ray = false;
    if (c >= 6) {
        sub_00B4E4(c);
        U16{0xFF0E1A} -= U16{0xFF116E};
        cr.d0 = cr.d1 = 0;
        cr.d3 = c;
        cr.a0 = a0;
        cr.a5 = cell_class;
        CellSpecialDispatch();
        every_ray = !U16{0xFF0EB2};
    }
    U16 ray{0xFF0E74};
    do {
        uint16_t angle = uint16_t((rd16(0x9124 + ray * 2) + player_angle) & 0x1FF);
        CastRay(angle);
        ray += every_ray ? 1 : 2;
    } while (s16(ray) <= 0x80);
    uint8_t own = rd8(cell_class + rd8(U32{0xFF0E12}));
    if (rd8(0xBB90 + own)) {
        sub_007F92();
        sub_009D72();
        U16{0xFF0E1A} += U16{0xFF116E};
    } else {
        sub_009D72();
        U16{0xFF0E1A} += U16{0xFF116E};
        sub_007F92();
    }
    if (!skip_object_pass) DrawEnemiesAndObjects();
}

// 0xB9A6: renders from a point 1/8 cell behind the player.
void sub_00B9A6() {
    int16_t bx = s16(U16{0xFF0E00}) >> 3, by = s16(U16{0xFF0E02}) >> 3;
    player_x -= uint16_t(bx);
    player_y -= uint16_t(by);
    Render3DView();
    player_x += uint16_t(s16(U16{0xFF0E00}) >> 3);
    player_y += uint16_t(s16(U16{0xFF0E02}) >> 3);
}

} // namespace game
