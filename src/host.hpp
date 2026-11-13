// Host side: window, input, audio, frame pacing, and the test plan runner.
#pragma once

#include <cstdint>
#include <string>

namespace host {

struct Options {
    std::string data = "data";
    std::string rom = "rom/zt.bin";
    std::string soundfont = "/usr/share/soundfonts/FluidR3_GM.sf2";
    int scale = 3;
    std::string plan;   // test plan, same format as tools/mame/harness.lua
    bool headless = false; // no window, no audio, no frame pacing
    int64_t seed = -1;  // replaces the random seed the game takes from the HV counter
};

extern Options opt;
extern uint32_t frame_count;

bool init(const Options& o);
// One VBlank: runs the game's VBlank handler, shows the frame, reads input and
// waits for the next 1/60 s. Translated code calls this wherever the original
// waits for the VBlank interrupt.
void frame();
void set_vblank_handler(void (*handler)());

} // namespace host
