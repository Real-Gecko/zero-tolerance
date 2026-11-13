// HUD: message box, enemy counter and health bar (0x1E00E-0x1E330, 0xD98E).
#include "game.hpp"

#include "../host.hpp"

namespace game {

namespace {
U16 msg_count{0xFF090E}; // queued messages
U16 msg_timer{0xFF0910}; // frames left for the shown message
constexpr uint32_t msg_queue = 0xFF08CE; // 16 pointers
constexpr uint32_t speech = 0xFF088C;   // (u16 time, u16 sound) list ending in 0xFFFF
}

// 0x1E00E: two-digit enemy counter.
void sub_01E00E(uint16_t n) {
    while (fb_dma_pending) host::frame();
    uint16_t tens = 0;
    while (n >= 10) {
        tens++;
        n = uint16_t(n - 10);
    }
    vdp_ctrl32(0x44820003);
    vdp_data(uint16_t(tens + 0xE58E));
    vdp_data(uint16_t(n + 0xE58E));
}

// 0x1E0C2: queues a message (4 lines of 9 characters, then a speech list).
void sub_01E0C2(uint32_t msg) {
    uint16_t n = msg_count;
    if (n == 0x10) return;
    msg_count += 1;
    wr32(msg_queue + n * 4, msg);
}

// 0x1E0DE
void sub_01E0DE() {
    msg_count = 0;
    msg_timer = 0;
    U16{0xFF088C} = 0xFFFF;
    U16{0xFF08CC} = 0;
    U16{0xFF2716} = 0;
    U16{0xFF2718} = 0;
}

// 0x1E2CC: plays the speech samples of the shown message one after another
// (FF08CC is counted down by VBlank).
void sub_01E2CC() {
    if (!U16{0xFF2716}) {
        U16{0xFF088C} = 0xFFFF;
        return;
    }
    if (U16{0xFF088C} == 0xFFFF || int16_t(uint16_t(U16{0xFF08CC})) >= 0) return;
    for (uint32_t a = speech; a < speech + 0x1C; a += 4) wr32(a, rd32(a + 4));
    if (U16{0xFF088C} == 0xFFFF) {
        U16{0xFF2716} = 0;
        return;
    }
    U16{0xFF08CC} = U16{0xFF088C};
    sub_00D760(U16{0xFF088E});
}

// 0x1E0FA: level-cleared countdown and the message box.
void sub_01E0FA() {
    U16 cleared{0xFF272C};
    if (cleared == 0x69) {
        cleared -= 1;
        sub_01E0C2(0x1E7EA);
    } else if (cleared) {
        cleared -= 1;
        if (!cleared) {
            cleared = 0xE1;
            sub_01E0C2(0x1E64C);
        }
    }
    sub_01E2CC();
    if (msg_timer) {
        int16_t left = int16_t(msg_timer - msg_count - 1);
        if (left > 0) {
            msg_timer = uint16_t(left);
            return;
        }
        msg_timer = 0;
        for (uint32_t cmd : {0x4A040003u, 0x4A840003u, 0x4B040003u, 0x4B840003u}) {
            vdp_ctrl32(cmd);
            for (int i = 0; i < 4; i++) vdp_data32(0xE573E573);
            vdp_data(0xE573);
        }
        return;
    }
    if (!msg_count) return;
    uint32_t text = rd32(msg_queue);
    for (uint32_t a = msg_queue; a < msg_queue + 0x3C; a += 4) wr32(a, rd32(a + 4));
    msg_count -= 1;
    msg_timer = 0x14;
    while (fb_dma_pending) host::frame();
    for (uint32_t cmd : {0x4A040003u, 0x4A840003u, 0x4B040003u, 0x4B840003u}) {
        vdp_ctrl32(cmd);
        for (int i = 0; i < 9; i++) vdp_data(rd16(0x1E04C + (uint8_t(rd8(text++) - 0x20) << 1)));
    }
    if (!(rd8(0xFF0015) & 2) || U16{0xFF2716}) return;
    if (rd16(text) == 0xFFFF) {
        U16{0xFF2716} = 0;
        return;
    }
    uint32_t dst = speech;
    do {
        wr32(dst, rd32(text));
        dst += 4;
        text += 4;
    } while (rd16(text) != 0xFFFF);
    wr16(dst, 0xFFFF);
    U16{0xFF2716} = 1;
    U16{0xFF08CC} = U16{0xFF088C};
    sub_00D760(U16{0xFF088E});
}

// 0xD98E: health bar tile, coloured by the amount left.
void sub_00D98E() {
    while (fb_dma_pending) host::frame();
    vdp_ctrl32(0x04CA0003);
    uint16_t tile = rd16(0xC00000);
    vdp_write_addr(uint16_t((tile & 0x7FF) << 5));
    int16_t h = int16_t(uint16_t(U16{0xFF0DE4}));
    if (h < 0) {
        h = 0;
        U16{0xFF0DE4} = 0;
    } else if (h > 0x64) {
        h = 0x64;
        U16{0xFF0DE4} = 0x64;
    }
    int32_t d0 = h * 0x64 - 1;
    if (d0 < 0) d0 = 0;
    uint16_t pct = uint16_t(uint32_t(d0) / 0x64);
    uint32_t d1 = pct > 0x50 ? 0x77777777 : pct > 0x3C ? 0x76767676 : pct > 0x28 ? 0x66666666
                : pct > 0x14 ? 0x65656565 : 0x55555555;
    uint32_t d3 = d1 >> 4 | d1 << 28;
    uint32_t a0 = 0x16E618;
    uint16_t ones = pct;
    while (ones >= 10) {
        ones = uint16_t(ones - 10);
        a0 += 0x20;
    }
    for (int i = 0; i < 8; i++, a0 += 4) vdp_data32(rd32(a0) | (i & 1 ? d3 : d1));
    a0 = 0x16E618 + (uint32_t(ones) << 5);
    for (int i = 0; i < 8; i++, a0 += 4) vdp_data32(rd32(a0) | (i & 1 ? d3 : d1));
}

} // namespace game
