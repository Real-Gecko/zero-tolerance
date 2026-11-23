// The player's weapon: the HUD sprites (0xECE2), the weapon in hand per
// weapon id (FF1050, table 0x11D0C), switching weapons (FF1051, FF1058) and
// firing (FF105A animation step).
#include "enemy.hpp"
#include "hires.hpp"
#include "m68k.hpp"
#include "render.hpp"

#include "../host.hpp"

namespace game {

const HandTiles kHandTiles[10] = {
    {"screens/tiles/weapon_punch.png", {{4, 4}, {4, 1}, {1, 1}}},
    {"screens/tiles/weapon_hands.png", {{3, 4}, {3, 3}}},
    {"screens/tiles/weapon_04.png", {{3, 4}, {3, 3}}},
    {"screens/tiles/weapon_07_thrower.png", {{3, 4}, {3, 3}}},
    {"screens/tiles/weapon_08.png", {{3, 4}, {3, 3}}},
    {"screens/tiles/weapon_10_flamethrower.png", {{3, 4}, {3, 3}}},
    {"screens/tiles/weapon_11_grenade_launcher.png", {{4, 3}, {3, 3}}},
    {"screens/tiles/weapon_12.png", {{3, 4}, {3, 3}}},
    {"screens/tiles/weapon_13_fireball_gun.png", {{3, 4}, {3, 3}}},
    {"screens/tiles/weapon_14.png", {{3, 4}, {3, 3}}},
};

namespace {

// 0x11C98: the kHandTiles of each weapon id.
const uint8_t kWeaponHand[15] = {1, 1, 1, 1, 2, 1, 1, 3, 4, 1, 5, 6, 7, 8, 9};

int16_t s16(uint16_t v) { return int16_t(v); }

auto& weapon = st.weapon;
auto& next_weapon = st.next_weapon;
auto& lower = st.lower;
auto& fire_step = st.fire_step;
auto& fire_kind = st.fire_kind;
auto& spr_link = st.sprite_link;
auto& spr_end = st.sprite_end;
auto& dirx = st.dir_x;
auto& diry = st.dir_y;

// One sprite at the end of the sprite list, link | flags.
void put_sprite(uint16_t y, uint16_t flags, uint16_t attr, uint16_t x) {
    st.sprites[spr_end++] = {y, uint16_t(spr_link | flags), attr, x};
    spr_link = uint16_t(spr_link + 1);
}

// A sprite from a table entry (y, flags, attr, x).
void put_table_sprite(const uint16_t* e) { put_sprite(e[0], e[1], e[2], e[3]); }

void shot_sound(uint16_t id) {
    st.speech_on = 0;
    st.dac_hold = 0;
    PlaySoundLocal(id);
    st.dac_hold = 0xF;
}

// The recoil offset while firing; returns the advanced step.
void recoil(uint16_t& d0) {
    uint16_t step = fire_step;
    if (!step) return;
    d0 = uint16_t(d0 + kRecoil[step]);
    fire_step = uint16_t(fire_step + 1);
    if (fire_step == 5) fire_step = 0;
}

// 0x12052-0x12198: the shot of the hand weapons: hits the nearest enemy in
// front (0x16388) inside the aim cone, else a wall sound.
void hand_shot() {
    uint16_t mask = 0x40;
    if (s16(st.view_z) > 0)
        mask = 0x80;
    else if (s16(st.view_z) < 0)
        mask = 0x08;
    uint8_t a1 = NearestEnemy(nullptr, mask);
    if (a1 != kNoTarget) {
        Enemy* a0 = &st.objects[a1];
        int16_t ex = s16(uint16_t(a0->x - player_x)), ey = s16(uint16_t(a0->y - player_y));
        int32_t along = (int32_t(ex) * s16(dirx) + int32_t(ey) * s16(diry)) >> 6;
        int32_t side = int32_t(ey) * s16(dirx) - int32_t(ex) * s16(diry);
        if (along >= 2) {
            uint16_t d5 = uint16_t(divs(0x10000, uint16_t(along)));
            bool near = st.character == 3 ? d5 > 0x64 : d5 > 0xA0;
            if (near) {
                uint16_t d6 = uint16_t(divs(uint32_t(side), uint16_t(along)));
                if (s16(d6) < 0) d6 = uint16_t(-d6);
                if (d6 <= uint16_t(s16(d5) >> 3)) {
                    uint16_t d3 = uint16_t(-diry), d4 = dirx;
                    if (!(uint8_t(st.fire_kind) & 1)) {
                        d3 = uint16_t(-d3);
                        d4 = uint16_t(-d4);
                    }
                    d3 = uint16_t((s16(d3) >> 2) + player_x - a0->x);
                    d4 = uint16_t((s16(d4) >> 2) + player_y - a0->y);
                    uint16_t d0 = 0x300;
                    if (st.character == 3) d0 = 0xC8;
                    if (st.character == 4) d0 = 0x1F4;
                    HitObject(a0, d0, d3, d4);
                    return shot_sound(fire_kind >= 2 ? 0x17 : 0x1A);
                }
            }
        }
    }
    uint16_t x = uint16_t((s16(dirx) >> 2) + player_x), y = uint16_t((s16(diry) >> 2) + player_y);
    if (MapCollisionShape(x, y) && !MapShotPasses(x, y)) shot_sound(0x1C);
}

// 0x11F3C: hands (and the weapons sharing them): two fists while idle, the
// punch frames of 0x1219A per FF105C while firing.
void DrawFists(uint16_t d0, uint16_t d1) {
    uint16_t d3 = fire_step;
    if (!d3) {
        d1 = uint16_t(d1 + 0xE8);
        uint16_t y = uint16_t(d0 + 0xEB);
        if (y < 0xF8) put_sprite(y, 0xB00, 0xA500, d1);
        y = uint16_t(lower - uint16_t(d0 - lower) + 0xEB);
        if (y < 0xF8) put_sprite(y, 0xB00, 0xAD00, uint16_t(d1 + 0x58));
        return;
    }
    fire_step = uint16_t(fire_step + 1);
    if (fire_kind < 2) {
        if (d3 == 1) return QueueVramDma(0);
        if (d3 == 5) {
            fire_step = 0;
            return QueueVramDma(1);
        }
        d3 = uint16_t(d3 - 1);
    } else if (fire_step == 4)
        fire_step = 0;
    int i = fire_kind * 3 + d3 - 1;
    put_table_sprite(kHandSprites[i]);
    if (fire_kind >= 4) put_table_sprite(kHandSprites[i + 6]);
    if (fire_step == 3) hand_shot();
}

// 0x11D4A: weapon 7 throws a projectile (0x13ADC) at step 4, aimed at the
// enemy 0x167B0 picks.
void DrawThrower(uint16_t d0, uint16_t d1) {
    d0 = uint16_t(d0 + 0xE8);
    d1 = uint16_t(d1 + 0x134);
    uint16_t d3 = fire_step;
    if (!d3) {
        if (d0 < 0xF8) put_sprite(d0, 0xB00, 0xA500, d1);
        return;
    }
    fire_step = uint16_t(fire_step + 1);
    if (fire_step == 6) fire_step = 0;
    put_table_sprite(kThrowSprites[d3 - 1]);
    if (fire_step != 4) return;
    Enemy* a0 = AllocEnemy();
    if (!a0) return;
    a0->vx = uint16_t(s16(dirx) >> 2);
    a0->x = uint16_t(a0->vx + player_x);
    a0->vy = uint16_t(s16(diry) >> 2);
    a0->y = uint16_t(a0->vy + player_y);
    a0->state = 0x32;
    Enemy* a1 = FindAimTarget(0x10, uint16_t(st.bob_turn + 0x40)).a1;
    if (a1) {
        uint16_t x = uint16_t(a1->vx << 2), y = uint16_t(a1->vy << 2);
        x = uint16_t(x + a1->x - player_x);
        y = uint16_t(y + a1->y - player_y);
        uint16_t d = Distance(s16(x), s16(y));
        a0->state = uint8_t((s16(d) >> 6) + 8);
        // The original scales the target's velocity here, not the projectile's.
        a1->vx = uint16_t((int32_t(s16(d)) * s16(a1->vx)) >> 11);
        a1->vy = uint16_t((int32_t(s16(d)) * s16(a1->vy)) >> 11);
    }
    a0->delay = 0;
    a0->ai = Act::BombAi;
    a0->draw = Draw::Bomb;
    a0->hit = Hit::Bomb;
    a0->flags = a0->flags | 0xC8;
    a0->z = uint16_t(st.view_z - st.stair_height - 6);
    a0->vz = 8;
    if (PlayerCellStopsShots()) a0->ai = Act::ExplodeOnPlayer;
    if (st.character == 1) {
        a0->vx = uint16_t(a0->vx << 1);
        a0->vy = uint16_t(a0->vy << 1);
        a0->state = uint8_t(int8_t(uint8_t(a0->state + 1)) >> 1);
    }
}

// 0x1225A: weapon 4 with its muzzle flash (flipped by FF105B).
void DrawWeapon4(uint16_t d0, uint16_t d1) {
    d0 = uint16_t(d0 + 0xD9);
    d1 = uint16_t(d1 + 0x114);
    recoil(d0);
    put_sprite(d0, 0xB00, 0xA500, d1);
    uint16_t step = fire_step;
    if (!step) return;
    uint16_t attr = 0xA50C;
    if (uint8_t(st.fire_step) & 1) attr |= 0x800;
    if (uint8_t(st.fire_step) & 2) attr |= 0x1000;
    put_sprite(uint16_t(d0 - 0x1D + (step << 3)), 0xA00, attr, uint16_t(d1 - 0xA));
}

// 0x122EE (weapon 12), 0x12362 (weapon 14, with sound 0x1E while firing),
// 0x12404 (weapon 8): the gun and its flash at step 2.
void gun(uint16_t d0, uint16_t d1, bool sound, int16_t flash_dx, int16_t flash_dy) {
    d0 = uint16_t(d0 + 0xD9);
    d1 = uint16_t(d1 + 0x114);
    recoil(d0);
    if (sound && fire_step && fire_step <= 4) {
        shot_sound(0x1E);
    }
    put_sprite(d0, 0xB00, 0xA500, d1);
    if (fire_step == 2) put_sprite(uint16_t(d0 + flash_dy), 0xA00, 0xA50C, uint16_t(d1 + flash_dx));
}

// Uses 0x40 of the selected item's charge and redraws its digits; false
// (after 0x10B8C) when it was used up. tweak runs between the check and the
// subtraction, as in the original.
template <class F> bool use_charge(F tweak) {
    while (fb_dma_pending) host::frame();
    uint16_t sel = st.slot_order[0];
    uint16_t d5 = uint16_t((sel << 9) + 0x9600);
    WeaponSlot& s = st.slots[sel];
    if (s.ammo <= 0x40) {
        UseUpItem();
        return false;
    }
    tweak();
    s.ammo = uint16_t(s.ammo - 0x40);
    DrawAmmoDigits(uint16_t((uint16_t(s.ammo - 1) >> 8) + 1), uint16_t(d5 + 0x68));
    return true;
}

// The flight time of a shot towards the target 0x167B0 found, from where
// the target will be (velocity * 4).
uint8_t lead_time(Enemy* a1, int shift) {
    uint16_t x = uint16_t(a1->vx << 2), y = uint16_t(a1->vy << 2);
    x = uint16_t(x + a1->x - player_x);
    y = uint16_t(y + a1->y - player_y);
    return uint8_t((s16(Distance(s16(x), s16(y))) >> shift) + 1);
}

// 0x1247A: weapon 10 (flame thrower): uses charge while B is held, burns
// the enemy in the cone (damage by distance squared) or the wall in front;
// the flame sprite sits on the floor at the cone's end.
void DrawFlameThrower(uint16_t d0, uint16_t d1) {
    uint16_t col = d1;
    d0 = uint16_t(d0 + 0xD9);
    d1 = uint16_t(d1 + 0x118);
    uint16_t d4 = 0xCE;
    if (fire_step) {
        int16_t r = kRecoil[fire_step];
        d0 = uint16_t(d0 + r);
        d4 = uint16_t(d4 - 2 * r);
        fire_step = uint16_t(fire_step + 1);
        bool used_up = false;
        if (fire_step <= 3 && !st.dying && ((pad_buttons & 0x10) || (actions & 4))) {
            shot_sound(0x1E);
            if (!use_charge([] {
                    if (fire_step == 3) fire_step = 1;
                }))
                used_up = true;
            else {
                uint16_t width = st.character == 0 ? 5 : 3;
                Enemy* a1 = FindAimTarget(width, uint16_t((st.bob_turn << 1) + 0x40)).a1;
                if (a1) {
                    uint16_t d = Distance(s16(uint16_t(a1->x - player_x)), s16(uint16_t(a1->y - player_y)));
                    uint16_t dmg = uint16_t(s16(uint16_t(int32_t(uint32_t(d) * d) >> 8)) >> 1);
                    if (st.character == 0) dmg = uint16_t(s16(dmg) >> 1);
                    if (dmg < 0x400)
                        HitObject(a1, dmg, uint16_t(player_x - a1->x), uint16_t(player_y - a1->y));
                } else {
                    if (!PlayerCellStopsShots()) BulletHole();
                    ShotBreakCell();
                }
            }
        }
        if (!used_up && fire_step == 5) fire_step = 0;
    }
    put_sprite(d0, 0xB00, 0xA500, uint16_t(d1 - 3));
    if (fire_step == 2) put_sprite(uint16_t(d0 - 7), 0xA00, 0xA50C, uint16_t(d1 - 5));
    d1 = uint16_t((col << 1) + 0x11E);
    // The framebuffer byte at column 0x40, row 0x28 + (d4 - 0xCE) (FF4DC6).
    if (!hires::pixel_set(64.5f, float(0x28 + int16_t(uint16_t(d4 - 0xCE))) + 0.5f)) return;
    uint16_t shade = uint16_t(view.depth[0x40 + (int16_t(uint16_t(d1 - 0x11E) & ~1) >> 1)] >> 4);
    if (shade > 4) shade = 4;
    put_sprite(d4, 0, uint16_t(shade + 0xA354), uint16_t(d1 - 1));
}

// 0x12690: weapon 11 fires a grenade (0x14076) at step 2.
void DrawGrenadeLauncher(uint16_t d0, uint16_t d1) {
    d0 = uint16_t(d0 + 0xE1);
    d1 = uint16_t(d1 + 0x120);
    if (fire_step) {
        int16_t r = kRecoil[fire_step];
        d0 = uint16_t(d0 + r);
        d1 = uint16_t(d1 + r);
        fire_step = uint16_t(fire_step + 1);
        if (fire_step == 5) fire_step = 0;
    }
    put_sprite(d0, 0xE00, 0xA500, d1);
    if (fire_step != 2) return;
    put_sprite(uint16_t(d0 - 8), 0xA00, 0xA50C, uint16_t(d1 - 5));
    Enemy* a0 = AllocEnemy();
    if (!a0) return;
    a0->state = 0xFF;
    Enemy* a1 = FindAimTarget(8, uint16_t(st.bob_turn + 0x40)).a1;
    if (a1) a0->state = lead_time(a1, 7);
    a0->delay = 0;
    a0->ai = Act::GrenadeAi;
    a0->draw = Draw::Grenade;
    a0->hit = Hit::Bomb;
    a0->flags = a0->flags | 0xC8;
    int16_t hx = int16_t(s16(dirx) >> 1), hy = int16_t(s16(diry) >> 1);
    a0->vx = uint16_t(hx);
    a0->x = uint16_t((hx >> 1) + player_x);
    a0->vy = uint16_t(hy);
    a0->y = uint16_t((hy >> 1) + player_y);
    a0->z = uint16_t(st.view_z - st.stair_height - 0xC);
    if (PlayerCellStopsShots()) a0->ai = Act::ExplodeOnPlayer;
}

// 0x1282E: weapon 13 sprays fire balls (0x142C6) while B is held.
void DrawFireBallGun(uint16_t d0, uint16_t d1) {
    d0 = uint16_t(d0 + 0xD9);
    d1 = uint16_t(d1 + 0x114);
    uint16_t step = fire_step;
    recoil(d0);
    put_sprite(d0, 0xB00, 0xA500, d1);
    if (st.dying) return;
    if (step == 3) {
        if ((pad_buttons & 0x10) || (actions & 4)) {
            fire_step = 3;
            if (!use_charge([] {})) fire_step = 4;
        }
    } else if (step == 1) {
        st.speech_on = 0;
        st.dac_hold = 0;
        PlaySoundLocal(0x96);
        st.dac_hold = 0x14;
    } else if (step != 2)
        return;
    Enemy* a0 = AllocEnemy();
    if (!a0) return;
    a0->state = 0xFF;
    Enemy* a1 = FindAimTarget(8, uint16_t(st.bob_turn + 0x40)).a1;
    if (a1) a0->state = lead_time(a1, 7);
    a0->delay = 0;
    a0->ai = Act::FireBallAi;
    a0->draw = Draw::Grenade;
    a0->vx = uint16_t(s16(dirx) >> 2);
    a0->x = uint16_t(a0->vx + player_x);
    a0->vy = uint16_t(s16(diry) >> 2);
    a0->y = uint16_t(a0->vy + player_y);
    uint32_t r = Random();
    a0->vx = uint16_t(a0->vx + ((s16(uint16_t(r)) >> 8) & 0x1F) - 0x10);
    a0->vy = uint16_t(a0->vy + ((r >> 16) & 0x1F) - 0x10);
    a0->z = uint16_t(st.view_z - st.stair_height - 0xC);
    a0->vz = 0xFFFE;
    if (PlayerCellStopsShots()) a0->ai = Act::ExplodeOnPlayerShielded;
}

// Enemy hit by a weapon firing straight ahead: the target 0x167B0 picks gets
// d0 from its distance (scaled by the weapon), else the wall in front.
void straight_shot(uint16_t width, uint16_t column, uint16_t (*damage)(uint16_t)) {
    Enemy* a1 = FindAimTarget(width, column).a1;
    if (!a1) {
        if (!PlayerCellStopsShots()) BulletHole();
        return ShotBreakCell();
    }
    uint16_t d = damage(Distance(s16(uint16_t(a1->x - player_x)), s16(uint16_t(a1->y - player_y))));
    if (d >= 0x400) return;
    HitObject(a1, d, uint16_t(player_x - a1->x), uint16_t(player_y - a1->y));
}

uint16_t squared(uint16_t d) { return uint16_t(s16(uint16_t(int32_t(uint32_t(d) * d) >> 8)) >> 1); }

bool soldier_build() { return st.character == 0 || st.character == 2; }

void start_fire(uint16_t sound, uint16_t hold) {
    st.speech_on = 0;
    st.dac_hold = 0;
    PlaySoundLocal(sound);
    st.dac_hold = hold;
    fire_step = 1;
    UseItemCharge();
}

// 0x12BAC: a punch; up/down/right pick the punch (FF105C).
void FirePunch() {
    if (fire_step || lower) return;
    shot_sound(0x1B);
    fire_step = 1;
    uint16_t kind = 3;
    if (pad_buttons & 1) kind = uint16_t(kind + 2);
    if (pad_buttons & 2) kind = uint16_t(kind - 2);
    if (pad_buttons & 8) kind = uint16_t(kind - 1);
    fire_kind = kind;
}

// 0x12BFE: weapon 2 lays a mine (0x13F7C) up to 8 steps ahead on the floor.
void LayMine() {
    if (lower) return;
    PlaySoundLocal(0x6C);
    UseItemCharge();
    Enemy* a0 = AllocEnemy();
    if (!a0) return;
    a0->delay = 0x14;
    a0->step = 0;
    a0->ai = Act::MineAi;
    a0->draw = Draw::Mine;
    a0->hit = Hit::Bomb;
    a0->flags = a0->flags | 8;
    int16_t sx = int16_t(s16(dirx) >> 4), sy = int16_t(s16(diry) >> 4);
    uint16_t x = player_x, y = player_y;
    for (int i = 0; i < 8; i++) {
        if (MapCollisionShape(uint16_t(x + sx), uint16_t(y + sy))) break;
        x = uint16_t(x + sx);
        y = uint16_t(y + sy);
    }
    a0->x = x;
    a0->y = y;
    a0->z = uint16_t(0xFFE0 - st.stair_height);
    if (PlayerCellStopsShots()) {
        a0->delay = 0;
        a0->ai = Act::ExplodeOnPlayer;
    }
}

// 0x12CFE: weapon 4 (close range); also blows the class 0x18 cell under or
// in front of the player into a 0x1423A object.
void FireWeapon4() {
    if (fire_step || lower) return;
    PlaySoundLocal(0x6D);
    fire_step = 1;
    UseItemCharge();
    Enemy* a1 = FindAimTarget(8, uint16_t(st.bob_turn + 0x40)).a1;
    if (a1) {
        uint16_t d = Distance(s16(uint16_t(a1->x - player_x)), s16(uint16_t(a1->y - player_y)));
        if (d < 0x12C) HitObject(a1, 0x3E8, uint16_t(player_x - a1->x), uint16_t(player_y - a1->y));
    }
    Cell a0 = st.player_cell;
    if (st.cell_class[map_cell(a0)] != 0x18) {
        a0 = cur_map + int16_t(uint16_t(s16(uint16_t((s16(dirx) >> 1) + player_x)) >> 8)) +
             int16_t(uint16_t((s16(uint16_t((s16(diry) >> 1) + player_y)) >> 8) << 5));
        if (st.cell_class[map_cell(a0)] != 0x18) return;
    }
    a0 = on_map(a0);
    put_cell(a0, 0);
    uint16_t off = uint16_t(a0 - cur_map);
    uint16_t x = uint16_t(((off & 0x1F) << 8) + 0x80), y = uint16_t(((s16(off) >> 5) << 8) + 0x80);
    Enemy* e = AllocEnemy();
    if (!e) return;
    e->delay = 0;
    e->step = 0;
    e->ai = Act::FireAi;
    e->draw = Draw::Fire;
    e->x = x;
    e->y = y;
    e->z = 0xFFE0;
}

// 0x12E7C
void FireThrower() {
    if (fire_step || lower) return;
    shot_sound(0x1B);
    fire_step = 1;
    UseItemCharge();
}

// 0x12EAC
void FireWeapon8() {
    if (lower) return;
    start_fire(0x19, 0x14);
    straight_shot(soldier_build() ? 4 : 1, uint16_t(st.bob_turn + 0x40), [](uint16_t d) { return soldier_build() ? uint16_t(s16(squared(d)) >> 1) : squared(d); });
}

// 0x12F62
void FireFlameThrower() {
    if (lower) return;
    start_fire(0x1E, 0xF);
    straight_shot(3, uint16_t((st.bob_turn << 1) + 0x40), squared);
}

// 0x12FF4
void FireGrenadeLauncher() {
    if (fire_step || lower) return;
    start_fire(0x96, 0x14);
}

// 0x13024
void FireFireBallGun() {
    if (fire_step || lower) return;
    fire_step = 1;
    UseItemCharge();
}

// 0x1303C
void FireWeapon12() {
    if (lower) return;
    start_fire(0x1D, 0x14);
    bool soldier = st.character == 0;
    straight_shot(soldier ? 0xC : 8, uint16_t(st.bob_turn + 0x40), [](uint16_t d) { return st.character == 0 ? uint16_t(s16(d) >> 1) : d; });
}

// 0x130DE
void FireWeapon14() {
    if (lower) return;
    start_fire(0x1E, 0xF);
    bool soldier = st.character == 0;
    straight_shot(soldier ? 0xC : 8, uint16_t(st.bob_turn + 0x40), [](uint16_t d) {
        d = uint16_t(s16(d) >> 1);
        return st.character == 0 ? uint16_t(s16(d) >> 1) : d;
    });
}

} // namespace

// 0x108D6: the five inventory slot sprites (slot order 3, 4, 0, 1, 2); an
// item blinks while its FF1052 counter runs.
uint16_t InventorySprites(uint16_t a2) {
    static const uint8_t order[] = {3, 4, 0, 1, 2};
    static const uint16_t xs[] = {0xA8, 0xD8, 0x110, 0x148, 0x178};
    uint16_t d0 = spr_link | 0xF00;
    for (int i = 0; i < 5; i++) {
        uint8_t id = st.slot_order[order[i]];
        uint8_t t = st.slot_blink[id];
        if (t) {
            st.slot_blink[id] = uint8_t(t - 1);
            if (uint8_t(t - 1) == 0 || t == 3) continue;
        }
        st.sprites[a2++] = {0x83, d0, uint16_t((id << 4) + 0xE4B0), xs[i]};
        d0 = uint16_t(d0 + 1);
    }
    spr_link = d0 & 0xF0FF;
    return a2;
}

// 0xECC0: undoes the head bob of 0xEC1C.
void UndoHeadBob() {
    if (!st.bob_turn && !st.bob_lift) return;
    player_angle = st.bob_angle;
    st.dir_x = st.bob_dir_x;
    st.dir_y = st.bob_dir_y;
    st.view_z = st.bob_z;
}

// 0x11C2C: B switches weapons (0x12A70 fires); the weapon is lowered by
// FF1058 while it changes, then drawn with the head bob.
void UpdateWeapon() {
    if (!st.dying && (((pad_buttons & 0x10) && !(st.pad_prev & 0x10)) || action_pressed(4))) FireWeapon();
    bool lowering = true;
    if (!fire_step && int8_t(next_weapon) >= 0) {
        uint16_t d0 = next_weapon;
        if (d0 == weapon) {
            next_weapon = 0xFF;
            lowering = false;
        } else {
            lower = uint16_t(lower + 8);
            if (lower >= 0x20) {
                lower = 0x20;
                next_weapon = 0xFF;
                if (d0 > 0xE) d0 = 0;
                weapon = uint8_t(d0);
                return QueueVramDma(kWeaponHand[d0]);
            }
        }
    }
    if (lowering && next_weapon == 0xFF && lower) lower = uint16_t(lower - 8);
    uint16_t d0 = uint16_t(lower + st.bob_lift);
    uint16_t d1 = uint16_t(st.bob_turn << 1);
    switch (weapon) { // table 0x11D0C
    case 0: case 1: case 3: case 5: case 6: case 9: return DrawFists(d0, d1);
    case 2: return;
    case 7: return DrawThrower(d0, d1);
    case 4: return DrawWeapon4(d0, d1);
    case 12: return gun(d0, d1, false, -1, -5);
    case 14: return gun(d0, d1, true, -1, -5);
    case 8: return gun(d0, d1, false, -1, -9);
    case 10: return DrawFlameThrower(d0, d1);
    case 11: return DrawGrenadeLauncher(d0, d1);
    case 13: return DrawFireBallGun(d0, d1);
    default: untranslated(0x11D0C);
    }
}

// 0x12A70: fires the weapon in hand (table 0x12A98) unless it is changing.
void FireWeapon() {
    uint16_t w = weapon;
    if (int8_t(w) < 0) return;
    if (next_weapon != 0xFF && next_weapon != w) return;
    if (w > 0xE) w = 0;
    switch (w) {
    case 2: return LayMine();
    case 4: return FireWeapon4();
    case 7: return FireThrower();
    case 8: return FireWeapon8();
    case 10: return FireFlameThrower();
    case 11: return FireGrenadeLauncher();
    case 12: return FireWeapon12();
    case 13: return FireFireBallGun();
    case 14: return FireWeapon14();
    default: return FirePunch();
    }
}

// 0x12AD4: a bullet hole: steps 32 times along the view and puts a 0x15AC4
// spark on the first wall cell shots do not pass.
void BulletHole() {
    int32_t x = int32_t(s16(player_x)) << 3, y = int32_t(s16(player_y)) << 3;
    int32_t vx = s16(dirx), vy = s16(diry);
    for (int i = 0; i < 32; i++, x += vx, y += vy) {
        uint16_t nx = uint16_t((x + vx) >> 3), ny = uint16_t((y + vy) >> 3);
        if (!MapCollisionShape(nx, ny)) continue;
        if (MapShotPasses(nx, ny)) return;
        Enemy* a0 = AllocEnemy();
        if (!a0) return;
        a0->x = uint16_t(x >> 3);
        a0->y = uint16_t(y >> 3);
        a0->z = uint16_t(st.view_z - st.stair_height);
        a0->delay = 0;
        a0->state = 3;
        a0->ai = Act::SparkAi;
        a0->draw = Draw::Spark;
        return;
    }
}

// 0xA5D4: a shot breaks the breakable cell (classes 0x79, 0x7B, 0x7F) up to
// 9 steps in front.
void ShotBreakCell() {
    uint16_t x = player_x, y = player_y;
    int16_t sx = int16_t(s16(dirx) >> 2), sy = int16_t(s16(diry) >> 2);
    for (int i = 0; i < 9; i++) {
        x = uint16_t(x + sx);
        y = uint16_t(y + sy);
        if (!MapCollisionShape(x, y)) continue;
        Cell a0 = cur_map + int16_t(uint16_t(s16(x) >> 8)) + int16_t(uint16_t(s16(uint16_t(y & 0xFF00)) >> 3));
        uint8_t cls = st.cell_class[map_cell(a0)];
        if (cls != 0x79 && cls != 0x7B && cls != 0x7F) return;
        put_cell(a0, st.class_cell[cls + 1]);
        // 0xA77C
        st.speech_on = 0;
        st.dac_hold = 0;
        PlaySoundLocal(0x3B);
        st.dac_hold = 0x14;
        return;
    }
}

uint16_t sky_sprites = 0;
uint16_t sky_link = 1;

// 0xECE2: inventory slots, the two halves of the status bar frame, the
// weapon and the sky sprites.
void StatusSprites() {
    uint16_t a2 = InventorySprites(spr_end);
    uint16_t d0 = spr_link | 0x300;
    spr_link = uint16_t(spr_link + 4);
    static const uint16_t frame[4][2] = {{0xF8, 1}, {0xF8, 0}, {0x88, 1}, {0x88, 0}};
    for (int i = 0; i < 4; i++) st.sprites[a2++] = {frame[i][0], uint16_t(d0 + i), 0x8001, frame[i][1]};
    spr_end = a2;
    UpdateWeapon();
    a2 = spr_end;
    sky_sprites = a2;
    sky_link = spr_link;
    if (!st.indoors) a2 = SkySprites(a2);
    spr_end = a2;
}

} // namespace game
