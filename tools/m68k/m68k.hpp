// A 68000 interpreter for running the original game code: all instructions,
// flags, exceptions (address error, illegal, privilege, divide by zero, CHK,
// TRAPV, TRAP, line A/F) and autovectored interrupts. Cycle counts are
// approximate: 4 per bus word plus the internal cycles of slow instructions.
#pragma once

#include <cstdint>

namespace m68k {

struct Bus {
    virtual ~Bus() = default;
    // Addresses are already masked to 24 bits; word accesses are even.
    virtual uint8_t r8(uint32_t a) = 0;
    virtual uint16_t r16(uint32_t a) = 0;
    virtual void w8(uint32_t a, uint8_t v) = 0;
    virtual void w16(uint32_t a, uint16_t v) = 0;
};

class Cpu {
public:
    uint32_t d[8] = {}, a[8] = {}; // a[7] is the active stack pointer
    uint32_t other_sp = 0;         // USP in supervisor mode, SSP in user mode
    uint32_t pc = 0;
    uint16_t sr = 0x2700;
    uint64_t cycles = 0;
    bool stopped = false; // STOP: waits for an interrupt
    bool halted = false;  // double bus/address fault
    Bus* bus = nullptr;

    void reset(); // SSP and PC from the vectors at 0 and 4
    void step();  // one instruction, or an interrupt / exception entry
    void set_irq(int level) { irq = level; }
    // Exception processing for vector n (also used to force an interrupt).
    void exception(int vec, uint32_t ret_pc);
    void interrupt(int level); // takes the interrupt now, ignoring the mask
    void set_sr(uint16_t v);
    uint32_t usp() const { return (sr & 0x2000) ? other_sp : a[7]; }

    // Last exception taken (vector number), for callers that want to stop on one.
    int last_exception = -1;

    uint32_t op_pc = 0; // address of the instruction being executed
    uint64_t instructions = 0;

private:
    int irq = 0;
    uint16_t op = 0;

    // Bus with address error checks and cycle counting.
    uint8_t rb(uint32_t a);
    uint16_t rw(uint32_t a);
    uint32_t rl(uint32_t a);
    void wb(uint32_t a, uint8_t v);
    void ww(uint32_t a, uint16_t v);
    void wl(uint32_t a, uint32_t v);
    uint16_t fetch16();
    uint32_t fetch32();
    void push16(uint16_t v);
    void push32(uint32_t v);
    uint16_t pop16();
    uint32_t pop32();

    // Effective addresses. A location is a data/address register, memory,
    // or an immediate value.
    struct Loc {
        enum Kind { DREG, AREG, MEM, IMM } kind;
        uint32_t v; // register number, address, or immediate value
    };
    Loc ea(int mode, int reg, int size); // size 1, 2, 4; applies (An)+ / -(An)
    uint32_t ea_addr(int mode, int reg);  // control modes only
    uint32_t read(const Loc& l, int size);
    void write(const Loc& l, int size, uint32_t v);
    uint32_t read_ea(int mode, int reg, int size) { return read(ea(mode, reg, size), size); }

    bool cond(int cc) const;
    void set_nz(uint32_t v, int size);
    uint32_t add(uint32_t s, uint32_t d, int size, bool x, bool addx);
    uint32_t sub(uint32_t s, uint32_t d, int size, bool x, bool subx, bool cmp);
    uint32_t shift(int type, bool left, uint32_t v, int count, int size);

    void execute();
    void group0();
    void group4();
    void group5();
    void group8();
    void group9d(bool is_add);
    void groupb();
    void groupc();
    void groupe();
    void movem();
    void privilege();
    void illegal() { exception(4, op_pc); }
};

// Thrown by the bus wrappers; step() turns it into an address error exception.
struct AddressError {
    uint32_t addr;
    bool write, fetch;
};

} // namespace m68k
