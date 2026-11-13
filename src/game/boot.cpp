#include "game.hpp"

#include "../host.hpp"
#include "../md/vdp.hpp"

#include <cstdio>
#include <cstdlib>

namespace game {

bool untranslated_throws = false;

void untranslated(uint32_t addr) {
    if (untranslated_throws) throw Untranslated{addr};
    std::fprintf(stderr, "reached untranslated code at 0x%06X (frame %u)\n", addr, host::frame_count);
    std::exit(2);
}

// 0x200: power-on hardware setup, then the region check at 0x2FA. Our console
// reports overseas NTSC, which the header ("JUE") accepts, so the
// "DEVELOPED FOR USE ONLY WITH..." screen at 0x332 is never shown.
static void power_on() {
    static const uint8_t vdp_regs[24] = {0x04, 0x14, 0x30, 0x3C, 0x07, 0x6C, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00,
                                         0x81, 0x37, 0x00, 0x01, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x80};
    rd16(VDP_CTRL);
    for (int i = 0; i < 24; i++) vdp_ctrl(uint16_t(0x8000 | i << 8 | vdp_regs[i]));
    vdp_ctrl32(0x40000080); // VRAM fill from 0
    vdp_data(0);
    for (auto& b : ram) b = 0;
    vdp_ctrl32(0x81048F02);
    vdp_ctrl32(0xC0000000);
    for (int i = 0; i < 32; i++) vdp_data32(0);
    vdp_ctrl32(0x40000010);
    for (int i = 0; i < 20; i++) vdp_data32(0);
    int_mask = 7;
}

// 0x976
static void main_init() {
    vdp_ctrl(0x8F02);
    sub_000BD2(0xFFEFFC);
    // 0xA3C: the seed sums 10000 reads of the HV counter.
    uint32_t seed = 0x82647359;
    for (int i = 0; i < 10000; i++) seed += uint32_t(int32_t(int16_t(rd16(0xC00008))));
    random_seed = host::opt.seed >= 0 ? uint32_t(host::opt.seed) : seed;
    vdp_ctrl32(0xC0000000);
    for (int i = 0; i < 32; i++) vdp_data32(0);
    vdp_ctrl32(0x40000000);
    for (int i = 0; i < 0x4000; i++) vdp_data32(0);
    while (vdp_dma_busy()) {}

    // 0x8F8: is a pad (id 0x0D) or six-button pad (id 0x04) in port 1?
    wr8(0xA10009, 0x40);
    wr8(0xA10003, 0x40);
    uint8_t d0 = rd8(0xA10003);
    wr8(0xA10003, 0x00);
    uint8_t d1 = rd8(0xA10003);
    wr8(0xA10003, 0x40);
    uint8_t id = uint8_t((d0 & 0xC ? 8 : 0) | (d0 & 3 ? 4 : 0) | (d1 & 0xC ? 2 : 0) | (d1 & 1));
    pad_present = (id == 4 || id == 0x0D) ? 1 : 0;

    sub_000BF2();
    fb_dma_pending = 0;
    sub_01BFF0();
    int_mask = 5;
    sound_init();
    PlaySound(3);
    sub_05711E();
    sub_01CDEE();
}

// 0xD98: title, story, character select and the game, until a full restart.
static void main_loop() {
    for (;;) {
        uint16_t s = last_sound;
        if (s <= 2 || s == 0x88) sub_0C62E8();
        link_mode = 0;
        U16{0xFF271A} = 1;
        cur_area = 0;
        sub_01C9CA();
        if (!demo_mode) {
            uint16_t story = 0;
            bool show = true;
            if (U8{0xFF28FD}) {
                story = 1;
                if (U8{0xFF28FD} != 0x10) {
                    story = 2;
                    if (U8{0xFF28FD} != 0x20) show = false;
                }
            }
            if (show) {
                sub_0CAA4E(story, 0x13298);
                sub_000BF2();
            }
            sub_000FC0();
            U16{0xFF1030} = 0;
            sub_0C6362(0x13298);
            GameLevelLoop();
            return;
        }
        // Attract mode: the demo story screen, then a recorded game.
        uint16_t d7 = sub_0CAA4E(3, 0x13298);
        sub_000BF2();
        if (d7) continue;
        uint16_t area = cur_area;
        sub_000FC0();
        level_id = uint8_t(area << 4);
        uint32_t r = Random();
        U16{0xFF1030} = uint16_t((r & 0x400) / 0xCD);
        uint16_t link = link_mode;
        link_mode = 0;
        sub_0C5DAC();
        uint16_t result = GameLevelLoop();
        link_mode = link;
        if (result == 1) return;
    }
}

[[noreturn]] void Reset() {
    power_on();
    // 0x690
    if (!warm_boot) {
        sub_000BD2(0xFFEFFC);
        warm_boot = 0xFFFF;
        for (int i = 0; i < 9; i++) wr8(password + i, 0x5F);
        wr8(password + 9, 0);
        sound_options = 0x0F;
        U16{0xFF0016} = 0;
    }
    host::set_vblank_handler(VBlank);
    for (;;) {
        main_init();
        main_loop();
    }
}

// 0xA5C: returns the new seed (d2).
uint32_t Random() {
    uint32_t d0 = random_seed;
    uint32_t d2 = (d0 & 0xFFFF) * 0xE673u;
    uint32_t d1 = ((d0 >> 16) * 0xE673u) << 16;
    d2 += d1;
    d2 += ((d0 & 0xFFFF) * 0xBB40u) << 16;
    d2 += 1;
    random_seed = d2;
    return d2;
}

// 0xAA2
void VBlank() {
    vblank_count += 1;
    U16{0xFF2916} += 1;
    U16{0xFF08CC} -= 1;
    if (U16{0xFF2718}) U16{0xFF2718} -= 1;
    if (!fb_dma_pending) return;
    vdp_ctrl(0x8174);
    fb_dma_pending -= 1;
    if (fb_dma_pending == 0) {
        // Second half of the 3D view: 0xA00 words from FF4D26 to VRAM 0x1420.
        for (uint16_t r : {0x9300, 0x940A, 0x9593, 0x96A6, 0x977F, 0x5420, 0x0080}) vdp_ctrl(r);
    } else {
        // First half: FF3926 to VRAM 0x0020.
        for (uint16_t r : {0x9300, 0x940A, 0x9593, 0x969C, 0x977F, 0x4020, 0x0080}) vdp_ctrl(r);
        U16 flash{0xFF1072};
        if (flash) {
            if (U16{0xFF0DE2}) {
                if (flash > 0xF) flash -= 0x110;
                else {
                    flash -= 1;
                    if (int16_t(uint16_t(flash)) < 0) flash = 0;
                }
            } else {
                flash -= 3;
                if (int16_t(uint16_t(flash)) < 0) flash = 0;
            }
            vdp_ctrl32(0xC07E0000);
            vdp_data(flash);
        }
    }
    vdp_ctrl(0x8164);
    vdp_ctrl32(0x73600002);
}

// 0x744: queues a 68k -> VRAM DMA (len words from src to dest) for sub_00079A.
void sub_000744(uint32_t src, uint16_t dest, uint16_t len) {
    uint32_t q = vdp_queue_ptr;
    auto put = [&](uint16_t v) { wr16(q, v); q += 2; };
    put(uint16_t(0x9300 | (len & 0xFF)));
    put(uint16_t(0x9400 | (len >> 8 & 0xFF)));
    src >>= 1;
    put(uint16_t(0x9500 | (src & 0xFF)));
    put(uint16_t(0x9600 | (src >> 8 & 0xFF)));
    put(uint16_t(0x9700 | (src >> 16 & 0x7F)));
    put(uint16_t((dest & 0x3FFF) + 0x4000));
    put(uint16_t((dest >> 14) | 0x80));
    vdp_queue_ptr = q;
}

// 0x79A: runs the queued DMAs.
void sub_00079A() {
    uint32_t q = vdp_queue;
    vdp_ctrl(0x8174);
    int saved = int_mask;
    int_mask = 7;
    while (rd32(q) != 0xFFFFFFFF) {
        vdp_ctrl32(rd32(q));
        vdp_ctrl32(rd32(q + 4));
        vdp_ctrl32(rd32(q + 8));
        vdp_ctrl(rd16(q + 12));
        q += 14;
        while (vdp_dma_busy()) {}
        vdp_ctrl32(0x00000020);
    }
    vdp_queue_ptr = vdp_queue;
    int_mask = saved;
    vdp_ctrl(0x8164);
}

// 0x804: reads the pad in port 1 into pad_buttons / pad_extra.
void ReadPad() {
    if (!pad_present) return;
    const uint32_t port = 0xA10003;
    pad_prev = U16{0xFF002E};
    wr8(port, 0x40);
    uint8_t d0 = rd8(port);
    wr8(port, 0);
    uint8_t d1 = rd8(port);
    wr8(port, 0x40);
    uint8_t d2 = rd8(port);
    wr8(port, 0);
    d2 = rd8(port);
    wr8(port, 0x40);
    d2 = rd8(port);
    wr8(port, 0);
    uint8_t d3 = rd8(port);
    wr8(port, 0x40);
    uint16_t d4 = rd8(port);
    wr8(port, 0);
    d2 = rd8(port);
    wr8(port, 0x40);
    if (((d3 & 0xF) ^ (d2 & 0xF)) == 0xF) d4 &= ~0x10; // six-button pad: d4 holds X/Y/Z/Mode
    else d4 = 0xFF;
    uint16_t w1 = uint16_t(d1 << 2);
    d0 = rd8(port);
    if (w1 & 0x30) w1 = 0xFF;
    uint16_t buttons = uint16_t((d0 & 0x3F) | (w1 & 0xC0));
    pad_buttons = uint8_t(~buttons);
    pad_extra = uint8_t(~d4);
}

// 0xBAE: waits for the 3D view DMA, then writes count+1 words to CRAM from src.
void sub_000BAE(uint32_t src, int count) {
    while (fb_dma_pending) host::frame();
    int saved = int_mask;
    int_mask = 7;
    vdp_ctrl32(0xC0000000);
    for (int i = 0; i <= count; i++, src += 2) vdp_data(rd16(src));
    int_mask = saved;
}

// 0xBD2: clears RAM from FF0018 up to the stack pointer, and FFF000-FFFFFF.
void sub_000BD2(uint32_t stack_top) {
    for (uint32_t a = 0xFF0018; a < stack_top; a += 2) wr16(a, 0);
    for (uint32_t a = 0xFFF000; a < 0x1000000; a += 4) wr32(a, 0);
}

// 0xBF2: VDP registers for the game screens.
void sub_000BF2() {
    uint32_t t = link_sr == 0x2100 ? 0x2298 : 0x2272;
    for (int i = 0; i <= 0x12; i++) vdp_ctrl(rd16(t + i * 2));
    vdp_ctrl32(0x78000002);
    vdp_data32(0);
    vdp_data32(0);
    vdp_ctrl32(0x7C000002);
    vdp_data32(0);
    vdp_ctrl32(0x40000010);
    vdp_data32(0);
}

// 0xC5A
void sub_000C5A() {
    vdp_ctrl32(0x40000003);
    for (int i = 0; i <= 0x6FF; i++) vdp_data(0);
    vdp_ctrl32(0x7C000002);
    vdp_data(0);
    vdp_data(0);
    vdp_ctrl32(0x40000010);
    vdp_data(0);
    vdp_data(0);
}

// 0xCAA
void sub_000CAA() {
    vdp_ctrl32(0x40000000);
    for (int i = 0; i < 8; i++) vdp_data32(0);
    vdp_ctrl32(0x60000003);
    for (int i = 0; i <= 0x7FF; i++) vdp_data(0x8000);
    vdp_ctrl32(0x7C000002);
    vdp_data(0);
    vdp_data(0);
    vdp_ctrl32(0x40000010);
    vdp_data(0);
    vdp_data(0);
}

// 0xD36: reads the pad, or replays/records the demo input (demo_mode 1 / 2).
void sub_000D36() {
    uint16_t mode = demo_mode;
    if (!mode) {
        ReadPad();
        return;
    }
    U16 left{0xFF06DA};
    U32 ptr{0xFF06DE};
    if (mode != 1) {
        left -= 1;
        if (!left) U16{0xFF2722} = 1;
        ReadPad();
        wr8(ptr, pad_buttons);
        ptr += 1;
        return;
    }
    left -= 1;
    if (!left) U16{0xFF2722} = 1;
    ReadPad();
    if (pad_buttons & 0x80) {
        U16{0xFF2722} = 1;
        demo_mode = 3;
    }
    pad_buttons = rd8(ptr);
    ptr += 1;
}

// 0xFC0: new game: health, lives and inventory, then the password.
void sub_000FC0() {
    U8{0xFF2732} = 0;
    level_id = 0;
    U8{0xFF28FB} = 0x64;
    for (uint32_t a = 0xFF28EC; a <= 0xFF28F0; a++) wr8(a, 1);
    for (uint32_t a = 0xFF28F1; a <= 0xFF28FA; a++) wr8(a, 0);
    if (demo_mode) return;
    sub_058A74(password);
    level_id = U8{0xFF28FD};
}

} // namespace game

namespace game {

// 0x1D18: terminates the sprite list built at FF0044 and queues its DMA to 0xB800.
void sub_001D18() {
    uint32_t a0 = U32{0xFF003E};
    if (a0 != 0xFF0044) wr8(a0 - 5, 0);
    else {
        wr32(a0, 0);
        wr32(a0 + 4, 0);
        a0 += 8;
    }
    sub_000744(0xFF0044, 0xB800, uint16_t((a0 - 0xFF0044) >> 1));
}

} // namespace game
