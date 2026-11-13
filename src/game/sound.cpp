#include "game.hpp"

#include "../md/gems.hpp"

namespace game {

static constexpr uint32_t sound_table = 0xC5EB4; // 3 bytes per id: type, a, b

// 0xC609A: GEMS setup. The banks themselves are loaded by the host side.
void sound_init() {
    gems::dac_rate(15, 4);
    gems::program(15, rd8(sound_table + 0x16 * 2 + 0x17));
    U16{0xFF270A} = 0xD;
}

// 0xC6156
void PlaySound(uint16_t id) {
    last_sound = id;
    sub_0C615A(id);
}

// 0xC615A
void sub_0C615A(uint16_t id) {
    uint32_t e = sound_table + (id & 0xFF) * 3;
    uint8_t type = rd8(e), a = rd8(e + 1), b = rd8(e + 2);
    uint16_t opts = sound_options;
    switch (type) {
    case 0: // music; b is the tempo, already in the MIDI file
        if (opts & 1) gems::start_seq(a);
        break;
    case 1: { // FM/PSG effect on channel 13 or 14, alternating
        if (!(opts & 4)) break;
        U16 next{0xFF270A};
        int ch = next == 0xD ? 0xD : 0xE;
        S32 prev{ch == 0xD ? 0xFF270Cu : 0xFF2710u};
        if (int32_t(prev) >= 0) gems::note_off(ch, int(prev));
        prev = int32_t(b);
        gems::program(ch, a);
        gems::note_on(ch, b);
        if (next >= 0xE) next = 0xD;
        else next += 1;
        break;
    }
    case 2:
        if (!(opts & 8) || U16{0xFF2716} || U16{0xFF2718}) break;
        gems::dac_rate(15, b);
        gems::note_on(15, a);
        break;
    case 3:
        if (U16{0xFF2718}) break;
        gems::dac_rate(15, b);
        gems::note_on(15, a);
        break;
    }
}

// 0xC62E8: stops the music started by the last PlaySound.
void sub_0C62E8() {
    uint16_t id = last_sound;
    last_sound = 0;
    uint32_t e = sound_table + (id & 0xFF) * 3;
    uint8_t type = rd8(e);
    if (type != 1 && type != 2) gems::stop_seq(rd8(e + 1));
}

void sub_0C5DAC() { gems::stop_all(); }
void sub_0C633A() { gems::pause_all(); }
void sub_0C6344() { gems::resume_all(); }

} // namespace game
