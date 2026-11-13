#include "m68k.hpp"

#include <climits>

namespace m68k {

namespace {

enum : uint16_t { C = 1, V = 2, Z = 4, N = 8, X = 0x10 };

constexpr uint32_t mask(int size) { return size == 4 ? 0xFFFFFFFFu : (1u << (size * 8)) - 1; }
constexpr uint32_t msb(int size) { return 1u << (size * 8 - 1); }
constexpr uint32_t sext(uint32_t v, int size) {
    return size == 1 ? uint32_t(int32_t(int8_t(v))) : size == 2 ? uint32_t(int32_t(int16_t(v))) : v;
}
// Size field 0/1/2 -> 1/2/4 bytes (3 = invalid).
constexpr int size_of(int s) { return s == 0 ? 1 : s == 1 ? 2 : s == 2 ? 4 : 0; }

struct Illegal {};

} // namespace

// ---- bus -------------------------------------------------------------------

uint8_t Cpu::rb(uint32_t a) {
    cycles += 4;
    return bus->r8(a & 0xFFFFFF);
}
uint16_t Cpu::rw(uint32_t a) {
    if (a & 1) throw AddressError{a, false, false};
    cycles += 4;
    return bus->r16(a & 0xFFFFFF);
}
uint32_t Cpu::rl(uint32_t a) {
    uint32_t hi = rw(a);
    return hi << 16 | rw(a + 2);
}
void Cpu::wb(uint32_t a, uint8_t v) {
    cycles += 4;
    bus->w8(a & 0xFFFFFF, v);
}
void Cpu::ww(uint32_t a, uint16_t v) {
    if (a & 1) throw AddressError{a, true, false};
    cycles += 4;
    bus->w16(a & 0xFFFFFF, v);
}
void Cpu::wl(uint32_t a, uint32_t v) {
    if (a & 1) throw AddressError{a, true, false};
    ww(a, uint16_t(v >> 16));
    ww(a + 2, uint16_t(v));
}
uint16_t Cpu::fetch16() {
    if (pc & 1) throw AddressError{pc, false, true};
    cycles += 4;
    uint16_t v = bus->r16(pc & 0xFFFFFF);
    pc += 2;
    return v;
}
uint32_t Cpu::fetch32() {
    uint32_t hi = fetch16();
    return hi << 16 | fetch16();
}
void Cpu::push16(uint16_t v) { a[7] -= 2; ww(a[7], v); }
void Cpu::push32(uint32_t v) { a[7] -= 4; wl(a[7], v); }
uint16_t Cpu::pop16() { uint16_t v = rw(a[7]); a[7] += 2; return v; }
uint32_t Cpu::pop32() { uint32_t v = rl(a[7]); a[7] += 4; return v; }

// ---- status register and exceptions ----------------------------------------

void Cpu::set_sr(uint16_t v) {
    v &= 0xA71F;
    if ((v ^ sr) & 0x2000) {
        uint32_t t = a[7];
        a[7] = other_sp;
        other_sp = t;
    }
    sr = v;
}

void Cpu::reset() {
    sr = 0x2700;
    stopped = halted = false;
    irq = 0;
    a[7] = bus->r16(0) << 16 | bus->r16(2);
    pc = bus->r16(4) << 16 | bus->r16(6);
    cycles += 40;
}

void Cpu::exception(int vec, uint32_t ret_pc) {
    last_exception = vec;
    uint16_t old = sr;
    set_sr(uint16_t((sr | 0x2000) & ~0x8000));
    push32(ret_pc);
    push16(old);
    pc = rl(uint32_t(vec) * 4);
    cycles += 34 - 12; // the pushes and the vector read counted 20
}

void Cpu::interrupt(int level) {
    stopped = false;
    uint16_t old = sr;
    set_sr(uint16_t(((sr | 0x2000) & ~0x8700) | (level << 8)));
    last_exception = 24 + level;
    push32(pc);
    push16(old);
    pc = rl(uint32_t(24 + level) * 4);
    cycles += 44 - 20;
}

void Cpu::step() {
    if (halted) { cycles += 4; return; }
    int mask_level = (sr >> 8) & 7;
    if (irq && (irq > mask_level || irq == 7)) {
        int level = irq;
        irq = 0; // acknowledged
        try { interrupt(level); } catch (const AddressError&) { halted = true; }
        return;
    }
    if (stopped) { cycles += 4; return; }
    bool trace = sr & 0x8000;
    op_pc = pc;
    instructions++;
    try {
        try {
            op = fetch16();
            execute();
        } catch (const Illegal&) {
            illegal();
        }
        if (trace && !stopped) exception(9, pc);
    } catch (const AddressError& e) {
        try {
            uint16_t old = sr;
            set_sr(uint16_t((sr | 0x2000) & ~0x8000));
            push32(op_pc + 2);
            push16(old);
            push16(op);
            push32(e.addr);
            uint16_t fc = (old & 0x2000) ? (e.fetch ? 6 : 5) : (e.fetch ? 2 : 1);
            push16(uint16_t((e.write ? 0 : 0x10) | (e.fetch ? 0 : 8) | fc));
            last_exception = 3;
            pc = rl(3 * 4);
            cycles += 50 - 36;
        } catch (const AddressError&) {
            halted = true;
        }
    }
}

// ---- effective addresses ---------------------------------------------------

Cpu::Loc Cpu::ea(int mode, int reg, int size) {
    switch (mode) {
    case 0: return {Loc::DREG, uint32_t(reg)};
    case 1: return {Loc::AREG, uint32_t(reg)};
    case 2: return {Loc::MEM, a[reg]};
    case 3: {
        uint32_t ad = a[reg];
        a[reg] += (size == 1 && reg == 7) ? 2 : size;
        return {Loc::MEM, ad};
    }
    case 4:
        cycles += 2;
        a[reg] -= (size == 1 && reg == 7) ? 2 : size;
        return {Loc::MEM, a[reg]};
    case 7:
        if (reg == 4) {
            if (size == 4) return {Loc::IMM, fetch32()};
            uint16_t v = fetch16();
            return {Loc::IMM, size == 1 ? uint32_t(v & 0xFF) : v};
        }
        [[fallthrough]];
    default: return {Loc::MEM, ea_addr(mode, reg)};
    }
}

uint32_t Cpu::ea_addr(int mode, int reg) {
    auto index = [&](uint32_t base) {
        uint16_t ext = fetch16();
        uint32_t x = (ext & 0x8000) ? a[(ext >> 12) & 7] : d[(ext >> 12) & 7];
        if (!(ext & 0x800)) x = sext(x, 2);
        cycles += 2;
        return base + x + sext(ext & 0xFF, 1);
    };
    switch (mode) {
    case 2: return a[reg];
    case 5: return a[reg] + sext(fetch16(), 2);
    case 6: return index(a[reg]);
    case 7:
        switch (reg) {
        case 0: return sext(fetch16(), 2);
        case 1: return fetch32();
        case 2: { uint32_t base = pc; return base + sext(fetch16(), 2); }
        case 3: return index(pc);
        }
    }
    throw Illegal{};
}

uint32_t Cpu::read(const Loc& l, int size) {
    switch (l.kind) {
    case Loc::DREG: return d[l.v] & mask(size);
    case Loc::AREG: return a[l.v] & mask(size);
    case Loc::IMM: return l.v;
    default: return size == 1 ? rb(l.v) : size == 2 ? rw(l.v) : rl(l.v);
    }
}

void Cpu::write(const Loc& l, int size, uint32_t v) {
    switch (l.kind) {
    case Loc::DREG: d[l.v] = (d[l.v] & ~mask(size)) | (v & mask(size)); break;
    case Loc::AREG: a[l.v] = v; break;
    case Loc::IMM: throw Illegal{};
    default:
        if (size == 1) wb(l.v, uint8_t(v));
        else if (size == 2) ww(l.v, uint16_t(v));
        else wl(l.v, v);
    }
}

// ---- flags and arithmetic --------------------------------------------------

bool Cpu::cond(int cc) const {
    bool c = sr & C, v = sr & V, z = sr & Z, n = sr & N;
    switch (cc) {
    case 0: return true;
    case 1: return false;
    case 2: return !c && !z;
    case 3: return c || z;
    case 4: return !c;
    case 5: return c;
    case 6: return !z;
    case 7: return z;
    case 8: return !v;
    case 9: return v;
    case 10: return !n;
    case 11: return n;
    case 12: return n == v;
    case 13: return n != v;
    case 14: return !z && n == v;
    default: return z || n != v;
    }
}

void Cpu::set_nz(uint32_t v, int size) {
    v &= mask(size);
    sr = uint16_t((sr & ~(N | Z | V | C)) | ((v & msb(size)) ? N : 0) | (v ? 0 : Z));
}

uint32_t Cpu::add(uint32_t s, uint32_t dv, int size, bool, bool addx) {
    s &= mask(size);
    dv &= mask(size);
    uint64_t r64 = uint64_t(dv) + s + ((addx && (sr & X)) ? 1 : 0);
    uint32_t r = uint32_t(r64) & mask(size);
    uint16_t f = sr & ~(N | V | C | X);
    if (r & msb(size)) f |= N;
    if ((s ^ r) & (dv ^ r) & msb(size)) f |= V;
    if (r64 > mask(size)) f |= C | X;
    if (addx) { if (r) f &= ~Z; }
    else f = uint16_t(r ? f & ~Z : f | Z);
    sr = f;
    return r;
}

uint32_t Cpu::sub(uint32_t s, uint32_t dv, int size, bool, bool subx, bool cmp) {
    s &= mask(size);
    dv &= mask(size);
    uint64_t sx = uint64_t(s) + ((subx && (sr & X)) ? 1 : 0);
    uint32_t r = uint32_t(dv - sx) & mask(size);
    uint16_t f = sr & ~(N | V | C | (cmp ? 0 : X));
    if (r & msb(size)) f |= N;
    if ((s ^ dv) & (r ^ dv) & msb(size)) f |= V;
    if (sx > dv) f |= cmp ? C : (C | X);
    if (subx) { if (r) f &= ~Z; }
    else f = uint16_t(r ? f & ~Z : f | Z);
    sr = f;
    return r;
}

// type 0 AS, 1 LS, 2 ROX, 3 RO
uint32_t Cpu::shift(int type, bool left, uint32_t v, int count, int size) {
    uint32_t m = mask(size), top = msb(size);
    v &= m;
    uint16_t f = sr & ~(N | Z | V | C);
    bool x = sr & X;
    bool c = false, ov = false;
    for (int i = 0; i < count; i++) {
        if (left) {
            c = v & top;
            uint32_t in = type == 2 ? x : type == 3 ? c : 0;
            uint32_t nv = ((v << 1) | in) & m;
            if (type == 0 && ((nv ^ v) & top)) ov = true;
            v = nv;
        } else {
            c = v & 1;
            uint32_t in = type == 0 ? (v & top) : type == 2 ? (x ? top : 0) : type == 3 ? (c ? top : 0) : 0;
            v = (v >> 1) | in;
        }
        if (type != 3) x = c;
    }
    if (count == 0) c = (type == 2) ? x : false;
    if (c) f |= C;
    if (ov) f |= V;
    if (v & top) f |= N;
    if (!v) f |= Z;
    if (type != 3 && count) f = uint16_t(x ? f | X : f & ~X);
    sr = f;
    cycles += 2 * uint64_t(count) + (size == 4 ? 4 : 2);
    return v;
}

void Cpu::privilege() { exception(8, op_pc); }

// ---- instructions ----------------------------------------------------------

void Cpu::execute() {
    switch (op >> 12) {
    case 0x0: group0(); break;
    case 0x1: case 0x2: case 0x3: {
        int size = (op >> 12) == 1 ? 1 : (op >> 12) == 3 ? 2 : 4;
        int dmode = (op >> 6) & 7, dreg = (op >> 9) & 7;
        if (dmode == 1 && size == 1) throw Illegal{};
        if (dmode == 7 && dreg > 1) throw Illegal{};
        uint32_t v = read_ea((op >> 3) & 7, op & 7, size);
        if (dmode == 1) { a[dreg] = sext(v, size); break; }
        Loc l = ea(dmode, dreg, size);
        if (dmode == 4) cycles -= 2;
        uint16_t keep = sr & X;
        set_nz(v, size);
        sr |= keep;
        write(l, size, v);
        break;
    }
    case 0x4: group4(); break;
    case 0x5: group5(); break;
    case 0x6: {
        int cc = (op >> 8) & 0xF;
        uint32_t base = pc;
        uint32_t disp = sext(op & 0xFF, 1);
        if ((op & 0xFF) == 0) disp = sext(fetch16(), 2);
        if (cc == 1) { push32(pc); pc = base + disp; cycles += 2; break; }
        if (cond(cc)) { pc = base + disp; cycles += 2; }
        else cycles += 4;
        break;
    }
    case 0x7: {
        if (op & 0x100) throw Illegal{};
        uint32_t v = sext(op & 0xFF, 1);
        d[(op >> 9) & 7] = v;
        uint16_t keep = sr & X;
        set_nz(v, 4);
        sr |= keep;
        break;
    }
    case 0x8: group8(); break;
    case 0x9: group9d(false); break;
    case 0xA: exception(10, op_pc); break;
    case 0xB: groupb(); break;
    case 0xC: groupc(); break;
    case 0xD: group9d(true); break;
    case 0xE: groupe(); break;
    default: exception(11, op_pc); break;
    }
}

void Cpu::group0() {
    int mode = (op >> 3) & 7, reg = op & 7;
    if ((op & 0x0138) == 0x0108) { // MOVEP
        int dr = (op >> 9) & 7;
        uint32_t ad = a[reg] + sext(fetch16(), 2);
        switch ((op >> 6) & 3) {
        case 0: d[dr] = (d[dr] & 0xFFFF0000) | rb(ad) << 8 | rb(ad + 2); break;
        case 1: d[dr] = uint32_t(rb(ad)) << 24 | rb(ad + 2) << 16 | rb(ad + 4) << 8 | rb(ad + 6); break;
        case 2: wb(ad, uint8_t(d[dr] >> 8)); wb(ad + 2, uint8_t(d[dr])); break;
        case 3:
            wb(ad, uint8_t(d[dr] >> 24)); wb(ad + 2, uint8_t(d[dr] >> 16));
            wb(ad + 4, uint8_t(d[dr] >> 8)); wb(ad + 6, uint8_t(d[dr]));
            break;
        }
        return;
    }
    if ((op & 0x0100) || (op & 0x0F00) == 0x0800) { // BTST/BCHG/BCLR/BSET
        uint32_t bit = (op & 0x0100) ? d[(op >> 9) & 7] : fetch16() & 0xFF;
        int type = (op >> 6) & 3;
        if (mode == 1) throw Illegal{};
        if (mode == 0) {
            bit &= 31;
            uint32_t& r = d[reg];
            sr = uint16_t((r >> bit) & 1 ? sr & ~Z : sr | Z);
            if (type == 1) r ^= 1u << bit;
            else if (type == 2) r &= ~(1u << bit);
            else if (type == 3) r |= 1u << bit;
            cycles += type == 0 ? 2 : 4;
            return;
        }
        if (type != 0 && mode == 7 && reg > 1) throw Illegal{};
        if (type == 0 && mode == 7 && reg == 4 && !(op & 0x100)) throw Illegal{};
        bit &= 7;
        Loc l = ea(mode, reg, 1);
        uint32_t v = read(l, 1);
        sr = uint16_t((v >> bit) & 1 ? sr & ~Z : sr | Z);
        if (type == 0) return;
        if (type == 1) v ^= 1u << bit;
        else if (type == 2) v &= ~(1u << bit);
        else v |= 1u << bit;
        write(l, 1, v);
        return;
    }
    int kind = (op >> 9) & 7;
    int sz = (op >> 6) & 3;
    if (mode == 7 && reg == 4 && (kind == 0 || kind == 1 || kind == 5)) { // to CCR / SR
        if (sz == 0) {
            uint16_t v = fetch16() & 0x1F;
            uint16_t ccr = sr & 0x1F;
            ccr = kind == 0 ? (ccr | v) : kind == 1 ? (ccr & v) : (ccr ^ v);
            sr = uint16_t((sr & 0xFF00) | ccr);
            cycles += 8;
            return;
        }
        if (sz == 1) {
            if (!(sr & 0x2000)) { privilege(); return; }
            uint16_t v = fetch16();
            set_sr(kind == 0 ? (sr | v) : kind == 1 ? (sr & v) : (sr ^ v));
            cycles += 8;
            return;
        }
        throw Illegal{};
    }
    int size = size_of(sz);
    if (!size || kind == 4 || kind == 7 || mode == 1) throw Illegal{};
    if (mode == 7 && reg > 1) throw Illegal{};
    uint32_t imm = size == 4 ? fetch32() : size == 2 ? fetch16() : fetch16() & 0xFF;
    Loc l = ea(mode, reg, size);
    uint32_t v = read(l, size);
    if (size == 4 && mode == 0) cycles += 4;
    switch (kind) {
    case 0: v |= imm; { uint16_t k = sr & X; set_nz(v, size); sr |= k; } break;
    case 1: v &= imm; { uint16_t k = sr & X; set_nz(v, size); sr |= k; } break;
    case 2: v = sub(imm, v, size, false, false, false); break;
    case 3: v = add(imm, v, size, false, false); break;
    case 5: v ^= imm; { uint16_t k = sr & X; set_nz(v, size); sr |= k; } break;
    case 6: sub(imm, v, size, false, false, true); return;
    }
    write(l, size, v);
}

void Cpu::movem() {
    bool to_reg = op & 0x400;
    int size = (op & 0x40) ? 4 : 2;
    int mode = (op >> 3) & 7, reg = op & 7;
    uint16_t list = fetch16();
    if (mode < 2 || (!to_reg && mode == 3) || (to_reg && mode == 4)) throw Illegal{};
    if (mode == 7 && reg > (to_reg ? 3 : 1)) throw Illegal{};
    if (mode == 4) {
        uint32_t ad = a[reg];
        for (int i = 0; i < 16; i++) {
            if (!(list & (1 << i))) continue;
            int r = 15 - i;
            uint32_t v = r < 8 ? d[r] : a[r - 8];
            ad -= size;
            if (size == 2) ww(ad, uint16_t(v)); else wl(ad, v);
        }
        a[reg] = ad;
        return;
    }
    uint32_t ad = mode == 3 ? a[reg] : ea_addr(mode, reg);
    for (int r = 0; r < 16; r++) {
        if (!(list & (1 << r))) continue;
        if (to_reg) {
            uint32_t v = size == 2 ? sext(rw(ad), 2) : rl(ad);
            if (r < 8) d[r] = v; else a[r - 8] = v;
        } else {
            uint32_t v = r < 8 ? d[r] : a[r - 8];
            if (size == 2) ww(ad, uint16_t(v)); else wl(ad, v);
        }
        ad += size;
    }
    if (to_reg) cycles += 4; // extra read at the end
    if (mode == 3) a[reg] = ad;
}

void Cpu::group4() {
    int mode = (op >> 3) & 7, reg = op & 7;
    if ((op & 0xF1C0) == 0x41C0) { // LEA
        if (mode < 2 || mode == 3 || mode == 4 || (mode == 7 && reg > 3)) throw Illegal{};
        a[(op >> 9) & 7] = ea_addr(mode, reg);
        return;
    }
    if ((op & 0xF1C0) == 0x4180) { // CHK.W
        if (mode == 1) throw Illegal{};
        int16_t bound = int16_t(read_ea(mode, reg, 2));
        int16_t v = int16_t(d[(op >> 9) & 7]);
        cycles += 6;
        if (v < 0) { sr |= N; exception(6, pc); }
        else if (v > bound) { sr &= ~N; exception(6, pc); }
        return;
    }
    switch (op & 0xFFC0) {
    case 0x40C0: { // MOVE from SR
        if (mode == 1 || (mode == 7 && reg > 1)) throw Illegal{};
        Loc l = ea(mode, reg, 2);
        if (l.kind == Loc::MEM) read(l, 2); // the 68000 reads before writing
        write(l, 2, sr);
        return;
    }
    case 0x44C0: { // MOVE to CCR
        if (mode == 1) throw Illegal{};
        uint16_t v = uint16_t(read_ea(mode, reg, 2));
        sr = uint16_t((sr & 0xFF00) | (v & 0x1F));
        cycles += 8;
        return;
    }
    case 0x46C0: { // MOVE to SR
        if (mode == 1) throw Illegal{};
        if (!(sr & 0x2000)) { privilege(); return; }
        set_sr(uint16_t(read_ea(mode, reg, 2)));
        cycles += 8;
        return;
    }
    case 0x4800: { // NBCD
        if (mode == 1 || (mode == 7 && reg > 1)) throw Illegal{};
        Loc l = ea(mode, reg, 1);
        uint32_t dv = read(l, 1);
        uint32_t res = (0x9A - dv - ((sr & X) ? 1 : 0)) & 0xFF;
        uint16_t f = sr & ~(N | V | C | X);
        if (res != 0x9A) {
            uint32_t before = res;
            if ((res & 0x0F) == 0xA) res = (res & 0xF0) + 0x10;
            res &= 0xFF;
            if (~before & res & 0x80) f |= V;
            write(l, 1, res);
            if (res) f &= ~Z;
            f |= C | X;
        }
        if (res & 0x80) f |= N;
        sr = f;
        if (mode == 0) cycles += 2;
        return;
    }
    case 0x4840:
        if (mode == 0) { // SWAP
            d[reg] = d[reg] >> 16 | d[reg] << 16;
            uint16_t k = sr & X;
            set_nz(d[reg], 4);
            sr |= k;
            return;
        }
        { // PEA
            if (mode == 1 || mode == 3 || mode == 4 || (mode == 7 && reg > 3)) throw Illegal{};
            uint32_t ad = ea_addr(mode, reg);
            push32(ad);
            return;
        }
    case 0x4880: case 0x48C0:
        if (mode == 0) { // EXT
            uint16_t k = sr & X;
            if (op & 0x40) { d[reg] = sext(d[reg], 2); set_nz(d[reg], 4); }
            else { d[reg] = (d[reg] & 0xFFFF0000) | (sext(d[reg], 1) & 0xFFFF); set_nz(d[reg], 2); }
            sr |= k;
            return;
        }
        movem();
        return;
    case 0x4C80: case 0x4CC0: movem(); return;
    case 0x4AC0: { // TAS (0x4AFC is ILLEGAL)
        if (op == 0x4AFC) throw Illegal{};
        if (mode == 1 || (mode == 7 && reg > 1)) throw Illegal{};
        Loc l = ea(mode, reg, 1);
        uint32_t v = read(l, 1);
        uint16_t k = sr & X;
        set_nz(v, 1);
        sr |= k;
        write(l, 1, v | 0x80);
        cycles += 2;
        return;
    }
    case 0x4E80: case 0x4EC0: { // JSR / JMP
        if (mode < 2 || mode == 3 || mode == 4 || (mode == 7 && reg > 3)) throw Illegal{};
        uint32_t ad = ea_addr(mode, reg);
        if (op & 0x40) { pc = ad; return; }
        push32(pc);
        pc = ad;
        return;
    }
    }
    if ((op & 0xFF00) == 0x4E00 && (op & 0xC0) == 0x40) {
        switch (op & 0x3F) {
        case 0x30: if (!(sr & 0x2000)) { privilege(); return; } cycles += 128; return; // RESET
        case 0x31: return;                                                       // NOP
        case 0x32: {                                                             // STOP
            if (!(sr & 0x2000)) { privilege(); return; }
            set_sr(fetch16());
            stopped = true;
            return;
        }
        case 0x33: { // RTE
            if (!(sr & 0x2000)) { privilege(); return; }
            uint16_t nsr = pop16();
            uint32_t npc = pop32();
            set_sr(nsr);
            pc = npc;
            return;
        }
        case 0x35: pc = pop32(); return; // RTS
        case 0x36: if (sr & V) exception(7, pc); return; // TRAPV
        case 0x37: { // RTR
            uint16_t ccr = pop16();
            sr = uint16_t((sr & 0xFF00) | (ccr & 0x1F));
            pc = pop32();
            return;
        }
        }
        switch (op & 0x38) {
        case 0x00: case 0x08: exception(32 + (op & 0xF), pc); return; // TRAP
        case 0x10: { // LINK
            uint32_t disp = sext(fetch16(), 2);
            if (reg == 7) { a[7] -= 4; wl(a[7], a[7]); }
            else push32(a[reg]);
            a[reg] = a[7];
            a[7] += disp;
            return;
        }
        case 0x18: { // UNLK
            a[7] = a[reg];
            uint32_t v = pop32();
            a[reg] = v;
            return;
        }
        case 0x20: case 0x28: // MOVE USP
            if (!(sr & 0x2000)) { privilege(); return; }
            if (op & 8) a[reg] = other_sp;
            else other_sp = a[reg];
            return;
        }
        throw Illegal{};
    }
    // NEGX / CLR / NEG / NOT / TST
    int sz = (op >> 6) & 3;
    int size = size_of(sz);
    int kind = (op >> 9) & 7;
    if (!size || mode == 1 || (kind != 5 && kind > 3)) throw Illegal{};
    if (mode == 7 && reg > 1) throw Illegal{};
    Loc l = ea(mode, reg, size);
    uint32_t v = read(l, size); // CLR also reads on the 68000
    if (size == 4 && mode == 0) cycles += 2;
    switch (kind) {
    case 0: v = sub(v, 0, size, false, true, false); break;
    case 1: sr = uint16_t((sr & ~(N | V | C)) | Z); v = 0; break;
    case 2: v = sub(v, 0, size, false, false, false); break;
    case 3: { v = ~v; uint16_t k = sr & X; set_nz(v, size); sr |= k; } break;
    case 5: { uint16_t k = sr & X; set_nz(v, size); sr |= k; } return;
    }
    write(l, size, v);
}

void Cpu::group5() {
    int mode = (op >> 3) & 7, reg = op & 7;
    int cc = (op >> 8) & 0xF;
    if ((op & 0xC0) == 0xC0) {
        if (mode == 1) { // DBcc
            uint32_t base = pc;
            uint32_t disp = sext(fetch16(), 2);
            if (cond(cc)) { cycles += 4; return; }
            uint16_t cnt = uint16_t(d[reg] - 1);
            d[reg] = (d[reg] & 0xFFFF0000) | cnt;
            if (cnt != 0xFFFF) { pc = base + disp; cycles += 2; }
            else cycles += 6;
            return;
        }
        if (mode == 7 && reg > 1) throw Illegal{};
        Loc l = ea(mode, reg, 1);
        if (l.kind == Loc::MEM) read(l, 1); // Scc reads before writing
        bool t = cond(cc);
        write(l, 1, t ? 0xFF : 0);
        if (mode == 0 && t) cycles += 2;
        return;
    }
    int size = size_of((op >> 6) & 3);
    uint32_t q = (op >> 9) & 7;
    if (!q) q = 8;
    if (mode == 7 && reg > 1) throw Illegal{};
    if (mode == 1) {
        if (size == 1) throw Illegal{};
        a[reg] = (op & 0x100) ? a[reg] - q : a[reg] + q;
        cycles += 4;
        return;
    }
    Loc l = ea(mode, reg, size);
    uint32_t v = read(l, size);
    v = (op & 0x100) ? sub(q, v, size, false, false, false) : add(q, v, size, false, false);
    if (size == 4 && mode == 0) cycles += 4;
    write(l, size, v);
}

// BCD byte add/sub (ABCD / SBCD), flags as on the 68000 (Musashi's model).
static uint32_t bcd(uint16_t& sr, uint32_t s, uint32_t dv, bool subtract) {
    uint32_t x = (sr & X) ? 1 : 0;
    uint32_t res, before;
    if (!subtract) {
        res = (s & 0xF) + (dv & 0xF) + x;
        before = ~res;
        if (res > 9) res += 6;
        res += (s & 0xF0) + (dv & 0xF0);
        bool c = res > 0x99;
        if (c) res -= 0xA0;
        sr = uint16_t((sr & ~(C | X | N | V)) | (c ? C | X : 0));
    } else {
        res = (dv & 0xF) - (s & 0xF) - x;
        before = ~res;
        if (res > 9) res -= 6;
        res += (dv & 0xF0) - (s & 0xF0);
        bool c = res > 0x99;
        if (c) res += 0xA0;
        sr = uint16_t((sr & ~(C | X | N | V)) | (c ? C | X : 0));
    }
    res &= 0xFF;
    if (before & res & 0x80) sr |= V;
    if (res & 0x80) sr |= N;
    if (res) sr &= ~Z;
    return res;
}

void Cpu::group8() {
    int mode = (op >> 3) & 7, reg = op & 7, dr = (op >> 9) & 7;
    if ((op & 0x1F0) == 0x100) { // SBCD
        if (op & 8) {
            uint32_t s = read(ea(4, reg, 1), 1);
            Loc l = ea(4, dr, 1);
            write(l, 1, bcd(sr, s, read(l, 1), true));
        } else {
            d[dr] = (d[dr] & ~0xFFu) | bcd(sr, d[reg] & 0xFF, d[dr] & 0xFF, true);
            cycles += 2;
        }
        return;
    }
    if ((op & 0xC0) == 0xC0) { // DIVU / DIVS
        if (mode == 1) throw Illegal{};
        uint32_t s = read_ea(mode, reg, 2);
        if (s == 0) { exception(5, pc); return; }
        uint16_t f = sr & (0xFF00 | X);
        if (!(op & 0x100)) {
            uint32_t q = d[dr] / s, r = d[dr] % s;
            cycles += 136;
            if (q > 0xFFFF) { sr = uint16_t(f | V | N); return; }
            d[dr] = r << 16 | q;
            sr = uint16_t(f | ((q & 0x8000) ? N : 0) | (q ? 0 : Z));
        } else {
            int32_t dv = int32_t(d[dr]), sv = int16_t(s);
            cycles += 154;
            if (dv == INT32_MIN && sv == -1) { sr = uint16_t(f | V | N); return; }
            int32_t q = dv / sv, r = dv % sv;
            if (q < -0x8000 || q > 0x7FFF) { sr = uint16_t(f | V | N); return; }
            d[dr] = uint32_t(r) << 16 | (uint32_t(q) & 0xFFFF);
            sr = uint16_t(f | ((q & 0x8000) ? N : 0) | (q ? 0 : Z));
        }
        return;
    }
    int size = size_of((op >> 6) & 3);
    if (mode == 1) throw Illegal{};
    uint16_t k = sr & X;
    if (op & 0x100) {
        if (mode == 0) throw Illegal{};
        if (mode == 7 && reg > 1) throw Illegal{};
        Loc l = ea(mode, reg, size);
        uint32_t v = read(l, size) | d[dr];
        set_nz(v, size);
        sr |= k;
        write(l, size, v);
    } else {
        uint32_t v = read_ea(mode, reg, size) | d[dr];
        set_nz(v, size);
        sr |= k;
        write({Loc::DREG, uint32_t(dr)}, size, v);
        if (size == 4) cycles += 2;
    }
}

void Cpu::group9d(bool is_add) {
    int mode = (op >> 3) & 7, reg = op & 7, dr = (op >> 9) & 7;
    int opmode = (op >> 6) & 7;
    if (opmode == 3 || opmode == 7) { // ADDA / SUBA
        int size = opmode == 3 ? 2 : 4;
        uint32_t s = sext(read_ea(mode, reg, size), size);
        a[dr] = is_add ? a[dr] + s : a[dr] - s;
        cycles += (size == 4 && !(mode == 0 || mode == 1 || (mode == 7 && reg == 4))) ? 2 : 4;
        return;
    }
    int size = size_of(opmode & 3);
    if (opmode >= 4 && mode < 2) { // ADDX / SUBX
        if (mode == 0) {
            uint32_t v = is_add ? add(d[reg], d[dr], size, false, true) : sub(d[reg], d[dr], size, false, true, false);
            write({Loc::DREG, uint32_t(dr)}, size, v);
            if (size == 4) cycles += 4;
        } else {
            uint32_t s = read(ea(4, reg, size), size);
            Loc l = ea(4, dr, size);
            uint32_t dv = read(l, size);
            write(l, size, is_add ? add(s, dv, size, false, true) : sub(s, dv, size, false, true, false));
        }
        return;
    }
    if (opmode < 4) {
        if (mode == 1 && size == 1) throw Illegal{};
        uint32_t s = read_ea(mode, reg, size);
        uint32_t v = is_add ? add(s, d[dr], size, false, false) : sub(s, d[dr], size, false, false, false);
        write({Loc::DREG, uint32_t(dr)}, size, v);
        if (size == 4) cycles += (mode == 0 || mode == 1 || (mode == 7 && reg == 4)) ? 4 : 2;
    } else {
        if (mode == 7 && reg > 1) throw Illegal{};
        Loc l = ea(mode, reg, size);
        uint32_t dv = read(l, size);
        write(l, size, is_add ? add(d[dr], dv, size, false, false) : sub(d[dr], dv, size, false, false, false));
    }
}

void Cpu::groupb() {
    int mode = (op >> 3) & 7, reg = op & 7, dr = (op >> 9) & 7;
    int opmode = (op >> 6) & 7;
    if (opmode == 3 || opmode == 7) { // CMPA
        int size = opmode == 3 ? 2 : 4;
        uint32_t s = sext(read_ea(mode, reg, size), size);
        sub(s, a[dr], 4, false, false, true);
        cycles += 2;
        return;
    }
    int size = size_of(opmode & 3);
    if (opmode < 4) { // CMP
        if (mode == 1 && size == 1) throw Illegal{};
        uint32_t s = read_ea(mode, reg, size);
        sub(s, d[dr], size, false, false, true);
        if (size == 4) cycles += 2;
        return;
    }
    if (mode == 1) { // CMPM
        uint32_t s = read(ea(3, reg, size), size);
        uint32_t dv = read(ea(3, dr, size), size);
        sub(s, dv, size, false, false, true);
        return;
    }
    if (mode == 7 && reg > 1) throw Illegal{};
    Loc l = ea(mode, reg, size); // EOR
    uint32_t v = read(l, size) ^ d[dr];
    uint16_t k = sr & X;
    set_nz(v, size);
    sr |= k;
    write(l, size, v);
    if (size == 4 && mode == 0) cycles += 4;
}

void Cpu::groupc() {
    int mode = (op >> 3) & 7, reg = op & 7, dr = (op >> 9) & 7;
    if ((op & 0x1F0) == 0x100) { // ABCD
        if (op & 8) {
            uint32_t s = read(ea(4, reg, 1), 1);
            Loc l = ea(4, dr, 1);
            write(l, 1, bcd(sr, s, read(l, 1), false));
        } else {
            d[dr] = (d[dr] & ~0xFFu) | bcd(sr, d[reg] & 0xFF, d[dr] & 0xFF, false);
            cycles += 2;
        }
        return;
    }
    if ((op & 0x1F8) == 0x140) { uint32_t t = d[dr]; d[dr] = d[reg]; d[reg] = t; cycles += 2; return; }
    if ((op & 0x1F8) == 0x148) { uint32_t t = a[dr]; a[dr] = a[reg]; a[reg] = t; cycles += 2; return; }
    if ((op & 0x1F8) == 0x188) { uint32_t t = d[dr]; d[dr] = a[reg]; a[reg] = t; cycles += 2; return; }
    if ((op & 0xC0) == 0xC0) { // MULU / MULS
        if (mode == 1) throw Illegal{};
        uint32_t s = read_ea(mode, reg, 2);
        uint32_t r;
        int n = 0;
        if (op & 0x100) {
            r = uint32_t(int32_t(int16_t(s)) * int32_t(int16_t(d[dr])));
            uint32_t t = (s << 1) & 0x1FFFF;
            for (; t; t >>= 2) if ((t & 3) == 1 || (t & 3) == 2) n++;
        } else {
            r = (s & 0xFFFF) * (d[dr] & 0xFFFF);
            for (uint32_t t = s & 0xFFFF; t; t >>= 1) n += t & 1;
        }
        cycles += 34 + 2 * uint64_t(n);
        d[dr] = r;
        uint16_t k = sr & X;
        set_nz(r, 4);
        sr |= k;
        return;
    }
    int size = size_of((op >> 6) & 3);
    if (mode == 1) throw Illegal{};
    uint16_t k = sr & X;
    if (op & 0x100) {
        if (mode == 0) throw Illegal{};
        if (mode == 7 && reg > 1) throw Illegal{};
        Loc l = ea(mode, reg, size);
        uint32_t v = read(l, size) & d[dr];
        set_nz(v, size);
        sr |= k;
        write(l, size, v);
    } else {
        uint32_t v = read_ea(mode, reg, size) & d[dr];
        set_nz(v, size);
        sr |= k;
        write({Loc::DREG, uint32_t(dr)}, size, v);
        if (size == 4) cycles += 2;
    }
}

void Cpu::groupe() {
    int mode = (op >> 3) & 7, reg = op & 7;
    bool left = op & 0x100;
    if ((op & 0xC0) == 0xC0) { // memory, word by 1
        if ((op & 0x800) || mode < 2 || (mode == 7 && reg > 1)) throw Illegal{};
        Loc l = ea(mode, reg, 2);
        uint32_t v = read(l, 2);
        v = shift((op >> 9) & 3, left, v, 1, 2);
        cycles -= 4; // shift() charged register timing
        write(l, 2, v);
        return;
    }
    int size = size_of((op >> 6) & 3);
    int count = (op & 0x20) ? int(d[(op >> 9) & 7] & 63) : (((op >> 9) & 7) ? ((op >> 9) & 7) : 8);
    uint32_t v = shift((op >> 3) & 3, left, d[reg], count, size);
    d[reg] = (d[reg] & ~mask(size)) | v;
}

} // namespace m68k
