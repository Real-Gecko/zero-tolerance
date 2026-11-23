// Audio: the GEMS music on an emulated YM2612 (ymfm) plus WAV sound effects,
// mixed into one SDL audio stream.
#pragma once

#include <memory>
#include <string>

class Audio {
public:
    Audio();
    ~Audio();
    bool init(const std::string& data_dir);
    void music_tempo(int bpm);
    void play_music(int seq);
    void stop_music(int seq);
    void stop_music();
    void play_sfx(const std::string& wav_path, int channel = -1, float gain = 1.0f); // a channel holds one sound at a time
    void stop_sfx(int channel);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
