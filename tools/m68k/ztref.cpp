// ztref: runs the original ROM on the 68000 interpreter with the port's VDP,
// pads and Z80-area stand-ins (src/md), for comparison with the port and MAME.
// The Z80 is not emulated: the GEMS command FIFO in Z80 RAM just fills up.
//
// Usage: ztref [--rom rom/zt.bin] --plan PLAN [--frames N] [--loop-ram DIR]
// PLAN has the format of tools/mame/harness.lua (press, release, snap PATH,
// ram PATH, poke ADDR HEX, exit); frames count VBlanks like the port's --plan. --loop-ram
// dumps RAM to DIR/<n>.bin each time the gameplay loop reaches 0x1442.
#include "m68k.hpp"
#include "md/mem.hpp"
#include "md/pad.hpp"
#include "md/vdp.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Bus : m68k::Bus {
    uint8_t r8(uint32_t a) override { return md::rd8(a); }
    uint16_t r16(uint32_t a) override { return md::rd16(a); }
    void w8(uint32_t a, uint8_t v) override { md::wr8(a, v); }
    void w16(uint32_t a, uint16_t v) override { md::wr16(a, v); }
};

uint32_t pixels[320 * 240];

void save_png(const std::string& path) {
    md::vdp::render(pixels, 320);
    SDL_Surface* s = SDL_CreateSurfaceFrom(md::vdp::width(), md::vdp::height(), SDL_PIXELFORMAT_XRGB8888, pixels, 320 * 4);
    if (s) {
        SDL_SavePNG(s, path.c_str());
        SDL_DestroySurface(s);
    }
}

uint16_t button(const std::string& name) {
    static const std::map<std::string, uint16_t> names = {
        {"P1 Up", md::pad::UP}, {"P1 Down", md::pad::DOWN}, {"P1 Left", md::pad::LEFT},
        {"P1 Right", md::pad::RIGHT}, {"P1 A", md::pad::A}, {"P1 B", md::pad::B},
        {"P1 C", md::pad::C}, {"P1 Start", md::pad::START},
    };
    auto it = names.find(name);
    return it == names.end() ? 0 : it->second;
}

} // namespace

int main(int argc, char** argv) {
    std::string rom_path = "rom/zt.bin", plan_path, loop_dir;
    uint64_t max_frames = 1000000;
    for (int i = 1; i < argc; i++) {
        std::string k = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (k == "--rom") rom_path = next();
        else if (k == "--plan") plan_path = next();
        else if (k == "--frames") max_frames = std::strtoull(next().c_str(), nullptr, 0);
        else if (k == "--loop-ram") loop_dir = next();
    }
    std::ifstream rf(rom_path, std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rf)), {});
    if (rom.size() < 0x200000) {
        std::fprintf(stderr, "cannot read ROM %s\n", rom_path.c_str());
        return 1;
    }
    md::rom = rom.data();
    md::rom_size = uint32_t(rom.size());
    std::multimap<uint64_t, std::pair<std::string, std::string>> plan;
    if (!plan_path.empty()) {
        std::ifstream pf(plan_path);
        std::string line;
        while (std::getline(pf, line)) {
            std::istringstream ss(line);
            uint64_t fr;
            std::string cmd, arg;
            if (!(ss >> fr >> cmd)) continue;
            std::getline(ss >> std::ws, arg);
            plan.emplace(fr, std::make_pair(cmd, arg));
        }
    }
    SDL_Init(0);
    md::vdp::reset();

    Bus bus;
    m68k::Cpu cpu;
    cpu.bus = &bus;
    cpu.reset();
    // NTSC: 262 lines of 3420 master clocks; the 68000 runs at master / 7.
    const uint64_t line_clocks = 3420;
    uint64_t master = 0;
    uint16_t held = 0;
    uint64_t loop_frames = 0;
    for (uint64_t frame = 0; frame < max_frames; frame++) {
        for (int line = 0; line < 262; line++) {
            master += line_clocks;
            while (cpu.cycles * 7 < master) {
                if (!loop_dir.empty() && cpu.pc == 0x1442 && !cpu.stopped) {
                    std::ofstream f(loop_dir + "/" + std::to_string(loop_frames++) + ".bin", std::ios::binary);
                    f.write(reinterpret_cast<const char*>(md::ram), sizeof md::ram);
                }
                uint32_t before = cpu.pc;
                cpu.step();
                if (cpu.halted) {
                    std::fprintf(stderr, "halted at %06X (frame %llu)\n", before, (unsigned long long)frame);
                    return 2;
                }
                if (cpu.last_exception >= 0 && (cpu.last_exception < 24 || cpu.last_exception >= 32)) {
                    std::fprintf(stderr, "exception %d at %06X (frame %llu)\n", cpu.last_exception, before,
                                 (unsigned long long)frame);
                    return 2;
                }
                cpu.last_exception = -1;
            }
            if (line == 223) {
                md::vdp::set_vblank(true);
                // Plan commands run at the start of VBlank, as in the port's host::frame().
                auto range = plan.equal_range(frame + 1);
                for (auto it = range.first; it != range.second; ++it) {
                    const auto& [cmd, arg] = it->second;
                    if (cmd == "press") held |= button(arg);
                    else if (cmd == "release") held &= uint16_t(~button(arg));
                    else if (cmd == "snap") save_png(arg);
                    else if (cmd == "ram") {
                        std::ofstream f(arg, std::ios::binary);
                        f.write(reinterpret_cast<const char*>(md::ram), sizeof md::ram);
                    } else if (cmd == "poke") { // poke ADDR HEXBYTES
                        std::istringstream ps(arg);
                        std::string addr, bytes;
                        ps >> addr >> bytes;
                        uint32_t a = uint32_t(std::stoul(addr, nullptr, 16));
                        for (size_t i = 0; i + 1 < bytes.size(); i += 2)
                            md::wr8(a + uint32_t(i / 2), uint8_t(std::stoul(bytes.substr(i, 2), nullptr, 16)));
                    } else if (cmd == "exit") return 0;
                }
                md::pad::set_buttons(0, held);
                md::pad::new_frame();
                if (md::vdp::vint_enabled()) cpu.set_irq(6);
            }
        }
        md::vdp::set_vblank(false);
    }
    return 0;
}
