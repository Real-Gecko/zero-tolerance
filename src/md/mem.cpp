#include "mem.hpp"

#include "pad.hpp"
#include "vdp.hpp"

#include <vector>

namespace md {

uint8_t ram[0x10000];
const uint8_t* rom = nullptr;
uint32_t rom_size = 0;
int int_mask = 7;
std::vector<uint32_t>* port_log = nullptr;

static uint8_t z80_ram[0x2000];

uint8_t io_rd8(uint32_t a) {
    if (a >= 0xA00000 && a < 0xA10000) return z80_ram[a & 0x1FFF];
    if (a >= 0xC00000 && a < 0xC00020) {
        uint16_t v = io_rd16(a & ~1u);
        return (a & 1) ? uint8_t(v) : uint8_t(v >> 8);
    }
    switch (a) {
    case 0xA10001: return 0xA0; // overseas NTSC, no expansion unit
    case 0xA10003: return pad::read(0);
    case 0xA10005: return pad::read(1);
    case 0xA11100: return 0;    // Z80 bus always granted
    }
    return 0;
}

uint16_t io_rd16(uint32_t a) {
    if (a >= 0xC00000 && a < 0xC00020) {
        switch (a & 0x1E) {
        case 0x00: case 0x02: return vdp::read_data();
        case 0x04: case 0x06: return vdp::read_ctrl();
        case 0x08: case 0x0A: case 0x0C: case 0x0E: return vdp::read_hv();
        }
        return 0;
    }
    if (a >= 0xA00000 && a < 0xA10000) return uint16_t(z80_ram[a & 0x1FFF] * 0x101);
    if (a >= 0xA10000 && a < 0xA10020) return uint16_t(io_rd8(a | 1) * 0x101);
    if (a == 0xA11100) return 0;
    return 0;
}

void io_wr8(uint32_t a, uint8_t v) {
    if (a >= 0xA00000 && a < 0xA10000) { z80_ram[a & 0x1FFF] = v; return; }
    if (a >= 0xC00000 && a < 0xC00010) { io_wr16(a & ~1u, uint16_t(v * 0x101)); return; }
    switch (a) {
    case 0xA10003: pad::write(0, v); break;
    case 0xA10005: pad::write(1, v); break;
    }
}

void io_wr16(uint32_t a, uint16_t v) {
    if (a >= 0xC00000 && a < 0xC00010) {
        if (port_log) port_log->push_back((a & 0x1F) << 16 | v);
        if ((a & 0x0C) == 0) vdp::write_data(v);
        else if ((a & 0x0C) == 4) vdp::write_ctrl(v);
        return;
    }
    if (a >= 0xA00000 && a < 0xA10000) { z80_ram[a & 0x1FFF] = uint8_t(v >> 8); return; }
    if (a >= 0xA10000 && a < 0xA10020) io_wr8(a | 1, uint8_t(v));
}

} // namespace md
