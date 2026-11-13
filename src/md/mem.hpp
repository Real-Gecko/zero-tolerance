// 68000 address space as the translated game code sees it: ROM, work RAM, and
// the hardware ports (VDP, pads, Z80 area). Values are big-endian like on the
// Mega Drive.
#pragma once

#include <cstdint>
#include <vector>

namespace md {

extern uint8_t ram[0x10000];
extern const uint8_t* rom;
extern uint32_t rom_size;

uint8_t io_rd8(uint32_t a);
uint16_t io_rd16(uint32_t a);
void io_wr8(uint32_t a, uint8_t v);
void io_wr16(uint32_t a, uint16_t v);

inline uint8_t rd8(uint32_t a) {
    a &= 0xFFFFFF;
    if (a >= 0xE00000) return ram[a & 0xFFFF];
    if (a < 0x400000) return a < rom_size ? rom[a] : 0xFF;
    return io_rd8(a);
}
inline uint16_t rd16(uint32_t a) {
    a &= 0xFFFFFE;
    if (a >= 0xE00000) return uint16_t(ram[a & 0xFFFF] << 8 | ram[(a + 1) & 0xFFFF]);
    if (a < 0x400000) return a + 1 < rom_size ? uint16_t(rom[a] << 8 | rom[a + 1]) : 0xFFFF;
    return io_rd16(a);
}
inline uint32_t rd32(uint32_t a) { return uint32_t(rd16(a)) << 16 | rd16(a + 2); }
inline int8_t rs8(uint32_t a) { return int8_t(rd8(a)); }
inline int16_t rs16(uint32_t a) { return int16_t(rd16(a)); }
inline int32_t rs32(uint32_t a) { return int32_t(rd32(a)); }

inline void wr8(uint32_t a, uint8_t v) {
    a &= 0xFFFFFF;
    if (a >= 0xE00000) ram[a & 0xFFFF] = v;
    else if (a >= 0x400000) io_wr8(a, v);
}
inline void wr16(uint32_t a, uint16_t v) {
    a &= 0xFFFFFE;
    if (a >= 0xE00000) {
        ram[a & 0xFFFF] = uint8_t(v >> 8);
        ram[(a + 1) & 0xFFFF] = uint8_t(v);
    } else if (a >= 0x400000) io_wr16(a, v);
}
inline void wr32(uint32_t a, uint32_t v) {
    wr16(a, uint16_t(v >> 16));
    wr16(a + 2, uint16_t(v));
}

// A RAM variable at a fixed address, so translated code can write
// `vblank_count += 1` instead of wr16(0xFF0000, rd16(0xFF0000) + 1).
template <typename T, int N>
struct Var {
    uint32_t a;
    static T get(uint32_t a) {
        if constexpr (N == 1) return T(rd8(a));
        else if constexpr (N == 2) return T(rd16(a));
        else return T(rd32(a));
    }
    static void set(uint32_t a, T v) {
        if constexpr (N == 1) wr8(a, uint8_t(v));
        else if constexpr (N == 2) wr16(a, uint16_t(v));
        else wr32(a, uint32_t(v));
    }
    operator T() const { return get(a); }
    // Assigning one variable to another copies the value, not the address.
    const Var& operator=(const Var& o) const { set(a, get(o.a)); return *this; }
    const Var& operator=(T v) const { set(a, v); return *this; }
    const Var& operator+=(T v) const { set(a, T(get(a) + v)); return *this; }
    const Var& operator-=(T v) const { set(a, T(get(a) - v)); return *this; }
    const Var& operator|=(T v) const { set(a, T(get(a) | v)); return *this; }
    const Var& operator&=(T v) const { set(a, T(get(a) & v)); return *this; }
    const Var& operator^=(T v) const { set(a, T(get(a) ^ v)); return *this; }
};
using U8 = Var<uint8_t, 1>;
using S8 = Var<int8_t, 1>;
using U16 = Var<uint16_t, 2>;
using S16 = Var<int16_t, 2>;
using U32 = Var<uint32_t, 4>;
using S32 = Var<int32_t, 4>;

// Interrupt mask from the status register. VBlank (level 6) only runs when it
// is below 6 and the VDP has VBlank interrupts enabled.
extern int int_mask;

// When set, VDP port writes are appended as (port << 16 | value); used by tests.
extern std::vector<uint32_t>* port_log;

} // namespace md
