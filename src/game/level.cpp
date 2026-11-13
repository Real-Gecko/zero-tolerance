// The game: level start, the gameplay loop and what happens on death or
// when an area is finished (GameLevelLoop 0x102C).
#include "game.hpp"
#include "m68k.hpp"

#include "../host.hpp"

namespace game {

namespace {

constexpr uint32_t area_blobs[3] = {0x15A10A, 0x160424, 0x16602C};
constexpr uint32_t demo_inputs[3] = {0x1CF338, 0x1CF408, 0x1CF4D8};

void clear_cram() {
    vdp_ctrl32(0xC0000000);
    for (int i = 0; i < 0x20; i++) vdp_data32(0);
}

// Copies the area blob into RAM (layout in docs/RE_NOTES.md).
void load_area() {
    uint32_t a0 = area_blobs[cur_area == 0 ? 0 : cur_area == 1 ? 1 : 2];
    auto copy = [&](uint32_t dest, uint32_t n) {
        for (uint32_t i = 0; i < n; i++) wr8(dest + i, rd8(a0++));
    };
    copy(0xFF63C6, 0x1000);
    copy(0xFF73C6, 0x800);
    copy(cell_class, 0x100);
    copy(0xFF7CD6, 0x4000);
    copy(0xFF7CC6, 0x10);
    uint32_t a1 = 0xFFBCD6;
    for (;;) {
        uint16_t len = rd16(a0);
        a0 += 2;
        wr16(a1, len);
        a1 += 2;
        if (!len) break;
        for (int i = 0; i <= int16_t(len - 3); i++) wr8(a1++, rd8(a0++));
    }
    // FF62C6: for each class, the first cell value that has it.
    for (int cls = 0xFF; cls >= 0; cls--) {
        uint8_t v = 0;
        while (rd8(cell_class + v) != cls) {
            v++;
            if (!v) break;
        }
        wr8(0xFF62C6 + cls, v);
    }
}

// Player start: the cell of class 0x77, or cell 0x288.
void place_player() {
    uint32_t a0 = cur_map_ptr;
    uint16_t d0 = 0;
    for (; d0 < 0x400; d0++)
        if (rd8(cell_class + rd8(a0 + d0)) == 0x77) break;
    if (d0 == 0x400) d0 = 0x288;
    player_x = uint16_t(((d0 & 0x1F) << 8) + 0x80);
    player_y = uint16_t(((d0 << 3) & 0xFF00) + 0x80);
}

// State shared by the level start (0x11C8) and a respawn (0x1848).
void reset_view() {
    wr32(vdp_queue, 0xFFFFFFFF);
    vdp_queue_ptr = vdp_queue;
    player_angle = 0;
    for (uint32_t a : {0xFF0E1A, 0xFF0E1E, 0xFF0E1C, 0xFF0E20, 0xFF0E24, 0xFF0E7E, 0xFF0E80, 0xFF0E82, 0xFF0E84,
                       0xFF0DF0, 0xFF0DF2})
        wr16(a, 0);
}

void demo_setup() {
    U16{0xFF06D8} = 0;
    U16{0xFF06DA} = 0xC8;
    uint32_t a0 = demo_inputs[cur_area == 0 ? 0 : cur_area == 1 ? 1 : 2];
    if (demo_mode == 1) {
        random_seed = rd32(a0);
        a0 += 4;
    } else if (demo_mode == 2) {
        wr32(a0, random_seed);
        a0 += 4;
    }
    U32{0xFF06DE} = a0;
    U16{0xFF0E00} = rd16(0x8124 + player_angle * 4);
    U16{0xFF0E02} = rd16(0x8126 + player_angle * 4);
    U16{0xFF0E22} = 0;
    U16{0xFF0DE0} = 0;
    U16{0xFF0DDE} = 0;
    U16{0xFF2726} = 0;
}

void show_view(bool level_start) {
    clear_cram();
    fb_dma_pending = 2;
    if (level_start) sub_001A0C();
    WaitVBlank();
    WaitVBlank();
    if (level_start) U16{0xFF06D4} = 0;
    vdp_ctrl(0x8164);
    sub_001C24();
}

// Writes the backdrop scroll: hscroll of plane A from the angle, vscroll
// from the level and the view tilt.
void stream_backdrop(uint16_t d3);

void write_scroll(uint16_t angle_scroll, bool with_h, int stream) {
    WaitVBlank();
    int saved = int_mask;
    int_mask = 7;
    if (with_h) {
        vdp_ctrl32(0x7C000002);
        vdp_data(uint16_t(angle_scroll + 0x10));
    }
    uint16_t v = uint16_t(cur_level * 4 + (int16_t(uint16_t(U16{0xFF116E})) >> 5) - 0x20);
    vdp_ctrl32(0x40000010);
    vdp_data(v);
    if (stream >= 0) stream_backdrop(uint16_t(stream));
    int_mask = saved;
}

// Streams 4 columns of the backdrop map (FF271E) into plane A when the
// view turns past an 8-pixel column.
void stream_backdrop(uint16_t d3) {
    vdp_ctrl(0x8F80);
    for (int i = 0; i < 4; i++, d3 = uint16_t(d3 + 2)) {
        uint16_t d4 = uint16_t(d3 + U16{0xFF0DDE});
        uint32_t a0 = U32{0xFF271E} + (d4 & 0xFF);
        vdp_write_addr(uint16_t((d4 & 0x7F) + 0xE000));
        for (int k = 0; k < 16; k++) vdp_data(rd16(a0 + k * 0x100));
    }
    vdp_ctrl(0x8F02);
}

} // namespace

// 0xE78: the alive flags and inventory carried to the next area.
void sub_000E78() {
    static const uint32_t from[10] = {0xFF1037, 0xFF1038, 0xFF103B, 0xFF103C, 0xFF103F,
                                      0xFF1040, 0xFF1043, 0xFF1044, 0xFF1047, 0xFF1048};
    static const uint32_t to[10] = {0xFF28F1, 0xFF28F6, 0xFF28F2, 0xFF28F7, 0xFF28F3,
                                    0xFF28F8, 0xFF28F4, 0xFF28F9, 0xFF28F5, 0xFF28FA};
    for (int i = 0; i < 10; i++) wr8(to[i], rd8(from[i]));
}

// 0xEB6: inventory from the carried weapons.
void sub_000EB6() {
    sub_0109E6();
    for (int i = 0; i < 5; i++) {
        U16{0xFF1034} = 0;
        wr8(0xFF1034, rd8(0xFF28F6 + i));
        uint8_t w = rd8(0xFF28F1 + i);
        if (w) sub_011084(w);
    }
    U16{0xFF1034} = 0;
}

// 0xF2A: the character's starting equipment.
void loc_000F2A() {
    if (cur_area == 1 && cur_level == 0xF) {
        if (!link_mode) return;
        for (uint32_t a = 0xFFB8D6; a < 0xFFB8D6 + 0x400; a++) {
            if (rd8(cell_class + rd8(a)) == 0x6A) {
                wr8(a, 0);
                return;
            }
        }
        return;
    }
    uint32_t a0 = 0xF98 + U16{0xFF1030} * 8;
    for (int i = 0; i < 2; i++) {
        uint16_t w = rd16(a0);
        if (!w) break;
        U16{0xFF1034} = rd16(a0 + 2);
        a0 += 4;
        sub_011084(w);
    }
    U16{0xFF1034} = 0;
}

// 0x1A0C: hides the sprite table.
void sub_001A0C() {
    int saved = int_mask;
    int_mask = 7;
    vdp_ctrl32(0x78000002);
    vdp_data32(0);
    vdp_data32(0);
    int_mask = saved;
}

// 0x1C24: fades the game palette in.
void sub_001C24() {
    for (uint32_t a = 0xFF06F6; a < 0xFF0776; a += 4) wr32(a, 0);
    bool goggles = U16{0xFF105E} != 0xFFFF;
    bool b = U16{0xFF271A};
    U32{0xFF07F8} = goggles ? (b ? 0x2172 : 0x2072) : (b ? 0x21F2 : 0x20F2);
    wr8(0xFF07FC, 0);
    U16{0xFF07F6} = 0;
    do {
        WaitVBlank();
        if (!U16{0xFF07F6}) {
            vdp_ctrl32(0xC0000000);
            for (uint32_t a = 0xFF06F6; a < 0xFF0776; a += 4) vdp_data32(rd32(a));
        }
        sub_01CC04(64);
    } while (U16{0xFF07F6} != 2);
}

// 0x1CBC: fades the game palette out.
void sub_001CBC() {
    U32{0xFF07F8} = 0x1CCEE;
    wr8(0xFF07FC, 0);
    U16{0xFF07F6} = 0;
    do {
        WaitVBlank();
        if (!U16{0xFF07F6}) {
            vdp_ctrl32(0xC0000000);
            for (uint32_t a = 0xFF06F6; a < 0xFF0776; a += 4) vdp_data32(rd32(a));
        }
        sub_01CC04(64);
    } while (U16{0xFF07F6} != 2);
}

// One pass of the gameplay loop (0x1442-0x1784). Returns where the original
// continues: 0x1442 (next frame), 0x1788 (dead) or 0x1A40 (level left).
uint32_t gameplay_frame() {
    if (!U16{0xFF272C} && !(U16{0xFF0EB6} & 0x3F)) sub_057E0C();
    if (!U16{0xFF271A}) sub_0022BE();
    sub_01E0FA();
    fb_dma_pending = 2;

    uint16_t d2 = uint16_t((-player_angle * 2) & 0x3FF);
    if (d2 == U16{0xFF0E22}) {
        write_scroll(d2, false, -1);
    } else {
        int16_t d1 = int16_t(U16{0xFF0E22} - d2);
        U16{0xFF0E22} = d2;
        d1 = int16_t(int16_t(d1 << 6) >> 6);
        U16 fine{0xFF0DE0}, col{0xFF0DDE};
        U16{0xFF107E} -= uint16_t(d1 * 2);
        fine += uint16_t(d1);
        int stream = -1;
        if (d1 >= 0) {
            if (int16_t(uint16_t(fine)) >= 0x20) {
                fine -= 0x20;
                col += 8;
                stream = 0x48;
            }
        } else if (int16_t(uint16_t(fine)) < 0) {
            fine += 0x20;
            col -= 8;
            stream = 0;
        }
        write_scroll(d2, true, stream);
    }

    U16 fade{0xFF06D4};
    if (fade) {
        fade -= 1;
        if (!fade) vdp_ctrl(0x8164);
    }
    sub_00079A();
    sub_000D36();
    U16{0xFF0042} = 1;
    U32{0xFF003E} = 0xFF0044;
    sub_0023C2();
    if ((pad_buttons & 0x80) && !(rd8(0xFF0032) & 0x80)) wr8(0xFF06D6, rd8(0xFF06D6) | 1);
    if (U16{0xFF06D6}) {
        sub_002CEC();
        U16{0xFF2916} = 0;
    }
    sub_00DAB8();
    sub_0131E8();
    sub_00EC1C();
    sub_00D49A();
    sub_00B9A6();
    DrawRadar();
    sub_00ECE2();
    sub_00ECC0();
    sub_00B35C();
    sub_013182();
    sub_001D18();
    wr32(vdp_queue_ptr, 0xFFFFFFFF);

    U16 exit_timer{0xFF2724};
    if (exit_timer) {
        exit_timer -= 1;
        if (!exit_timer) U16{0xFF2722} = 1;
    }
    if (U16{0xFF2722}) return 0x1A40;
    U16 dying{0xFF0DE2};
    if (U16{0xFF0DE4} <= 1) {
        dying += 1;
        if (dying == 1) U16{0xFF1072} = 0xFFF;
    }
    return dying < 0x1E ? 0x1442 : 0x1788;
}

uint16_t GameLevelLoop() {
restart:
    for (uint32_t a : {0xFF2724, 0xFF272A, 0xFF272C, 0xFF272E}) wr16(a, 0);
    U16{0xFF2730} = 0xFFFF;
    U8{0xFF26F0} = 0;
    U8{0xFF26F1} = 0;
    U8{0xFF2726} = 0x65;
    U16{0xFF2728} = 0;
    U8{0xFF1074} = 0;
    U8{0xFF1077} = 3;
    U16{0xFF271A} = 0;
    cur_area = uint16_t((uint8_t(level_id) >> 4) & 3);
    if (cur_area != 1) U16{0xFF271A} = 1;
    uint16_t lvl = level_id & 0xF;
    cur_level = lvl;
    U16{0xFF272A} = lvl;
    // Passwords of the levels before the start one are "*SECURED*".
    uint32_t a0 = 0xFF273C;
    for (uint16_t i = lvl; i; i--)
        for (char c : {'*', 'S', 'E', 'C', 'U', 'R', 'E', 'D', '*'}) wr8(a0++, uint8_t(c));
    load_area();
    for (uint16_t l = 0; l != cur_level; l++) sub_00A9CE(l);
    clear_cram();
    U16{0xFF0DE4} = 0;
    U8{0xFF0DE5} = U8{0xFF28FB};
    U16{0xFF0DE2} = 0;
    sub_0109E6();
    PlaySound(cur_area);
    sub_000BF2();
    vdp_ctrl(0x8124);
    // 0xA3C: reseeds from the HV counter.
    uint32_t seed = 0x82647359;
    for (int i = 0; i < 10000; i++) seed += uint32_t(int32_t(int16_t(rd16(0xC00008))));
    random_seed = seed;
    U32{0xFF271E} = U16{0xFF271A} ? 0x1553E6 : 0x153106;
    reset_view();
    U32{0xFF0EC2} = 0xFF0EC6;
    demo_setup();
    cur_map_ptr = 0xFF7CD6 + uint32_t(cur_level) * 0x400;
    sub_0133A4();
    sub_00B8FC(cur_level);
    sub_0133A4();
    U16{0xFF1072} = 0;
    U16{0xFF116E} = 0;
    U16{0xFF1170} = 0;
    sub_01E0DE();
    place_player();
    U8{0xFF06D6} = 0;
    for (int i = 0; i < 5; i++) {
        uint8_t w = rd8(0xFF28F1 + i);
        if (!w) continue;
        U16{0xFF1034} = uint16_t(rd8(0xFF28F6 + i) << 8);
        sub_011084(w);
    }
    U16{0xFF1034} = 0;
    loc_000F2A();
    for (uint32_t a = 0xFF1052; a < 0xFF1057; a++) wr8(a, 0);
    U32{0xFF0E16} = 0;
    U16{0xFF0EB8} = 1;
    sub_0029DA();
    sub_00B9A6();
    DrawRadar();
    show_view(true);
    U16{0xFF2916} = 0;

    uint16_t last = vblank_count;
    for (;;) {
        // The original draws as fast as the 68000 allows, about 5 VBlanks
        // per frame; the port draws instantly, so it waits for that pace.
        while (uint16_t(vblank_count - last) < 5) host::frame();
        last = vblank_count;
        uint32_t next = gameplay_frame();
        if (next == 0x1442) continue;
        if (next == 0x1A40) break;

        // 0x1788: dead. Enemies forget the player, the character is lost.
        uint16_t n = U16{0xFF24E8};
        uint32_t e = U32{0xFF24EA};
        for (int i = 0; i < n; i++, e = rd32(e))
            if ((U16{0xFF8004} & 0x14) == 0x14) wr8(e + 0x35, 1);
        sub_0C62E8();
        wr8(0xFF28EC + U16{0xFF1030}, 0);
        if (!(U8{0xFF28EC} | U8{0xFF28ED} | U8{0xFF28EE} | U8{0xFF28EF} | U8{0xFF28F0})) {
            // 0x19D8: everyone is dead.
            sub_0CAA4E(5, 0x13298);
            sub_000BF2();
            return 0;
        }
        sub_000E78();
        sub_0C6362(0x13298);
        clear_cram();
        PlaySound(cur_area);
        sub_000BF2();
        vdp_ctrl(0x8124);
        uint32_t s = 0x82647359;
        for (int i = 0; i < 10000; i++) s += uint32_t(int32_t(int16_t(rd16(0xC00008))));
        random_seed = s;
        U16{0xFF0DE4} = 0x64;
        sub_0149DE();
        reset_view();
        demo_setup();
        U16{0xFF0DD0} = 0;
        U32{0xFF0DD2} = 0;
        U32{0xFF0DD6} = 0;
        U32{0xFF0DDA} = 0;
        U16{0xFF1072} = 0;
        U16{0xFF116E} = 0;
        U16{0xFF1170} = 0;
        sub_01E0DE();
        place_player();
        sub_000EB6();
        loc_000F2A();
        sub_0029DA();
        sub_00B9A6();
        DrawRadar();
        U16{0xFF0DE2} = 0;
        show_view(false);
        for (uint32_t a = 0xFF1052; a < 0xFF1057; a++) wr8(a, 0);
        U16{0xFF2916} = 0;
        last = vblank_count;
    }

    // 0x1A40: the level exit was reached.
    U16{0xFF2722} = 0;
    sub_001CBC();
    if (demo_mode == 3) {
        sub_0C62E8();
        sub_0168AC();
        return 2;
    }
    if (demo_mode) {
        sub_0C62E8();
        sub_0168AC();
        return 1;
    }
    sub_057F98();
    sub_0C62E8();
    uint16_t story = cur_area == 0 ? 1 : cur_area == 1 ? 2 : 4;
    sub_0CAA4E(story, 0x13298);
    sub_000BF2();
    uint16_t next = uint16_t(cur_area + 1);
    if (next == 3) return 0; // game finished: back to the boot screens
    cur_area = next;
    level_id = uint8_t(next << 4);
    U8{0xFF28FB} = U8{0xFF0DE5};
    sub_000E78();
    goto restart;
}

} // namespace game
