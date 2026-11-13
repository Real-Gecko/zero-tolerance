// High-level stand-in for the GEMS sound driver on the Z80. The game's GEMS
// command wrappers call these instead of writing to the Z80 command FIFO.
#pragma once

#include <string>

class Audio;

namespace md::gems {

void init(Audio* audio, const std::string& data_dir);
void start_seq(int seq);
void stop_seq(int seq);
void stop_all();
void pause_all();
void resume_all();
void program(int ch, int patch);
void dac_rate(int ch, int rate);
void note_on(int ch, int note);
void note_off(int ch, int note);

} // namespace md::gems
