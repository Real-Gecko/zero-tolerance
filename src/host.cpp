#include "host.hpp"

#include "audio.hpp"
#include "input.hpp"
#include "md/gems.hpp"
#include "md/mem.hpp"
#include "md/pad.hpp"
#include "md/vdp.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace host {

Options opt;
uint32_t frame_count = 0;

static void (*vblank_handler)() = nullptr;
static SDL_Window* window = nullptr;
static SDL_Renderer* renderer = nullptr;
static SDL_Texture* texture = nullptr;
static Audio* audio = nullptr;
static Input input;
static uint32_t pixels[320 * 240];
static uint64_t next_tick = 0;

struct PlanStep {
    std::string cmd, arg;
};
static std::multimap<uint32_t, PlanStep> plan;
static uint16_t plan_held = 0;

static uint16_t plan_button(const std::string& name) {
    static const std::map<std::string, uint16_t> names = {
        {"P1 Up", md::pad::UP}, {"P1 Down", md::pad::DOWN}, {"P1 Left", md::pad::LEFT},
        {"P1 Right", md::pad::RIGHT}, {"P1 A", md::pad::A}, {"P1 B", md::pad::B},
        {"P1 C", md::pad::C}, {"P1 Start", md::pad::START},
    };
    auto it = names.find(name);
    return it == names.end() ? 0 : it->second;
}

static void save_snap(const std::string& path) {
    int w = md::vdp::width(), h = md::vdp::height();
    SDL_Surface* s = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_XRGB8888, pixels, 320 * 4);
    if (s) {
        SDL_SavePNG(s, path.c_str());
        SDL_DestroySurface(s);
    }
}

static void quit() {
    md::gems::init(nullptr, opt.data);
    delete audio; // before SDL_Quit, which closes the audio device under it
    audio = nullptr;
    SDL_Quit();
    std::exit(0);
}

static void run_plan() {
    auto range = plan.equal_range(frame_count);
    for (auto it = range.first; it != range.second; ++it) {
        const PlanStep& s = it->second;
        if (s.cmd == "press") plan_held |= plan_button(s.arg);
        else if (s.cmd == "release") plan_held &= uint16_t(~plan_button(s.arg));
        else if (s.cmd == "snap") save_snap(s.arg);
        else if (s.cmd == "ram") {
            std::ofstream f(s.arg, std::ios::binary);
            f.write(reinterpret_cast<const char*>(md::ram), sizeof md::ram);
        } else if (s.cmd == "exit") quit();
    }
}

bool init(const Options& o) {
    opt = o;
    static std::vector<uint8_t> rom;
    std::ifstream f(opt.rom, std::ios::binary);
    rom.assign(std::istreambuf_iterator<char>(f), {});
    if (rom.size() < 0x200000) {
        std::fprintf(stderr, "cannot read ROM %s\n", opt.rom.c_str());
        return false;
    }
    md::rom = rom.data();
    md::rom_size = uint32_t(rom.size());

    if (!opt.plan.empty()) {
        std::ifstream pf(opt.plan);
        std::string line;
        while (std::getline(pf, line)) {
            std::istringstream ss(line);
            uint32_t fr;
            PlanStep s;
            if (!(ss >> fr >> s.cmd)) continue;
            std::getline(ss >> std::ws, s.arg);
            plan.emplace(fr, s);
        }
    }

    if (!SDL_Init(opt.headless ? 0 : SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    if (!opt.headless) {
        if (!SDL_CreateWindowAndRenderer("Zero Tolerance", 320 * opt.scale, 224 * opt.scale,
                                         SDL_WINDOW_RESIZABLE, &window, &renderer)) {
            std::fprintf(stderr, "window: %s\n", SDL_GetError());
            return false;
        }
        SDL_SetRenderVSync(renderer, 0);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        audio = new Audio;
        if (audio->init(opt.soundfont)) md::gems::init(audio, opt.data);
    }
    md::vdp::reset();
    next_tick = SDL_GetTicksNS();
    return true;
}

void set_vblank_handler(void (*handler)()) { vblank_handler = handler; }

void frame() {
    md::vdp::set_vblank(true);
    if (vblank_handler && md::int_mask < 6 && md::vdp::vint_enabled()) {
        int saved = md::int_mask;
        md::int_mask = 7;
        vblank_handler();
        md::int_mask = saved;
    }
    md::vdp::render(pixels, 320);
    frame_count++;
    run_plan();

    uint16_t held = plan_held;
    if (!opt.headless) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) quit();
            input.handle_event(e);
        }
        input.update();
        held |= input.held();

        int w = md::vdp::width(), h = md::vdp::height();
        SDL_UpdateTexture(texture, nullptr, pixels, 320 * 4);
        SDL_FRect src{0, 0, float(w), float(h)};
        SDL_SetRenderLogicalPresentation(renderer, 320, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_FRect dst{0, 0, 320, float(h)};
        SDL_RenderClear(renderer);
        SDL_RenderTexture(renderer, texture, &src, &dst);
        SDL_RenderPresent(renderer);

        next_tick += 1000000000ull / 60;
        uint64_t now = SDL_GetTicksNS();
        if (next_tick > now) SDL_DelayPrecise(next_tick - now);
        else if (now - next_tick > 100000000ull) next_tick = now;
    }
    md::pad::set_buttons(0, held);
    md::pad::new_frame();
    md::vdp::set_vblank(false);
}

} // namespace host
