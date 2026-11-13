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
    void pause_music(bool paused);
    void set_volumes(float music, float sfx);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// PlaySound ids (data/sound/sounds.txt) mapped to music and effect files.
class Sounds {
public:
    bool load(Audio* audio, const std::string& data_dir);
    void play(int id);
    void set_enabled(bool music, bool sfx);

private:
    Audio* audio_ = nullptr;
    std::string dir_;
    std::string file_[256];
    bool music_[256]{};
    bool music_on_ = true, sfx_on_ = true;
    int last_music_ = -1;
};
