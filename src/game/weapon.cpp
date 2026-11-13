// The player's weapon: the HUD sprites (0xECE2), the weapon in hand per
// weapon id (FF1050, table 0x11D0C), switching weapons (FF1051, FF1058) and
// firing (FF105A animation step).
#include "enemy.hpp"
#include "m68k.hpp"
#include "render.hpp"

#include "../host.hpp"

namespace game {

namespace {

int16_t s16(uint16_t v) { return int16_t(v); }

U8 weapon{0xFF1050}, next_weapon{0xFF1051};
U16 lower{0xFF1058}, fire_step{0xFF105A}, fire_kind{0xFF105C};
U16 spr_link{0xFF0042};
U32 spr_end{0xFF003E};
U16 dirx{0xFF0E00}, diry{0xFF0E02};

// One sprite at the end of the list FF003E, link | flags.
void put_sprite(uint16_t y, uint16_t flags, uint16_t attr, uint16_t x) {
    uint32_t a2 = spr_end;
    wr16(a2, y);
    wr16(a2 + 2, uint16_t(spr_link | flags));
    spr_link = uint16_t(spr_link + 1);
    wr16(a2 + 4, attr);
    wr16(a2 + 6, x);
    spr_end = a2 + 8;
}

// A sprite from a table entry (y, flags, attr, x).
uint32_t put_table_sprite(uint32_t a0) {
    put_sprite(rd16(a0), rd16(a0 + 2), rd16(a0 + 4), rd16(a0 + 6));
    return a0 + 8;
}

void shot_sound(uint16_t id) {
    U16{0xFF2716} = 0;
    U16{0xFF2718} = 0;
    sub_00D760(id);
    U16{0xFF2718} = 0xF;
}

// The recoil offset of 0x12828 while firing; returns the advanced step.
void recoil(uint16_t& d0) {
    uint16_t step = fire_step;
    if (!step) return;
    d0 = uint16_t(d0 + int8_t(rd8(0x12828 + step)));
    fire_step = uint16_t(fire_step + 1);
    if (fire_step == 5) fire_step = 0;
}

// 0x12052-0x12198: the shot of the hand weapons: hits the nearest enemy in
// front (0x16388) inside the aim cone, else a wall sound.
void hand_shot() {
    uint16_t mask = 0x40;
    if (s16(U16{0xFF0E1A}) > 0)
        mask = 0x80;
    else if (s16(U16{0xFF0E1A}) < 0)
        mask = 0x08;
    uint32_t a1 = sub_016388(0, mask);
    if (a1 != 0xFFFFFFFF) {
        uint32_t a0 = a1;
        int16_t ex = s16(uint16_t(rd16(a0 + 0x20) - player_x)), ey = s16(uint16_t(rd16(a0 + 0x22) - player_y));
        int32_t along = (int32_t(ex) * s16(dirx) + int32_t(ey) * s16(diry)) >> 6;
        int32_t side = int32_t(ey) * s16(dirx) - int32_t(ex) * s16(diry);
        if (along >= 2) {
            uint16_t d5 = uint16_t(divs(0x10000, uint16_t(along)));
            bool near = U16{0xFF1030} == 3 ? d5 > 0x64 : d5 > 0xA0;
            if (near) {
                uint16_t d6 = uint16_t(divs(uint32_t(side), uint16_t(along)));
                if (s16(d6) < 0) d6 = uint16_t(-d6);
                if (d6 <= uint16_t(s16(d5) >> 3)) {
                    uint16_t d3 = uint16_t(-diry), d4 = dirx;
                    if (!(U8{0xFF105D} & 1)) {
                        d3 = uint16_t(-d3);
                        d4 = uint16_t(-d4);
                    }
                    d3 = uint16_t((s16(d3) >> 2) + player_x - rd16(a0 + 0x20));
                    d4 = uint16_t((s16(d4) >> 2) + player_y - rd16(a0 + 0x22));
                    uint16_t d0 = 0x300;
                    if (U16{0xFF1030} == 3) d0 = 0xC8;
                    if (U16{0xFF1030} == 4) d0 = 0x1F4;
                    CallEnemyHit(rd32(a0 + 0xA), a0, d0, d3, d4);
                    return shot_sound(fire_kind >= 2 ? 0x17 : 0x1A);
                }
            }
        }
    }
    uint16_t x = uint16_t((s16(dirx) >> 2) + player_x), y = uint16_t((s16(diry) >> 2) + player_y);
    if (sub_013D48(x, y) && !sub_013C36(x, y)) shot_sound(0x1C);
}

// 0x11F3C: hands (and the weapons sharing them): two fists while idle, the
// punch frames of 0x1219A per FF105C while firing.
void h_011F3C(uint16_t d0, uint16_t d1) {
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
        if (d3 == 1) return sub_000744(0x16C038, 0xA000, 0x150);
        if (d3 == 5) {
            fire_step = 0;
            return sub_000744(0x16C2B8, 0xA000, 0x150);
        }
        d3 = uint16_t(d3 - 1);
    } else if (fire_step == 4)
        fire_step = 0;
    uint32_t a0 = 0x1219A + fire_kind * 24 + uint16_t((d3 - 1) << 3);
    a0 = put_table_sprite(a0);
    if (fire_kind >= 4) put_table_sprite(a0 + 0x28);
    if (fire_step == 3) hand_shot();
}

// 0x11D4A: weapon 7 throws a projectile (0x13ADC) at step 4, aimed at the
// enemy 0x167B0 picks.
void h_011D4A(uint16_t d0, uint16_t d1) {
    d0 = uint16_t(d0 + 0xE8);
    d1 = uint16_t(d1 + 0x134);
    uint16_t d3 = fire_step;
    if (!d3) {
        if (d0 < 0xF8) put_sprite(d0, 0xB00, 0xA500, d1);
        return;
    }
    fire_step = uint16_t(fire_step + 1);
    if (fire_step == 6) fire_step = 0;
    put_table_sprite(0x11F14 + uint16_t((d3 - 1) << 3));
    if (fire_step != 4) return;
    uint32_t a0 = sub_013466();
    if (!a0) return;
    wr16(a0 + 0x2A, uint16_t(s16(dirx) >> 2));
    wr16(a0 + 0x20, uint16_t(rd16(a0 + 0x2A) + player_x));
    wr16(a0 + 0x2C, uint16_t(s16(diry) >> 2));
    wr16(a0 + 0x22, uint16_t(rd16(a0 + 0x2C) + player_y));
    wr8(a0 + 0x34, 0x32);
    uint32_t a1 = sub_0167B0(0x10, uint16_t(U16{0xFF0E28} + 0x40)).a1;
    if (a1) {
        uint16_t x = 0, y = 0;
        if (!link_mode || a1 != 0xFF11E8) {
            x = uint16_t(rd16(a1 + 0x2A) << 2);
            y = uint16_t(rd16(a1 + 0x2C) << 2);
        }
        x = uint16_t(x + rd16(a1 + 0x20) - player_x);
        y = uint16_t(y + rd16(a1 + 0x22) - player_y);
        uint16_t d = Distance(s16(x), s16(y));
        wr8(a0 + 0x34, uint8_t((s16(d) >> 6) + 8));
        // The original scales the target's velocity here, not the projectile's.
        wr16(a1 + 0x2A, uint16_t((int32_t(s16(d)) * s16(rd16(a1 + 0x2A))) >> 11));
        wr16(a1 + 0x2C, uint16_t((int32_t(s16(d)) * s16(rd16(a1 + 0x2C))) >> 11));
    }
    wr8(a0 + 0x1F, 0);
    wr32(a0 + 0x12, 0x13ADC);
    wr32(a0 + 0xE, 0x13F3E);
    wr32(a0 + 0xA, 0x13AD2);
    wr16(a0 + 4, rd16(a0 + 4) | 0xC8);
    wr16(a0 + 0x24, uint16_t(U16{0xFF0E1A} - U16{0xFF116E} - 6));
    wr16(a0 + 0x2E, 8);
    if (sub_00A8B6()) wr32(a0 + 0x12, 0x13E42);
    if (U16{0xFF1030} == 1) {
        wr16(a0 + 0x2A, uint16_t(rd16(a0 + 0x2A) << 1));
        wr16(a0 + 0x2C, uint16_t(rd16(a0 + 0x2C) << 1));
        wr8(a0 + 0x34, uint8_t(int8_t(uint8_t(rd8(a0 + 0x34) + 1)) >> 1));
    }
    if (!link_mode) return;
    wr32(a0 + 0x16, 0x13554);
    wr32(a0 + 0x1A, 0x16DC4);
    wr8(0xFF0FFA, 4);
    wr8(0xFF0FFB, rd8(a0 + 0x3E));
    wr16(0xFF0FFC, rd16(a0 + 0x20));
    wr16(0xFF0FFE, rd16(a0 + 0x22));
    wr8(0xFF1000, rd8(a0 + 0x25));
    wr8(0xFF1001, uint8_t(rd8(a0 + 5) | 0x20));
    wr8(0xFF1002, rd8(a0 + 0x32));
    wr8(0xFF1003, 0);
    wr16(0xFF1004, rd16(a0 + 0x2A));
    wr16(0xFF1006, rd16(a0 + 0x2C));
    sub_01C03A(0xFF0FFA);
}

// 0x1225A: weapon 4 with its muzzle flash (flipped by FF105B).
void h_01225A(uint16_t d0, uint16_t d1) {
    d0 = uint16_t(d0 + 0xD9);
    d1 = uint16_t(d1 + 0x114);
    recoil(d0);
    put_sprite(d0, 0xB00, 0xA500, d1);
    uint16_t step = fire_step;
    if (!step) return;
    uint16_t attr = 0xA50C;
    if (U8{0xFF105B} & 1) attr |= 0x800;
    if (U8{0xFF105B} & 2) attr |= 0x1000;
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
    uint16_t sel = rd8(0xFF104A);
    uint16_t d5 = uint16_t((sel << 9) + 0x9600);
    uint32_t a = 0xFF1036 + sel * 4;
    if (rd16(a + 2) <= 0x40) {
        sub_010B8C();
        return false;
    }
    tweak();
    wr16(a + 2, uint16_t(rd16(a + 2) - 0x40));
    vdp_write_addr(uint16_t(d5 + 0x68));
    DrawAmmoDigits(uint16_t((uint16_t(rd16(a + 2) - 1) >> 8) + 1));
    return true;
}

// The flight time of a shot towards the target 0x167B0 found, from where
// the target will be (velocity * 4).
uint8_t lead_time(uint32_t a1, int shift) {
    uint16_t x = 0, y = 0;
    if (!link_mode || a1 != 0xFF11E8) {
        x = uint16_t(rd16(a1 + 0x2A) << 2);
        y = uint16_t(rd16(a1 + 0x2C) << 2);
    }
    x = uint16_t(x + rd16(a1 + 0x20) - player_x);
    y = uint16_t(y + rd16(a1 + 0x22) - player_y);
    return uint8_t((s16(Distance(s16(x), s16(y))) >> shift) + 1);
}

void link_shot_packet(uint32_t a0, uint32_t handler, uint8_t kind) {
    if (!link_mode) return;
    wr32(a0 + 0x16, 0x13554);
    wr32(a0 + 0x1A, handler);
    wr8(0xFF0FFA, 4);
    wr8(0xFF0FFB, rd8(a0 + 0x3E));
    wr16(0xFF0FFC, rd16(a0 + 0x20));
    wr16(0xFF0FFE, rd16(a0 + 0x22));
    wr8(0xFF1000, rd8(a0 + 0x25));
    wr8(0xFF1001, uint8_t(rd8(a0 + 5) | 0x20));
    wr8(0xFF1002, rd8(a0 + 0x32));
    wr8(0xFF1003, kind);
    wr16(0xFF1004, rd16(a0 + 0x2A));
    wr16(0xFF1006, rd16(a0 + 0x2C));
    sub_01C03A(0xFF0FFA);
}

// 0x1247A: weapon 10 (flame thrower): uses charge while B is held, burns
// the enemy in the cone (damage by distance squared) or the wall in front;
// the flame sprite sits on the floor at the cone's end.
void h_01247A(uint16_t d0, uint16_t d1) {
    uint16_t col = d1;
    d0 = uint16_t(d0 + 0xD9);
    d1 = uint16_t(d1 + 0x118);
    uint16_t d4 = 0xCE;
    if (fire_step) {
        int16_t r = int8_t(rd8(0x12828 + fire_step));
        d0 = uint16_t(d0 + r);
        d4 = uint16_t(d4 - 2 * r);
        fire_step = uint16_t(fire_step + 1);
        bool used_up = false;
        if (fire_step <= 3 && !U16{0xFF0DE2} && (pad_buttons & 0x10)) {
            shot_sound(0x1E);
            if (!use_charge([] {
                    if (fire_step == 3) fire_step = 1;
                }))
                used_up = true;
            else {
                uint16_t width = U16{0xFF1030} == 0 ? 5 : 3;
                uint32_t a1 = sub_0167B0(width, uint16_t((U16{0xFF0E28} << 1) + 0x40)).a1;
                if (a1) {
                    uint16_t d = Distance(s16(uint16_t(rd16(a1 + 0x20) - player_x)), s16(uint16_t(rd16(a1 + 0x22) - player_y)));
                    uint16_t dmg = uint16_t(s16(uint16_t(int32_t(uint32_t(d) * d) >> 8)) >> 1);
                    if (U16{0xFF1030} == 0) dmg = uint16_t(s16(dmg) >> 1);
                    if (dmg < 0x400)
                        CallEnemyHit(rd32(a1 + 0xA), a1, dmg, uint16_t(player_x - rd16(a1 + 0x20)), uint16_t(player_y - rd16(a1 + 0x22)));
                } else {
                    if (!sub_00A8B6()) sub_012AD4();
                    sub_00A5D4();
                }
            }
        }
        if (!used_up && fire_step == 5) fire_step = 0;
    }
    put_sprite(d0, 0xB00, 0xA500, uint16_t(d1 - 3));
    if (fire_step == 2) put_sprite(uint16_t(d0 - 7), 0xA00, 0xA50C, uint16_t(d1 - 5));
    d1 = uint16_t((col << 1) + 0x11E);
    if (!rd8(0xFF4DC6 + int16_t(uint16_t((d4 - 0xCE) << 2)))) return;
    uint16_t shade = uint16_t(rd16(0xFF61A6 + (uint16_t(d1 - 0x11E) & ~1)) >> 4);
    if (shade > 4) shade = 4;
    put_sprite(d4, 0, uint16_t(shade + 0xA354), uint16_t(d1 - 1));
}

// 0x12690: weapon 11 fires a grenade (0x14076) at step 2.
void h_012690(uint16_t d0, uint16_t d1) {
    d0 = uint16_t(d0 + 0xE1);
    d1 = uint16_t(d1 + 0x120);
    if (fire_step) {
        int16_t r = int8_t(rd8(0x12828 + fire_step));
        d0 = uint16_t(d0 + r);
        d1 = uint16_t(d1 + r);
        fire_step = uint16_t(fire_step + 1);
        if (fire_step == 5) fire_step = 0;
    }
    put_sprite(d0, 0xE00, 0xA500, d1);
    if (fire_step != 2) return;
    put_sprite(uint16_t(d0 - 8), 0xA00, 0xA50C, uint16_t(d1 - 5));
    uint32_t a0 = sub_013466();
    if (!a0) return;
    wr8(a0 + 0x34, 0xFF);
    uint32_t a1 = sub_0167B0(8, uint16_t(U16{0xFF0E28} + 0x40)).a1;
    if (a1) wr8(a0 + 0x34, lead_time(a1, 7));
    wr8(a0 + 0x1F, 0);
    wr32(a0 + 0x12, 0x14076);
    wr32(a0 + 0xE, 0x14150);
    wr32(a0 + 0xA, 0x13AD2);
    wr16(a0 + 4, rd16(a0 + 4) | 0xC8);
    int16_t hx = int16_t(s16(dirx) >> 1), hy = int16_t(s16(diry) >> 1);
    wr16(a0 + 0x2A, uint16_t(hx));
    wr16(a0 + 0x20, uint16_t((hx >> 1) + player_x));
    wr16(a0 + 0x2C, uint16_t(hy));
    wr16(a0 + 0x22, uint16_t((hy >> 1) + player_y));
    wr16(a0 + 0x24, uint16_t(U16{0xFF0E1A} - U16{0xFF116E} - 0xC));
    if (sub_00A8B6()) wr32(a0 + 0x12, 0x13E42);
    link_shot_packet(a0, 0x16DEA, 1);
}

// 0x1282E: weapon 13 sprays fire balls (0x142C6) while B is held.
void h_01282E(uint16_t d0, uint16_t d1) {
    d0 = uint16_t(d0 + 0xD9);
    d1 = uint16_t(d1 + 0x114);
    uint16_t step = fire_step;
    recoil(d0);
    put_sprite(d0, 0xB00, 0xA500, d1);
    if (U16{0xFF0DE2}) return;
    if (step == 3) {
        if (pad_buttons & 0x10) {
            fire_step = 3;
            if (!use_charge([] {})) fire_step = 4;
        }
    } else if (step == 1) {
        U16{0xFF2716} = 0;
        U16{0xFF2718} = 0;
        sub_00D760(0x96);
        U16{0xFF2718} = 0x14;
    } else if (step != 2)
        return;
    uint32_t a0 = sub_013466();
    if (!a0) return;
    wr8(a0 + 0x34, 0xFF);
    uint32_t a1 = sub_0167B0(8, uint16_t(U16{0xFF0E28} + 0x40)).a1;
    if (a1) wr8(a0 + 0x34, lead_time(a1, 7));
    wr8(a0 + 0x1F, 0);
    wr32(a0 + 0x12, 0x142C6);
    wr32(a0 + 0xE, 0x14150);
    wr16(a0 + 0x2A, uint16_t(s16(dirx) >> 2));
    wr16(a0 + 0x20, uint16_t(rd16(a0 + 0x2A) + player_x));
    wr16(a0 + 0x2C, uint16_t(s16(diry) >> 2));
    wr16(a0 + 0x22, uint16_t(rd16(a0 + 0x2C) + player_y));
    uint32_t r = Random();
    wr16(a0 + 0x2A, uint16_t(rd16(a0 + 0x2A) + ((s16(uint16_t(r)) >> 8) & 0x1F) - 0x10));
    wr16(a0 + 0x2C, uint16_t(rd16(a0 + 0x2C) + ((r >> 16) & 0x1F) - 0x10));
    wr16(a0 + 0x24, uint16_t(U16{0xFF0E1A} - U16{0xFF116E} - 0xC));
    wr16(a0 + 0x2E, 0xFFFE);
    if (sub_00A8B6()) wr32(a0 + 0x12, 0x13EC0);
    link_shot_packet(a0, 0x16E0E, 4);
}

// Enemy hit by a weapon firing straight ahead: the target 0x167B0 picks gets
// d0 from its distance (scaled by the weapon), else the wall in front.
void straight_shot(uint16_t width, uint16_t column, uint16_t (*damage)(uint16_t)) {
    uint32_t a1 = sub_0167B0(width, column).a1;
    if (!a1) {
        if (!sub_00A8B6()) sub_012AD4();
        return sub_00A5D4();
    }
    uint16_t d = damage(Distance(s16(uint16_t(rd16(a1 + 0x20) - player_x)), s16(uint16_t(rd16(a1 + 0x22) - player_y))));
    if (d >= 0x400) return;
    CallEnemyHit(rd32(a1 + 0xA), a1, d, uint16_t(player_x - rd16(a1 + 0x20)), uint16_t(player_y - rd16(a1 + 0x22)));
}

uint16_t squared(uint16_t d) { return uint16_t(s16(uint16_t(int32_t(uint32_t(d) * d) >> 8)) >> 1); }

bool soldier_build() { return U16{0xFF1030} == 0 || U16{0xFF1030} == 2; }

void start_fire(uint16_t sound, uint16_t hold) {
    U16{0xFF2716} = 0;
    U16{0xFF2718} = 0;
    sub_00D760(sound);
    U16{0xFF2718} = hold;
    fire_step = 1;
    sub_010B00();
}

// 0x12BAC: a punch; up/down/right pick the punch (FF105C).
void h_012BAC() {
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
void h_012BFE() {
    if (lower) return;
    sub_00D760(0x6C);
    sub_010B00();
    uint32_t a0 = sub_013466();
    if (!a0) return;
    wr8(a0 + 0x1F, 0x14);
    wr8(a0 + 0x1E, 0);
    wr32(a0 + 0x12, 0x13F7C);
    wr32(a0 + 0xE, 0x1402E);
    wr32(a0 + 0xA, 0x13AD2);
    wr16(a0 + 4, rd16(a0 + 4) | 8);
    int16_t sx = int16_t(s16(dirx) >> 4), sy = int16_t(s16(diry) >> 4);
    uint16_t x = player_x, y = player_y;
    for (int i = 0; i < 8; i++) {
        if (sub_013D48(uint16_t(x + sx), uint16_t(y + sy))) break;
        x = uint16_t(x + sx);
        y = uint16_t(y + sy);
    }
    wr16(a0 + 0x20, x);
    wr16(a0 + 0x22, y);
    wr16(a0 + 0x24, uint16_t(0xFFE0 - U16{0xFF116E}));
    if (sub_00A8B6()) {
        wr8(a0 + 0x1F, 0);
        wr32(a0 + 0x12, 0x13E42);
    }
    link_shot_packet(a0, 0x16E0C, 2);
}

// 0x12CFE: weapon 4 (close range); also blows the class 0x18 cell under or
// in front of the player into a 0x1423A object.
void h_012CFE() {
    if (fire_step || lower) return;
    sub_00D760(0x6D);
    fire_step = 1;
    sub_010B00();
    uint32_t a1 = sub_0167B0(8, uint16_t(U16{0xFF0E28} + 0x40)).a1;
    if (a1) {
        uint16_t d = Distance(s16(uint16_t(rd16(a1 + 0x20) - player_x)), s16(uint16_t(rd16(a1 + 0x22) - player_y)));
        if (d < 0x12C) CallEnemyHit(rd32(a1 + 0xA), a1, 0x3E8, uint16_t(player_x - rd16(a1 + 0x20)), uint16_t(player_y - rd16(a1 + 0x22)));
    }
    uint32_t a0 = U32{0xFF0E12};
    if (rd8(cell_class + rd8(a0)) != 0x18) {
        a0 = cur_map_ptr + int16_t(uint16_t(s16(uint16_t((s16(dirx) >> 1) + player_x)) >> 8)) +
             int16_t(uint16_t((s16(uint16_t((s16(diry) >> 1) + player_y)) >> 8) << 5));
        if (rd8(cell_class + rd8(a0)) != 0x18) return;
    }
    if (a0 < 0xFF7CD6 || a0 >= 0xFFBCD6) a0 = 0xFFCCD6;
    wr8(a0, 0);
    sub_01C9A0(a0);
    uint16_t off = uint16_t(a0 - cur_map_ptr);
    uint16_t x = uint16_t(((off & 0x1F) << 8) + 0x80), y = uint16_t(((s16(off) >> 5) << 8) + 0x80);
    a0 = sub_013466();
    if (!a0) return;
    wr8(a0 + 0x1F, 0);
    wr8(a0 + 0x1E, 0);
    wr32(a0 + 0x12, 0x1423A);
    wr32(a0 + 0xE, 0x14272);
    wr16(a0 + 0x20, x);
    wr16(a0 + 0x22, y);
    wr16(a0 + 0x24, 0xFFE0);
    link_shot_packet(a0, 0x16E88, 5);
}

void h_012E7C() {
    if (fire_step || lower) return;
    shot_sound(0x1B);
    fire_step = 1;
    sub_010B00();
}

void h_012EAC() {
    if (lower) return;
    start_fire(0x19, 0x14);
    straight_shot(soldier_build() ? 4 : 1, uint16_t(U16{0xFF0E28} + 0x40), [](uint16_t d) { return soldier_build() ? uint16_t(s16(squared(d)) >> 1) : squared(d); });
}

void h_012F62() {
    if (lower) return;
    start_fire(0x1E, 0xF);
    straight_shot(3, uint16_t((U16{0xFF0E28} << 1) + 0x40), squared);
}

void h_012FF4() {
    if (fire_step || lower) return;
    start_fire(0x96, 0x14);
}

void h_013024() {
    if (fire_step || lower) return;
    fire_step = 1;
    sub_010B00();
}

void h_01303C() {
    if (lower) return;
    start_fire(0x1D, 0x14);
    bool soldier = U16{0xFF1030} == 0;
    straight_shot(soldier ? 0xC : 8, uint16_t(U16{0xFF0E28} + 0x40), [](uint16_t d) { return U16{0xFF1030} == 0 ? uint16_t(s16(d) >> 1) : d; });
}

void h_0130DE() {
    if (lower) return;
    start_fire(0x1E, 0xF);
    bool soldier = U16{0xFF1030} == 0;
    straight_shot(soldier ? 0xC : 8, uint16_t(U16{0xFF0E28} + 0x40), [](uint16_t d) {
        d = uint16_t(s16(d) >> 1);
        return U16{0xFF1030} == 0 ? uint16_t(s16(d) >> 1) : d;
    });
}

} // namespace

// 0x108D6: the five inventory slot sprites (slot order 3, 4, 0, 1, 2); an
// item blinks while its FF1052 counter runs.
uint32_t sub_0108D6(uint32_t a2) {
    static const uint8_t order[] = {3, 4, 0, 1, 2};
    static const uint16_t xs[] = {0xA8, 0xD8, 0x110, 0x148, 0x178};
    uint16_t d0 = spr_link | 0xF00;
    for (int i = 0; i < 5; i++) {
        uint8_t id = rd8(0xFF104A + order[i]);
        uint8_t t = rd8(0xFF1052 + id);
        if (t) {
            wr8(0xFF1052 + id, uint8_t(t - 1));
            if (uint8_t(t - 1) == 0 || t == 3) continue;
        }
        wr16(a2, 0x83);
        wr16(a2 + 2, d0);
        d0 = uint16_t(d0 + 1);
        wr16(a2 + 4, uint16_t((id << 4) + 0xE4B0));
        wr16(a2 + 6, xs[i]);
        a2 += 8;
    }
    spr_link = d0 & 0xF0FF;
    return a2;
}

// 0xECC0: undoes the head bob of 0xEC1C.
void sub_00ECC0() {
    if (!U32{0xFF0E28}) return;
    player_angle = U16{0xFF0E2C};
    U16{0xFF0E00} = U16{0xFF0E2E};
    U16{0xFF0E02} = U16{0xFF0E30};
    U16{0xFF0E1A} = U16{0xFF0E32};
}

// 0x11C2C: B switches weapons (0x12A70 fires); the weapon is lowered by
// FF1058 while it changes, then drawn with the head bob.
void sub_011C2C() {
    if (!U16{0xFF0DE2} && (pad_buttons & 0x10) && !(U8{0xFF0032} & 0x10)) sub_012A70();
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
                return sub_000744(rd32(0x11C98 + d0 * 4), 0xA000, 0x150);
            }
        }
    }
    if (lowering && next_weapon == 0xFF && lower) lower = uint16_t(lower - 8);
    uint16_t d0 = uint16_t(lower + U16{0xFF0E2A});
    uint16_t d1 = uint16_t(U16{0xFF0E28} << 1);
    switch (rd32(0x11D0C + weapon * 4)) {
    case 0x11F3C: return h_011F3C(d0, d1);
    case 0x11D48: return;
    case 0x11D4A: return h_011D4A(d0, d1);
    case 0x1225A: return h_01225A(d0, d1);
    case 0x122EE: return gun(d0, d1, false, -1, -5);
    case 0x12362: return gun(d0, d1, true, -1, -5);
    case 0x12404: return gun(d0, d1, false, -1, -9);
    case 0x1247A: return h_01247A(d0, d1);
    case 0x12690: return h_012690(d0, d1);
    case 0x1282E: return h_01282E(d0, d1);
    default: untranslated(rd32(0x11D0C + weapon * 4));
    }
}

// 0x12A70: fires the weapon in hand (table 0x12A98) unless it is changing.
void sub_012A70() {
    uint16_t w = weapon;
    if (int8_t(w) < 0) return;
    if (next_weapon != 0xFF && next_weapon != w) return;
    if (w > 0xE) w = 0;
    switch (rd32(0x12A98 + w * 4)) {
    case 0x12BAC: return h_012BAC();
    case 0x12BFE: return h_012BFE();
    case 0x12CFE: return h_012CFE();
    case 0x12E7C: return h_012E7C();
    case 0x12EAC: return h_012EAC();
    case 0x12F62: return h_012F62();
    case 0x12FF4: return h_012FF4();
    case 0x1303C: return h_01303C();
    case 0x13024: return h_013024();
    case 0x130DE: return h_0130DE();
    default: untranslated(rd32(0x12A98 + w * 4));
    }
}

// 0x12AD4: a bullet hole: steps 32 times along the view and puts a 0x15AC4
// spark on the first wall cell shots do not pass.
void sub_012AD4() {
    int32_t x = int32_t(s16(player_x)) << 3, y = int32_t(s16(player_y)) << 3;
    int32_t vx = s16(dirx), vy = s16(diry);
    for (int i = 0; i < 32; i++, x += vx, y += vy) {
        uint16_t nx = uint16_t((x + vx) >> 3), ny = uint16_t((y + vy) >> 3);
        if (!sub_013D48(nx, ny)) continue;
        if (sub_013C36(nx, ny)) return;
        uint32_t a0 = sub_013466();
        if (!a0) return;
        wr16(a0 + 0x20, uint16_t(x >> 3));
        wr16(a0 + 0x22, uint16_t(y >> 3));
        wr16(a0 + 0x24, uint16_t(U16{0xFF0E1A} - U16{0xFF116E}));
        wr8(a0 + 0x1F, 0);
        wr8(a0 + 0x34, 3);
        wr32(a0 + 0x12, 0x15AC4);
        wr32(a0 + 0xE, 0x15AD6);
        return link_shot_packet(a0, 0x16EAE, 0xC);
    }
}

// 0xA5D4: a shot breaks the breakable cell (classes 0x79, 0x7B, 0x7F) up to
// 9 steps in front.
void sub_00A5D4() {
    uint16_t x = player_x, y = player_y;
    int16_t sx = int16_t(s16(dirx) >> 2), sy = int16_t(s16(diry) >> 2);
    for (int i = 0; i < 9; i++) {
        x = uint16_t(x + sx);
        y = uint16_t(y + sy);
        if (!sub_013D48(x, y)) continue;
        uint32_t a0 = cur_map_ptr + int16_t(uint16_t(s16(x) >> 8)) + int16_t(uint16_t(s16(uint16_t(y & 0xFF00)) >> 3));
        uint8_t cls = rd8(cell_class + rd8(a0));
        uint32_t item = cls == 0x79 ? 0xFF6340 : cls == 0x7B ? 0xFF6342 : cls == 0x7F ? 0xFF6346 : 0;
        if (!item) return;
        if (a0 < 0xFF7CD6 || a0 >= 0xFFBCD6) a0 = 0xFFCCD6;
        wr8(a0, rd8(item));
        sub_01C9A0(a0);
        // 0xA77C
        U16{0xFF2716} = 0;
        U16{0xFF2718} = 0;
        sub_00D760(0x3B);
        U16{0xFF2718} = 0x14;
        return;
    }
}

// 0xECE2: inventory slots, the two halves of the status bar frame, the
// weapon and the sky sprites.
void sub_00ECE2() {
    uint32_t a2 = sub_0108D6(spr_end);
    uint16_t d0 = spr_link | 0x300;
    spr_link = uint16_t(spr_link + 4);
    static const uint16_t frame[4][2] = {{0xF8, 1}, {0xF8, 0}, {0x88, 1}, {0x88, 0}};
    for (int i = 0; i < 4; i++, a2 += 8) {
        wr16(a2, frame[i][0]);
        wr16(a2 + 2, uint16_t(d0 + i));
        wr16(a2 + 4, 0x8001);
        wr16(a2 + 6, frame[i][1]);
    }
    spr_end = a2;
    sub_011C2C();
    a2 = spr_end;
    if (!U16{0xFF271A}) a2 = sub_002404(a2);
    spr_end = a2;
}

} // namespace game
