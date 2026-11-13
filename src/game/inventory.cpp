// Weapons inventory and its HUD slots (0x109E6-0x11320), level passwords
// (0x57E0C-0x58054).
#include "game.hpp"
#include "m68k.hpp"

#include "../host.hpp"

namespace game {

namespace {

constexpr uint32_t slots = 0xFF1036; // 5 x (u16 weapon, u16 ammo * 256)
constexpr uint32_t start_ammo = 0x11222, max_ammo = 0x11240; // per weapon id
constexpr uint32_t slot_tiles = 0x10AC8; // per weapon id - 1: 16 HUD tiles

void wait_dma() {
    while (fb_dma_pending) host::frame();
}

} // namespace

// 0x1DF54: two-digit number (0-127) in the HUD digit font, as two tile
// columns of 6 rows at the current VDP write address.
void DrawAmmoDigits(uint16_t n) {
    n &= 0x7F;
    uint16_t tens = 0;
    while (n >= 10) {
        n = uint16_t(n - 10);
        tens++;
    }
    auto glyph = [](uint16_t d) { return 0x15A06Eu + 24 * (d >> 1) + 2 * (d & 1); };
    uint32_t ones = glyph(n), t = glyph(tens);
    for (int row = 0; row < 6; row++) {
        vdp_data(tens ? rd16(t + row * 4) : 0);
        vdp_data(rd16(ones + row * 4));
    }
}

// 0x10A7A: the 16 tiles of one inventory slot at the current VDP address.
void sub_010A7A(uint32_t slot) {
    uint16_t w = rd16(slot);
    if (!w) {
        for (int i = 0; i < 0x80; i++) vdp_data32(0);
        return;
    }
    uint32_t a1 = rd32(slot_tiles + (w - 1) * 4);
    for (int i = 0; i < 0x1A; i++, a1 += 4) vdp_data32(rd32(a1));
    DrawAmmoDigits(uint16_t(((rd16(slot + 2) - 1) >> 8) + 1));
    a1 += 0x18;
    for (int i = 0; i < 0x60; i++, a1 += 4) vdp_data32(rd32(a1));
}

// 0x109E6: empty inventory.
void sub_0109E6() {
    U16{0xFF1034} = 0;
    for (uint32_t a = 0xFF105E; a <= 0xFF1066; a += 2) wr16(a, 0xFFFF);
    U32{0xFF106A} = 0xFF61B6; // the original stores 0xFF6196 first, then overwrites it
    U8{0xFF1050} = 0xFF;
    U8{0xFF1051} = 0;
    U16{0xFF1058} = 0x20;
    U16{0xFF105A} = 0;
    U8{0xFF104F} = 0;
    wr32(0xFF104A, 0x00010203);
    wr8(0xFF104E, 4);
    for (int i = 0; i < 5; i++) wr16(slots + i * 4, 0);
    wait_dma();
    vdp_ctrl32(0x56000002);
    for (int i = 0; i < 5; i++) sub_010A7A(slots + i * 4);
}

// 0x11C02: firing animation type of the selected weapon.
void sub_011C02() {
    uint16_t w = rd16(slots + rd8(0xFF104A) * 4);
    U8{0xFF1051} = rd8(0x11C1C + w);
}

// 0x1125E: cycles to the next weapon.
void sub_01125E() {
    sub_00D760(0x6B);
    uint8_t first = rd8(0xFF104A);
    for (uint32_t a = 0xFF104A; a < 0xFF104E; a++) wr8(a, rd8(a + 1));
    wr8(0xFF104E, first);
    sub_011C02();
}

// 0x11286: night vision on. Returns d0.
uint16_t sub_011286() {
    uint32_t pal = U16{0xFF271A} ? 0x2172 : 0x2072;
    sub_000BAE(pal, 0x3F);
    U32{0xFF0EAA} = 0x4436;
    U32{0xFF1096} = 0xED8C;
    U16{0xFF0EB4} = 0x10;
    U32{0xFF0EAE} = U16{0xFF0EB2} == 3 ? 0x14EF06 : 0x14EE66;
    return sub_00D494();
}

// 0x112F2: night vision off. Returns d0.
uint16_t sub_0112F2() {
    uint32_t pal = U16{0xFF271A} ? 0x21F2 : 0x20F2;
    sub_000BAE(pal, 0x3F);
    uint16_t mode = U16{0xFF0EB2};
    U16{0xFF0EB2} = 0xFFFF;
    return sub_001D46(mode);
}

// 0x11084: adds weapon d0 (with FF1034 ammo, or its default) to the
// inventory. Returns the slot, or 0xFFFF if it could not be taken.
uint16_t sub_011084(uint16_t weapon) {
    uint16_t d7 = 0;
    uint32_t a0 = slots;
    for (; d7 < 5; d7++, a0 += 4)
        if (rd16(a0) == weapon) break;
    uint16_t slot;
    if (d7 < 5) {
        wait_dma();
        uint16_t d6 = uint16_t((d7 << 9) + 0x9600);
        uint16_t d2 = U16{0xFF1034};
        if (!d2) d2 = rd16(start_ammo + weapon * 2);
        uint16_t d1 = rd16(max_ammo + weapon * 2);
        if (rd16(a0 + 2) == d1) return 0xFFFF;
        d2 = uint16_t(d2 + rd16(a0 + 2));
        if (d2 > d1) d2 = d1;
        wr16(a0 + 2, d2);
        vdp_write_addr(d6);
        sub_010A7A(a0);
        slot = d7;
    } else {
        if (U8{0xFF104F} == 5) return 0xFFFF;
        U8{0xFF104F} += 1;
        a0 = slots;
        uint16_t d6 = 0x9600;
        wait_dma();
        for (d7 = 0; rd16(a0); d7++, a0 += 4, d6 = uint16_t(d6 + 0x200)) {
            if (d7 == 4) {
                U8{0xFF104F} = 5;
                return 0xFFFF;
            }
        }
        wr16(a0, weapon);
        uint16_t ammo = U16{0xFF1034};
        wr16(a0 + 2, ammo ? ammo : rd16(start_ammo + weapon * 2));
        vdp_write_addr(d6);
        sub_010A7A(a0);
        sub_011C02();
        slot = d7;
    }
    wr8(0xFF1052 + slot, 3);
    if (weapon == 9 && int16_t(uint16_t(U16{0xFF105E})) < 0) {
        U16{0xFF105E} = slot;
        sub_011286();
        return 0;
    }
    if (weapon == 6 && int16_t(uint16_t(U16{0xFF1060})) < 0) {
        U16{0xFF1060} = slot;
        U16{0xFF0EB4} = 0x10;
    }
    if (weapon == 1 && int16_t(uint16_t(U16{0xFF1062})) < 0) U16{0xFF1062} = slot;
    if (weapon == 3 && int16_t(uint16_t(U16{0xFF1064})) < 0) U16{0xFF1064} = slot;
    if (weapon == 5 && int16_t(uint16_t(U16{0xFF1066})) < 0) U16{0xFF1066} = slot;
    return 0;
}

// 0x10B8C: the selected item (FF104A) is used up: its slot is cleared.
void sub_010B8C() {
    uint16_t sel = rd8(0xFF104A);
    uint32_t a = slots + sel * 4;
    U16{0xFF2728} = 1;
    wr16(a, 0);
    wr16(a + 2, 0);
    U8{0xFF104F} -= 1;
    U8{0xFF1051} = 0;
    vdp_write_addr(uint16_t((sel << 9) + 0x9600));
    for (int i = 0; i < 0x80; i++) vdp_data32(0);
}

// 0x10B00: uses one unit of the selected item's charge (night vision,
// goggles...). Called once per frame while it is active.
void sub_010B00() {
    wait_dma();
    uint16_t sel = rd8(0xFF104A);
    uint16_t d5 = uint16_t((sel << 9) + 0x9600);
    uint32_t a = slots + sel * 4;
    if (rd16(a + 2) <= 0x100) return sub_010B8C();
    wr16(a + 2, uint16_t(rd16(a + 2) - 0x100));
    uint16_t left = rd16(a + 2);
    if (left < 0x300 && left >= 0x200) sub_01E0C2(0x1E754);
    vdp_write_addr(uint16_t(d5 + 0x68));
    DrawAmmoDigits(uint16_t(((left - 1) >> 8) + 1));
}

namespace {

// 0x10BD8, 0x10CC6, 0x10DC0, 0x10EAC: uses one unit of the charge of the
// active timed item whose slot index is in var. When it runs out, the slot is
// cleared and var moves to another slot holding the same item, or becomes -1
// (gone = true). Returns d0.
uint16_t item_timer(const U16& var, uint16_t item, bool& gone) {
    wait_dma();
    uint16_t d5 = uint16_t((var << 9) + 0x9600);
    uint32_t a = slots + int16_t(uint16_t(var * 4));
    gone = false;
    if (rd16(a + 2) > 0x100) {
        wr16(a + 2, uint16_t(rd16(a + 2) - 0x100));
        vdp_write_addr(uint16_t(d5 + 0x68));
        uint16_t n = uint16_t((uint16_t(rd16(a + 2) - 1) >> 8) + 1);
        DrawAmmoDigits(n);
        return n;
    }
    U16{0xFF2728} = 1;
    wr16(a, 0);
    wr16(a + 2, 0);
    U8{0xFF104F} -= 1;
    vdp_write_addr(d5);
    for (int i = 0; i < 0x80; i++) vdp_data32(0);
    for (uint16_t i = 0; i < 5; i++) {
        if (rd16(slots + i * 4) == item) {
            var = i;
            return uint16_t(d5 >> 14);
        }
    }
    var = 0xFFFF;
    gone = true;
    return uint16_t(d5 >> 14);
}

} // namespace

// 0x10BD8: night vision charge. Returns d0.
uint16_t sub_010BD8() {
    bool gone;
    uint16_t d0 = item_timer(U16{0xFF105E}, 9, gone);
    return gone ? sub_0112F2() : d0;
}

// 0x10CC6: charge of item 6; when gone the lighting mode is reapplied. Returns d0.
uint16_t sub_010CC6() {
    bool gone;
    uint16_t d0 = item_timer(U16{0xFF1060}, 6, gone);
    if (!gone) return d0;
    uint16_t mode = U16{0xFF0EB2};
    U16{0xFF0EB2} = 0xFFFF;
    return sub_001D46(mode);
}

// 0x10DC0: charge of item 1.
void sub_010DC0() {
    bool gone;
    item_timer(U16{0xFF1062}, 1, gone);
}

// 0x10EAC: charge of item 3 (armour).
void sub_010EAC() {
    bool gone;
    item_timer(U16{0xFF1064}, 3, gone);
}

// 0x10F98: charge of item 5 (shield).
void sub_010F98() {
    bool gone;
    item_timer(U16{0xFF1066}, 5, gone);
}

// 0x57E0C: counts the enemies left on the current level; when none are
// left, starts the "level cleared" countdown.
void sub_057E0C() {
    if (U16{0xFF272C}) return;
    uint16_t level = U16{0xFF272A};
    uint16_t d6 = 0;
    uint16_t n = U16{0xFF24E8};
    uint32_t a0 = U32{0xFF24EA};
    for (int i = 0; i < n; i++) {
        if (rd8(a0 + 0x32) == uint8_t(level) && (rd16(a0 + 4) & 0x14) == 0x14) d6++;
        a0 = rd32(a0);
        if (!a0) break;
    }
    uint32_t map = 0xFF7CD6 + uint32_t(level) * 0x400;
    for (int i = 0; i < 0x400; i++) {
        uint8_t c = rd8(cell_class + rd8(map + i));
        if (c >= 0x29 && c <= 0x6B && (c <= 0x2B || c >= 0x65)) d6++;
    }
    if (d6 > 0x63) d6 = 0x63;
    if (d6 != U16{0xFF2730}) sub_01E00E(d6);
    U16{0xFF272E} = d6;
    if (d6) return;
    U16{0xFF272C} = 0xE1;
    sub_01E0C2(0x1E7B8);
}

namespace {

// Ammo as eighths of each weapon's maximum, into FF28F6-FF28FA; health into FF28FB.
void save_inventory() {
    for (int i = 0; i < 5; i++) wr8(0xFF28F1 + i, rd8(slots + i * 4 + 1));
    uint32_t a2 = 0xFF28F6;
    for (uint16_t w = 1; w < 0xF && a2 != 0xFF28FB; w++) {
        for (int i = 0; i < 5; i++) {
            uint32_t a3 = slots + i * 4;
            if (rd16(a3) != w) continue;
            uint32_t d0 = ext_l(rd16(a3 + 2)) << 3;
            if (d0) d0--;
            wr8(a2++, uint8_t(divu(d0, rd16(max_ammo + w * 2))));
            break;
        }
    }
    while (a2 != 0xFF28FB) wr8(a2++, 0);
    uint32_t h = ext_l(U16{0xFF0DE4}) << 6;
    if (h) h--;
    U8{0xFF28FB} = uint8_t(divu(h, 0x64));
}

} // namespace

// 0x57EC4: after the countdown, stores the password for the cleared level.
void sub_057EC4() {
    if (!U16{0xFF272C}) return;
    uint32_t out = 0xFF273C + U16{0xFF272A} * 9;
    save_inventory();
    level_id = uint8_t((U8{0xFF271D} << 4) + U8{0xFF272B});
    sub_058720(out);
    U16{0xFF272A} += 1;
    U16{0xFF272C} = 0;
    sub_057E0C();
}

// 0x57F98: password at FF2732 for the next area.
void sub_057F98() {
    save_inventory();
    level_id = uint8_t((U8{0xFF271D} << 4) + 0xF);
    sub_058720(0xFF2732);
    U16{0xFF272A} += 1;
    U16{0xFF272C} = 0;
    sub_057E0C();
}

} // namespace game
