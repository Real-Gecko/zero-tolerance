#include "gems.hpp"

#include "../audio.hpp"

#include <SDL3/SDL.h>

#include <cstdio>

namespace md::gems {

static Audio* audio = nullptr;
static std::string dir;
static int patch[16];
static int rate[16];

static bool exists(const std::string& path) {
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info);
}

void init(Audio* a, const std::string& data_dir) {
    audio = a;
    dir = data_dir + "/";
    for (int& r : rate) r = 4;
}

void tempo(int bpm) {
    if (audio) audio->music_tempo(bpm);
}

void start_seq(int seq) {
    if (audio) audio->play_music(seq);
}

void stop_seq(int seq) {
    if (audio) audio->stop_music(seq);
}

void stop_all() {
    if (audio) audio->stop_music();
}

void program(int ch, int p) { patch[ch & 15] = p; }
void dac_rate(int ch, int r) { rate[ch & 15] = r; }

// Effects were recorded or extracted by tools/extract_sound.py: channel 15 is
// the DAC (note selects the sample), other channels play FM/PSG patches.
void note_on(int ch, int note) {
    if (!audio) return;
    ch &= 15;
    char name[48];
    if (ch == 15) {
        int sample = note < 48 ? note + 48 : note - 48;
        if (rate[ch] == 4) std::snprintf(name, sizeof name, "sfx/dac%02d.wav", sample);
        else std::snprintf(name, sizeof name, "sfx/dac%02d_r%d.wav", sample, rate[ch]);
    } else {
        std::snprintf(name, sizeof name, "sfx/fm_p%02d_n%02d.wav", patch[ch], note);
    }
    std::string path = dir + name;
    // DGen's levels: a full-scale DAC sample is 0.375 of the output; the FM
    // effects were recorded in MAME, which is 4 times quieter.
    float gain = ch == 15 ? 0.375f : 4.0f;
    if (exists(path)) audio->play_sfx(path, ch, gain);
}

void note_off(int ch, int) {
    if (audio && (ch & 15) != 15) audio->stop_sfx(ch & 15);
}

} // namespace md::gems
