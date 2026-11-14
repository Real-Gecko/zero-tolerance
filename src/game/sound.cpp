#include "game.hpp"

#include "../md/gems.hpp"

namespace game {

// 0xC609A: GEMS setup. The banks themselves are loaded by the host side.
void sound_init() {
    md::gems::dac_rate(15, 4);
    md::gems::program(15, kSounds[0x16][1]);
    st.fm_channel = 0xD;
}

// 0xC6156
void PlaySound(uint16_t id) {
    last_sound = id;
    GemsPlaySound(id);
}

bool sound_muted = false;

// 0xC615A
void GemsPlaySound(uint16_t id) {
    if (sound_muted) return;
    const uint8_t* e = kSounds[id & 0xFF];
    uint8_t type = e[0], a = e[1], b = e[2];
    uint16_t opts = sound_options;
    switch (type) {
    case 0: // music; b is the tempo, already in the MIDI file
        if (opts & 1) md::gems::start_seq(a);
        break;
    case 1: { // FM/PSG effect on channel 13 or 14, alternating
        if (!(opts & 4)) break;
        auto& next = st.fm_channel;
        int ch = next == 0xD ? 0xD : 0xE;
        int32_t& prev = st.fm_note[ch - 0xD];
        if (prev >= 0) md::gems::note_off(ch, int(prev));
        prev = int32_t(b);
        md::gems::program(ch, a);
        md::gems::note_on(ch, b);
        if (next >= 0xE) next = 0xD;
        else next += 1;
        break;
    }
    case 2:
        if (!(opts & 8) || st.speech_on || st.dac_hold) break;
        md::gems::dac_rate(15, b);
        md::gems::note_on(15, a);
        break;
    case 3:
        if (st.dac_hold) break;
        md::gems::dac_rate(15, b);
        md::gems::note_on(15, a);
        break;
    }
}

// 0xC62E8: stops the music started by the last PlaySound.
void StopMusic() {
    uint16_t id = last_sound;
    last_sound = 0;
    const uint8_t* e = kSounds[id & 0xFF];
    if (e[0] != 1 && e[0] != 2) md::gems::stop_seq(e[1]);
}

void StopAllSound() { md::gems::stop_all(); } // 0xC5DAC

} // namespace game
