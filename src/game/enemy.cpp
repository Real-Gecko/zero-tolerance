// Enemies: the per-frame update of the enemy list (0x131E8), timed items
// (0x13182) and the shared enemy helpers.
#include "enemy.hpp"
#include "m68k.hpp"
#include "render.hpp"

namespace game {

namespace {

int16_t s16(uint16_t v) { return int16_t(v); }

// Map index of a position: (x >> 8) + (y & 0xFF00) >> 3.
int16_t cell_index(uint16_t x, uint16_t y) {
    return int16_t(uint16_t((s16(x) >> 8) + (s16(uint16_t(y & 0xFF00)) >> 3)));
}

// 0x14852: nonzero when cell class d3 blocks enemies.
uint8_t CellBlocksEnemy(uint8_t cls) { return kEnemyBlocked[cls]; }

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
// in the map at a1 (cur_map or the enemy's level).
uint16_t line_blocked(bool own_map, Enemy* a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4) {
    uint16_t d2 = uint16_t(Distance(s16(uint16_t(d0 - d3)), s16(uint16_t(d1 - d4))) >> 6);
    if (d2 == 0 || d2 == 1) return 0;
    int16_t sx = int16_t(uint16_t(divs(ext_l(uint16_t(d3 - d0)), d2)));
    int16_t sy = int16_t(uint16_t(divs(ext_l(uint16_t(d4 - d1)), d2)));
    for (int n = d2 - 1; n > 0; n--) {
        Cell a1 = own_map ? enemy_map(a0) : cur_map;
        d0 = uint16_t(d0 + sx);
        d1 = uint16_t(d1 + sy);
        uint8_t shape = CollisionShape(st.cell_class[map_cell(a1 + cell_index(d0, d1))]);
        if (!shape) continue;
        if (shape >= 6 || shape == 1) return 1;
        if (diagonal_hit(shape, uint8_t(d0), uint8_t(d1))) return 1;
    }
    return 0;
}

// 0x14454, 0x144FE, 0x14A56, 0x14B08: drops the item (the first cell of the
// class in item_b or item_a) on the first empty cell around the enemy and
// removes the enemy.
// With every cell taken, keep = true leaves the enemy alone; counted calls
// 0x57E0C first.
void drop_item(Enemy* a0, uint16_t item_ofs, bool keep, bool counted) {
    Cell a1 = enemy_map(a0) + cell_index(a0->x, a0->y);
    static const int16_t around[] = {0, -1, 1, -0x20, 0x20, 0x1F, 0x21, -0x1F, -0x21};
    bool found = false;
    for (int16_t d : around) {
        if (!map_cell(a1 + d)) {
            a1 += d;
            found = true;
            break;
        }
    }
    if (found)
        put_cell(a1, st.class_cell[item_ofs == 0x47 ? a0->item_b : a0->item_a]);
    else if (keep)
        return;
    else if (counted)
        CountEnemiesLeft();
    RemoveEnemy(a0);
}

} // namespace

// The spawn templates by enemy type - 1 (0xAB0C).
const EnemyTemplate kEnemyTemplates[10] = {
    {Act::Type0Ai, Draw::Type0, Hit::Type0, Act::DieDropItem46Counted, 0xDC, 0x190, SpriteSet::Type0, 0x29, 0x2C, 0xFFE0, 0x2C},
    {Act::Type1Ai, Draw::Type1, Hit::Type1, Act::DieDropItem46Counted, 0xDC, 0x190, SpriteSet::Type1, 0x2A, 0x6C, 0xFFE0, 0x2C},
    {Act::Type2Ai, Draw::Type2, Hit::Type1, Act::DieDropItem46Counted, 0xDC, 0x320, SpriteSet::Type2, 0x2B, 0x6D, 0xFFE0, 0x22},
    {Act::Type3Start, Draw::Type3, Hit::Type3, Act::DieDropItem46Counted, 0xDC, 0xC8, SpriteSet::Type3, 0x65, 0x6E, 0, 0x24},
    {Act::Type4Ai, Draw::Type4, Hit::Type4, Act::DieDropItem46Counted, 0xDC, 0x190, SpriteSet::Type4, 0x66, 0x6F, 0xFFE0, 0x26},
    {Act::Type5Ai, Draw::Type5, Hit::Walker, Act::DieDropItem46Keep, 0xDC, 0xBB8, SpriteSet::Type5, 0x67, 0x70, 0xFFE0, 0x28},
    {Act::Type6Ai, Draw::Type6, Hit::Type6, Act::DieDropItem46Counted, 0x5C, 0x190, SpriteSet::Type6, 0x68, 0x71, 0xFFE0, 0x2A},
    {Act::Type7Ai, Draw::Type7, Hit::Type7, Act::DieDropItem46Counted, 0xDC, 0x258, SpriteSet::Type7, 0x69, 0x72, 0xFFE0, 0x20},
    {Act::Type8Ai, Draw::Type8, Hit::Type8, Act::DieDropItem46Keep, 0xDC, 0x1F40, SpriteSet::Type8, 0x6A, 0x73, 0xFFE0, 0x2E},
    {Act::Type9Ai, Draw::Type9, Hit::Walker, Act::DieDropItem46Keep, 0xDC, 0x1770, SpriteSet::Type9, 0x6B, 0x74, 0xFFE0, 0x30},
};

// 0x1657A: map of the enemy's level.
Cell enemy_map(const Enemy* a0) { return level_map(a0->level); }

// 0xD740: sound d0 when the enemy is on the player's level.
void EnemySound(Enemy* a0, uint16_t id) {
    if (a0->level == uint8_t(st.cur_level)) PlaySoundLocal(id);
}

// 0x145AA (margin 0x20) / 0x146FE (margin 0x40): moves the enemy by (d0, d1),
// sliding along blocking cells, keeps it margin away from blocking
// neighbours and 0x20 away from the player. Returns the movement done.
EnemyMove move_enemy(Enemy* a0, uint16_t d0, uint16_t d1, uint8_t margin) {
    uint16_t ox = a0->x, oy = a0->y;
    Cell a1 = enemy_map(a0);
    auto blocked = [&](int16_t i) { return CellBlocksEnemy(st.cell_class[map_cell(a1 + i)]) != 0; };
    uint16_t x = ox, y = oy;
    if (!blocked(cell_index(uint16_t(x + d0), uint16_t(y + d1)))) {
        x = uint16_t(x + d0);
        y = uint16_t(y + d1);
    } else if (!blocked(cell_index(x, uint16_t(y + d1))))
        y = uint16_t(y + d1);
    else if (!blocked(cell_index(uint16_t(x + d0), y)))
        x = uint16_t(x + d0);
    a0->x = x;
    a0->y = y;
    a1 += cell_index(x, y);
    uint8_t hi = uint8_t(0xFF - margin);
    uint8_t xl = uint8_t(a0->x);
    if (xl < margin) {
        if (CellBlocksEnemy(st.cell_class[map_cell(a1 - 1)])) set_low_byte(a0->x, margin);
    } else if (xl > hi) {
        if (CellBlocksEnemy(st.cell_class[map_cell(a1 + 1)])) set_low_byte(a0->x, hi);
    }
    uint8_t yl = uint8_t(a0->y);
    if (yl < margin) {
        if (CellBlocksEnemy(st.cell_class[map_cell(a1 - 0x20)])) set_low_byte(a0->y, margin);
    } else if (yl > hi) {
        if (CellBlocksEnemy(st.cell_class[map_cell(a1 + 0x20)])) set_low_byte(a0->y, hi);
    }
    uint16_t dx = uint16_t(a0->x - player_x), dy = uint16_t(a0->y - player_y);
    uint16_t d = uint16_t(Distance(s16(dx), s16(dy)) + 1);
    if (d != 0 && d < 0x20) {
        a0->x = uint16_t(uint16_t(divs(ext_l(dx) << 5, d)) + player_x);
        a0->y = uint16_t(uint16_t(divs(ext_l(dy) << 5, d)) + player_y);
    }
    return {uint16_t(a0->x - ox), uint16_t(a0->y - oy)};
}

EnemyMove MoveEnemyNarrow(Enemy* a0, uint16_t d0, uint16_t d1) { return move_enemy(a0, d0, d1, 0x20); } // 0x145AA
EnemyMove MoveEnemyWide(Enemy* a0, uint16_t d0, uint16_t d1) { return move_enemy(a0, d0, d1, 0x40); } // 0x146FE

// 0x13D42 (map of the enemy's level) / 0x13D48 (current map): the collision
// shape at (d0, d1): 0 free, 1 wall, >= 6 the shape itself.
uint8_t EnemyCollisionShape(Enemy* a0, uint16_t d0, uint16_t d1) {
    uint8_t shape = CollisionShape(st.cell_class[map_cell(enemy_map(a0) + cell_index(d0, d1))]);
    if (shape == 0 || shape == 1 || shape >= 6) return shape;
    return diagonal_hit(shape, uint8_t(d0), uint8_t(d1));
}

// 0x13CBC: 1 when the cell at (d0, d1) of the enemy's level is one that
// shots pass over (per area).
uint16_t EnemyMapShotPasses(Enemy* a0, uint16_t d0, uint16_t d1) {
    uint8_t c = map_cell(enemy_map(a0) + cell_index(d0, d1));
    if (cur_area == 0) return c == 1 || c == 0x6E || c == 0x6F || (c >= 0x91 && c <= 0x96);
    if (cur_area == 1) {
        if (c == 1 || c == 0x8F || c == 0xA3) return 1;
        if (s16(a0->z) < 0) return 0;
        return c == 3 || c == 0xA2 || (c >= 6 && c <= 8);
    }
    return 0;
}

// 0x1642C: the enemy's target: the player when on the enemy's level, else
// none (in a link game, the nearer of the two players).
Enemy* FindTarget(Enemy* a0) {
    uint8_t level = a0->level;
    return uint8_t(cur_level) == level ? player_struct : nullptr;
}

// 0x1649C: like 0x1642C, preferring a target in sight in a link game.
Enemy* FindVisibleTarget(Enemy* a0) {
    uint8_t level = a0->level;
    return uint8_t(cur_level) == level ? player_struct : nullptr;
}

// 0x165D8 / 0x166E8 (see line_blocked).
uint16_t LineBlocked(Enemy* a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4) { return line_blocked(false, a0, d0, d1, d3, d4); }
uint16_t EnemyLineBlocked(Enemy* a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4) { return line_blocked(true, a0, d0, d1, d3, d4); }

// 0x13D48: EnemyCollisionShape on the current map.
uint8_t MapCollisionShape(uint16_t d0, uint16_t d1) {
    uint8_t shape = CollisionShape(st.cell_class[map_cell(cur_map + cell_index(d0, d1))]);
    if (shape == 0 || shape == 1 || shape >= 6) return shape;
    return diagonal_hit(shape, uint8_t(d0), uint8_t(d1));
}

// 0x13C36: EnemyMapShotPasses on the current map, by the player's height.
uint16_t MapShotPasses(uint16_t d0, uint16_t d1) {
    uint8_t c = map_cell(cur_map + cell_index(d0, d1));
    if (cur_area == 0) return c == 1 || c == 0x6E || c == 0x6F || (c >= 0x91 && c <= 0x96);
    if (cur_area == 1) {
        if (c == 1 || c == 0x8F || c == 0xA3) return 1;
        if (s16(st.view_z) < 0) return 0;
        return c == 3 || c == 0xA2 || (c >= 6 && c <= 8);
    }
    return 0;
}

// 0xA8B6: nonzero when the player's cell (FF0E12) is one of the classes of
// table 0xA8CC.
uint8_t PlayerCellStopsShots() { return kStopsShots[st.cell_class[map_cell(st.player_cell)]]; }

// 0x16388: the nearest enemy to a0 (or the player for a0 = 0) on its level
// whose flags include all of d5, as a target code: kPlayerPos for the
// player, kNoTarget for none.
uint8_t NearestEnemy(Enemy* a0, uint16_t d5) {
    uint8_t a1 = kNoTarget;
    uint16_t best = 0xFFFF, x = player_x, y = player_y;
    uint8_t level = uint8_t(cur_level);
    if (a0) {
        x = a0->x;
        y = a0->y;
        level = a0->level;
    }
    uint16_t left = st.object_count;
    if (left) {
        Enemy* a3 = first_object();
        do {
            if (a3 != a0 && (a3->flags & d5) == d5 && a3->level == level) {
                uint16_t d = Distance(s16(uint16_t(x - a3->x)), s16(uint16_t(y - a3->y)));
                if (d < best) {
                    best = d;
                    a1 = slot_of(a3);
                }
            }
            a3 = next_object(a3);
            left = uint16_t(left - 1);
        } while (left);
    }
    if (a0 && (0xDA & d5) == d5 && level == uint8_t(st.cur_level)) {
        if (Distance(s16(uint16_t(x - player_x)), s16(uint16_t(y - player_y))) < best) a1 = kPlayerPos;
    }
    return a1;
}

// 0x167B0: the enemy in the aim cone (width d1, column d2) nearest to the
// view, nearer than the wall in the middle column (FF61A6); also lets a shot
// at the sky hit the flash (0x29BA).
TargetResult FindAimTarget(uint16_t d1, uint16_t d2) {
    TargetResult r{nullptr, 0};
    uint16_t n = st.object_count;
    if (n) {
        Enemy* a0 = first_object();
        r.d0 = view.depth[0x40];
        uint16_t mask = 0x40;
        if (s16(st.view_z) > 0)
            mask = 0x80;
        else if (s16(st.view_z) < 0)
            mask = 0x08;
        for (; n; n--, a0 = next_object(a0)) {
            if (!(a0->flags & mask) || a0->level != uint8_t(cur_level)) continue;
            int16_t ex = s16(uint16_t(a0->x - player_x)), ey = s16(uint16_t(a0->y - player_y));
            int16_t dx = s16(st.dir_x), dy = s16(st.dir_y);
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
    if (cur_level || !st.sky_flash) return r;
    uint8_t cls = st.cell_class[map_cell(st.player_cell)];
    if (cls != 0x28 && !(s16(st.view_z) >= -5 && cls == 0x27)) return r;
    int16_t d = int16_t(uint16_t((d2 << 1) + 0x9E - st.flash_col));
    if (d < 0) d = int16_t(-d);
    if (uint16_t(d) <= 3) {
        st.flash_time = 0x20;
    }
    return r;
}

// 0x166A0: line of sight from (d3, d4) to (d0, d1) also checked from 0x1C
// to each side; true when any line is blocked.
bool EnemySightBlocked(Enemy* a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4) {
    return EnemyLineBlocked(a0, d0, d1, d3, d4) || EnemyLineBlocked(a0, d0, d1, uint16_t(d3 + 0x1C), d4) ||
           EnemyLineBlocked(a0, d0, d1, uint16_t(d3 - 0x1C), d4) || EnemyLineBlocked(a0, d0, d1, d3, uint16_t(d4 + 0x1C)) ||
           EnemyLineBlocked(a0, d0, d1, d3, uint16_t(d4 - 0x1C));
}

// 0x1BA04: draws animation d0 of the enemy's sprite set (+0x42) at column d1,
// scale d5. The view the set shows depends on its mode word: 1 one view,
// 2 front/back, 4/6/8 directions from the angle between the enemy's heading
// (+0x2A) and the player. d2 >= 0 is the frame; d2 < 0 steps the walking
// frames by the distance moved (+0x3C).
void DrawEnemySprite(Enemy* a0, uint16_t d0, uint16_t d2, int16_t col, uint16_t scale) {
    const SpriteAnims& anims = sprite_anims(a0->sprites);
    if (d0 >= anims.size()) return;
    uint16_t mode = uint16_t(anims[d0].size());
    int16_t vx = s16(a0->vx), vy = s16(a0->vy);
    int16_t ex = s16(uint16_t(a0->x - player_x)), ey = s16(uint16_t(a0->y - player_y));
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
    const std::vector<SpritePart>& parts = anims[d0][view / 2 - 1];
    uint16_t frames = uint16_t(parts.size() - 1);
    uint16_t part = 0;
    if (s16(d2) >= 0)
        part = d2 <= frames ? d2 : 0;
    else if (frames) {
        uint16_t speed = Distance(vx, vy);
        if (speed >= 4) {
            a0->walk = uint16_t(a0->walk + speed);
            part = kAnimPart[frames - 1][(s16(a0->walk) >> 3) & 0x1F];
        }
    }
    st.sprite_scale = scale;
    int16_t d3 = s16(uint16_t(st.view_z - st.stair_height - a0->z));
    int16_t row = int16_t(uint16_t(((int32_t(s16(scale)) * d3) >> 6) + 0x28));
    DrawSpriteBlocks(parts[part], a0->sprites, col, row, scale);
}

void DieDropItem47(Enemy* a0) { drop_item(a0, 0x47, false, false); } // 0x14454
void DieDropItem47Keep(Enemy* a0) { drop_item(a0, 0x47, true, false); } // 0x144FE
void DieDropItem46Counted(Enemy* a0) { drop_item(a0, 0x46, false, true); } // 0x14A56
void DieDropItem46Keep(Enemy* a0) { drop_item(a0, 0x46, true, false); } // 0x14B08

namespace {

void enemy_die(Enemy* a0) { RunAct(a0->death, a0); }

uint16_t blink_sum(Enemy* a0) { return uint8_t(uint8_t(st.frame) + a0->id); }

uint16_t target_distance(Enemy* a0, const Enemy* a1) {
    return Distance(s16(uint16_t(a0->x - a1->x)), s16(uint16_t(a0->y - a1->y)));
}

// Keeps a low byte of a position at 0x40 / 0xC0 from the edge of a door cell
// (0x1615E-0x161A8) and plays the door sound when it moved.
void door_clamp(Enemy* a0, uint8_t& at, uint8_t lo, uint8_t hi) {
    uint8_t v = at;
    if (int8_t(v) >= 0) {
        if (v <= lo) return;
        at = lo;
    } else {
        if (v >= hi) return;
        at = hi;
    }
    EnemySound(a0, 0x36); // 0x1628A
}

// Door cells push towards their middle line: classes 6/0x83 act on y, 7/0x84 on x.
void door_cells(Enemy* a0, uint8_t& y_byte, uint8_t& x_byte, uint8_t lo, uint8_t hi) {
    uint8_t cls = st.cell_class[map_cell(enemy_map(a0) + cell_index(a0->x, a0->y))];
    if (cls == 6 || cls == 0x83)
        door_clamp(a0, y_byte, lo, hi);
    else if (cls == 7 || cls == 0x84)
        door_clamp(a0, x_byte, lo, hi);
}

} // namespace

// 0x16084: every 8 frames picks the target (0x1642C); without one, or with
// it 0xA00 away, the enemy dies (+0x16).
void TrackTarget(Enemy* a0) {
    if (!(blink_sum(a0) & 7)) {
        Enemy* a1 = FindTarget(a0);
        set_target(a0, a1);
        if (!a1) return enemy_die(a0);
    }
    if (target_distance(a0, target_of(a0)) >= 0xA00) enemy_die(a0);
}

// 0x160C4: every 8 frames picks the target (0x1649C).
void TrackVisibleTarget(Enemy* a0) {
    if (!(blink_sum(a0) & 7)) {
        Enemy* a1 = FindVisibleTarget(a0);
        set_target(a0, a1);
        if (!a1) enemy_die(a0);
    }
}

// 0x160E8: like 0x16084 every 4 frames with 0x600, then keeps the enemy out
// of the door frames.
void TrackNearTarget(Enemy* a0) {
    if (!(blink_sum(a0) & 3)) {
        Enemy* a1 = FindTarget(a0);
        set_target(a0, a1);
        if (!a1) return enemy_die(a0);
    }
    if (target_distance(a0, target_of(a0)) >= 0x600) return enemy_die(a0);
    door_cells(a0, low_byte(a0->y), low_byte(a0->x), 0x40, 0xC0);
}

// 0x161B2: like 0x160E8, but checks the door cells every 8 frames and then
// clamps the bytes at FF8023 / FF8021 (a6-relative in the original, where
// a0 was probably meant): level-0 map cells (13, 26) and (11, 26).
void TrackNearTargetBadClamp(Enemy* a0) {
    if (!(blink_sum(a0) & 3)) {
        Enemy* a1 = FindTarget(a0);
        set_target(a0, a1);
        if (!a1) return enemy_die(a0);
    }
    if (target_distance(a0, target_of(a0)) >= 0x600) return enemy_die(a0);
    if (!(blink_sum(a0) & 7)) door_cells(a0, st.maps[0][0x34D], st.maps[0][0x34B], 0x20, 0xE0);
}

// Halves the enemy's velocity (rounding towards zero) and moves it
// (0x186B8-0x186F2 and its copies).
void slow_down(Enemy* a0) {
    auto half = [](uint16_t v) { return uint16_t(s16(v) < 0 ? -(-s16(v) >> 1) : s16(v) >> 1); };
    uint16_t vx = half(a0->vx), vy = half(a0->vy);
    a0->vx = vx;
    a0->vy = vy;
    if (vx | vy) MoveEnemyNarrow(a0, vx, vy);
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
void set_knock(Enemy* a0, EnemyMove dir, int16_t strength) {
    a0->vx = uint16_t((int32_t(s16(dir.d0)) * strength) >> 8);
    a0->vy = uint16_t((int32_t(s16(dir.d1)) * strength) >> 8);
}

// 0x1B8D8: the enemy is stunned: handlers 0x1B97A / 0x1B952 / 0x1B9BC /
// 0x14454 and the stunned sprite set (0x1CB96A).
void StunEnemy(Enemy* a0) {
    a0->flags = a0->flags & 0xFF2F;
    a0->ai = Act::BodyAi;
    a0->delay = 0;
    a0->hit = Hit::BodyFlip;
    a0->death = Act::DieDropItem47;
    a0->draw = Draw::Stunned;
    a0->vx = 0;
    a0->vy = 0;
    a0->sprites = SpriteSet::Stunned;
    a0->item_b = 0x35;
    CountEnemiesLeft();
}

// 0x1B952
void StunnedDraw(Enemy* a0, uint16_t d5, uint16_t d1) {
    a0->sprites = SpriteSet::Stunned;
    uint16_t d2 = (blink_sum(a0) >> 2) & 1;
    if (a0->count & 1) d2 = uint16_t(d2 + 3);
    DrawEnemySprite(a0, 0, d2, s16(d1), d5);
}

// 0x1B97A
void BodyAi(Enemy* a0) {
    a0->delay = 0;
    slow_down(a0);
    TrackNearTarget(a0);
}

// 0x1B9BC (and its copies): pushed away by a hit, flips the frame.
void BodyHitFlip(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    a0->vx = 0;
    a0->vy = 0;
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    set_knock(a0, dir, int16_t(f >> 4));
    a0->count = uint8_t(a0->count + 1);
}

// 0x157CA: blood: up to 5 particles for damage d0 >= 0x2F8, flying with
// (d3, d4) plus noise.
void SpawnBlood(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    int16_t n = int16_t(((s16(uint16_t(d0 - 0x338))) >> 6) + 1);
    if (n < 0 || n > 4) return;
    for (; n >= 0; n--) {
        if (st.blood_count == 0x10) return;
        Enemy* e = AllocEnemy();
        if (!e) return;
        st.blood_count = uint16_t(st.blood_count + 1);
        e->x = a0->x;
        e->y = a0->y;
        e->ai = Act::BloodAi;
        e->draw = Draw::Blood;
        e->death = Act::RemoveBlood;
        e->vx = d3;
        e->vy = d4;
        int32_t r = int32_t(Random());
        r >>= 5;
        e->vx = uint16_t(e->vx + (r & 0x7F) - 0x40);
        r >>= 7;
        e->vy = uint16_t(e->vy + (r & 0x7F) - 0x40);
        r >>= 7;
        e->vz = uint16_t((r & 0xF) - 9);
        r >>= 4;
        e->z = uint16_t((r & 0xF) - 8);
        e->state = 0;
    }
}

// 0x159EE: removes a blood particle.
void RemoveBlood(Enemy* a0) {
    if (st.blood_count) st.blood_count = uint16_t(st.blood_count - 1);
    RemoveEnemy(a0);
}

// 0x158CC: blood particle: flies in 16 substeps until it hits a wall (it
// then sticks, state 1) or falls to the floor (state 2), then fades.
void BloodAi(Enemy* a0) {
    a0->delay = 0;
    if (a0->state) {
        if (!(blink_sum(a0) & 7) && s16(a0->z) > -0x10) a0->z = uint16_t(a0->z - 1);
        uint16_t sum = uint16_t(a0->count + uint8_t(uint16_t(s16(uint16_t(st.blood_count + 1)) >> 1)));
        a0->count = uint8_t(sum);
        if (sum > 0xFF) RemoveBlood(a0);
        return;
    }
    if (a0->vx | a0->vy) {
        int32_t x = int32_t(s16(a0->x)) << 4, y = int32_t(s16(a0->y)) << 4;
        int32_t vx = s16(a0->vx), vy = s16(a0->vy);
        for (int i = 0; i < 16; i++, x += vx, y += vy) {
            uint16_t nx = uint16_t((x + vx) >> 4), ny = uint16_t((y + vy) >> 4);
            uint8_t hit = EnemyCollisionShape(a0, nx, ny);
            if (!hit) continue;
            if (hit >= 6 || EnemyMapShotPasses(a0, nx, ny)) return RemoveBlood(a0);
            a0->x = uint16_t(x >> 4);
            a0->y = uint16_t(y >> 4);
            a0->vz = 0;
            a0->vx = 0;
            a0->vy = 0;
            a0->state = 1;
            a0->count = 0;
            return;
        }
        a0->x = uint16_t(x >> 4);
        a0->y = uint16_t(y >> 4);
        a0->vx = uint16_t(a0->vx - (s16(a0->vx) >> 2));
        a0->vy = uint16_t(a0->vy - (s16(a0->vy) >> 2));
    }
    if (a0->z == 0xFFE0) return;
    uint16_t dz = a0->vz;
    a0->z = uint16_t(a0->z + dz);
    if (s16(a0->z) <= -0x20) {
        a0->z = 0xFFE0;
        a0->vx = 0;
        a0->vy = 0;
        a0->state = 2;
        a0->count = 0;
    } else
        a0->vz = uint16_t(dz - 2);
}

namespace {

// Screen row of the enemy's height for scale d5 (0x15A02-0x15A14 and copies).
int16_t sprite_row(Enemy* a0, uint16_t d5) {
    st.sprite_scale = d5;
    int16_t d3 = s16(uint16_t(st.view_z - st.stair_height - a0->z));
    return int16_t(uint16_t(((int32_t(s16(d5)) * d3) >> 6) + 0x28));
}

// One sprite d5 / 4 high, half as wide, centred on (d1, row).
void draw_small(SpriteBlock a1, uint16_t d5, int16_t col, int16_t row) {
    int16_t h = int16_t(s16(d5) >> 2), w = int16_t(h >> 1);
    DrawSprite(h, w, int16_t(col - (w >> 1)), int16_t(row - w), a1);
}

} // namespace

// 0x159FE: blood particle: a drop, a splash on a wall or a pool on the floor.
void BloodDraw(Enemy* a0, uint16_t d5, uint16_t d1) {
    int16_t row = sprite_row(a0, d5);
    uint16_t d7 = a0->count;
    st.sprite_mirror = a0->id & 1;
    int16_t col = s16(d1);
    switch (a0->state) {
    case 0: return draw_small(object_block(61), d5, col, row);
    case 1: {
        int16_t h = int16_t(int16_t(uint16_t((uint32_t(d5) * uint16_t(d7 + 0x100)) >> 8)) >> 3);
        int16_t w = int16_t(int16_t(uint16_t((uint32_t(d5) * uint16_t(0x100 - d7)) >> 8)) >> 4);
        return DrawSprite(h, w, int16_t(col - (w >> 1)), row, object_block(62));
    }
    default: {
        int16_t w = int16_t(int16_t(uint16_t((uint32_t(d5) * uint16_t(d7 + 0x80)) >> 7)) >> 3);
        int16_t h = int16_t(int16_t(uint16_t((uint32_t(d5) * uint16_t(0x100 - d7)) >> 8)) >> 4);
        return DrawSprite(h, w, int16_t(col - (w >> 1)), int16_t(row - (h >> 1)), object_block(62));
    }
    }
}

// 0x15AC4: bullet spark: rises for 3 frames.
void SparkAi(Enemy* a0) {
    a0->delay = 0;
    a0->state = uint8_t(a0->state - 1);
    if (int8_t(a0->state) < 0) return RemoveEnemy(a0);
    a0->z = uint16_t(a0->z + 2);
}

// 0x15AD6: bullet spark frames (0x1167BE).
void SparkDraw(Enemy* a0, uint16_t d5, uint16_t d1) {
    int16_t row = sprite_row(a0, d5);
    SpriteBlock a1 = object_block(63);
    uint8_t f = uint8_t(2 - a0->state);
    if (f) a1.index = uint8_t(a1.index + (int16_t(uint16_t(f << 9)) >> 9));
    st.sprite_mirror = 0;
    draw_small(a1, d5, s16(d1), row);
}

// 0x16040: a small blinking sprite (0x10EBBE).
void EnemyShotDraw(Enemy* a0, uint16_t d5, uint16_t d1) {
    int16_t row = sprite_row(a0, d5);
    st.sprite_mirror = blink_sum(a0) & 1;
    draw_small(object_block(1), d5, s16(d1), row);
}

// ---- Objects the weapons create (0x13AD2-0x1444A).

namespace {

void explosion_sound(Enemy* a0, uint16_t id, uint16_t time) {
    st.speech_on = 0;
    st.dac_hold = 0;
    EnemySound(a0, id);
    st.dac_hold = time;
}

// 0x13DE8: turns the object into an explosion (0x14196 / 0x141E2).
void become_explosion(Enemy* a0) {
    a0->draw = Draw::Explosion;
    a0->ai = Act::ExplosionAi;
    a0->hit = Hit::None;
    a0->flags = a0->flags & 0xFF37;
    a0->delay = 0;
    a0->step = 0;
}

// 0x13DCC
void explode(Enemy* a0) {
    explosion_sound(a0, 0x8A, 0x1E);
    become_explosion(a0);
}

// 0x13E42 / 0x13EC0: explodes on the player at once (the flags are or-ed
// with 0xFF37 where the other paths and them).
void explode_on_player(Enemy* a0, uint16_t sound, bool shielded) {
    explosion_sound(a0, sound, 0x1E);
    uint16_t px = st.push_x, py = st.push_y;
    uint16_t dx = uint16_t(a0->x - player_x), dy = uint16_t(a0->y - player_y);
    uint16_t d = Distance(s16(dx), s16(dy));
    if (shielded)
        HitPlayerIgnoreArmour(d, dx, dy);
    else
        HitPlayer(d, dx, dy);
    st.push_y = py;
    st.push_x = px;
    a0->draw = Draw::Explosion;
    a0->ai = Act::ExplosionAi;
    a0->flags = a0->flags | 0xFF37;
    a0->hit = Hit::None;
    a0->delay = 0;
    a0->step = 2;
}

// Halves a velocity word, rounding towards zero.
uint16_t half(uint16_t v) { return uint16_t(s16(v) < 0 ? -(-s16(v) >> 1) : s16(v) >> 1); }

// neg.w then asr.w on memory: reverses and halves a velocity.
void bounce(uint16_t& v) { v = uint16_t(s16(uint16_t(-v)) >> 1); }

// Projectile sprites centred on column d1 at the object's height; the
// width is height >> wshift, the row moves up by height >> rshift.
void draw_shot(Enemy* a0, uint16_t d5, uint16_t d1, SpriteBlock a1, int16_t h, int wshift, int rshift) {
    int16_t row = sprite_row(a0, d5);
    int16_t w = int16_t(h >> wshift);
    DrawSprite(h, w, int16_t(s16(d1) - (w >> 1)), int16_t(row - (h >> rshift)), a1);
}

// 0x9F8E table: enemy type of the start cell classes, 1-10 (templates by
// type at 0xA09A).
// 0xA08E: spawns the enemy of start cell a1 (on the level in cur_level).
void SpawnEnemy(uint16_t d3, Cell a1) {
    const EnemyTemplate& t = kEnemyTemplates[d3 - 1];
    st.spawn_template = uint8_t(d3 - 1);
    a1 = on_map(a1);
    put_cell(a1, 0);
    Enemy* e = AllocEnemy();
    if (!e) return;
    e->delay = 0;
    e->ai = t.ai;
    e->draw = t.draw;
    e->hit = t.hit;
    e->death = t.death;
    e->flags = uint16_t(e->flags | t.flags);
    e->health = t.health;
    e->level = uint8_t(st.cur_level);
    uint16_t idx = uint16_t(a1 - enemy_map(e)) & 0x3FF;
    e->x = uint16_t(((idx & 0x1F) << 8) + 0x80);
    e->y = uint16_t(((idx << 3) & 0xFF00) | 0x80);
    e->z = t.z;
    e->state = 0;
    e->count = 0x14;
    e->timer = 0;
    e->dest_x = player_x;
    e->dest_y = player_y;
    e->sprites = t.sprites;
    if (cur_level && e->sprites == SpriteSet::Type1 && ((e->x ^ e->y) & 0x100)) e->sprites = SpriteSet::Type1Alt;
    e->item_a = t.item_a;
    e->item_b = t.item_b;
    set_target(e, player_struct);
    PlaySoundLocal(t.sound);
}

// 0x9E1E: spawns the enemies of the start cells in the 7x7 cells around
// cell a0, skipping rows and columns outside the map as seen from the
// player's position.
void SpawnEnemiesAround(Cell a0) {
    int16_t row = int16_t((s16(player_y) >> 8) - 4);
    Cell a1 = a0 - 0x63;
    for (int i = 0; i < 7; i++) {
        row++;
        if (row >= 0) {
            if (row >= 0x20) return;
            int16_t col = int16_t((s16(player_x) >> 8) - 4);
            for (int j = 0; j < 7; j++, a1++) {
                col++;
                if (col < 0 || col >= 0x20) continue;
                uint8_t n = kSpawnType[st.cell_class[map_cell(a1)]];
                if (n) SpawnEnemy(n, a1);
            }
            a1 -= 7;
        }
        a1 += 0x20;
    }
}

// 0x16294: an explosion at (d0, d1) on the level of a0: wakes the enemy
// start cells around it, then hits every enemy and the player within 0x400
// in sight (FF26F0 set within 0x80).
void BlastAt(Enemy* a0, uint16_t d0, uint16_t d1) {
    uint8_t level = a0->level;
    uint16_t saved = cur_level;
    cur_level = level;
    SpawnEnemiesAround(enemy_map(a0) + cell_index(d0, d1));
    cur_level = saved;
    uint16_t n = st.object_count;
    for (Enemy* e = n ? first_object() : nullptr; n; n--) {
        Enemy* next = next_object(e);
        if (e->level == level && !EnemyLineBlocked(e, d0, d1, e->x, e->y)) {
            uint16_t dx = uint16_t(d0 - e->x), dy = uint16_t(d1 - e->y);
            uint16_t d = Distance(s16(dx), s16(dy));
            if (d < 0x400) {
                if (d <= 0x80) st.blast_near = 1;
                HitObject(e, d, dx, dy);
                st.blast_near = 0;
            }
        }
        e = next;
    }
    if (level != uint8_t(st.cur_level)) return;
    if (LineBlocked(a0, d0, d1, player_x, player_y)) return;
    uint16_t dx = uint16_t(d0 - player_x), dy = uint16_t(d1 - player_y);
    uint16_t d = Distance(s16(dx), s16(dy));
    if (st.character == 1) d = uint16_t(d << 1);
    if (d < 0x400) HitPlayer(d, dx, dy);
}

} // namespace

// 0xA6BC: a blast at (d0, d1) breaks the breakable cell there (classes
// 0x79, 0x7B, 0x7D, 0x7F) into the first cell of class + 1.
void BlastBreakCell(uint16_t d0, uint16_t d1) {
    Cell a0 = cur_map + cell_index(d0, d1);
    uint8_t cls = st.cell_class[map_cell(a0)];
    if (cls != 0x79 && cls != 0x7B && cls != 0x7D && cls != 0x7F) return;
    put_cell(a0, st.class_cell[cls + 1]);
    st.speech_on = 0;
    st.dac_hold = 0;
    PlaySoundLocal(0x3B);
    st.dac_hold = 0x14;
}

// 0x13AD2: a thrown bomb explodes when hit nearer than 0x200.
void BombHit(Enemy* a0, uint16_t d0) {
    if (d0 < 0x200) explode(a0);
}

// 0x13ADC: thrown bomb: bounces off walls and the floor, then explodes,
// breaking the cells around it.
void BombAi(Enemy* a0) {
    a0->delay = 0;
    if (a0->vx | a0->vy) {
        uint16_t x = a0->x, y = a0->y;
        uint16_t nx = uint16_t(x + a0->vx), ny = uint16_t(y + a0->vy);
        if (EnemyCollisionShape(a0, nx, ny)) {
            if (EnemyMapShotPasses(a0, nx, ny)) return RemoveEnemy(a0);
            EnemySound(a0, 0x6C);
            bool xwall = EnemyCollisionShape(a0, uint16_t(a0->x + a0->vx), a0->y);
            bool ywall = EnemyCollisionShape(a0, a0->x, uint16_t(a0->y + a0->vy));
            if (xwall == ywall) {
                bounce(a0->vy);
                bounce(a0->vx);
            } else if (xwall)
                bounce(a0->vx);
            else
                bounce(a0->vy);
        }
        a0->x = uint16_t(a0->x + a0->vx);
        a0->y = uint16_t(a0->y + a0->vy);
    }
    uint16_t vz = a0->vz;
    a0->z = uint16_t(a0->z + vz);
    if (s16(a0->z) > -0x18)
        a0->vz = uint16_t(vz - 3);
    else {
        a0->z = 0xFFE8;
        if (vz) {
            vz = uint16_t(s16(uint16_t(-(vz - 3))) >> 2);
            if (vz < 3) vz = 0;
            a0->vz = vz;
            EnemySound(a0, 0x6C);
        }
        if (a0->z == 0xFFE8) {
            a0->vx = half(a0->vx);
            a0->vy = half(a0->vy);
        }
    }
    a0->state = uint8_t(a0->state - 1);
    if (a0->state) return;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            BlastBreakCell(uint16_t(a0->x - 0x100 + i * 0x100), uint16_t(a0->y - 0x100 + j * 0x100));
    explode(a0);
}

void ExplodeOnPlayer(Enemy* a0) { explode_on_player(a0, 0x8A, false); } // 0x13E42
void ExplodeOnPlayerShielded(Enemy* a0) { explode_on_player(a0, 0x6D, true); } // 0x13EC0

// 0x13F3E: thrown bomb sprite.
void BombDraw(Enemy* a0, uint16_t d5, uint16_t d1) {
    st.sprite_mirror = 0;
    draw_shot(a0, d5, d1, object_block(16), int16_t(s16(d5) >> 2), 1, 1);
}

// 0x13F7C: mine: every 20 frames picks the nearest enemy with flag 0x10 (or
// the player), and explodes when it comes within 0x100.
void MineAi(Enemy* a0) {
    a0->delay = 0;
    a0->step = uint8_t(a0->step - 1);
    if (int8_t(a0->step) < 0) {
        a0->step = 0x14;
        a0->target = NearestEnemy(a0, 0x10);
        if (a0->target == kNoTarget) return RemoveEnemy(a0);
    }
    if (a0->target == kNoTarget) return;
    Enemy* a1 = target_of(a0);
    uint16_t tx, ty;
    if (a0->target == kPlayerPos || a1 == player_struct) {
        if (st.character == 1) return;
        tx = a0->target == kPlayerTarget ? a1->x : uint16_t(player_x);
        ty = a0->target == kPlayerTarget ? a1->y : uint16_t(player_y);
    } else {
        if (!(a0->flags & 1)) return RemoveEnemy(a0);
        tx = a1->x;
        ty = a1->y;
    }
    if (Distance(s16(uint16_t(a0->x - tx)), s16(uint16_t(a0->y - ty))) < 0x100) explode(a0);
}

// 0x1402E: mine sprite, blinking.
void MineDraw(Enemy* a0, uint16_t d5, uint16_t d1) {
    st.sprite_mirror = 0;
    SpriteBlock a1 = (uint8_t(st.frame) & 2) ? object_block(9) : object_block(8);
    draw_shot(a0, d5, d1, a1, int16_t(s16(d5) >> 2), 0, 0);
}

// 0x14076: grenade: flies until it hits a wall or its time runs out, then
// explodes (an open sky cell takes it as a lightning bolt instead).
void GrenadeAi(Enemy* a0) {
    a0->delay = 0;
    a0->state = uint8_t(a0->state - 1);
    uint16_t nx = uint16_t(a0->x + a0->vx), ny = uint16_t(a0->y + a0->vy);
    if (a0->state && !EnemyCollisionShape(a0, nx, ny)) {
        a0->x = nx;
        a0->y = ny;
        return;
    }
    if (nx >= 0x1F00) {
        uint8_t cls = st.cell_class[map_cell(enemy_map(a0) + cell_index(a0->x, a0->y))];
        if (cls == 0x28 || (cls == 0x27 && s16(a0->z) > 0)) {
            StartBolt(0x120);
            return RemoveEnemy(a0);
        }
    }
    if (EnemyMapShotPasses(a0, nx, ny)) return RemoveEnemy(a0);
    BlastBreakCell(nx, ny);
    a0->vx = 0;
    a0->vy = 0;
    explosion_sound(a0, 0x18, 0x32);
    become_explosion(a0);
}

// 0x14150: grenade and fire ball sprite.
void GrenadeDraw(Enemy* a0, uint16_t d5, uint16_t d1) {
    st.sprite_mirror = st.frame & 1;
    draw_shot(a0, d5, d1, object_block(1), int16_t(s16(d5) >> 1), 1, 1);
}

// 0x14196: explosion: hits what is around at step 2, ends at step 6.
void ExplosionAi(Enemy* a0) {
    a0->delay = 0;
    a0->step = uint8_t(a0->step + 1);
    if (a0->step == 6) return UnlinkEnemy(a0);
    if (a0->step == 2) BlastAt(a0, a0->x, a0->y);
}

// 0x141E2: explosion sprite, sized by step (table 0x14234).
void ExplosionDraw(Enemy* a0, uint16_t d5, uint16_t d1) {
    st.sprite_mirror = st.frame & 1;
    uint8_t shift = kShotShift[a0->step];
    draw_shot(a0, d5, d1, object_block(1), int16_t(s16(uint16_t(d5 + d5)) >> shift), 1, 1);
}

// 0x1423A: fire on the floor, burns for 16 frames.
void FireAi(Enemy* a0) {
    a0->delay = 0;
    a0->step = uint8_t(a0->step + 1);
    if (a0->step == 0x10) UnlinkEnemy(a0);
}

// 0x14272: the fire shrinks as it burns down.
void FireDraw(Enemy* a0, uint16_t d5, uint16_t d1) {
    st.sprite_scale = d5;
    int16_t d3 = s16(uint16_t(st.view_z - st.stair_height + 0x20));
    int16_t row = int16_t(uint16_t(((int32_t(s16(d5)) * d3) >> 6) + 0x28));
    st.sprite_mirror = st.frame & 1;
    int16_t h = int16_t(uint16_t((int32_t(s16(d5)) * int16_t(0x10 - a0->step)) >> 4));
    int16_t w = int16_t(h >> 1);
    DrawSprite(h, w, int16_t(s16(d1) - (w >> 1)), int16_t(row - h), object_block(0));
}

// 0x142C6: fire ball: arcs until it lands, hits a wall or its time runs out,
// then sets fire to what is within 0x100 (the player within 0x30).
void FireBallAi(Enemy* a0) {
    a0->delay = 0;
    a0->state = uint8_t(a0->state - 1);
    if (a0->state) {
        a0->z = uint16_t(a0->z - a0->vz);
        a0->vz = uint16_t(a0->vz + 1);
        if (s16(a0->z) >= -0x1C) {
            uint16_t nx = uint16_t(a0->x + a0->vx), ny = uint16_t(a0->y + a0->vy);
            if (!EnemyCollisionShape(a0, nx, ny)) {
                a0->x = nx;
                a0->y = ny;
                return;
            }
        }
    }
    st.blast_near = 1;
    st.burning = 1;
    uint16_t d0 = a0->x, d1 = a0->y;
    uint8_t level = a0->level;
    uint16_t n = st.object_count;
    if (n) {
        Enemy* e = first_object();
        for (; n; n--) {
            Enemy* next = next_object(e);
            if ((e->flags & 0xC8) && e->level == level && !EnemyLineBlocked(e, d0, d1, e->x, e->y)) {
                uint16_t dx = uint16_t(d0 - e->x), dy = uint16_t(d1 - e->y);
                uint16_t d = Distance(s16(dx), s16(dy));
                if (d < 0x100) {
                    uint16_t blood = st.blood_count;
                    st.blood_count = 0x10;
                    HitObject(e, d, dx, dy);
                    st.blood_count = blood;
                    e->vx = 0;
                    e->vy = 0;
                }
            }
            e = next;
        }
    }
    if (cur_level == level && !LineBlocked(a0, d0, d1, player_x, player_y)) {
        uint16_t dx = uint16_t(d0 - player_x), dy = uint16_t(d1 - player_y);
        uint16_t d = Distance(s16(dx), s16(dy));
        if (d < 0x30) HitPlayerIgnoreArmour(d, dx, dy);
    }
    st.blast_near = 0;
    st.burning = 0;
    // The original's sound 0x6D here never plays: it takes the level of the
    // object the loop ends on, address 0 (ROM byte 0x10, no level).
    a0->delay = 0;
    a0->step = 0;
    a0->ai = Act::FireAi;
    a0->draw = Draw::Fire;
    a0->z = 0xFFE0;
    a0->flags = a0->flags & 0xFF37;
}

// 0x15F38: makes a0 a 0x15F6E shot at (d0, d1) with velocity (d3, d4),
// height d5 and vertical speed d6.
void InitEnemyShot(Enemy* a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4, uint16_t d5, uint16_t d6) {
    a0->x = d0;
    a0->y = d1;
    a0->vx = d3;
    a0->vy = d4;
    a0->z = d5;
    a0->vz = d6;
    a0->delay = 0;
    a0->ai = Act::EnemyShotAi;
    a0->draw = Draw::EnemyShot;
}

// 0x15F6E: enemy shot: flies until a wall, hits the target within 0x40
// unless it passes over (the target jumping) or under (crouching) it.
void EnemyShotAi(Enemy* a0) {
    a0->delay = 0;
    int16_t h = s16(uint16_t(a0->z + a0->vz));
    if (h < -0x1C) h = -0x1C;
    else if (h > 0x1C) h = 0x1C;
    a0->z = uint16_t(h);
    uint16_t nx = uint16_t(a0->x + a0->vx), ny = uint16_t(a0->y + a0->vy);
    if (EnemyCollisionShape(a0, nx, ny)) return RemoveEnemy(a0);
    a0->x = nx;
    a0->y = ny;
    Enemy* a3 = FindTarget(a0);
    if (!a3) return RemoveEnemy(a0);
    if (Distance(s16(uint16_t(a3->x - nx)), s16(uint16_t(a3->y - ny))) > 0x40) return;
    int16_t jump = s16(st.view_z);
    if (h < -8) {
        if (jump > 0) return;
    } else if (h > 8 && jump < 0)
        return;
    HitObject(a3, 0x1F4, uint16_t(nx - a3->x), uint16_t(ny - a3->y));
    st.speech_on = 0;
    st.dac_hold = 0;
    EnemySound(a0, 0x1B);
    st.dac_hold = 0xF;
    RemoveEnemy(a0);
}

// ---- The boss start cell (class 0x26, spawned by 0xA796): AI 0x15B1E, draw
// 0x15D84, hit 0x15E4E, death 0x15ED2. Once the target is near and in sight
// it counts down (state 1), then wakes the enemy start cells and opens the
// doors in the 11x11 cells around it (state 2).

namespace {

// The cells of the 11x11 square around cell a0 (the cell of a2) that lie on
// the map: fn(cell, column, row).
template <class F> void around_11(Cell a0, const Enemy* a2, F fn) {
    int16_t row = int16_t((s16(a2->y) >> 8) - 6);
    Cell a1 = a0 - 0xA5;
    for (int i = 0; i < 11; i++) {
        row++;
        if (row >= 0) {
            if (row >= 0x20) return;
            int16_t col = int16_t((s16(a2->x) >> 8) - 6);
            for (int j = 0; j < 11; j++, a1++) {
                col++;
                if (col >= 0 && col < 0x20) fn(a1, col, row);
            }
            a1 -= 0xB;
        }
        a1 += 0x20;
    }
}

Cell own_cell(Enemy* a0) { return enemy_map(a0) + cell_index(a0->x, a0->y); }

// 0x15D18: spawns the enemies of the start cells around (on a2's level).
void BossCellSpawnEnemies(Cell a0, const Enemy* a2) {
    around_11(a0, a2, [&](Cell a1, int16_t, int16_t) {
        uint8_t n = kSpawnType[st.cell_class[map_cell(a1)]];
        if (!n) return;
        uint16_t saved = cur_level;
        set_low_byte(st.cur_level, a2->level);
        SpawnEnemy(n, a1);
        cur_level = saved;
    });
}

// 0x15C74: opens the doors around.
void BossCellOpenDoors(Cell a0, const Enemy* a2) {
    around_11(a0, a2, [&](Cell a1, int16_t col, int16_t row) {
        uint8_t cls = st.cell_class[map_cell(a1)];
        if (cls == 6 || cls == 0x83)
            BossOpenDoorA(a1, uint16_t(col << 8), uint16_t(row << 8));
        else if (cls == 7 || cls == 0x84)
            BossOpenDoorB(a1, uint16_t(col << 8), uint16_t(row << 8));
    });
    PlaySoundLocal(0x67);
}

// The target (0x1642C) and its distance; false when there is none within
// 0xC00 (the cell then goes back to the map).
bool boss_target(Enemy* a0, Enemy*& a1, uint16_t& d) {
    a1 = FindTarget(a0);
    if (!a1) return false;
    d = Distance(s16(uint16_t(a0->x - a1->x)), s16(uint16_t(a0->y - a1->y)));
    return d <= 0xC00;
}

} // namespace

// 0x15B1E
void BossCellAi(Enemy* a0) {
    a0->delay = 0;
    Enemy* a1;
    uint16_t d;
    uint8_t state = a0->state;
    if (state == 0) {
        if (!boss_target(a0, a1, d)) return BossCellDeath(a0);
        bool hard = st.character == 4;
        if (a1 == player_struct && hard && d > 0x180) return;
        if (d > 0x300) return;
        if (EnemySightBlocked(a0, a1->x, a1->y, a0->x, a0->y)) return;
        a0->count = hard ? 0x31 : 0x21;
        a0->state = 1;
        return;
    }
    if (state != 1) {
        if (!boss_target(a0, a1, d)) BossCellDeath(a0);
        return;
    }
    a0->count = uint8_t(a0->count - 1);
    if (a0->count) {
        if (!(a0->count & 7)) EnemySound(a0, 0x71);
        return;
    }
    a0->state = 2;
    BossCellSpawnEnemies(own_cell(a0), a0);
    BossCellOpenDoors(own_cell(a0), a0);
    EnemySound(a0, 0x70);
}

// 0x15D84: the cell sprite, blinking.
void BossCellDraw(Enemy*, uint16_t d5, uint16_t d1) {
    st.sprite_scale = d5;
    int16_t d3 = s16(uint16_t(st.view_z - st.stair_height - 0x20));
    int16_t row = int16_t(uint16_t(((int32_t(s16(d5)) * d3) >> 6) + 0x28));
    int16_t h = int16_t(s16(d5) >> 1);
    h = int16_t(h - (h >> 2));
    int16_t w = int16_t(h >> 1);
    st.sprite_mirror = 0;
    DrawSprite(h, w, int16_t(s16(d1) - (w >> 1)), row, (uint8_t(st.frame) & 2) ? object_block(20) : object_block(19));
}

// 0x15E4E: a hit destroys it: it flies up as an explosion (0x15DD0).
void BossCellHit(Enemy* a0) {
    a0->ai = Act::BossCellBlastAi;
    a0->draw = Draw::BossCellBlast;
    a0->flags = a0->flags & 0xFF37;
    a0->death = Act::None;
    a0->hit = Hit::None;
    a0->count = 0;
    a0->z = 0x10;
    a0->vz = 0;
    explosion_sound(a0, 0x8A, 0x1E);
}

// 0x15DD0
void BossCellBlastAi(Enemy* a0) {
    uint16_t v = a0->vz;
    a0->z = uint16_t(a0->z - v);
    a0->vz = uint16_t(v + 1);
    if (s16(a0->z) < -0x20) a0->z = 0xFFE0;
    a0->count = uint8_t(a0->count + 1);
    if (a0->count == 0xA) RemoveEnemy(a0);
}

// 0x15DFC: the explosion, sized by step (table 0x15E48).
void BossCellBlastDraw(Enemy* a0, uint16_t d5, uint16_t d1) {
    int16_t row = sprite_row(a0, d5);
    int16_t h = int16_t(s16(d5) >> kBlastShift[a0->count >> 1]);
    int16_t w = int16_t(h >> 1);
    st.sprite_mirror = st.frame & 1;
    DrawSprite(h, w, int16_t(s16(d1) - (w >> 1)), int16_t(row - w), object_block(1));
}

// 0x15ED2: puts the cell (first of class 0x26) back on the map and removes the object.
void BossCellDeath(Enemy* a0) {
    put_cell(own_cell(a0), st.class_cell[0x26]);
    UnlinkEnemy(a0);
}

// 0x13182: timed item charges, used every 128 frames.
void UpdateItemCharges() {
    uint16_t d0 = st.frame & 0x7F;
    if (!d0 && int16_t(uint16_t(st.nv_slot)) >= 0) d0 = NightVisionCharge();
    if (int16_t(uint16_t(st.nv_slot)) < 0 && int16_t(uint16_t(st.light_slot)) >= 0) {
        int16_t d1 = int16_t(0x38 + (int16_t(uint16_t(st.bob_turn << 2)) >> 1));
        st.lit_start = d1;
        st.lit_end = int16_t(d1 + 0x10);
        if (!((d0 + 0x10) & 0x7F)) LightCharge();
    }
    if (!((st.frame + 0x30) & 0x7F) && int16_t(uint16_t(st.scanner_slot)) >= 0) ScannerCharge();
}

// 0x131E8: copies the player into the player's enemy struct (FF119C), then
// removes enemies that left the map and runs the AI handler (+0x12) of the
// others when their delay (+0x1F) runs out.
void RunEnemies() {
    player_x = player_x & 0x1FFF;
    player_y = player_y & 0x1FFF;
    Enemy* p = player_struct;
    p->z = st.view_z;
    p->x = player_x;
    p->y = player_y;
    p->vx = st.dir_x;
    p->vy = st.dir_y;
    p->level = uint8_t(st.cur_level);
    p->hit = Hit::Player;
    uint16_t n = st.object_count;
    if (!n) return;
    for (Enemy* a0 = first_object(); n; n--) {
        Enemy* next = next_object(a0);
        uint16_t x = a0->x, y = a0->y;
        uint8_t level = a0->level;
        bool off_map = x >= 0x2000 || y >= 0x2000 || level > 0xF;
        if (off_map && !(a0->flags & 0x20))
            RemoveEnemy(a0);
        else {
            a0->delay = uint8_t(a0->delay - 1);
            if (int8_t(a0->delay) < 0) RunAct(a0->ai, a0);
        }
        a0 = next;
        if (!a0) return;
    }
}

// 0x149DE: drops the item (first cell of class 0x2C) on the first empty cell around the player.
void DropItemNearPlayer() {
    Cell a1 = cur_map + cell_index(player_x, player_y);
    static const int16_t around[] = {0, -1, 1, -0x20, 0x20, 0x1F, 0x21, -0x1F, -0x21};
    for (int16_t d : around) {
        if (map_cell(a1 + d)) continue;
        put_cell(a1 + d, st.class_cell[0x2C]);
        return;
    }
}

// 0x168AC: runs the death handler of every enemy without flag 0x20, then
// resets the doors (0xB48A).
void KillAllEnemies() {
    uint16_t n = st.object_count;
    for (Enemy* a0 = first_object(); n; n--) {
        Enemy* next = next_object(a0);
        if (!(a0->flags & 0x20)) enemy_die(a0);
        a0 = next;
    }
    ResetDoors();
}

// 0x133A4: empties the enemy slots.
void ClearEnemies() {
    for (Enemy& e : st.objects) e = {};
    st.object_count = 0;
    st.object_list = 0;
    st.blood_count = 0;
}

// 0x13554: removes an enemy (in a link game, the other console as well).
void RemoveEnemy(Enemy* a0) {
    UnlinkEnemy(a0);
}

// The handlers of an object: AI (+0x12) and death (+0x16), drawing (+0xE)
// at scale d5 and column d1, hits (+0xA) with the distance d0 and the
// direction d3/d4 to the shooter. Several original addresses ran the same code.
void RunAct(Act act, Enemy* a0) {
    switch (act) {
    case Act::None: break;
    case Act::Unlink: UnlinkEnemy(a0); break;
    case Act::BombAi: BombAi(a0); break;
    case Act::ExplodeOnPlayer: ExplodeOnPlayer(a0); break;
    case Act::ExplodeOnPlayerShielded: ExplodeOnPlayerShielded(a0); break;
    case Act::MineAi: MineAi(a0); break;
    case Act::GrenadeAi: GrenadeAi(a0); break;
    case Act::ExplosionAi: ExplosionAi(a0); break;
    case Act::FireAi: FireAi(a0); break;
    case Act::FireBallAi: FireBallAi(a0); break;
    case Act::DieDropItem47: DieDropItem47(a0); break;
    case Act::DieDropItem47Keep: DieDropItem47Keep(a0); break;
    case Act::DieDropItem46Counted: DieDropItem46Counted(a0); break;
    case Act::DieDropItem46Keep: DieDropItem46Keep(a0); break;
    case Act::BloodAi: BloodAi(a0); break;
    case Act::RemoveBlood: RemoveBlood(a0); break;
    case Act::SparkAi: SparkAi(a0); break;
    case Act::Type1Ai: Type1Ai(a0); break;
    case Act::Type1BodyAi: Type1BodyAi(a0); break;
    case Act::Type2Ai: Type2Ai(a0); break;
    case Act::Type9Ai: Type9Ai(a0); break;
    case Act::Type5Ai: Type5Ai(a0); break;
    case Act::Type6Ai: Type6Ai(a0); break;
    case Act::BodyAiWide: BodyAiWide(a0); break;
    case Act::Type7Ai: Type7Ai(a0); break;
    case Act::Type8Ai: Type8Ai(a0); break;
    case Act::Type4Ai: Type4Ai(a0); break;
    case Act::Type4BodyAi: Type4BodyAi(a0); break;
    case Act::Type7BodyAi: Type7BodyAi(a0); break;
    case Act::Type0Ai: Type0Ai(a0); break;
    case Act::Type3Start: Type3Start(a0); break;
    case Act::Type3Ai: Type3Ai(a0); break;
    case Act::EnemyShotAi: EnemyShotAi(a0); break;
    case Act::BossCellAi: BossCellAi(a0); break;
    case Act::BossCellBlastAi: BossCellBlastAi(a0); break;
    case Act::BossCellDeath: BossCellDeath(a0); break;
    case Act::BodyAiBadClamp: BodyAiBadClamp(a0); break;
    case Act::BodyAi: BodyAi(a0); break;
    }
}

void DrawObject(Enemy* a0, uint16_t d5, uint16_t d1) {
    switch (a0->draw) {
    case Draw::None: break;
    case Draw::Bomb: BombDraw(a0, d5, d1); break;
    case Draw::Mine: MineDraw(a0, d5, d1); break;
    case Draw::Grenade: GrenadeDraw(a0, d5, d1); break;
    case Draw::Explosion: ExplosionDraw(a0, d5, d1); break;
    case Draw::Fire: FireDraw(a0, d5, d1); break;
    case Draw::Blood: BloodDraw(a0, d5, d1); break;
    case Draw::Spark: SparkDraw(a0, d5, d1); break;
    case Draw::EnemyShot: EnemyShotDraw(a0, d5, d1); break;
    case Draw::Type1: Type1Draw(a0, d5, d1); break;
    case Draw::Type1Body: Type1BodyDraw(a0, d5, d1); break;
    case Draw::Type2: Type2Draw(a0, d5, d1); break;
    case Draw::BodyFrame6: body_frame(a0, 6, d5, d1); break;
    case Draw::Type9: Type9Draw(a0, d5, d1); break;
    case Draw::BodyWalk4: body_walk(a0, 4, d5, d1); break;
    case Draw::Type5: Type5Draw(a0, d5, d1); break;
    case Draw::BodyWalk6: body_walk(a0, 6, d5, d1); break;
    case Draw::Type6: Type6Draw(a0, d5, d1); break;
    case Draw::Type7: Type7Draw(a0, d5, d1); break;
    case Draw::BodyFrame4: body_frame(a0, 4, d5, d1); break;
    case Draw::Type8: Type8Draw(a0, d5, d1); break;
    case Draw::BodyWalk9: body_walk(a0, 9, d5, d1); break;
    case Draw::Type4: Type4Draw(a0, d5, d1); break;
    case Draw::BodyFrame7: body_frame(a0, 7, d5, d1); break;
    case Draw::Type0: Type0Draw(a0, d5, d1); break;
    case Draw::Type3: Type3Draw(a0, d5, d1); break;
    case Draw::Type3Body: Type3BodyDraw(a0, d5, d1); break;
    case Draw::BossCell: BossCellDraw(a0, d5, d1); break;
    case Draw::BossCellBlast: BossCellBlastDraw(a0, d5, d1); break;
    case Draw::BodyFrame11: body_frame(a0, 0xB, d5, d1); break;
    case Draw::Stunned: StunnedDraw(a0, d5, d1); break;
    }
}

void HitObject(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    switch (a0->hit) {
    case Hit::None: break;
    case Hit::Player: HitPlayer(d0, d3, d4); break;
    case Hit::Bomb: BombHit(a0, d0); break;
    case Hit::Type6: Type6Hit(a0, d0, d3, d4); break;
    case Hit::Type7: Type7Hit(a0, d0, d3, d4); break;
    case Hit::Type8: Type8Hit(a0, d0, d3, d4); break;
    case Hit::Type4: Type4Hit(a0, d0, d3, d4); break;
    case Hit::Type0: Type0Hit(a0, d0, d3, d4); break;
    case Hit::Type3Body: Type3BodyHit(a0, d0, d3, d4); break;
    case Hit::BossCell: BossCellHit(a0); break;
    case Hit::Type3: Type3Hit(a0, d0, d3, d4); break;
    case Hit::Walker: WalkerHit(a0, d0, d3, d4); break;
    case Hit::Body: BodyHit(a0, d0, d3, d4); break;
    case Hit::Type1: Type1Hit(a0, d0, d3, d4); break;
    case Hit::BodyFlip: BodyHitFlip(a0, d0, d3, d4); break;
    }
}

} // namespace game
