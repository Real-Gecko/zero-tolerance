// C API of the 68000 interpreter for Python tools (ctypes).
#include "m68k.hpp"

#include <cstring>
#include <vector>

namespace {

// Flat 16 MB address space.
struct FlatBus : m68k::Bus {
    std::vector<uint8_t> mem = std::vector<uint8_t>(0x1000000);
    uint8_t r8(uint32_t a) override { return mem[a]; }
    uint16_t r16(uint32_t a) override { return uint16_t(mem[a] << 8 | mem[a + 1]); }
    void w8(uint32_t a, uint8_t v) override { mem[a] = v; }
    void w16(uint32_t a, uint16_t v) override { mem[a] = uint8_t(v >> 8); mem[a + 1] = uint8_t(v); }
};

FlatBus flat;

} // namespace

extern "C" {

// Loads len bytes at addr into the flat test memory.
void m68k_flat_load(uint32_t addr, const uint8_t* data, uint32_t len) {
    std::memcpy(&flat.mem[addr & 0xFFFFFF], data, len);
}
void m68k_flat_read(uint32_t addr, uint8_t* out, uint32_t len) {
    std::memcpy(out, &flat.mem[addr & 0xFFFFFF], len);
}

// One instruction on the flat memory. regs: d0-d7, a0-a7, pc, sr, usp (in/out).
// Returns the exception vector taken, or -1.
int m68k_flat_step(uint32_t* regs) {
    m68k::Cpu cpu;
    cpu.bus = &flat;
    std::memcpy(cpu.d, regs, 32);
    std::memcpy(cpu.a, regs + 8, 32);
    cpu.pc = regs[16];
    cpu.sr = uint16_t(regs[17]);
    cpu.other_sp = regs[18];
    cpu.step();
    std::memcpy(regs, cpu.d, 32);
    std::memcpy(regs + 8, cpu.a, 32);
    regs[16] = cpu.pc;
    regs[17] = cpu.sr;
    regs[18] = cpu.other_sp;
    return cpu.halted ? 256 : cpu.last_exception;
}

}
