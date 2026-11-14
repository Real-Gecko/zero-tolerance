// Audio: MIDI music through FluidSynth plus WAV sound effects, mixed into one
// SDL audio stream.
#pragma once

#include <memory>
#include <string>

class Audio {
public:
    Audio();
    ~Audio();
    bool init(const std::string& soundfont);
    void play_music(const std::string& midi_path); // loops between loopStart/loopEnd markers if present
    void stop_music();
    void play_sfx(const std::string& wav_path, int channel = -1); // a channel holds one sound at a time
    void stop_sfx(int channel);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
