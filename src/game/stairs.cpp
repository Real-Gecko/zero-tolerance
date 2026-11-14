// Stairs and lifts under the player (0xB4E4-0xB8FC), level changes, and
// the depth-sorted drawing of enemies and map objects (0x1329A).
#include "render.hpp"

#include "game.hpp"
#include "hires.hpp"

#include <algorithm>

namespace game {

namespace {

auto& height = st.stair_height; // view height offset on stairs: -0x40..0x40
auto& lift = st.lift;   // 1 going down, -1 going up, 2 / -2 arrived

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
    if (cur_area == 2) QueueMessage(Msg::StepUp);
    uint16_t lvl = uint16_t(cur_level - 1);
    if (cur_area != 2) QueueMessage(Msg(uint8_t(Msg::Level) + (cur_area << 4) + lvl));
    ChangeLevel(lvl);
}

// 0xB862: one level up.
void level_up() {
    if (cur_area == 2) QueueMessage(Msg::StepDown);
    uint16_t lvl = uint16_t(cur_level + 1);
    if (cur_area != 2) QueueMessage(Msg(uint8_t(Msg::Level) + (cur_area << 4) + lvl));
    ChangeLevel(lvl);
}

} // namespace

// 0xB4E4: the player stands on a special cell of class cls.
void StandOnCell(uint8_t cls) {
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
void ChangeLevel(uint16_t level) {
    CountEnemiesLeft();
    StoreLevelPassword();
    cur_level = level;
    cur_map = level_map(level);
    // Floor number shown on the lift faces (faces words 0xA0-0xA4): "165 - level" as three digits.
    uint16_t n = uint16_t(0xA5 - cur_level);
    uint16_t d1 = 0x2E;
    if (n >= 0x64) {
        const uint16_t* a1 = kFloorDigits + 1;
        do {
            n = uint16_t(n - 0x64);
            d1 = *a1++;
        } while (n >= 0x64);
    }
    st.faces[0xA0] = d1;
    d1 = 0x2E;
    if (n >= 0xA) {
        const uint16_t* a1 = kFloorDigits + 1;
        do {
            n = uint16_t(n - 0xA);
            d1 = *a1++;
        } while (n >= 0xA);
    }
    st.faces[0xA2] = d1;
    st.faces[0xA4] = kFloorDigits[n];
    // level_light lies just before the maps: level 16 reads the first cell.
    SetLighting(cur_level < 16 ? st.level_light[cur_level] : map_cell(cur_level - 16));
}

// 0x13466: a free enemy slot, linked into the list.
Enemy* AllocEnemy() {
    if (st.object_count == 0x40) return nullptr;
    st.object_count += 1;
    Enemy* a0 = st.objects;
    if (a0->flags) {
        Enemy* a1;
        do {
            a1 = a0;
            a0++;
        } while (a0->flags);
        a0->next = a1->next;
        a1->next = link_of(a0);
    } else {
        a0->next = st.object_list;
        st.object_list = link_of(a0);
    }
    a0->ai = Act::None;
    a0->hit = Hit::None;
    a0->death = Act::Unlink;
    a0->draw = Draw::None;
    a0->stun = 0;
    a0->delay = 0;
    a0->level = uint8_t(st.cur_level);
    a0->id = link_of(a0);
    a0->flags = 1;
    return a0;
}

// 0x13526: removes an enemy from the list.
void UnlinkEnemy(Enemy* a0) {
    st.object_count -= 1;
    if (link_of(a0) == st.object_list) {
        st.object_list = a0->next;
    } else {
        Enemy* a1 = first_object();
        while (a1->next != link_of(a0)) a1 = next_object(a1);
        a1->next = a0->next;
    }
    a0->flags = 0;
    a0->next = 0;
}

namespace {

// The enemies in view by scale, smallest (farthest) first (FF24F0, count FF24EE).
struct HeapEntry {
    int16_t scale, col;
    Enemy* e;
};
HeapEntry heap[0x40];
uint16_t heap_n;

// 0x133C0: inserts scale d5 into the heap; returns the free entry.
HeapEntry& heap_push(int16_t d5) {
    uint16_t i = heap_n;
    heap_n += 1;
    while (i) {
        uint16_t p = uint16_t(int16_t(i - 1) >> 1);
        if (d5 >= heap[p].scale) break;
        heap[i] = heap[p];
        i = p;
    }
    return heap[i];
}

// 0x133FA: removes the top of the heap.
void heap_pop() {
    heap_n -= 1;
    uint16_t n = heap_n;
    int16_t last = heap[n].scale;
    uint16_t hole = 0, d0 = 0;
    for (;;) {
        uint16_t c = uint16_t(d0 * 2 + 1);
        if (s16(c) >= s16(n)) break;
        d0 = c;
        if (s16(uint16_t(c + 1)) < s16(n) && heap[c + 1].scale < heap[c].scale) d0 = uint16_t(c + 1);
        if (last <= heap[d0].scale) break;
        heap[hole] = heap[d0];
        hole = d0;
    }
    heap[hole] = heap[n];
}

void draw_top_enemy() {
    uint16_t d5 = uint16_t(heap[0].scale), d1 = uint16_t(heap[0].col);
    Enemy* e = heap[0].e;
    float fcol, fscale;
    hires::exact(int16_t(e->x), int16_t(e->y), fcol, fscale);
    hires::anchor(int16_t(d1), int16_t(d5), fcol, std::min(fscale, 300.0f));
    DrawObject(e, d5, d1);
    heap_pop();
}

} // namespace

// 0x1329A: enemies on the level in view, sorted by scale, drawn back to front
// together with the map objects of FF10A2.
void DrawEnemiesAndObjects() {
    uint16_t n = st.object_count;
    heap_n = 0;
    Enemy* a0 = first_object();
    for (int i = 0; i < n; i++, a0 = next_object(a0)) {
        if (a0->level != uint8_t(st.cur_level)) continue;
        int16_t dx = int16_t(a0->x - player_x), dy = int16_t(a0->y - player_y);
        if (Distance(dx, dy) > 0xC00) continue;
        int16_t cx = s16(st.dir_x), cy = s16(st.dir_y);
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
        heap_push(scale) = {scale, col, a0};
    }
    int objects = view.object_n; // walked backwards, nearest last
    if (!objects) {
        while (heap_n) draw_top_enemy();
        return;
    }
    if (!heap_n) {
        do DrawObjectSprite(view.objects[--objects]); while (objects);
        return;
    }
    for (;;) {
        if (heap[0].scale >= view.objects[objects - 1].scale) {
            DrawObjectSprite(view.objects[--objects]);
            if (!objects) {
                while (heap_n) draw_top_enemy();
                return;
            }
        } else {
            draw_top_enemy();
            if (!heap_n) {
                do DrawObjectSprite(view.objects[--objects]); while (objects);
                return;
            }
        }
    }
}

} // namespace game
