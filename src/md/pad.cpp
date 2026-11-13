#include "pad.hpp"

namespace md::pad {

struct Port {
    uint16_t held = 0;
    bool th = true;
    int step = 0;
};
static Port ports[2];

void set_buttons(int port, uint16_t held) { ports[port].held = held; }

void new_frame() {
    for (auto& p : ports) p.step = 0;
}

void write(int port, uint8_t v) {
    Port& p = ports[port];
    bool th = v & 0x40;
    if (th != p.th) p.step++;
    else if (th) p.step = 0; // a read sequence starts with TH already high; the real pad resets on a timeout
    p.th = th;
}

uint8_t read(int port) {
    const Port& p = ports[port];
    uint16_t h = p.held;
    // Bits read as 0 while a button is pressed.
    uint8_t v;
    if (p.th) {
        uint8_t pressed = p.step == 6
            ? uint8_t((h & Z ? 1 : 0) | (h & Y ? 2 : 0) | (h & X ? 4 : 0) | (h & MODE ? 8 : 0) | (h & (B | C)))
            : uint8_t(h & 0x3F);
        v = uint8_t((~pressed & 0x3F) | 0x40);
    } else {
        uint8_t as = uint8_t(~(h & (A | START)) >> 2 & 0x30);
        if (p.step == 5) v = as;
        else if (p.step == 7) v = as | 0x0F;
        else v = uint8_t(as | (~h & (UP | DOWN)));
    }
    return uint8_t(v | 0x80);
}

} // namespace md::pad
