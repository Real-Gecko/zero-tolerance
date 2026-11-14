// Ray caster of the 3D view: Render3DView (0xB9D2), CastRay (0xBC90), the
// wall-end projection (0xCA4C) and the special cell handlers called through
// CellSpecialDispatch (0x9226, table 0x923A).
//
// The caster keeps its state in 68000 registers that the cell handlers read
// and change, so it is translated with an explicit register set (cr).
#include "render.hpp"

#include "game.hpp"
#include "hires.hpp"
#include "m68k.hpp"

#include "../host.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace game {

CastRegs cr;
View view;
bool skip_object_pass = false;

namespace {

bool original_ray = true; // PC: the ray being cast is one the original cast (only these spawn)
// PC: the object list FF10A2 as the original's rays alone would fill it (20
// entries): the cell keys, and how many of them call Random() when drawn.
uint32_t original_objects[20];
int original_object_count, original_random_objects;

int16_t s16(uint16_t v) { return int16_t(v); }

// ---- edge records (0xC9A8, 0xCA04) ----

// 0xC9A8: left end of a wall at (d0, d1), reusing the last right end when it
// is the same point.
void edge_left(uint16_t x, uint16_t y) {
    if (x == view.b_x && y == view.b_y) {
        view.a = view.b;
        return;
    }
    int16_t dx = int16_t(x - player_x), dy = int16_t(y - player_y);
    int16_t cx = s16(st.dir_x), cy = s16(st.dir_y);
    view.a = {int32_t(dx) * cx + int32_t(dy) * cy, int32_t(dy) * cx - int32_t(dx) * cy};
}

// 0xCA04: right end.
void edge_right(uint16_t x, uint16_t y) {
    view.b_x = x;
    view.b_y = y;
    int16_t dx = int16_t(x - player_x), dy = int16_t(y - player_y);
    int16_t cx = s16(st.dir_x), cy = s16(st.dir_y);
    view.b = {int32_t(dx) * cx + int32_t(dy) * cy, int32_t(dy) * cx - int32_t(dx) * cy};
}

enum class Tail { Wall, Portal, None };

// The ends of the last span project_wide projected (columns, heights).
struct {
    float xa, ha, xb, hb;
} wide_edges;

// PC: project_and_draw in the wide pass: the wall from edge A (u = 0) to
// edge B (u = 1) clipped in floating point to the field of view of
// hires::view_tan and to a near plane, then recorded.
void project_wide(Tail tail) {
    float ad = float(view.a.depth), al = float(view.a.lateral);
    float bd = float(view.b.depth), bl = float(view.b.lateral);
    float t = hires::view_tan, u0 = 0, u1 = 1;
    // Keeps the part where f (linear along the wall) is not negative.
    auto keep = [&](float fa, float fb) {
        if (fa < 0 && fb < 0) return false;
        if (fa < 0) u0 = std::max(u0, fa / (fa - fb));
        else if (fb < 0) u1 = std::min(u1, fa / (fa - fb));
        return true;
    };
    if (!keep(t * ad + al, t * bd + bl) || !keep(t * ad - al, t * bd - bl) || !keep(ad - 64, bd - 64) || u0 >= u1)
        return;
    auto end = [&](float u, float& x, float& h) {
        float d = ad + (bd - ad) * u;
        x = 64 + (al + (bl - al) * u) * 64 / d;
        h = 4194304.0f / d;
    };
    float xa, ha, xb, hb;
    end(u0, xa, ha);
    end(u1, xb, hb);
    hires::edges(xa, ha, xb, hb, u0 * 256, u1 * 256);
    wide_edges = {xa, ha, xb, hb};
    if (tail == Tail::Wall) DrawWallSpan();
    else if (tail == Tail::Portal) DrawPortalSpan();
}

// 0xCA4C: projects the two edges, clipping them to the view sides, then
// draws the span (PC: project_wide).
void project_and_draw(Tail tail) { project_wide(tail); }

// ---- wall emitters (0xC70E-0xC9A4) ----

void set_face(int word) { view.strip = uint16_t(st.cell_faces[view.faces + word] * 8); }

void emit_diag_up_south();
void emit_diag_down_south();
void emit_diag_down_north();
void emit_diag_up_north();

// 0xC70E: south face (y = top of cell, viewed from +y).
void emit_south_wall() {
    uint8_t c = uint8_t(cr.d3);
    if (c == 3) return emit_diag_up_south();
    if (c == 4) return emit_diag_down_south();
    std::copy_n(view.sides, 2, view.door);
    set_face(0);
    edge_right(cr.d0, cr.d1);
    cr.d0 = uint16_t(cr.d0 + 0x100);
    edge_left(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

// 0xC748: north face (y = bottom of cell).
void emit_north_wall() {
    uint8_t c = uint8_t(cr.d3);
    if (c == 2) return emit_diag_down_north();
    if (c == 5) return emit_diag_up_north();
    std::copy_n(view.sides + 2, 2, view.door);
    set_face(3);
    cr.d1 = uint16_t(cr.d1 + 0x100);
    edge_left(cr.d0, cr.d1);
    cr.d0 = uint16_t(cr.d0 + 0x100);
    edge_right(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

// 0xC788: east face (x = left of cell).
void emit_east_wall() {
    uint8_t c = uint8_t(cr.d3);
    if (c == 2) return emit_diag_down_north();
    if (c == 3) return emit_diag_up_south();
    std::copy_n(view.sides + 6, 2, view.door);
    set_face(1);
    edge_left(cr.d0, cr.d1);
    cr.d1 = uint16_t(cr.d1 + 0x100);
    edge_right(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

// 0xC7C4: west face (x = right of cell).
void emit_west_wall() {
    uint8_t c = uint8_t(cr.d3);
    if (c == 5) return emit_diag_up_north();
    if (c == 4) return emit_diag_down_south();
    std::copy_n(view.sides + 4, 2, view.door);
    set_face(2);
    cr.d0 = uint16_t(cr.d0 + 0x100);
    edge_right(cr.d0, cr.d1);
    cr.d1 = uint16_t(cr.d1 + 0x100);
    edge_left(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

// Diagonal walls: down = (x, y) to (x+1, y+1), up = (x, y+1) to (x+1, y);
// south / north = the face drawn like the south / north wall.
// 0xC842
void emit_diag_down_south() {
    set_face(0);
    edge_right(cr.d0, cr.d1);
    cr.d0 = uint16_t(cr.d0 + 0x100);
    cr.d1 = uint16_t(cr.d1 + 0x100);
    edge_left(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

// 0xC8A8
void emit_diag_down_north() {
    set_face(3);
    edge_left(cr.d0, cr.d1);
    cr.d0 = uint16_t(cr.d0 + 0x100);
    cr.d1 = uint16_t(cr.d1 + 0x100);
    edge_right(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

// 0xC910
void emit_diag_up_north() {
    set_face(3);
    cr.d1 = uint16_t(cr.d1 + 0x100);
    edge_left(cr.d0, cr.d1);
    cr.d1 = uint16_t(cr.d1 - 0x100);
    cr.d0 = uint16_t(cr.d0 + 0x100);
    edge_right(cr.d0, cr.d1);
    project_and_draw(Tail::Wall);
}

// 0xC97C
void emit_diag_up_south() {
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
    if (cr.a0 == view.last_cell) return false;
    view.last_cell = cr.a0;
    view.faces = uint16_t(map_cell(cr.a0) * 4);
    cr.d0 = uint16_t((cr.d0 + view.cell_x) << 8);
    cr.d1 = uint16_t((cr.d1 + view.cell_y) << 8);
    return true;
}

// A diagonal wall in the player's own cell (0xC804, 0xC86A, 0xC8D2, 0xC93E):
// the rays it covers are skipped.
void own_diagonal(void (*emit)()) {
    if (cr.a0 == view.last_cell) return;
    view.last_cell = cr.a0;
    view.faces = uint16_t(map_cell(cr.a0) * 4);
    cr.d0 = uint16_t((cr.d0 + view.cell_x) << 8);
    cr.d1 = uint16_t((cr.d1 + view.cell_y) << 8);
    view.right_col = -1;
    emit();
    if (view.right_col > view.first_ray) view.first_ray = view.right_col;
}

uint8_t cls(uint8_t v) { return st.cell_class[v]; }

// ---- axis-aligned rays ----

struct Axis {
    uint8_t own_a, own_b;      // diagonal classes that block in the own cell
    int16_t step_a0, step_x, step_y;
    int16_t View::*bound;      // FF0E34/36/38/3A
    bool bound_min;            // stop below the bound instead of above it
    uint8_t ca, cb;            // classes with their own emitter
    void (*ea)(), (*eb)(), (*e)();
};

void cast_axis(const Axis& ax) {
    cr.d0 = cr.d1 = 0;
    cr.a0 = st.player_cell;
    auto in_bound = [&]() {
        int16_t v = s16(ax.step_x ? cr.d0 : cr.d1), b = view.*ax.bound;
        return ax.bound_min ? v >= b : v <= b;
    };
    auto step = [&]() {
        cr.a0 += ax.step_a0;
        cr.d0 = uint16_t(cr.d0 + ax.step_x);
        cr.d1 = uint16_t(cr.d1 + ax.step_y);
    };
    bool hit = false;
    cr.d3 = uint16_t((cr.d3 & 0xFF00) | map_cell(cr.a0));
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
            cr.d3 = uint16_t((cr.d3 & 0xFF00) | map_cell(cr.a0));
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

uint16_t slope_a() { return kRaySlopes[cr.a4][0]; }
uint16_t slope_b() { return kRaySlopes[cr.a4][1]; }

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
    uint16_t View::*fa_mul, View::*fa_add; // walker A start fraction: (mul * slope) >> 8 + add
    uint8_t diag_a;             // own-cell diagonal hit by A
    void (*own_a)();
    uint16_t View::*fb_mul, View::*fb_add;
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
    cr.a4 = angle;
    cr.d7 = 0xFF;
    cr.d0 = cr.d1 = 0;
    cr.a0 = st.player_cell;
    cr.d3 = cls(map_cell(cr.a0));
    if (uint8_t(cr.d3) == q.own_diag) return own_diagonal(q.own_d);
    cr.d2 = uint16_t(((uint32_t(view.*q.fa_mul) * slope_a()) >> 8) + view.*q.fa_add);
    if (cr.d2 <= 0xFF && uint8_t(cr.d3) == q.diag_a) return own_diagonal(q.own_a);
    cr.d6 = uint16_t(((uint32_t(view.*q.fb_mul) * slope_b()) >> 8) + view.*q.fb_add);
    if (cr.d6 <= 0xFF && uint8_t(cr.d3) == q.diag_b) return own_diagonal(q.own_b);
    cr.d4 = cr.d0;
    cr.d5 = cr.d1;
    cr.a1 = cr.a0;
    cr.a0 += q.sx;
    cr.d0 = uint16_t(cr.d0 + q.sx);
    cr.a1 += q.sy * 0x20;
    cr.d5 = uint16_t(cr.d5 + q.sy);
    auto carry_a = [&]() {
        if (cr.d2 > 0xFF) {
            uint16_t n = cr.d2 >> 8;
            cr.d3 = cr.d2;
            cr.d2 &= 0xFF;
            cr.d1 = uint16_t(cr.d1 + q.sy * n);
            cr.a0 += q.sy * int(n << 5);
        }
    };
    auto carry_b = [&]() {
        if (cr.d6 > 0xFF) {
            uint16_t n = cr.d6 >> 8;
            cr.d3 = cr.d6;
            cr.d6 &= 0xFF;
            cr.d4 = uint16_t(cr.d4 + q.sx * n);
            cr.a1 += q.sx * int(n);
        }
    };
    carry_a();
    carry_b();
    int16_t bx = q.sx > 0 ? view.east : view.west, by = q.sy > 0 ? view.south : view.north;
    auto inside = [&](uint16_t x, uint16_t y) {
        bool okx = q.sx > 0 ? s16(x) <= bx : s16(x) >= bx;
        bool oky = q.sy > 0 ? s16(y) <= by : s16(y) >= by;
        return okx && oky;
    };
    for (;;) {
        // Walker A goes first while it is nearer along x (ties broken on y).
        int16_t ax_ = s16(cr.d0), bx_ = s16(cr.d4);
        bool a_first = q.sx > 0 ? (ax_ < bx_ || (ax_ == bx_ && (q.sy > 0 ? s16(cr.d1) < s16(cr.d5) : s16(cr.d1) > s16(cr.d5))))
                                : (ax_ > bx_ || (ax_ == bx_ && (q.sy > 0 ? s16(cr.d1) < s16(cr.d5) : s16(cr.d1) > s16(cr.d5))));
        if (a_first) {
            if (!inside(cr.d0, cr.d1)) return;
            cr.d3 = uint16_t((cr.d3 & 0xFF00) | map_cell(cr.a0));
            if (uint8_t(cr.d3) && check_a(q.check_a)) {
                if (!hit_cell()) return;
                if (q.neighbour_on_a) {
                    uint8_t n = cls(map_cell(cr.a0 + q.neighbour));
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
            cr.a0 += q.sx;
            cr.d0 = uint16_t(cr.d0 + q.sx);
            cr.d2 = uint16_t(cr.d2 + slope_a());
            carry_a();
        } else {
            if (!inside(cr.d4, cr.d5)) return;
            cr.d3 = uint16_t((cr.d3 & 0xFF00) | map_cell(cr.a1));
            if (uint8_t(cr.d3) && check_b(q.check_b)) {
                if (cr.a1 == view.last_cell) return;
                cr.d0 = cr.d4;
                cr.d1 = cr.d5;
                cr.d2 = cr.d6;
                cr.a0 = cr.a1;
                if (!hit_cell()) return;
                if (!q.neighbour_on_a) {
                    uint8_t n = cls(map_cell(cr.a0 + q.neighbour));
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
            cr.a1 += q.sy * 0x20;
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
    std::fill_n(view.sides, 8, 0);
    std::fill_n(view.door, 2, 0);
    view.door_hit = false;
    cr.d3 = 0;
    static const Axis north{2, 5, -0x20, 0, -1, &View::north, true, 2, 5, emit_diag_down_north, emit_diag_up_north, emit_north_wall};
    static const Axis south{3, 4, 0x20, 0, 1, &View::south, false, 3, 4, emit_diag_up_south, emit_diag_down_south, emit_south_wall};
    static const Axis east{2, 3, 1, 1, 0, &View::east, false, 2, 3, emit_diag_down_north, emit_diag_up_south, emit_east_wall};
    static const Axis west{4, 5, -1, -1, 0, &View::west, true, 4, 5, emit_diag_down_south, emit_diag_up_north, emit_west_wall};
    static const Quadrant q_se{1, 1, 3, &View::x_rest, &View::y_in, 2, emit_diag_down_north, &View::y_rest, &View::x_in, 4, emit_diag_down_south,
                               emit_diag_up_south, 2, 4, emit_east_wall, emit_south_wall, false, -1, {1, 2, 3, 5}};
    static const Quadrant q_ne{1, -1, 2, &View::x_rest, &View::y_rest, 3, emit_diag_up_south, &View::y_in, &View::x_in, 5, emit_diag_up_north,
                               emit_diag_down_north, 3, 5, emit_east_wall, emit_north_wall, true, 0x20, {1, 2, 4, 5}};
    static const Quadrant q_nw{-1, -1, 5, &View::x_in, &View::y_rest, 4, emit_diag_down_south, &View::y_in, &View::x_rest, 2, emit_diag_down_north,
                               emit_diag_up_north, 4, 2, emit_west_wall, emit_north_wall, false, 1, {1, 3, 4, 5}};
    static const Quadrant q_sw{-1, 1, 4, &View::x_in, &View::y_in, 5, emit_diag_up_north, &View::y_rest, &View::x_rest, 3, emit_diag_up_south,
                               emit_diag_down_south, 5, 3, emit_west_wall, emit_south_wall, true, -0x20, {1, 2, 3, 4}};
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
static void add_portal(Portal p, uint16_t flag) {
    if (flag) flag = uint16_t((cr.d6 & 0xFF00) | 0xFF); // st.b d6 keeps d6's high byte
    for (int i = 0; i < view.portal_n; i++) {
        const Portal& q = view.portals[i];
        if (q.x0 == p.x0 && q.y0 == p.y0 && q.x1 == p.x1 && q.y1 == p.y1) return;
    }
    // The original has room for 4 (more overwrite FF119A on); the wide view meets more.
    if (view.portal_n == 64) return;
    p.gradient = flag;
    view.portals[view.portal_n++] = p;
}

// 0x9D72: draws the recorded portals.
void DrawPortals() {
    auto corner = [](int8_t x, int8_t y) {
        return std::pair{uint16_t((x + view.cell_x) << 8), uint16_t((y + view.cell_y) << 8)};
    };
    for (int i = 0; i < view.portal_n; i++) {
        const Portal& p = view.portals[i];
        auto [x0, y0] = corner(p.x0, p.y0);
        edge_left(x0, y0);
        auto [x1, y1] = corner(p.x1, p.y1);
        view.portal_gradient = p.gradient;
        edge_right(x1, y1);
        project_and_draw(Tail::Portal);
    }
    view.portal_n = 0;
}

namespace {

// Door offsets of the faces (FF1174 / FF1178 as longs: S and N, W and E),
// chosen by the player's height offset FF116E.
void set_sides(uint32_t sn, uint32_t we) {
    for (int i = 0; i < 4; i++) {
        view.sides[i] = uint8_t(sn >> (24 - 8 * i));
        view.sides[4 + i] = uint8_t(we >> (24 - 8 * i));
    }
}

void doors(uint32_t up, uint32_t up2, uint32_t down, uint32_t down2) {
    if (s16(st.stair_height) > 0) set_sides(up, up2);
    else set_sides(down, down2);
    cr.d3 = 1;
}

void doors3(uint32_t up, uint32_t up2, uint32_t down, uint32_t down2, uint32_t level, uint32_t level2) {
    int16_t h = s16(st.stair_height);
    if (h > 0) set_sides(up, up2);
    else if (h < 0) set_sides(down, down2);
    else set_sides(level, level2);
    cr.d3 = 1;
}

// Portal cells: the corners of the edge (cells relative to the player's),
// added when the ray comes from the given side.
Portal pack(int dx0, int dy0, int dx1, int dy1) {
    auto b = [](uint16_t v, int add) { return int8_t(uint8_t(v) + add); };
    return {b(cr.d0, dx0), b(cr.d1, dy0), b(cr.d0, dx1), b(cr.d1, dy1), 0};
}

// Enemy start cells: spawn the enemy from template FF26F2 once it is near.
// PC: only the rays the original cast spawn, so the game is unchanged by the
// window; the others show the cell like a far one.
void spawn_from_template() {
    uint16_t d0 = cr.d0, d1 = cr.d1, d2 = cr.d2;
    int16_t x = int16_t(((d0 + view.cell_x) << 8) + 0x80 - player_x);
    int16_t y = int16_t(((d1 + view.cell_y) << 8) + 0x80 - player_y);
    if (!original_ray || Distance(x, y) >= 0x800) {
        AddVisibleObject();
        cr.d3 = 0;
        return;
    }
    put_cell(cr.a0, 0);
    uint16_t sx = cr.d0;
    Enemy* e = AllocEnemy();
    if (e) {
        const EnemyTemplate& t = kEnemyTemplates[st.spawn_template];
        e->delay = 0;
        e->ai = t.ai;
        e->draw = t.draw;
        e->hit = t.hit;
        e->death = t.death;
        e->flags = uint16_t(e->flags | t.flags);
        e->health = t.health;
        e->x = uint16_t(((sx + view.cell_x) << 8) + 0x80);
        e->y = uint16_t(((cr.d1 + view.cell_y) << 8) + 0x80);
        e->z = t.z;
        e->level = uint8_t(st.cur_level);
        e->state = 0;
        e->count = 0x14;
        e->timer = 0;
        e->dest_x = player_x;
        e->dest_y = player_y;
        e->sprites = t.sprites;
        if (e->sprites == SpriteSet::Type1) {
            // The original uses d1 as scratch here and never restores it.
            d1 = e->y;
            if ((e->x ^ d1) & 0x100) e->sprites = SpriteSet::Type1Alt;
        }
        e->item_a = t.item_a;
        e->item_b = t.item_b;
        set_target(e, player_struct);
        PlaySoundLocal(t.sound);
    }
    cr.d0 = d0;
    cr.d1 = d1;
    cr.d2 = d2;
    cr.d3 = 0;
}

void spawn(uint8_t tmpl) {
    if (view.door_hit) {
        cr.d3 = 0;
        return;
    }
    st.spawn_template = tmpl;
    spawn_from_template();
}

// 0xA796: the boss start cell (class 0x26), with fixed handlers.
void spawn_boss_cell() {
    if (view.door_hit) {
        cr.d3 = 0;
        return;
    }
    uint16_t d0 = cr.d0, d1 = cr.d1;
    int16_t x = int16_t(((d0 + view.cell_x) << 8) + 0x80 - player_x);
    int16_t y = int16_t(((d1 + view.cell_y) << 8) + 0x80 - player_y);
    if (!original_ray || Distance(x, y) >= 0xA00) {
        AddVisibleObject();
        cr.d3 = 0;
        return;
    }
    put_cell(cr.a0, 0);
    Enemy* e = AllocEnemy();
    if (!e) {
        AddVisibleObject();
        cr.d3 = 0;
        return;
    }
    e->delay = 0;
    e->ai = Act::BossCellAi;
    e->draw = Draw::BossCell;
    e->hit = Hit::BossCell;
    e->death = Act::BossCellDeath;
    e->flags = uint16_t(e->flags | 0x84);
    e->health = 0x64;
    e->x = uint16_t(((d0 + view.cell_x) << 8) + 0x80);
    e->y = uint16_t(((d1 + view.cell_y) << 8) + 0x80);
    e->z = 0;
    e->state = 0;
    e->level = uint8_t(st.cur_level);
    cr.d3 = 0;
}

// Doors closed across the middle of a cell (0xAD8C x, 0xAF5E y) and sliding
// doors (0xAE2E, 0xB000) with their opening from the door list.
// The original lists have room for 10 (more overwrite what follows); the
// wide view can meet more.
bool seen(Cell* list, uint8_t& n) {
    if (std::find(list, list + n, cr.a0) != list + n) return true;
    if (n < 64) list[n++] = cr.a0;
    return false;
}

void door_x() {
    view.door_hit = true;
    if (seen(view.seen_x, view.seen_x_n)) {
        cr.d3 = 0;
        return;
    }
    std::fill_n(view.sides, 8, 0);
    std::fill_n(view.door, 2, 0);
    CastRegs saved = cr;
    uint16_t x = uint16_t(((cr.d0 + view.cell_x) << 8) + 0x80);
    uint16_t y = uint16_t((cr.d1 + view.cell_y) << 8);
    bool from_left = s16(cr.d0) < 0 || (cr.d0 == 0 && view.x_in >= 0x80);
    if (from_left) {
        edge_right(x, y);
        edge_left(x, uint16_t(y + 0x100));
    } else {
        edge_left(x, y);
        edge_right(x, uint16_t(y + 0x100));
    }
    view.strip = 0x90; // face 0x12
    project_and_draw(Tail::Wall);
    cr = saved;
    cr.d3 = 0;
}

void door_y() {
    view.door_hit = true;
    if (seen(view.seen_y, view.seen_y_n)) {
        cr.d3 = 0;
        return;
    }
    std::fill_n(view.sides, 8, 0);
    std::fill_n(view.door, 2, 0);
    CastRegs saved = cr;
    uint16_t x = uint16_t((cr.d0 + view.cell_x) << 8);
    uint16_t y = uint16_t(((cr.d1 + view.cell_y) << 8) + 0x80);
    bool from_top = s16(cr.d1) < 0 || (cr.d1 == 0 && view.y_in >= 0x80);
    if (from_top) {
        edge_left(x, y);
        edge_right(uint16_t(x + 0x100), y);
    } else {
        edge_right(x, y);
        edge_left(uint16_t(x + 0x100), y);
    }
    view.strip = 0x90; // face 0x12
    project_and_draw(Tail::Wall);
    cr = saved;
    cr.d3 = 0;
}

// A door half: the strips from word strip of face 0x12, half the face's width.
void half(uint16_t strip) {
    view.strip = strip;
    project_and_draw(Tail::None);
    hires::scale_u(0.5f);
    DrawWallSpan();
}

void sliding_door_x() {
    if (seen(view.seen_x, view.seen_x_n)) {
        cr.d3 = 0;
        return;
    }
    uint16_t open = DoorOpening(cr.a0);
    if (open == 0x80) {
        cr.d3 = 0;
        return;
    }
    std::fill_n(view.sides, 8, 0);
    std::fill_n(view.door, 2, 0);
    CastRegs saved = cr;
    uint16_t x = uint16_t(((cr.d0 + view.cell_x) << 8) + 0x80);
    uint16_t y = uint16_t(((cr.d1 + view.cell_y) << 8) - open);
    bool from_left = s16(cr.d0) < 0 || (cr.d0 == 0 && view.x_in >= 0x80);
    uint16_t y2 = uint16_t(y + 0x80 + 2 * open);
    if (from_left) {
        edge_right(x, y);
        edge_left(x, uint16_t(y + 0x80));
        half(0x94);
        edge_right(x, y2);
        edge_left(x, uint16_t(y2 + 0x80));
        half(0x90);
    } else {
        edge_left(x, y);
        edge_right(x, uint16_t(y + 0x80));
        half(0x90);
        edge_left(x, y2);
        edge_right(x, uint16_t(y2 + 0x80));
        half(0x94);
    }
    cr = saved;
    cr.d3 = 0;
}

void sliding_door_y() {
    if (seen(view.seen_y, view.seen_y_n)) {
        cr.d3 = 0;
        return;
    }
    std::fill_n(view.sides, 8, 0);
    std::fill_n(view.door, 2, 0);
    uint16_t open = DoorOpening(cr.a0);
    if (open == 0x80) {
        cr.d3 = 0;
        return;
    }
    CastRegs saved = cr;
    uint16_t x = uint16_t(((cr.d0 + view.cell_x) << 8) - open);
    uint16_t y = uint16_t(((cr.d1 + view.cell_y) << 8) + 0x80);
    bool from_top = s16(cr.d1) < 0 || (cr.d1 == 0 && view.y_in >= 0x80);
    uint16_t x2 = uint16_t(x + 0x80 + 2 * open);
    if (from_top) {
        edge_left(x, y);
        edge_right(uint16_t(x + 0x80), y);
        half(0x90);
        edge_left(x2, y);
        edge_right(uint16_t(x2 + 0x80), y);
        half(0x94);
    } else {
        edge_right(x, y);
        edge_left(uint16_t(x + 0x80), y);
        half(0x94);
        edge_right(x2, y);
        edge_left(uint16_t(x2 + 0x80), y);
        half(0x90);
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
        int16_t h = s16(st.stair_height);
        bool up = h > 0 || (h == 0 && s16(cr.d0) < 0);
        set_sides(up ? 0x40404040 : 0xC0C0C0C0, up ? 0x40404040 : 0xC0C0C0C0);
        cr.d3 = 1;
        break;
    }
    case 0x0F:
        if (s16(st.stair_height) >= 0) set_sides(0x40404040, 0x40404040);
        else set_sides(0xC0C0C0C0, 0xC0C0C0C0);
        cr.d3 = 1;
        break;
    case 0x10:
        if (s16(st.stair_height) >= 0) set_sides(0x40400000, 0x00404000);
        else set_sides(0xC0C08080, 0x80C0C080);
        cr.d3 = 1;
        break;
    case 0x11: doors3(0x40407F00, 0x0040407F, 0xC0C00080, 0x80C0C000, 0x40C00000, 0x0040C000); break;
    case 0x38: doors(0x7F7F4040, 0x407F7F40, 0x0000C0C0, 0xC00000C0); break;
    case 0x39: {
        int16_t h = s16(st.stair_height);
        bool up = h > 0 || (h == 0 && s16(cr.d0) >= 0);
        set_sides(up ? 0x40404040 : 0xC0C0C0C0, up ? 0x40404040 : 0xC0C0C0C0);
        cr.d3 = 1;
        break;
    }
    case 0x3A:
        if (s16(st.stair_height) >= 0) set_sides(0x00004040, 0x40000040);
        else set_sides(0x8080C0C0, 0xC08080C0);
        cr.d3 = 1;
        break;
    case 0x3B: doors3(0x7F004040, 0x407F0040, 0x0080C0C0, 0xC00080C0, 0x000040C0, 0xC0000040); break;
    case 0x40: doors(0x407F7F40, 0x40407F7F, 0xC00000C0, 0xC0C00000); break;
    case 0x41: {
        int16_t h = s16(st.stair_height);
        bool up = h > 0 || (h == 0 && s16(cr.d1) < 0);
        set_sides(up ? 0x40404040 : 0xC0C0C0C0, up ? 0x40404040 : 0xC0C0C0C0);
        cr.d3 = 1;
        break;
    }
    case 0x42:
        if (s16(st.stair_height) >= 0) set_sides(0x40000040, 0x40400000);
        else set_sides(0xC08080C0, 0xC0C08080);
        cr.d3 = 1;
        break;
    case 0x43: doors3(0x407F0040, 0x40407F00, 0xC00080C0, 0xC0C00080, 0xC0000040, 0x40C00000); break;
    case 0x48: doors(0x7F40407F, 0x7F7F4040, 0x00C0C000, 0x0000C0C0); break;
    case 0x49: {
        int16_t h = s16(st.stair_height);
        bool up = h > 0 || (h == 0 && s16(cr.d1) >= 0);
        set_sides(up ? 0x40404040 : 0xC0C0C0C0, up ? 0x40404040 : 0xC0C0C0C0);
        cr.d3 = 1;
        break;
    }
    case 0x4A:
        if (s16(st.stair_height) >= 0) set_sides(0x00404000, 0x00004040);
        else set_sides(0x80C0C080, 0x8080C0C0);
        cr.d3 = 1;
        break;
    case 0x4B: doors3(0x0040407F, 0x7F004040, 0x80C0C000, 0x0080C0C0, 0x0040C000, 0x000040C0); break;
    case 0x30: {
        // 0x9862: lift platform at height FF116F.
        std::fill_n(view.sides, 8, uint8_t(uint8_t(st.stair_height) | 1));
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
    case 0x26: spawn_boss_cell(); break;
    case 0x29: spawn(0); break;
    case 0x2A: spawn(1); break;
    case 0x2B: spawn(2); break;
    case 0x65: spawn(3); break;
    case 0x66: spawn(4); break;
    case 0x67: spawn(5); break;
    case 0x68: spawn(6); break;
    case 0x69: spawn(7); break;
    case 0x6A: spawn(8); break;
    case 0x6B: spawn(9); break;
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
    if (original_ray && original_object_count < 20 &&
        std::find(original_objects, original_objects + original_object_count, key) ==
            original_objects + original_object_count) {
        int16_t dx = int16_t(((cr.d0 + view.cell_x) << 8) + 0x80 - player_x);
        int16_t dy = int16_t(((cr.d1 + view.cell_y) << 8) + 0x80 - player_y);
        if (((int32_t(dx) * s16(st.dir_x) + int32_t(dy) * s16(st.dir_y)) >> 6) >= 2) {
            original_objects[original_object_count++] = key;
            if (uint8_t(cr.d3) == 0x6E) original_random_objects++;
        }
    }
    for (int i = 0; i < view.object_n; i++)
        if (view.objects[i].y == int16_t(cr.d1) && view.objects[i].x == int16_t(cr.d0)) return;
    if (view.object_n == 20) return;
    ViewObject& o = view.objects[view.object_n];
    o.y = int16_t(cr.d1);
    o.x = int16_t(cr.d0);
    o.cls = uint8_t(cr.d3);
    int16_t x = int16_t(((cr.d0 + view.cell_x) << 8) + 0x80);
    int16_t y = int16_t(((cr.d1 + view.cell_y) << 8) + 0x80);
    int16_t dx = int16_t(x - player_x), dy = int16_t(y - player_y);
    int16_t cx = s16(st.dir_x), cy = s16(st.dir_y);
    int32_t depth = (int32_t(dx) * cx + int32_t(dy) * cy) >> 6;
    if (depth < 2) return;
    uint32_t lat = uint32_t(int32_t(dy) * cx - int32_t(dx) * cy);
    uint16_t col = uint16_t(divs(lat, uint16_t(depth)) + 0x40);
    uint16_t scale = uint16_t(divs(0x10000, uint16_t(depth)));
    o.scale = int16_t(scale);
    o.col = int16_t(col);
    view.object_n++;
}

// 0xD7C0: distance estimate max + (max + min) / 2 ... as (|x| + |y| + max) / 2.
uint16_t Distance(int16_t x, int16_t y) {
    uint16_t ax = uint16_t(x < 0 ? -x : x), ay = uint16_t(y < 0 ? -y : y);
    uint16_t m = ax >= ay ? ax : ay;
    return uint16_t(int16_t(uint16_t(ax + ay + m)) >> 1);
}

// 0x7F92: background (sky and floor colours FF6226) in columns no wall covered.
void FillBackground() { hires::fill(); }

namespace {

void render_view() {
    view.portal_n = 0;
    view.b_x = view.b_y = 0xFFFF;
    view.seen_y_n = view.seen_x_n = 0;
    view.object_n = 0;
    original_object_count = original_random_objects = 0;
    std::fill_n(view.depth, 0x80, 0);
    view.last_cell = kNoCell;
    int16_t cx = int16_t(player_x) >> 8, cy = int16_t(player_y) >> 8;
    view.cell_x = uint16_t(cx);
    view.cell_y = uint16_t(cy);
    view.west = int16_t(-cx);
    view.east = int16_t(-cx + 0x1F);
    view.north = int16_t(-cy);
    view.south = int16_t(-cy + 0x1F);
    int16_t vis = s16(st.visibility);
    if (-vis > view.west) view.west = int16_t(-vis);
    if (-vis > view.north) view.north = int16_t(-vis);
    if (vis < view.east) view.east = vis;
    if (vis < view.south) view.south = vis;
    view.x_in = player_x & 0xFF;
    view.x_rest = uint16_t(0xFF - (player_x & 0xFF));
    view.y_in = player_y & 0xFF;
    view.y_rest = uint16_t(0xFF - (player_y & 0xFF));
    Cell a0 = cur_map + cx + cy * 0x20;
    st.player_cell = a0;
    view.first_ray = 0;
    uint8_t c = st.cell_class[map_cell(a0)];
    bool every_ray = false;
    if (c >= 6) {
        StandOnCell(c);
        st.view_z -= st.stair_height;
        cr.d0 = cr.d1 = 0;
        cr.d3 = c;
        cr.a0 = a0;
        CellSpecialDispatch();
        every_ray = !st.lighting;
    }
    // How often the original cast each angle step: table 0x9124 from ray
    // FF0E74, every other entry unless it is dark.
    uint8_t casts[0x81] = {};
    int16_t ray = view.first_ray;
    do {
        casts[kRayAngles[ray] + 64]++;
        ray = int16_t(ray + (every_ray ? 1 : 2));
    } while (ray <= 0x80);
    // Every angle step (512 per turn) across the field of view; the
    // original's rays again as often as it cast them.
    int n = int(std::ceil(std::atan(hires::view_tan) * 256 / float(M_PI)));
    for (int d = -n; d <= n; d++) {
        int k = d < -64 || d > 64 ? 0 : casts[d + 64];
        original_ray = k > 0;
        CastRay(uint16_t((d + player_angle) & 0x1FF));
        hires::record(false); // the repeats only for the game
        while (--k > 0) CastRay(uint16_t((d + player_angle) & 0x1FF));
        hires::record(true);
    }
    original_ray = true;
    uint8_t own = st.cell_class[map_cell(st.player_cell)];
    if (kOwnSprite[own]) {
        FillBackground();
        DrawPortals();
        st.view_z += st.stair_height;
    } else {
        DrawPortals();
        st.view_z += st.stair_height;
        FillBackground();
    }
    if (!skip_object_pass) {
        DrawEnemiesAndObjects();
        // The game's random numbers the original's object list drew (class 0x6E).
        for (int i = 0; i < original_random_objects; i++) Random();
    }
}

} // namespace

// 0xB9D2. PC: cast over the window's field of view and recorded for the
// screen (hires); the 128x80 view is not drawn.
void Render3DView() {
    hires::begin();
    render_view();
    hires::end();
}

// PC: for the wall span project_wide projected last, what DrawWallSpan
// leaves for the game: the depth buffer (FF6126, the wall height in each of
// the 128 original columns, kept the highest) and its right column (FF0E76).
void WideWallDepth() {
    auto [xa, ha, xb, hb] = wide_edges;
    view.right_col = int16_t(std::min(0x7F, int(std::trunc(xb - 64)) + 64));
    int x0 = std::max(0, int(std::ceil(xa))), x1 = std::min(0x7F, int(std::ceil(xb)) - 1);
    for (int x = x0; x <= x1; x++) {
        float t = xb > xa ? (float(x) - xa) / (xb - xa) : 0;
        uint16_t h = uint16_t(std::clamp(ha + (hb - ha) * t, 0.0f, 32767.0f));
        if (h >= view.depth[x]) view.depth[x] = h;
    }
}

// 0xB9A6: renders from a point 1/8 cell behind the player.
void RenderFromBehind() {
    int16_t bx = s16(st.dir_x) >> 3, by = s16(st.dir_y) >> 3;
    player_x -= uint16_t(bx);
    player_y -= uint16_t(by);
    Render3DView();
    player_x += uint16_t(s16(st.dir_x) >> 3);
    player_y += uint16_t(s16(st.dir_y) >> 3);
}

} // namespace game
