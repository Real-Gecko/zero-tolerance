// Title screen and its menu (0x1C9CA, 0x1CF36).
#include "game.hpp"
#include "m68k.hpp"

#include <utility>

namespace game {

namespace {

constexpr uint32_t sprite_buf = 0xFF0044; // sprite table built each frame, DMA'd to 0xB800
constexpr uint32_t hscroll_buf = 0xFF0938;
U16 menu_choice{0xFF0872}; // 0 = "press start", 1 = START, 2 = OPTIONS

void write_longs(uint32_t src, int n) {
    for (int i = 0; i < n; i++, src += 4) vdp_data32(rd32(src));
}

void zero_longs(int n) {
    for (int i = 0; i < n; i++) vdp_data32(0);
}

// 0x1D472: restarts the logo strip scroll.
void sub_01D472() {
    if (!menu_choice) menu_choice = 1;
    U16{0xFF0912} = 0;
    U16{0xFF0914} = 0;
    U16{0xFF0916} = 0;
    U16{0xFF0878} = 0;
    U16{0xFF087A} = 0;
    U32{0xFF087C} = 0x1D584;
    U32{0xFF0880} = 0x1D594;
    U32{0xFF0884} = 0x1D8F4;
    U32{0xFF0888} = 0x1D8F4;
    vdp_ctrl32(0x60000003);
    zero_longs(0xC0 * 4);
}

// Palette for one of the two flashing lights, from its timer.
uint32_t light_palette(uint16_t t) {
    int16_t d0 = int16_t(t);
    if (d0 > 0x40) d0 = 0x40;
    d0 = int16_t(d0 - 0x20);
    if (d0 < 0) d0 = int16_t(-d0);
    if (uint16_t(d0) >= 0x20) d0 = 0x1F;
    return 0x1DCD4 + ext_l(uint16_t(-(d0 << 5)));
}

// 0x1D4F6: steps the two light timers through their tables.
void sub_01D4F6() {
    for (auto [timer, table] : {std::pair{0xFF0878u, 0xFF087Cu}, std::pair{0xFF087Au, 0xFF0880u}}) {
        U16 t{timer};
        U32 p{table};
        t -= 1;
        if (int16_t(uint16_t(t)) < 0) {
            t = uint16_t(rd16(p) - 1);
            p += 2;
        }
    }
    U32{0xFF0884} = light_palette(U16{0xFF0878});
    U32{0xFF0888} = light_palette(U16{0xFF087A});
}

// 0x1D7D2: sprite palette (FF0918) for the light timers.
void sub_01D7D2() {
    for (uint32_t a = 0xFF0918; a < 0xFF0938; a += 4) wr32(a, 0);
    static const uint32_t dest[5] = {0xFF0932, 0xFF092C, 0xFF0926, 0xFF0920, 0xFF091A};
    for (uint32_t timer : {0xFF0878u, 0xFF087Au}) {
        uint16_t t = rd16(timer);
        if (t >= 0x40) continue;
        uint32_t a1 = 0x1D692 + t * 5;
        for (int i = 0; i < 5; i++) {
            uint8_t k = rd8(a1 + i);
            if (!k) continue;
            uint32_t src = 0x1D622 + (k - 1) * 8;
            wr32(dest[i], rd32(src));
            wr16(dest[i] + 4, rd16(src + 4));
        }
    }
}

// Appends n sprites from a ROM list (y, size, attr, x) with consecutive links.
uint32_t add_sprites(uint32_t a2, uint32_t src, int n, uint16_t& link) {
    for (int i = 0; i < n; i++, src += 8, a2 += 8) {
        wr16(a2, rd16(src));
        wr16(a2 + 2, uint16_t(rd16(src + 2) | link++));
        wr32(a2 + 4, rd32(src + 4));
    }
    return a2;
}

} // namespace

// 0x1CF36: returns with menu_choice set, or with demo_mode = 1 when the
// attract timer runs out.
void sub_01CF36() {
    WaitVBlank();
    vdp_ctrl(0x8124);
    vdp_ctrl32(0x40000000);
    zero_longs(0x4000);
    vdp_ctrl32(0x7C000002);
    vdp_data32(0);
    vdp_ctrl32(0x40000010);
    vdp_data32(0);
    vdp_ctrl32(0x78000002);
    vdp_data32(0);
    vdp_data32(0);
    for (int i = 0; i <= 0x12; i++) vdp_ctrl(rd16(0x1DD14 + i * 2));
    vdp_ctrl32(0xC0000000);
    write_longs(0x1DCF4, 8);
    zero_longs(0x18);
    vdp_ctrl32(0x60000003);
    zero_longs(0x800);
    vdp_ctrl32(0x40000003);
    zero_longs(0x800);
    wr32(vdp_queue, 0xFFFFFFFF);
    vdp_queue_ptr = vdp_queue;
    vdp_ctrl32(0x40000000);
    zero_longs(8);
    for (uint32_t res : {0x119A86u, 0x11C56Eu}) {
        uint32_t a0 = res + ext_l(rd16(res + 2));
        write_longs(a0, rd16(res) * 8);
    }
    for (auto [cmd, attr] : {std::pair{0x6D000003u, 0x4090}, std::pair{0x4D000003u, 0x6090}}) {
        vdp_ctrl32(cmd);
        for (uint32_t a = 0x11C576; a < 0x11C576 + 0x780 * 2; a += 2) vdp_data(uint16_t(rd16(a) + attr));
    }
    vdp_ctrl32(0x6AA00001);
    write_longs(0x123716, 0x5B0);
    write_longs(0x124DD6, 0xD0);
    U16 strip_pos{0xFF0912}, strip_tick{0xFF0914}, scroll{0xFF0916};
    strip_pos = 0;
    strip_tick = 0;
    scroll = 0;
    U16{0xFF0878} = 0;
    U16{0xFF087A} = 0;
    U32{0xFF087C} = 0x1D584;
    U32{0xFF0880} = 0x1D594;
    U32{0xFF0884} = 0x1D8F4;
    U32{0xFF0888} = 0x1D8F4;
    WaitVBlank();
    vdp_ctrl(0x8164);
    menu_choice = 0;
    U16 attract{0xFF06D8};
    attract = 0x564;
    demo_mode = 0;

    for (;;) {
        WaitVBlank();
        sub_00079A();
        vdp_ctrl32(0xC0200000);
        write_longs(0xFF0918, 8);
        write_longs(U32{0xFF0884}, 8);
        write_longs(U32{0xFF0888}, 8);
        ReadPad();
        uint16_t link = 1;
        U16{0xFF0042} = 1;
        U32{0xFF003E} = sprite_buf;
        sub_01D4F6();
        sub_01D7D2();

        // The logo strip scrolls in one column every 4 frames.
        strip_tick += 1;
        if (strip_tick == 4) {
            strip_tick = 0;
            uint16_t d2 = strip_pos;
            uint16_t d3 = uint16_t(((d2 + 0x54) & 0xFF) + 0xE000);
            strip_pos += 2;
            uint32_t a0 = 0x119B8E + d2;
            for (int row = 0; row < 12; row++, d3 = uint16_t(d3 + 0x100), a0 += 0x100) {
                vdp_write_addr(d3);
                vdp_data(d2 < 0x100 ? uint16_t(rd16(a0) + 1) : 0);
            }
        }

        // Line scroll: the top lines scroll left, the floor gets its perspective.
        scroll -= 2;
        uint32_t a0 = hscroll_buf;
        for (int i = 0; i < 0xD0; i++, a0 += 2) wr16(a0, scroll);
        auto clamp = [](uint16_t t) { return int32_t(int16_t(uint16_t((t >= 0x40 ? 0x3F : t) - 0x20))); };
        int32_t d0 = clamp(U16{0xFF0878}), d4 = clamp(U16{0xFF087A});
        int32_t d2 = int32_t(0xFFFFE9E0), d3 = d2;
        for (int i = 0; i < 0x78; i++) {
            d2 -= d0;
            d3 -= d4;
            wr16(a0, uint16_t(lo(uint32_t(d2 >> 4)) & ~1));
            wr16(a0 + 2, uint16_t(lo(uint32_t(d3 >> 4)) | 1));
            a0 += 4;
        }

        uint32_t a2 = add_sprites(sprite_buf, 0x1D5A2, 16, link);
        if (menu_choice) {
            wr16(a2, uint16_t((menu_choice - 1) * 24 + 0x112));
            wr16(a2 + 2, uint16_t(0x400 | link++));
            wr16(a2 + 4, 0x840B);
            wr16(a2 + 6, 0xFC);
            a2 = add_sprites(a2 + 8, 0x1D4D6, 4, link);
        }
        U32{0xFF003E} = a2;
        U16{0xFF0042} = link;
        sub_000744(hscroll_buf, 0xBC00, 0x1C0);
        sub_001D18();
        wr32(vdp_queue_ptr, 0xFFFFFFFF);

        attract -= 1;
        if (!attract) {
            menu_choice = 1;
            uint32_t r = Random();
            r = (r >> 16 | r << 16) & 0xFF;
            cur_area = lo(divu(r, 0x56));
            if (!cur_area) U16{0xFF271A} = 1;
            demo_mode = 1;
            break;
        }
        if (scroll == 0xFAA0) sub_01D472();
        uint8_t pressed = pad_buttons & 0xF0;
        if (pressed && (rd8(0xFF0032) & pressed) != pressed) {
            if (menu_choice) break;
            menu_choice = 1;
            continue;
        }
        auto newly = [](int bit) { return (pad_buttons >> bit & 1) && !(rd8(0xFF0032) >> bit & 1); };
        if (newly(0) && menu_choice) menu_choice = 1;
        if (newly(1) && menu_choice) menu_choice = 2;
    }

    WaitVBlank();
    vdp_ctrl(0x8124);
    sub_000BF2();
    vdp_ctrl32(0xC0000000);
    zero_longs(0x20);
    vdp_ctrl32(0x7C000002);
    vdp_data32(0);
    vdp_ctrl32(0x40000010);
    vdp_data32(0);
    vdp_ctrl32(0x78000002);
    vdp_data32(0);
    vdp_data32(0);
    WaitVBlank();
    vdp_ctrl(0x8164);
}

// 0x1C9CA: title, options, then the password check of a new game.
void sub_01C9CA() {
    for (;;) {
        if (last_sound != 3) PlaySound(3);
        sub_01CF36();
        if (menu_choice == 2) {
            sub_10B11A();
            continue;
        }
        link_mode = 0;
        if (demo_mode) {
            U8{0xFF28FD} = 0;
            link_mode = 0;
            return;
        }
        U8{0xFF28FD} = 0;
        if (sub_058A74(password) != 0xFFFF) U8{0xFF28FD} = U8{0xFF28FC};
        sub_0C62E8();
        return; // the two-console link game (0x1CA32) is not supported
    }
}

} // namespace game
