// Zero Tolerance native port: translated 68000 game code on a host VDP.
#include "game/game.hpp"
#include "host.hpp"

#include <SDL3/SDL_main.h>

#include <cstdlib>
#include <string>

int main(int argc, char** argv) {
    host::Options o;
    for (int i = 1; i < argc; i++) {
        std::string k = argv[i];
        auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
        if (k == "--data") o.data = next();
        else if (k == "--soundfont") o.soundfont = next();
        else if (k == "--scale") o.scale = std::atoi(next().c_str());
        else if (k == "--plan") o.plan = next();
        else if (k == "--seed") o.seed = std::strtoll(next().c_str(), nullptr, 0);
        else if (k == "--headless") o.headless = true;
    }
    if (!host::init(o)) return 1;
    game::Reset();
}
