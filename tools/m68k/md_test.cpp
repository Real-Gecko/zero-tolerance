// The original game code on the Mega Drive memory map, as tools/difftest.py
// runs it: ROM, work RAM, a logged VDP port, pads and the Z80 area. VBlank
// is taken when a VBlank wait loop is about to spin.
#include "m68k.hpp"

#include <cstring>
#include <vector>

namespace {

// Same protocol as src/md/pad.cpp (active-high buttons, md::pad bits).
struct Pad {
    uint32_t held = 0;
    bool th = true;
    int step = 0;
    void write(uint8_t v) {
        bool t = v & 0x40;
        if (t != th) step++;
        else if (t) step = 0;
        th = t;
    }
    uint8_t read() const {
        uint32_t h = held;
        uint32_t v;
        if (th) {
            uint32_t pressed = step == 6 ? (h & 0x30) : (h & 0x3F);
            v = (~pressed & 0x3F) | 0x40;
        } else {
            uint32_t a_s = (~(h & 0xC0) >> 2) & 0x30;
            v = step == 5 ? a_s : step == 7 ? (a_s | 0x0F) : (a_s | (~h & 3));
        }
        return uint8_t((v | 0x80) & 0xFF);
    }
};

struct MdBus : m68k::Bus {
    const uint8_t* rom = nullptr;
    uint32_t rom_size = 0;
    uint8_t ram[0x10000] = {};
    uint8_t z80[0x10000] = {};
    Pad pads[2];
    uint16_t hv = 0;
    std::vector<uint32_t> log;
    m68k::Cpu* cpu = nullptr;
    bool vblank_now = false; // a wait loop read: VBlank after this instruction

    uint16_t vdp_read(uint32_t off) {
        if (((off & 0x1E) >= 8) && ((off & 0x1E) <= 0xE)) {
            hv = uint16_t(hv * 0x6255 + 0x3619);
            return hv;
        }
        if ((off & 0x1C) == 4) return 0x3600;
        return 0;
    }
    uint8_t io_read(uint32_t off) {
        if (off < 0x10000) return z80[off];
        if (off == 0x10001) return 0xA0;
        if (off == 0x10003 || off == 0x10005) return pads[(off - 0x10003) / 2].read();
        return 0;
    }
    void io_write(uint32_t off, uint8_t v) {
        if (off < 0x10000) z80[off] = v;
        if (off == 0x10003 || off == 0x10005) pads[(off - 0x10003) / 2].write(v);
    }

    // The VBlank wait loops: tst.w d16(a6) / tst.w abs.l followed by a bne to
    // itself, and cmp.w d16(a6),Dn followed by beq/bgt to itself.
    void wait_loop_check(uint32_t a) {
        uint32_t pc = cpu->op_pc;
        uint16_t op = r16(pc);
        auto sx8 = [](uint16_t br) { return int32_t(int8_t(br & 0xFF)); };
        if (op == 0x4A6E || op == 0x4A79) {
            uint32_t len = op == 0x4A6E ? 4 : 6;
            uint16_t br = r16(pc + len);
            if ((br >> 8) == 0x66 && pc + len + 2 + sx8(br) == pc && word(a & ~1u)) vblank_now = true;
        } else if ((op & 0xF1FF) == 0xB06E) {
            uint16_t br = r16(pc + 4);
            if (pc + 6 + sx8(br) == pc) {
                uint16_t dn = uint16_t(cpu->d[(op >> 9) & 7]);
                uint16_t cur = word(a & ~1u);
                if (((br >> 8) == 0x67 && dn == cur) || ((br >> 8) == 0x6E && int16_t(dn) > int16_t(cur)))
                    vblank_now = true;
            }
        }
    }
    uint16_t word(uint32_t a) const { return uint16_t(ram[a & 0xFFFF] << 8 | ram[(a + 1) & 0xFFFF]); }

    uint8_t r8(uint32_t a) override {
        if (a >= 0xFF0000) {
            if (a < 0xFF0004) wait_loop_check(a);
            return ram[a & 0xFFFF];
        }
        if (a < 0x400000) return a < rom_size ? rom[a] : 0xFF;
        if (a >= 0xC00000 && a < 0xC10000) return uint8_t(vdp_read(a - 0xC00000));
        if (a >= 0xA00000 && a < 0xA20000) return io_read(a - 0xA00000);
        return 0;
    }
    uint16_t r16(uint32_t a) override {
        if (a >= 0xFF0000) {
            if (a < 0xFF0004) wait_loop_check(a);
            return word(a);
        }
        if (a < 0x400000) return uint16_t(r8(a) << 8 | r8(a + 1));
        if (a >= 0xC00000 && a < 0xC10000) return vdp_read(a - 0xC00000);
        if (a >= 0xA00000 && a < 0xA20000) return io_read(a - 0xA00000);
        return 0;
    }
    void vdp_write(uint32_t off, uint16_t v) {
        if (off >= 0x10) return;
        log.push_back((off & 0x1F) << 16 | v);
    }
    void w8(uint32_t a, uint8_t v) override {
        if (a >= 0xFF0000) ram[a & 0xFFFF] = v;
        else if (a >= 0xC00000 && a < 0xC10000) vdp_write(a - 0xC00000, v);
        else if (a >= 0xA00000 && a < 0xA20000) io_write(a - 0xA00000, v);
    }
    void w16(uint32_t a, uint16_t v) override {
        if (a >= 0xFF0000) { ram[a & 0xFFFF] = uint8_t(v >> 8); ram[(a + 1) & 0xFFFF] = uint8_t(v); }
        else if (a >= 0xC00000 && a < 0xC10000) vdp_write(a - 0xC00000, v);
        else if (a >= 0xA00000 && a < 0xA20000) io_write(a - 0xA00000, uint8_t(v));
    }
};

} // namespace

extern "C" {

// Runs the original code from start on a RAM state.
//   regs: d0-d7, a0-a7 in; d0-d7, a0-a7, pc out.
//   ret: return address pushed on the stack; the run ends when pc reaches it.
//   stops: the run also ends at one of these (not at the first instruction).
//   log/nlog: VDP port writes ((offset << 16) | value); nlog in = capacity.
// Returns 0 when finished, 1 at the instruction limit, 2 on an exception
// (vector in *info), 3 when halted.
int m68k_md_run(const uint8_t* rom, uint32_t rom_size, uint8_t* ram, uint32_t* regs, uint32_t start, uint32_t ret,
                const uint32_t* stops, int nstops, uint32_t pad, uint64_t max_insns, uint32_t* log, uint32_t* nlog,
                int* info) {
    static MdBus bus; // large; one run at a time
    bus = MdBus{};
    bus.rom = rom;
    bus.rom_size = rom_size;
    std::memcpy(bus.ram, ram, sizeof bus.ram);
    bus.pads[0].held = pad;
    m68k::Cpu cpu;
    cpu.bus = &bus;
    bus.cpu = &cpu;
    std::memcpy(cpu.d, regs, 32);
    std::memcpy(cpu.a, regs + 8, 32);
    cpu.a[7] -= 4;
    bus.w16(cpu.a[7] & 0xFFFFFF, uint16_t(ret >> 16));
    bus.w16((cpu.a[7] + 2) & 0xFFFFFF, uint16_t(ret));
    cpu.sr = 0x2700;
    cpu.pc = start;
    int result = 1;
    *info = 0;
    for (uint64_t n = 0; n < max_insns; n++) {
        if (cpu.pc == ret) { result = 0; break; }
        if (n) {
            bool hit = false;
            for (int i = 0; i < nstops; i++) hit |= cpu.pc == stops[i];
            if (hit) { result = 0; break; }
        }
        cpu.last_exception = -1;
        cpu.step();
        if (cpu.halted) { result = 3; break; }
        int e = cpu.last_exception;
        if (e >= 0 && (e < 24 || e >= 32)) { result = 2; *info = e; break; }
        if (bus.vblank_now) {
            bus.vblank_now = false;
            cpu.interrupt(6);
        }
    }
    std::memcpy(ram, bus.ram, sizeof bus.ram);
    std::memcpy(regs, cpu.d, 32);
    std::memcpy(regs + 8, cpu.a, 32);
    regs[16] = cpu.pc;
    uint32_t n = uint32_t(bus.log.size()) < *nlog ? uint32_t(bus.log.size()) : *nlog;
    std::memcpy(log, bus.log.data(), n * 4);
    *nlog = uint32_t(bus.log.size());
    return result;
}

}
