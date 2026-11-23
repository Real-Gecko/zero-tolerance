#include "music.hpp"

#include <algorithm>

namespace {

// Voice numbers in the order the driver tries them (the key-on register values: 3 and 7 do not exist).
constexpr uint8_t kVoices[6] = {0, 1, 4, 5, 6, 2};
constexpr uint16_t kFnum[12] = {644, 682, 723, 766, 811, 859, 910, 965, 1022, 1083, 1147, 1215};
// Carrier operators per algorithm, bit n for the nth total level written below (operators 1, 2, 3, 4).
constexpr uint8_t kCarriers[8] = {0x08, 0x08, 0x08, 0x08, 0x0A, 0x0E, 0x0E, 0x0F};
constexpr int kPatchSize = 39;
constexpr int kTotalLevel = 0x80; // marks the total level entries of kRegs

// Register and offset in the patch (after its type byte) of each write, 0 for the register that is written 0.
constexpr struct {
    uint8_t reg, offset;
} kRegs[] = {
    {0xB0, 2},    {0xB4, 3},          {0x30, 4},          {0x40, 5 | kTotalLevel},  {0x50, 6},  {0x60, 7},
    {0x70, 8},    {0x80, 9},          {0x90, 0},          {0x38, 16},               {0x48, 17 | kTotalLevel}, {0x58, 18},
    {0x68, 19},   {0x78, 20},         {0x88, 21},         {0x98, 0},                {0x34, 10},  {0x44, 11 | kTotalLevel},
    {0x54, 12},   {0x64, 13},         {0x74, 14},         {0x84, 15},               {0x94, 0},   {0x3C, 22},
    {0x4C, 23 | kTotalLevel},         {0x5C, 24},         {0x6C, 25},               {0x7C, 26},  {0x8C, 27},
    {0x9C, 0},
};

} // namespace

Music::Music(ymfm::ym2612& chip, std::vector<uint8_t> patches) : chip_(chip), patches_(std::move(patches)) {
    for (int i = 0; i < 6; i++)
        voices_[i].flags = 0x80 | kVoices[i];
    for (int ch : {4, 13, 14}) // locked by the game for its effects
        tracks_[ch].flags = 0x20;
}

void Music::write(int part, int reg, int val) {
    if (part) {
        chip_.write_address_hi(uint8_t(reg));
        chip_.write_data_hi(uint8_t(val));
    } else {
        chip_.write_address(uint8_t(reg));
        chip_.write_data(uint8_t(val));
    }
}

void Music::key_off(int voice) { write(0, 0x28, voice); }

void Music::tempo(int bpm) { sbpt_ = (bpm * 218 >> 7) & 0xFFFF; }

void Music::start(int seq, const std::vector<uint8_t>& data) {
    const uint8_t* d = songs_.try_emplace(seq, data).first->second.data();
    sbptacc_ = 0;
    int first = 0;
    for (Track& t : tracks_) {
        if (first >= d[0])
            break;
        if (t.flags & 0x21)
            continue;
        t.flags = 0x11;
        t.seq = seq;
        t.base = d;
        t.pos = d[1 + 2 * first] | d[2 + 2 * first] << 8;
        t.timer = -1;
        t.atten = 0;
        for (auto& l : t.loops)
            l = {};
        first++;
    }
}

// Silences the voices of tracks that are not running.
void Music::clip_voices() {
    for (Voice& v : voices_) {
        if (tracks_[v.ch & 15].flags & 0x10)
            continue;
        int vn = v.flags & 7;
        v.flags = vn | 0x80;
        v.timer = v.release = 0;
        int part = vn > 3, c = vn & 3;
        for (int reg : {0x40, 0x44, 0x48, 0x4C})
            write(part, reg + c, 0x7F);
        key_off(vn);
    }
}

void Music::stop(int seq) {
    for (Track& t : tracks_)
        if ((t.flags & 1) && !(t.flags & 0x20) && t.seq == seq) {
            t.flags = 0;
            t.dur = 0;
        }
    clip_voices();
}

void Music::stop_all() {
    for (Track& t : tracks_) {
        t.flags = 0;
        t.dur = 0;
    }
    clip_voices();
}

// A free voice of the same track, else the one released longest ago, else the first one in use (all music
// tracks have priority 0, so any voice may be taken).
int Music::alloc(int ch) {
    int freest = -1, used = -1, freest_release = 0xFF;
    for (int i = 0; i < 6; i++) {
        const Voice& v = voices_[i];
        if (v.flags & 0x20)
            continue;
        if (v.flags & 0x80) {
            if (v.ch == ch)
                return i;
            if (v.release < freest_release) {
                freest_release = v.release;
                freest = i;
            }
        } else if (used < 0) {
            used = i;
        }
    }
    return freest >= 0 ? freest : used;
}

void Music::note_on(int ch, int note) {
    Track& t = tracks_[ch];
    const uint8_t* patch = &patches_[size_t(t.patch) * kPatchSize];
    if (patch[0] != 0)
        return;
    int i = alloc(ch);
    if (i < 0)
        return;
    Voice& v = voices_[i];
    int vn = v.flags & 7;
    if (!(v.flags & 0x80))
        key_off(vn);
    v.flags = vn | (t.dur ? 0x10 : 0);
    v.note = note;
    v.ch = ch;
    v.timer = -t.dur;
    v.release = 254;

    const uint8_t* p = patch + 1;
    int atten = std::min(t.atten, 127);
    int part = vn > 3, c = vn & 3;
    if (p[0] & 0x08)
        write(0, 0x22, p[0]);
    int carriers = kCarriers[p[2] & 7];
    for (auto [reg, offset] : kRegs) {
        int val = offset ? p[offset & ~kTotalLevel] : 0;
        if (offset & kTotalLevel) {
            if (carriers & 1) {
                int level = 127 - val;
                val = 127 - (level - (level * 2 * atten >> 8));
            }
            carriers >>= 1;
        }
        write(part, reg + c, val);
    }
    int fnum = kFnum[note % 12] | (note / 12) << 11;
    write(part, 0xA4 + c, fnum >> 8);
    write(part, 0xA0 + c, fnum & 0xFF);
    write(0, 0x28, p[36] << 4 | vn);
}

void Music::voice_timers() {
    for (Voice& v : voices_) {
        if (v.flags & 0x40) {
            if (--v.release == 0)
                v.flags &= ~0x40;
        } else if (v.flags & 0x10) {
            if (++v.timer == 0) {
                v.flags = (v.flags & ~0x18) | 0xC0;
                key_off(v.flags & 7);
            }
        }
    }
}

// Runs one command byte (96 and up) of a track: 0 to go on with the next one, 1 to wait for its delay, 2 when
// the track ended.
int Music::command(Track& t, int cmd) {
    auto next = [&] { return int(t.base[t.pos++]); };
    switch (cmd) {
    case 0x60:
        t.flags = 0;
        t.dur = 0;
        return 2;
    case 0x61:
        t.patch = next();
        return 1;
    case 0x63:
        return 1;
    case 0x64: {
        int i = 0;
        while (t.loops[i].count)
            i++;
        t.loops[i].count = next();
        t.loops[i].pos = t.pos;
        return 0;
    }
    case 0x65: {
        int i = 3;
        while (!t.loops[i].count)
            i--;
        if (t.loops[i].count != 127 && --t.loops[i].count == 0)
            return 0;
        t.pos = t.loops[i].pos;
        return 0;
    }
    case 0x68:
        tempo(next() + 40);
        return 1;
    case 0x72: {
        int code = next(), val = next();
        if (code == 5)
            t.atten = val;
        return 1;
    }
    }
    return 0;
}

void Music::sequencer(int ch) {
    Track& t = tracks_[ch];
    if (++t.timer != 0)
        return;
    int cmd = t.base[t.pos++];
    for (;;) {
        if (cmd & 0x80) {
            int group = cmd & 0xC0, value = cmd & 0x3F;
            for (;;) {
                cmd = t.base[t.pos++];
                if ((cmd & 0xC0) != group)
                    break;
                value = (value << 6 | (cmd & 0x3F)) & 0xFFFF;
            }
            (group == 0xC0 ? t.delay : t.dur) = value;
            continue;
        }
        if (cmd >= 0x60) {
            int r = command(t, cmd);
            if (r == 2)
                return;
            if (r == 0) {
                cmd = t.base[t.pos++];
                continue;
            }
        } else {
            note_on(ch, cmd);
        }
        if (t.delay) {
            t.timer = -t.delay;
            return;
        }
        cmd = t.base[t.pos++];
    }
}

void Music::tick() {
    sbptacc_ += sbpt_;
    if (sbptacc_ < 0x100)
        return;
    sbptacc_ -= 0x100;
    voice_timers();
    for (int ch = 0; ch < 16; ch++)
        if (tracks_[ch].flags & 0x10)
            sequencer(ch);
}
