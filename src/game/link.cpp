#include "game.hpp"

namespace game {

// 0x1BFF0: two-console link over port 2 off; pads only.
void sub_01BFF0() {
    U16{0xFF291E} = 0;
    U16{0xFF2920} = 0;
    U16{0xFF2922} = 0;
    U16{0xFF2924} = 0;
    U16{0xFF2918} = 0x8000;
    U16{0xFF291A} = 0;
    link_sr = 0x2500;
    int_mask = 5;
    vdp_ctrl(0x8B00);
    wr8(0xA1000B, 0x20);
    wr8(0xA10005, uint8_t(rd8(0xA10005) | 0x20));
}

// 0x1C03A: queues the link packet at a0 (type byte, then words) in the ring
// buffer FF3126; a full buffer drops the rest of the packet.
void sub_01C03A(uint32_t a0) {
    uint16_t d0 = U16{0xFF2924};
    uint8_t len = rd8(0x1C3F4 + rd8(a0));
    wr16(0xFF3126 + d0, rd16(a0));
    a0 += 2;
    if (!(len & 0x80)) {
        for (int i = 0; i <= len; i++) {
            d0 = (d0 + 2) & 0x7FF;
            if (d0 == U16{0xFF2922}) return;
            wr16(0xFF3126 + d0, rd16(a0));
            a0 += 2;
        }
    }
    d0 = (d0 + 2) & 0x7FF;
    if (d0 == U16{0xFF2922}) return;
    U16{0xFF2924} = d0;
}

// 0x1C9A0: sends packet 3 (a1's first byte and address) in a link game.
void sub_01C9A0(uint32_t a1) {
    if (!link_mode) return;
    wr8(0xFF0FFA, 3);
    wr8(0xFF0FFB, rd8(a1));
    wr32(0xFF0FFC, a1);
    sub_01C03A(0xFF0FFA);
}

} // namespace game
