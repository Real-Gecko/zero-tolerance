// 68000 arithmetic that C++ does not express directly.
#pragma once

#include <cstdint>

namespace game {

// divs.w: returns the new 32-bit register (remainder << 16 | quotient). On
// overflow the 68000 leaves the register unchanged.
inline uint32_t divs(uint32_t reg, uint16_t divisor) {
    int32_t n = int32_t(reg), d = int16_t(divisor);
    if (d == 0) return reg; // the game never divides by zero; the CPU would trap
    int32_t q = n / d, r = n % d;
    if (q < -32768 || q > 32767) return reg;
    return uint32_t(uint16_t(r)) << 16 | uint16_t(q);
}

inline uint32_t divu(uint32_t reg, uint16_t divisor) {
    if (divisor == 0) return reg;
    uint32_t q = reg / divisor, r = reg % divisor;
    if (q > 0xFFFF) return reg;
    return r << 16 | q;
}

inline uint32_t ext_l(uint32_t w) { return uint32_t(int32_t(int16_t(uint16_t(w)))); }
inline uint16_t lo(uint32_t v) { return uint16_t(v); }

} // namespace game
