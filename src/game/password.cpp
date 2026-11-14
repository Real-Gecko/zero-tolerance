// Password check (0x58A74). The decoder works bit by bit on shared
// registers, so its helpers keep the 68000 register roles.
#include "game.hpp"

namespace game {

namespace {

struct Bits {
    uint16_t d0 = 0, d1 = 0, d2 = 0;
    uint8_t* a1 = nullptr;
    const uint8_t* a2 = nullptr;

    // 0x58660: stores bit 0 of d0 as bit d1 of the buffer at a1, then
    // advances d1 and rotates d0 right.
    void put() {
        uint16_t bit = d1 & 7, byte = (d1 & 0xFF) >> 3;
        uint8_t v = a1[byte];
        a1[byte] = (d0 & 1) ? uint8_t(v | 1 << bit) : uint8_t(v & ~(1 << bit));
        d1++;
        d0 = uint16_t(d0 >> 1 | d0 << 15);
    }
    // 0x5868A: rotates d0 right, then sets its bit 0 to bit d2 of the buffer at a2.
    void get() {
        d0 = uint16_t(d0 >> 1 | d0 << 15);
        uint16_t bit = d2 & 7, byte = (d2 & 0xFF) >> 3;
        if (a2[byte] >> bit & 1) d0 |= 1;
        else d0 &= ~1;
        d2++;
    }
    void get(int n) {
        for (int i = 0; i < n; i++) get();
    }
};

uint16_t rol(uint16_t v, int n) { return uint16_t(v << n | v >> (16 - n)); }

// 0x5897A: all weapons with 99 rounds.
void give_all_weapons() {
    static const uint8_t weapons[5] = {1, 3, 0xB, 0xC, 0xE};
    for (int i = 0; i < 5; i++) {
        st.carry_weapon[i] = weapons[i];
        st.carry_ammo[i] = 0x63;
    }
    for (uint8_t& a : st.alive) a = 1;
    st.carry_health = 0x63;
}

bool matches(const uint8_t* pw, const char* text) {
    for (int i = 0; i < 9; i++)
        if (pw[i] != uint8_t(text[i])) return false;
    return true;
}

// 0x58F58: character -> 6-bit value (0x2D if not in the alphabet).
uint16_t char_value(const uint8_t*& a2) {
    uint8_t c = *a2++;
    for (int d0 = 0x3F; d0 >= 0; d0--)
        if (uint8_t(kPwAlphabet[0x3F - d0]) == c) return uint16_t(0x3F - d0);
    return 0x2D;
}

// 0x586E8: undoes the XOR stream and the bit shuffle, from
// pw_bits (a2) into pw_data (a1).
void unscramble(Bits& b) {
    uint32_t d3 = 0x56CA2D69;
    for (int i = 0; i < 7; i++) {
        st.pw_bits[i] = uint8_t(st.pw_bits[i] ^ d3);
        d3 = d3 >> 7 | d3 << 25;
        d3 = d3 >> 16 | d3 << 16;
    }
    const uint8_t* a3 = kPwShuffle;
    b.d1 = 0;
    do {
        b.d2 = uint16_t((b.d2 & 0xFF00) | *a3++);
        b.get();
        b.put();
    } while (b.d1 != 0x38);
}

// 0x586B4: bit shuffle from a2 into a1, then the XOR stream.
void scramble(Bits& b) {
    const uint8_t* a3 = kPwShuffle;
    b.d2 = 0;
    do {
        b.get();
        b.d1 = uint16_t((b.d1 & 0xFF00) | *a3++);
        b.put();
    } while (b.d2 != 0x38);
    uint32_t d3 = 0x56CA2D69;
    for (int i = 0; i < 7; i++) {
        b.a1[i] = uint8_t(b.a1[i] ^ d3);
        d3 = d3 >> 7 | d3 << 25;
        d3 = d3 >> 16 | d3 << 16;
    }
}

} // namespace

// 0x58720: encodes the inventory, health and level into a password at out.
void EncodePassword(uint8_t* out) {
    Bits b;
    b.a1 = st.pw_data;
    b.d1 = 0;
    auto put_byte = [&](uint8_t v, int n) {
        b.d0 = uint16_t((b.d0 & 0xFF00) | v);
        for (int i = 0; i < n; i++) b.put();
    };
    for (uint8_t a : st.alive) put_byte(a, 1);
    uint16_t w = 0;
    for (uint8_t a : st.carry_weapon) w |= uint16_t(1 << (a & 15));
    b.d0 = uint16_t(int16_t(w) >> 1);
    for (int i = 0; i < 14; i++) b.put();
    for (uint8_t a : st.carry_ammo) put_byte(a, 3);
    put_byte(st.carry_health, 6);
    put_byte(st.level_id, 6);
    b.a2 = st.pw_data;
    b.d2 = 0;
    uint16_t d3 = 0;
    static const int fields[6][2] = {{8, 7}, {8, 7}, {8, 7}, {8, 7}, {8, 7}, {6, 5}};
    for (const auto& [n, r] : fields) {
        b.d0 = 0;
        b.get(n);
        d3 = uint16_t(d3 + rol(b.d0, r));
    }
    b.d0 = d3;
    for (int i = 0; i < 8; i++) b.put();
    b.a2 = st.pw_data;
    b.a1 = st.pw_bits;
    scramble(b);
    b.a2 = st.pw_bits;
    b.d2 = 0;
    for (int i = 0; i < 9; i++) {
        b.d0 = 0;
        b.get(6);
        *out++ = uint8_t(kPwAlphabet[uint16_t(rol(b.d0, 5))]);
    }
}

// 0x58A74
uint16_t DecodePassword(const uint8_t* pw) {
    if (matches(pw, "Highrise!")) {
        give_all_weapons();
        level_id = 0x10;
        return 0;
    }
    if (matches(pw, "Basement!")) {
        give_all_weapons();
        level_id = 0x20;
        return 0;
    }
    if (matches(pw, "Boxing!!!")) {
        for (uint8_t& a : st.carry_weapon) a = 0;
        for (uint8_t& a : st.alive) a = 1;
        st.carry_health = 0x63;
        level_id = 0x1F;
        return 0;
    }

    // Nine characters of 6 bits -> 54 bits in pw_bits.
    Bits b;
    const uint8_t* a2 = pw;
    b.a1 = st.pw_bits;
    for (int i = 0; i < 9; i++) {
        b.d0 = char_value(a2);
        for (int k = 0; k < 6; k++) b.put();
    }
    b.a2 = st.pw_bits;
    b.a1 = st.pw_data;
    unscramble(b);

    // Checksum: the sum of the data fields must match the last 7 bits.
    b.a2 = st.pw_data;
    b.d2 = 0;
    uint16_t d3 = 0;
    static const int fields[6][2] = {{8, 7}, {8, 7}, {8, 7}, {8, 7}, {8, 7}, {6, 5}};
    for (const auto& [n, r] : fields) {
        b.d0 = 0;
        b.get(n);
        d3 = uint16_t(d3 + rol(b.d0, r));
    }
    b.d0 = 0;
    b.get(8);
    b.d0 = rol(b.d0, 7);
    if (uint8_t(b.d0) != uint8_t(d3)) return 0xFFFF;

    b.a2 = st.pw_data;
    b.d2 = 0;
    for (uint8_t& a : st.alive) {
        b.d0 = 0;
        b.get();
        a = uint8_t(b.d0);
    }
    b.d0 = 0;
    b.get(14);
    b.d0 = rol(rol(b.d0, 8), 5);
    // Owned weapons: one bit per weapon 1-14, at most five.
    int n = 0;
    uint16_t weapon = 1;
    for (int i = 0; i < 14; i++, weapon++) {
        if (b.d0 & 1) {
            if (n == 5) return 0xFFFF;
            st.carry_weapon[n++] = uint8_t(weapon);
        }
        b.d0 = uint16_t(b.d0 >> 1 | b.d0 << 15);
    }
    while (n != 5) st.carry_weapon[n++] = 0;
    for (uint8_t& a : st.carry_ammo) {
        b.d0 = 0;
        b.get(3);
        a = uint8_t(rol(b.d0, 2));
    }
    b.d0 = 0;
    b.get(6);
    st.carry_health = uint8_t(rol(b.d0, 5));
    b.d0 = 0;
    b.get(6);
    level_id = uint8_t(rol(b.d0, 5));

    // Ammo: stored as eighths of the weapon's maximum.
    for (int i = 0; i < 5; i++) {
        uint16_t w = st.carry_weapon[i];
        uint32_t d0 = uint32_t(uint16_t(st.carry_ammo[i] * 2 + 1)) * kMaxAmmo[w];
        d0 = uint32_t(int32_t(d0) >> 4);
        d0 = uint16_t(int16_t(uint16_t(d0)) >> 8);
        st.carry_ammo[i] = uint8_t(d0);
    }
    uint32_t h = uint32_t(uint16_t(st.carry_health * 2 + 1)) * 0x64;
    st.carry_health = uint8_t(h >> 7);
    level_id += 1;
    if (!kValidLevels[uint8_t(level_id) & 0x3F]) return 0xFFFF;
    return 0;
}

} // namespace game
