// Status display: the HUD set up at level start (0x29DA), the map screen
// (0x2CEC), lighting modes (0x1D46), the backdrop slice (0xD494) and the face
// animation (0x22BE, 0x23C2).
#include "game.hpp"
#include "m68k.hpp"

namespace game {

namespace {

void copy_longs(uint32_t src, int n) {
    for (int i = 0; i < n; i++, src += 4) vdp_data32(rd32(src));
}

void fill_ff(int n) {
    for (int i = 0; i < n; i++) vdp_data32(0xFFFFFFFF);
}

// Iterations of `subq #1,d7; bmi skip; loop: ... dbra d7,loop` for d7 = n.
int dbra_count(int16_t n) {
    int16_t d7 = int16_t(n - 1);
    return d7 < 0 ? 0 : d7 + 1;
}

void reset_queues() {
    wr32(vdp_queue, 0xFFFFFFFF);
    vdp_queue_ptr = vdp_queue;
    U16{0xFF0042} = 1;
    U32{0xFF003E} = 0xFF0044;
}

} // namespace

// 0x29DA: draws the HUD and the status area at the start of a level.
void sub_0029DA() {
    WaitVBlank();
    vdp_ctrl(0x8124);
    fb_dma_pending = 0;
    sub_001A0C();
    uint32_t pal;
    if (U16{0xFF105E} == 0xFFFF)
        pal = U16{0xFF271A} ? 0x21F2 : 0x20F2;
    else
        pal = U16{0xFF271A} ? 0x2172 : 0x2072;
    vdp_ctrl32(0xC0000000);
    copy_longs(pal, 32);
    sub_000CAA();
    sub_000C5A();
    LoadImageRes(0x6B20, 0xC000, 0xE000, 0x1563E6);
    vdp_ctrl32(0x0B100003);
    U32{0xFF0DCC} = rd32(VDP_DATA);
    sub_002CDC();
    vdp_ctrl32(0x04CA0003);
    uint16_t d0 = rd16(VDP_DATA);
    sub_002CDC();
    vdp_write_addr(uint16_t((d0 & 0x7FF) << 5));
    fill_ff(8);
    sub_00D98E();
    sub_01E00E(U16{0xFF272E});
    uint16_t face = U16{0xFF1030};
    if (face > 4) face = 0;
    U16{0xFF1030} = face;
    LoadImageRes(0x80A0, 0xC830, 0xE000, rd32(0x2CC8 + face * 4));
    vdp_ctrl32(0x6E600002);
    fill_ff(8);
    copy_longs(0x16E758, 0x140);
    vdp_ctrl32(0x68200000);
    if (!U16{0xFF271A})
        copy_longs(0x14F046, 0x1030);
    else
        copy_longs(0x154406, 0x3F8);
    uint16_t tile = 0x8001;
    for (uint16_t col = 0xC288; col < 0xC288 + 64; col += 2) {
        for (uint16_t a = col, i = 0; i < 10; i++, a = uint16_t(a + 0x80)) {
            vdp_write_addr(a);
            vdp_data(tile++);
        }
    }
    vdp_ctrl32(0x68E00001);
    copy_longs(0x15822E, 0x90);
    vdp_ctrl32(0x62A00002);
    copy_longs(0x16DA58, 0x180);
    vdp_ctrl(0x8F80);
    uint16_t d3 = U16{0xFF0DDE}, d4 = d3;
    for (int i = 0; i < 0x2A; i++) {
        d3 &= 0xFF;
        d4 &= 0x7F;
        vdp_write_addr(uint16_t(0xE000 + d4));
        uint32_t a0 = U32{0xFF271E} + int16_t(d3);
        d3 = uint16_t(d3 + 2);
        d4 = uint16_t(d4 + 2);
        for (int j = 0; j < 16; j++, a0 += 0x100) vdp_data(rd16(a0));
    }
    vdp_ctrl(0x8F02);
    vdp_ctrl32(0x40000010);
    uint16_t vs = uint16_t(cur_level * 4 + (int16_t(uint16_t(U16{0xFF116E})) >> 5) - 0x20);
    vdp_ctrl32(0x40000010);
    vdp_data(vs);
    vdp_ctrl32(0x7C000002);
    vdp_data(uint16_t(U16{0xFF0E22} + 0x10));
    U16{0xFF0EB8} = 1;
    uint16_t mode = U16{0xFF0EB2};
    U16{0xFF0EB2} = 0xFFFF;
    sub_001D46(mode);
    reset_queues();
    U16{0xFF06D4} = 2;
}

// 0x2CDC: short delay between VDP accesses.
void sub_002CDC() {}

// 0x2CEC: map screen (Start during gameplay); Up/Down change the map scale.
// The link-game packet exchange (0x1C0F6, 0x1C52C) is not supported.
void sub_002CEC() {
    fb_dma_pending = 0;
    WaitVBlank();
    vdp_ctrl(0x8124);
    sub_001A0C();
    vdp_ctrl32(0xC07E0000);
    vdp_data(0);
    sub_0C633A();
    sub_000CAA();
    sub_000C5A();
    LoadImageRes(0x2820, 0xE000, 0x6000, 0x12D43E);
    vdp_ctrl32(0x73600000);
    copy_longs(0x12E846, 0x90);
    vdp_ctrl32(0x75A00000);
    copy_longs(0x125116, 0x4B0);
    vdp_ctrl32(0x48600001);
    copy_longs(0x12EAA6, 0x120);
    uint16_t t = 0xE267;
    for (uint16_t row = 0xC518, i = 0; i < 16; i++, row = uint16_t(row + 0x80)) {
        vdp_write_addr(row);
        for (int j = 0; j < 16; j++) vdp_data(t++);
    }
    vdp_ctrl32(0x40000010);
    vdp_data(0);
    vdp_data(0xFFFC);
    vdp_ctrl32(0xC0400000);
    copy_longs(0x1CDAE, 8);
    sub_002FA0();
    reset_queues();
    U16{0xFF102A} = U16{0xFF272A};
    sub_05755E(U16{0xFF102A});
    U16{0xFF102C} = 0;
    WaitVBlank();
    vdp_ctrl(0x8164);
    U8 rep_up{0xFF102C}, rep_down{0xFF102D};
    U8 prev{0xFF0032};
    do {
        WaitVBlank();
        sub_00079A();
        sub_000D36();
        bool step = false;
        if (pad_buttons & 1) {
            if (!(prev & 1)) {
                rep_up = 0x10;
                step = true;
            } else {
                rep_up = uint8_t(rep_up - 1);
                if (rep_up == 1) {
                    rep_up = 4;
                    step = true;
                }
            }
            if (step) {
                if (U16{0xFF102A} == 0)
                    rep_up = 0;
                else {
                    U16{0xFF102A} = uint16_t(U16{0xFF102A} - 1);
                    sub_05755E(U16{0xFF102A});
                    sub_00D760(0x6B);
                }
            }
        } else
            rep_up = 0;
        step = false;
        if (pad_buttons & 2) {
            if (!(prev & 2)) {
                rep_down = 0x10;
                step = true;
            } else {
                rep_down = uint8_t(rep_down - 1);
                if (rep_down == 1) {
                    rep_down = 4;
                    step = true;
                }
            }
            if (step) {
                if (U16{0xFF102A} == U16{0xFF272A})
                    rep_down = 0;
                else {
                    U16{0xFF102A} = uint16_t(U16{0xFF102A} + 1);
                    sub_05755E(U16{0xFF102A});
                    sub_00D760(0x6B);
                }
            }
        } else
            rep_down = 0;
        U16{0xFF0042} = 1;
        U32{0xFF003E} = 0xFF0044;
        if (U8{0xFF0001} & 0x10) {
            uint32_t a2 = U32{0xFF003E};
            wr16(a2, uint16_t((int16_t(uint16_t(player_y)) >> 6) + 0xD1));
            wr16(a2 + 2, U16{0xFF0042});
            U16{0xFF0042} = uint16_t(U16{0xFF0042} + 1);
            wr16(a2 + 4, 0xE19D);
            wr16(a2 + 6, uint16_t((int16_t(uint16_t(player_x)) >> 6) + 0xDD));
            U32{0xFF003E} = a2 + 8;
        }
        sub_001D18();
        wr32(vdp_queue_ptr, 0xFFFFFFFF);
        U8 map_flags{0xFF06D6};
        if ((map_flags & 1) && (pad_buttons & 0x80) && !(prev & 0x80)) {
            map_flags = uint8_t(map_flags & ~1);
            wr8(0xFF0FFA, 0x15);
            sub_01C03A(0xFF0FFA);
        }
    } while (U8{0xFF06D6});
    sub_0C6344();
    sub_0029DA();
}

// 0x2FA0: draws the level map (16x16 cells of 2x2 tiles from the table at
// 0x3372) and the level name.
void sub_002FA0() {
    vdp_ctrl32(0x4CE00001);
    uint32_t a0 = cur_map_ptr;
    auto cell = [](uint32_t a) { return rd32(0x3372 + sub_00E238(rd8(cell_class + rd8(a))) * 4); };
    for (int y = 0; y < 16; y++, a0 += 0x20) {
        for (int x = 0; x < 16; x++, a0 += 2) {
            for (int row = 0; row < 0x21; row += 0x20) {
                uint32_t a1 = cell(a0 + row), a2 = cell(a0 + row + 1);
                for (int i = 0; i < 4; i++, a1 += 4, a2 += 4) {
                    vdp_data(rd16(a1));
                    vdp_data(rd16(a2));
                }
            }
        }
    }
    vdp_ctrl32(0x4D180003);
    uint32_t name = 0x30B2 + uint16_t((cur_area * 16 + cur_level) * 16);
    for (int i = 0; i < 16; i++) {
        uint8_t c = rd8(name + i);
        uint16_t t;
        if (c < 0x30)
            t = 0x8000;
        else if (c <= 0x39)
            t = uint16_t(c - 0x30 + 0xE243);
        else
            t = uint16_t(uint8_t(c - 0x41) + 0xE24D);
        vdp_data(t);
    }
}

// 0x1D46: sets the lighting mode (FF0EB2), or only records it while FF105E >= 0.
// Returns d0.
uint16_t sub_001D46(uint16_t mode) {
    if (int16_t(uint16_t(U16{0xFF105E})) >= 0) {
        U16{0xFF0EB2} = mode;
        return sub_011286();
    }
    if (int16_t(uint16_t(U16{0xFF1060})) < 0) return sub_001D66(mode);
    uint16_t d0 = sub_001D66(mode);
    U16{0xFF0EB4} = 0x10;
    return d0;
}

// 0x1D66: switches to lighting mode d0 (0-4, others mean 0): wall shade
// tables FF0E8A-FF0EAA, backdrop FF0EAE, FF0EB4 and FF1096. Returns d0.
uint16_t sub_001D66(uint16_t mode) {
    if (mode > 4) mode = 0;
    if (mode == U16{0xFF0EB2}) return mode;
    U16{0xFF0EB2} = mode;
    if (mode == 0) {
        U32{0xFF0EAA} = 0x4436;
        U32{0xFF0EAE} = 0x14EE66;
        U16{0xFF0EB4} = 0x10;
        U32{0xFF1096} = 0xED50;
    } else {
        static const uint32_t backdrop[] = {0x14ED26, 0x14EDC6, 0x14EF06, 0x14EFA6};
        uint32_t shade = mode == 2 ? 0x3B92 : 0x3392;
        for (uint32_t a = 0xFF0E8A; a < 0xFF0EAA; a += 4, shade += 0x100) U32{a} = shade;
        U32{0xFF0EAA} = 0x4392;
        U32{0xFF0EAE} = backdrop[mode - 1];
        U16{0xFF0EB4} = mode == 3 ? 0xC : 5;
        U32{0xFF1096} = mode == 2 ? 0xED78 : 0xED64;
    }
    return sub_00D494();
}

// 0xD494: rebuilds the backdrop (two 80-byte rows at FF6226/FF6276) from the
// image at FF0EAE, shifted by FF0E1A. Returns d0.
uint16_t sub_00D494() {
    uint16_t d0 = U16{0xFF0E1A};
    U16{0xFF0E1E} = d0;
    const int16_t d5 = 0x28;
    uint32_t a5 = U32{0xFF0EAE};
    if (d0 == 0) {
        for (uint32_t a4 = 0xFF6226; a4 < 0xFF6226 + 0xA0; a4 += 4, a5 += 4) wr32(a4, rd32(a5));
        return 0;
    }
    int16_t n = int16_t(d0);
    if (n > 0) {
        uint32_t a4 = 0xFF6226, a3 = a5 + 0x50, a2 = a4 + 0x50;
        uint32_t saved = a5;
        n = int16_t(n + (n >> 2));
        int16_t d1 = int16_t(d5 - n);
        uint8_t d3 = rd8(a5), d4 = rd8(a3);
        for (int i = dbra_count(n); i > 0; i--) {
            wr8(a4++, d3);
            wr8(a2++, d4);
        }
        {
            int16_t d2 = int16_t(-d1);
            for (int i = dbra_count(d1); i > 0; i--) {
                wr8(a4++, rd8(a5++));
                wr8(a2++, rd8(a3++));
                d2 = int16_t(d2 + d5 - d1);
                while (d2 >= 0) {
                    a5++;
                    a3++;
                    d2 = int16_t(d2 - d1);
                }
            }
        }
        a5 = saved + 0x28;
        a3 = a5 + 0x50;
        n = int16_t(n + d5);
        int16_t d2 = int16_t(-n);
        for (int i = 0; i < 0x28; i++) {
            wr8(a4++, rd8(a5));
            wr8(a2++, rd8(a3));
            d2 = int16_t(d2 + d5);
            while (d2 >= 0) {
                a5++;
                a3++;
                d2 = int16_t(d2 - n);
            }
        }
    } else {
        a5 += 0x50;
        uint32_t a4 = 0xFF6276, a3 = a5 + 0x50, a2 = a4 + 0x50;
        uint32_t saved = a5;
        n = int16_t(-n);
        n = int16_t(n + (n >> 2));
        int16_t d1 = int16_t(d5 - n);
        uint8_t d3 = rd8(a5 - 1), d4 = rd8(a3 - 1);
        for (int i = dbra_count(n); i > 0; i--) {
            wr8(--a4, d3);
            wr8(--a2, d4);
        }
        {
            int16_t d2 = int16_t(-d1);
            for (int i = dbra_count(d1); i > 0; i--) {
                wr8(--a4, rd8(--a5));
                wr8(--a2, rd8(--a3));
                d2 = int16_t(d2 + d5 - d1);
                while (d2 >= 0) {
                    a5--;
                    a3--;
                    d2 = int16_t(d2 - d1);
                }
            }
        }
        a5 = saved - 0x29;
        a3 = a5 + 0x50;
        n = int16_t(n + d5);
        int16_t d2 = int16_t(-n);
        for (int i = 0; i < 0x28; i++) {
            wr8(--a4, rd8(a5));
            wr8(--a2, rd8(a3));
            d2 = int16_t(d2 + d5);
            while (d2 >= 0) {
                a5--;
                a3--;
                d2 = int16_t(d2 - n);
            }
        }
    }
    return uint16_t(n);
}

// 0xD49A: sub_00D494 when FF0E1A changed. Returns d0.
uint16_t sub_00D49A() {
    if (U16{0xFF0E1A} == U16{0xFF0E1E}) return U16{0xFF0E1A};
    return sub_00D494();
}

// 0x22BE: animates one of the three HUD faces per call (blinking/eye frames
// from 0x154106-0x154306, timers FF0DD2/FF0DD6/FF0DDA).
void sub_0022BE() {
    U8 turn{0xFF0DD0};
    turn = uint8_t(turn - 1);
    uint32_t a1, a0;
    uint16_t d4, d3;
    if (int8_t(uint8_t(turn)) < 0) {
        a1 = 0x154306, d4 = 0x3040, d3 = 0x3D00, a0 = 0xFF0DD2;
        turn = 2;
    } else if (turn == 0) {
        a1 = 0x154206, d4 = 0x2E40, d3 = 0x2E60, a0 = 0xFF0DD6;
    } else {
        a1 = 0x154106, d4 = 0x2960, d3 = 0x2980, a0 = 0xFF0DDA;
    }
    uint8_t t = uint8_t(rd8(a0) - 1);
    wr8(a0, t);
    if (int8_t(t) < 0) {
        wr8(a0, uint8_t(t + 8));
        wr8(a0 + 1, uint8_t(rd8(a0 + 1) + 0x40));
        wr16(a0 + 2, uint16_t(Random() >> 16) & 0x38);
    }
    a1 += rd8(a0 + 1);
    uint16_t d0 = rd8(0x2382 + (uint8_t(rd16(a0 + 2) + rd8(a0)) & 0x3F));
    a1 += d0;
    vdp_write_addr(uint16_t(d4 + d0));
    vdp_data32(rd32(a1));
    vdp_write_addr(uint16_t(d3 + d0));
    vdp_data32(rd32(a1 + 0x20));
}

// 0x23C2: runs the face animation scripts in the list at FFBCD6, writing
// words into face_desc (FF63C6).
void sub_0023C2() {
    U16{0xFF0EB6} = uint16_t(U16{0xFF0EB6} + 1);
    uint32_t a0 = 0xFFBCD6;
    while (uint16_t d3 = rd16(a0)) {
        uint8_t t = uint8_t(rd8(a0 + 2) - 1);
        wr8(a0 + 2, t);
        if (t == 0) {
            wr8(a0 + 2, rd8(a0 + 3));
            uint32_t a2 = a0 + int16_t(rd16(a0 + 4));
            for (int16_t off; (off = int16_t(rd16(a2))) >= 0; a2 += 4)
                wr16(0xFF63C6 + off, rd16(a2 + 2));
            wr16(a0 + 4, rd16(a2 + 2));
        }
        a0 += int16_t(d3);
    }
}

} // namespace game
