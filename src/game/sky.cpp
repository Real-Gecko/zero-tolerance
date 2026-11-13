// The sky seen outdoors (FF271A = 0): the moving backdrop sprites (0x2404),
// the lightning flash (0x2682, 0x27F8) that hurts a player standing on the
// open cells 0x27 / 0x28, and the bolt sprite (0x28B4, 0x28F6).
#include "game.hpp"

#include "../host.hpp"

namespace game {

namespace {

int16_t s16(uint16_t v) { return int16_t(v); }

U8 flash{0xFF1074}, flash_x{0xFF1075}, flash_count{0xFF1077}, flash_time{0xFF1078}, flash_lit{0xFF1079};
U8 bolt_on{0xFF107C}, bolt_time{0xFF107D};
U16 flash_col{0xFF107A}, bolt_x{0xFF107E}, bolt_y{0xFF1080};
U16 spr_link{0xFF0042};

uint32_t put_sprite(uint32_t a2, uint16_t y, uint16_t link, uint16_t attr, uint16_t x) {
    wr16(a2, y);
    wr16(a2 + 2, link);
    wr16(a2 + 4, attr);
    wr16(a2 + 6, x);
    return a2 + 8;
}

// 0x2794: the cloud lit by the flash, d5 its column.
uint16_t sub_002794(uint16_t& d1, uint16_t& d2, uint16_t d5) {
    d1 = uint16_t(d1 + 0xB0);
    d2 = uint16_t(d2 + 0xE0);
    uint16_t d3 = uint16_t((d1 & 3) + 0xC54C);
    if (s16(d1) >= 0xC6 && s16(d1) <= 0xDA) d3 = 0xC54F;
    if (s16(d2) <= s16(uint16_t(d5 + 0x18)) && s16(d2) >= s16(uint16_t(d5 - 0x18))) d3 = 0xC550;
    if (s16(d1) <= 0xD0) d3 |= 0x1000;
    if (s16(d2) <= s16(d5)) d3 |= 0x800;
    return d3;
}

// 0x271C
uint16_t sub_00271C(uint16_t& d1, uint16_t& d2) {
    uint16_t d3 = 0xC551;
    if (d1 < 0x10)
        d3 = uint16_t(d3 + 0x1002);
    else if (d1 >= 0x30)
        d3 = uint16_t(d3 + 2);
    d1 = uint16_t(d1 + 0xB0);
    d2 = uint16_t(d2 + 0xE0);
    return d3;
}

uint16_t flash_column(uint16_t base) {
    return uint16_t((uint16_t(uint16_t(-player_angle) << 1) + base - (s16(player_y) >> 6) + flash_x) & 0x3FF);
}

// 0x26DA: a random spark of the flash, placed by the view direction.
uint32_t sub_0026DA(uint32_t a2) {
    uint32_t r = Random();
    uint16_t d1 = uint16_t(r >> 16) & 0x3F, d2 = uint16_t(r >> 8) & 0x7F;
    uint16_t a = player_angle & 0x1FF;
    uint16_t d0 = 0, d3;
    if (a < 0x40 || a >= 0x1C0) {
        d3 = uint16_t(sub_00271C(d1, d2) | 0x800);
        d0 = 0x400;
    } else if (a < 0xC0)
        d3 = sub_002794(d1, d2, flash_column(0x220));
    else if (a < 0x140) {
        d3 = sub_00271C(d1, d2);
        d0 = 0x400;
    } else
        d3 = uint16_t(sub_002794(d1, d2, flash_column(0x20)) ^ 0x1800);
    a2 = put_sprite(a2, d1, uint16_t(d0 | spr_link), d3, d2);
    spr_link = uint16_t(spr_link + 1);
    return a2;
}

// 0x27F8: the flash: thunder at 0x0A-0x0F, the hit on open cells at 0x0A
// and 0x10, over at 0x20 (or 0x24 counting down FF1077).
void sub_0027F8() {
    flash_time = uint8_t(flash_time + 1);
    uint8_t t = flash_time;
    auto thunder = [] {
        sub_00D760(0x68);
        sub_00D760(0x97);
    };
    auto strike = [] {
        uint8_t cls = rd8(cell_class + rd8(U32{0xFF0E12}));
        if (cls == 0x28 || (cls == 0x27 && s16(U16{0xFF0E1A}) > -10)) sub_00D800(0x64, 0x64, 0);
    };
    if (t == 0x20) {
        flash = 0;
        flash_time = 0;
    } else if (t == 0x24) {
        flash = 0;
        flash_lit = 0;
        flash_count = uint8_t(flash_count - 1);
    } else if (t == 0xA) {
        flash_lit = 1;
        thunder();
        strike();
    } else if (t == 0x10)
        strike();
    else if (t == 0x16)
        flash_lit = 0;
    else if (t > 0xA && t < 0x10)
        thunder();
}

// 0x28F6: the bolt sprite; at count 8 it strikes (sound 0x18).
uint32_t sub_0028F6(uint32_t a2) {
    if (bolt_on) {
        bolt_x = uint16_t(s16(uint16_t((bolt_x << 1) + bolt_x + flash_col)) >> 2);
        flash_time = 0x14;
    }
    bolt_time = uint8_t(bolt_time - 1);
    if (bolt_time == 8) {
        sub_00D760(0x18);
        flash_count = 1;
        flash_time = 0x20;
        sub_0029BA();
        sub_0029BA();
        sub_0029BA();
        bolt_on = 0;
    }
    uint32_t a0 = 0x297A + (bolt_time & 7) * 8;
    a2 = put_sprite(a2, uint16_t(bolt_y + rd16(a0)), uint16_t(spr_link | rd16(a0 + 2)), rd16(a0 + 4), uint16_t(bolt_x + rd16(a0 + 6)));
    spr_link = uint16_t(spr_link + 1);
    return a2;
}

} // namespace

// 0x2682: starts a lightning flash (cells 0x27/0x28) unless one is running.
void h_002682() {
    if (!flash_count || flash) return;
    flash = 0xFF;
    flash_time = 0;
    flash_lit = 0;
    U8{0xFF1076} = 4;
    flash_x = uint8_t(uint16_t(Random() >> 16) & 0x78);
    while (fb_dma_pending) host::frame();
    vdp_ctrl32(0x68A00002);
    for (int i = 0; i <= 0x16F; i++) vdp_data32(rd32(0x16E058 + i * 4));
}

// 0x28B4: starts the bolt at column d0; it hits the flash when that is
// within 0x40 of the middle.
void sub_0028B4(uint16_t d0) {
    bolt_x = d0;
    bolt_y = 0xD0;
    if (flash) {
        int16_t d = int16_t(flash_col - 0x120);
        if (d < 0) d = int16_t(-d);
        if (uint16_t(d) < 0x40) {
            bolt_on = 1;
            bolt_time = 0x10;
            flash_time = 0x14;
            return;
        }
    }
    bolt_on = 0;
    bolt_time = 8;
}

// 0x29BA: tells the other console about the bolt.
void sub_0029BA() {
    if (!link_mode) return;
    wr8(0xFF0FFA, 0x16);
    sub_01C03A(0xFF0FFA);
}

// 0x2404: the sky sprites (FF003E list a2), returns the new end.
uint32_t sub_002404(uint32_t a2) {
    if (flash) {
        sub_0027F8();
        if (flash_lit) a2 = sub_0026DA(a2);
    }
    if (bolt_time) a2 = sub_0028F6(a2);
    uint16_t d2 = uint16_t((uint16_t(uint16_t(-player_angle) << 1) + 0x210 - (s16(player_y) >> 6)) & 0x3FF);
    if (d2 == 0 || d2 >= 0x1A0) return a2;
    flash_col = 0;
    if (flash) {
        uint16_t x = uint16_t(flash_x + 0x10 + d2);
        a2 = put_sprite(a2, 0xC9, spr_link, rd16(0x265E + (flash_time & 0xFE)), x);
        spr_link = uint16_t(spr_link + 1);
        flash_col = x;
    }
    uint16_t d3 = 0x13;
    if (d2 < 0xA0) {
        d3 = uint16_t(d2 >> 3);
        d2 = uint16_t((d2 & 7) + 0x98);
    } else if (d2 > 0x100)
        d3 = uint16_t(s16(uint16_t(0x1A0 - d2)) >> 3);
    uint32_t a0 = 0x2510 + d3 * 12;
    uint16_t d1 = spr_link;
    int16_t d5 = int16_t(0xCF - int16_t(uint16_t((cur_level << 3) + (s16(U16{0xFF116E}) >> 4))));
    uint32_t a1 = 0x2600;
    while (d5 <= 0x98) {
        d5 = int16_t(d5 + 0x10);
        a1 += 2;
    }
    do {
        uint16_t d6 = rd16(a1);
        a1 += 2;
        uint16_t d7 = d2;
        for (uint32_t a3 = a0;; a3 += 2) {
            uint16_t d0 = rd16(a3);
            if (s16(d0) < 0) break;
            a2 = put_sprite(a2, uint16_t(d5), uint16_t(d0 | d1), d6, d7);
            d1 = uint16_t(d1 + 1);
            d7 = uint16_t(d7 + 0x20);
        }
        d5 = int16_t(d5 + 0x10);
    } while (d5 < 0xF8);
    spr_link = d1;
    return a2;
}

} // namespace game
