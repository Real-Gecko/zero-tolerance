// The music side of the GEMS Z80 driver: the sequencer, the FM voice allocator and the note-on register
// writes, run 60 times a second on a YM2612.
#pragma once

#include "ymfm/ymfm_opn.h"

#include <cstdint>
#include <map>
#include <vector>

class Music {
public:
    Music(ymfm::ym2612& chip, std::vector<uint8_t> patches);
    void tempo(int bpm);
    void start(int seq, const std::vector<uint8_t>& data);
    void stop(int seq);
    void stop_all();
    void tick();

private:
    struct Track {
        uint8_t flags = 0; // 1 in use, 0x10 running, 0x20 locked
        int seq = 0;
        const uint8_t* base = nullptr;
        size_t pos = 0;
        int timer = 0, delay = 0, dur = 0;
        int patch = 0, atten = 0;
        struct { int count; size_t pos; } loops[4] = {};
    };
    struct Voice {
        uint8_t flags;
        int note = 0, ch = 0, timer = 0, release = 0;
    };

    void write(int part, int reg, int val);
    void key_off(int voice);
    void clip_voices();
    int alloc(int ch);
    void note_on(int ch, int note);
    void voice_timers();
    void sequencer(int ch);
    int command(Track& t, int cmd);

    ymfm::ym2612& chip_;
    std::vector<uint8_t> patches_;
    std::map<int, std::vector<uint8_t>> songs_;
    Track tracks_[16];
    Voice voices_[6];
    int sbpt_ = 0, sbptacc_ = 0;
};
