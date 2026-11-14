#include "host.hpp"

#include "assets.hpp"
#include "audio.hpp"
#include "game/game.hpp"
#include "game/state.hpp"
#include "game/display.hpp"
#include "input.hpp"
#include "md/gems.hpp"
#include "game/display.hpp"
#include "game/hires.hpp"
#include "game/textures.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <utility>
#include <vector>

namespace host {

Options opt;
uint32_t frame_count = 0;
void (*wide_screen)(int extra) = nullptr;

static void (*vblank_handler)() = nullptr;
static SDL_Window* window = nullptr;
static SDL_Renderer* renderer = nullptr;
static SDL_Texture* texture = nullptr;
static SDL_Texture* wide_texture = nullptr; // screens shown wider (wide_screen)
static SDL_Texture* view_texture = nullptr; // the high resolution 3D view
static SDL_Texture* sky_texture = nullptr;   // sprites in the view (sky)
static SDL_Texture* weapon_texture = nullptr;
static SDL_Texture* hud_texture = nullptr;
static SDL_Texture* menu_texture = nullptr; // pause menu screen
static Audio* audio = nullptr;
static Input input;
static uint32_t pixels[320 * 240];
static uint64_t next_tick = 0;
static uint16_t last_held = 0;
static uint8_t pad_held = 0;
int int_mask = 7;
static bool any_press = false;
static bool text_on = false; // text_input()
static std::string typed;
static uint32_t mouse_read_frame = 0;
static float mouse_rest = 0;
constexpr float kMousePixelsPerTurnUnit = 4; // 512 units per full turn

struct PlanStep {
    std::string cmd, arg;
};
static std::multimap<uint32_t, PlanStep> plan;
static uint16_t plan_held = 0;
static std::string shot_path; // window screenshot for the next present
static bool esc_requested = false;
static bool menu_on = false;
static std::string menu_title;
static std::vector<std::string> menu_items;
static uint32_t menu_enabled = 0;
static int menu_selected = 0;

static uint16_t plan_button(const std::string& name) {
    static const std::map<std::string, uint16_t> names = {
        {"P1 Up", PAD_UP}, {"P1 Down", PAD_DOWN}, {"P1 Left", PAD_LEFT},
        {"P1 Right", PAD_RIGHT}, {"P1 A", PAD_A}, {"P1 B", PAD_B},
        {"P1 C", PAD_C}, {"P1 Start", PAD_START},
    };
    auto it = names.find(name);
    return it == names.end() ? 0 : it->second;
}

static void save_snap(const std::string& path) {
    SDL_Surface* s = SDL_CreateSurfaceFrom(320, 224, SDL_PIXELFORMAT_XRGB8888, pixels, 320 * 4);
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

// The character a key types on a US keyboard, whatever the active layout
// (a Cyrillic one gives no password characters). 0 for other keys.
static char us_char(SDL_Scancode sc, SDL_Keymod mod) {
    bool shift = mod & SDL_KMOD_SHIFT;
    if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return char((shift != !!(mod & SDL_KMOD_CAPS) ? 'A' : 'a') + sc - SDL_SCANCODE_A);
    if (sc >= SDL_SCANCODE_1 && sc <= SDL_SCANCODE_0) return shift ? "!@#$%^&*()"[sc - SDL_SCANCODE_1] : "1234567890"[sc - SDL_SCANCODE_1];
    if (sc == SDL_SCANCODE_MINUS) return shift ? '_' : '-';
    if (sc == SDL_SCANCODE_SLASH) return shift ? '?' : '/';
    if (sc == SDL_SCANCODE_SPACE) return ' ';
    return 0;
}

static void run_plan() {
    auto range = plan.equal_range(frame_count);
    for (auto it = range.first; it != range.second; ++it) {
        const PlanStep& s = it->second;
        if (s.cmd == "press") plan_held |= plan_button(s.arg);
        else if (s.cmd == "release") plan_held &= uint16_t(~plan_button(s.arg));
        else if (s.cmd == "snap") save_snap(s.arg);
        else if (s.cmd == "shot") shot_path = s.arg;
        else if (s.cmd == "esc") esc_requested = true;
        else if (s.cmd == "type") typed += s.arg; // tests: keyboard text
        else if (s.cmd == "key") typed += s.arg == "Return" ? '\r' : s.arg == "BackSpace" ? '\b' : '\x1b';
        else if (s.cmd == "password") { // tests: start a new game from a password
            for (size_t i = 0; i < 9; i++) game::st.password[i] = i < s.arg.size() ? uint8_t(s.arg[i]) : 0x5F;
        } else if (s.cmd == "view") { // tests: the shown 3D view, also headless
            static uint32_t px[512 * 160];
            uint32_t pal[64];
            for (int i = 0; i < 64; i++) pal[i] = game::display::argb(i);
            std::fill(std::begin(px), std::end(px), 0u);
            game::hires::draw(px, 512, 160, pal);
            if (SDL_Surface* v = SDL_CreateSurfaceFrom(512, 160, SDL_PIXELFORMAT_ARGB8888, px, 512 * 4)) {
                SDL_SavePNG(v, s.arg.c_str());
                SDL_DestroySurface(v);
            }
        } else if (s.cmd == "ram") { // tests: the game state
            std::ofstream f(s.arg, std::ios::binary);
            f.write(reinterpret_cast<const char*>(&game::st), sizeof game::st);
        } else if (s.cmd == "disp") {
            std::ofstream f(s.arg, std::ios::binary);
            auto w16 = [&](uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
            w16(game::display::ntA);
            w16(game::display::ntB);
            w16(uint16_t(game::display::pwA));
            w16(uint16_t(game::display::phA));
            w16(uint16_t(game::display::pwB));
            w16(uint16_t(game::display::phB));
            f.write(reinterpret_cast<const char*>(game::display::hscrollA), sizeof game::display::hscrollA);
            f.write(reinterpret_cast<const char*>(game::display::hscrollB), sizeof game::display::hscrollB);
            f.write(reinterpret_cast<const char*>(game::display::pal), sizeof game::display::pal);
            f.write(reinterpret_cast<const char*>(game::display::planeA), sizeof game::display::planeA);
            f.write(reinterpret_cast<const char*>(game::display::planeB), sizeof game::display::planeB);
            f.write(reinterpret_cast<const char*>(game::display::tiles), sizeof game::display::tiles);
        } else if (s.cmd == "exit") quit();
    }
}

bool init(const Options& o) {
    opt = o;
    if (opt.soundfont.empty()) opt.soundfont = opt.data + "/gm.sf2";
    assets::init(opt.data);

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
                                         SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN, &window, &renderer)) {
            std::fprintf(stderr, "window: %s\n", SDL_GetError());
            return false;
        }
        SDL_SetRenderVSync(renderer, 0);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_SetWindowRelativeMouseMode(window, true);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        wide_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888, SDL_TEXTUREACCESS_STREAMING, 1024, 240);
        SDL_SetTextureScaleMode(wide_texture, SDL_SCALEMODE_NEAREST);
        for (SDL_Texture** t : {&sky_texture, &weapon_texture, &hud_texture, &menu_texture}) {
            *t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 320, 240);
            SDL_SetTextureScaleMode(*t, SDL_SCALEMODE_NEAREST);
            SDL_SetTextureBlendMode(*t, SDL_BLENDMODE_BLEND);
        }
        SDL_SetTextureScaleMode(hud_texture, SDL_SCALEMODE_PIXELART); // fractional panel scale
        SDL_SetTextureScaleMode(menu_texture, SDL_SCALEMODE_PIXELART);
        audio = new Audio;
        if (audio->init(opt.soundfont)) md::gems::init(audio, opt.data);
    }
    game::display::reset();
    game::textures::load(opt.data);
    next_tick = SDL_GetTicksNS();
    return true;
}

void set_vblank_handler(void (*handler)()) { vblank_handler = handler; }

uint8_t strafe_keys() { return uint8_t(input.strafe()); }

std::string save_path(int slot) {
    char* p = SDL_GetPrefPath("ZeroTolerance", "zt");
    std::string dir = p ? p : "./";
    if (p) SDL_free(p);
    return dir + "save" + std::to_string(slot + 1) + ".bin";
}

void menu_show(const std::string& title, const std::vector<std::string>& items, uint32_t enabled, int selected) {
    menu_on = true;
    menu_title = title;
    menu_items = items;
    menu_enabled = enabled;
    menu_selected = selected;
}

void menu_hide() { menu_on = false; }

bool take_menu_request() {
    bool r = esc_requested;
    esc_requested = false;
    return r;
}

void capture_mouse() {
    if (window) SDL_SetWindowRelativeMouseMode(window, true);
}

void text_input(bool on) {
    text_on = on;
    typed.clear();
}

std::string take_text() { return std::exchange(typed, {}); }

bool any_pressed() {
    bool r = any_press;
    any_press = false;
    return r;
}

int mouse_turn() {
    mouse_read_frame = frame_count;
    float x = input.take_mouse_x() / kMousePixelsPerTurnUnit + mouse_rest;
    int n = int(x);
    mouse_rest = x - float(n);
    return n;
}

// The gameplay screen: the 3D view tiles in plane B at 0xC288 (0x29DA).
static bool gameplay_screen() {
    return game::display::ntB == 0xC000 && game::display::planeB[0x144] == 0x8001 &&
           game::display::planeB[0x145] == 0x800B;
}

static bool weapon_sprite(uint16_t entry) {
    uint16_t tile = entry & 0x7FF; // weapon graphics at tiles 0x500-0x514
    return tile >= 0x500 && tile < 0x515;
}
// Low priority sprites are behind the view's walls and floor, high priority
// ones (the lightning) in front. The skyline (tiles 0x515-0x544) is drawn
// unclipped by game::hires::skyline_pixel instead.
static bool back_sprite(uint16_t entry) {
    uint16_t tile = entry & 0x7FF;
    return !weapon_sprite(entry) && !(entry & 0x8000) && (tile < 0x515 || tile > 0x544);
}
static bool front_sprite(uint16_t entry) { return !weapon_sprite(entry) && (entry & 0x8000); }

// The 3D view at twice the original resolution (game::hires), as many rows as
// the window's aspect needs, over the backdrop.
static void draw_view(int w, int h) {
    int vw = int(std::lround(512 * game::hires::shown.tan));
    int vh = std::max(1, int(std::lround(double(vw) * h / std::max(1, w))));
    static std::vector<uint32_t> argb;
    static int tw = 0, th = 0;
    if (vw != tw || vh != th) {
        if (view_texture) SDL_DestroyTexture(view_texture);
        view_texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, vw, vh);
        SDL_SetTextureScaleMode(view_texture, SDL_SCALEMODE_NEAREST);
        tw = vw;
        th = vh;
    }
    argb.resize(size_t(vw) * vh);
    uint32_t pal[64];
    for (int i = 0; i < 64; i++) pal[i] = game::display::argb(i);
    game::hires::draw(argb.data(), vw, vh, pal);
    uint32_t bg = game::display::argb(0);
    static uint32_t back[320 * 240];
    game::display::compose(back, 320, 0, 320, {false, back_sprite, true});
    // Screen position of the view in the 320x224 picture: x 32-288 for the
    // middle 90 degrees, rows 40-120.
    float k = 2, x0 = 32 - 128 * (game::hires::shown.tan - 1);
    for (int y = 0; y < vh; y++) {
        int sy = int(std::floor(80 + (float(y) + 0.5f - float(vh) / 2) / k));
        for (int x = 0; x < vw; x++) {
            uint32_t& c = argb[size_t(y) * vw + x];
            if (!c) {
                int sx = int(std::floor(x0 + (float(x) + 0.5f) / k));
                uint32_t s = sx >= 32 && sx < 288 && sy >= 40 && sy < 120 ? back[sy * 320 + sx] : 0;
                int b = game::hires::skyline_pixel(sx, sy);
                if (!b) b = game::hires::backdrop(sx, sy);
                c = s ? s : b ? pal[b] : bg;
            }
        }
    }
    SDL_UpdateTexture(view_texture, nullptr, argb.data(), vw * 4);
    SDL_FRect dst{0, 0, float(w), float(h)};
    SDL_RenderTexture(renderer, view_texture, nullptr, &dst);
}

// Makes the black around a HUD piece transparent (the ID card's rounded
// corners, the ends of the bar): black reachable from the piece's border.
static void clear_outside(uint32_t* p, const SDL_FRect& r) {
    int x0 = int(r.x), y0 = int(r.y), x1 = x0 + int(r.w), y1 = y0 + int(r.h);
    std::vector<std::pair<int, int>> todo;
    for (int x = x0; x < x1; x++) todo.insert(todo.end(), {{x, y0}, {x, y1 - 1}});
    for (int y = y0; y < y1; y++) todo.insert(todo.end(), {{x0, y}, {x1 - 1, y}});
    while (!todo.empty()) {
        auto [x, y] = todo.back();
        todo.pop_back();
        if (x < x0 || x >= x1 || y < y0 || y >= y1 || p[y * 320 + x] != 0xFF000000u) continue;
        p[y * 320 + x] = 0;
        todo.insert(todo.end(), {{x + 1, y}, {x - 1, y}, {x, y + 1}, {x, y - 1}});
    }
}

// A recessed slot in the HUD panel around dst: dark rim, lit lower right; t =
// line width.
static void draw_recess(const SDL_FRect& dst, float t, uint32_t dark, uint32_t edge) {
    auto set = [](uint32_t c) { SDL_SetRenderDrawColor(renderer, c >> 16 & 0xFF, c >> 8 & 0xFF, c & 0xFF, 255); };
    SDL_FRect rim{dst.x - 2 * t, dst.y - 2 * t, dst.w + 4 * t, dst.h + 4 * t};
    set(dark);
    SDL_RenderFillRect(renderer, &rim);
    rim = {rim.x + rim.w - t, rim.y + t, t, rim.h - t}; // lit lower right
    set(edge);
    SDL_RenderFillRect(renderer, &rim);
    rim = {dst.x - t, dst.y + dst.h + t, dst.w + 2 * t, t};
    SDL_RenderFillRect(renderer, &rim);
}

// The gameplay screen laid out for the window: a solid HUD panel across the
// bottom, the 3D view above it (90 degrees across a 4:3 view), the weapon
// standing on the panel.
static void present_gameplay() {
    static uint32_t sky[320 * 240], weapon[320 * 240];
    game::display::compose(sky, 320, 0, 320, {false, front_sprite, true});
    game::display::compose(weapon, 320, 0, 320, {false, weapon_sprite, true});
    SDL_UpdateTexture(sky_texture, nullptr, sky, 320 * 4);
    SDL_UpdateTexture(weapon_texture, nullptr, weapon, 320 * 4);
    int w, h;
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    SDL_RenderClear(renderer);

    // HUD pieces of the 320x224 screen, at panel position (x, y) and zoom z:
    // message screen (0x1E0FA), radar, the five inventory slots, enemy count
    // over health, ID card.
    struct Piece {
        SDL_FRect src;
        float x, y, z;
        bool cut;
    };
    static const Piece pieces[] = {
        {{15, 159, 73, 58}, 0, 19, 1, false},   {{102, 135, 84, 83}, 81, 7, 1, false},
        {{38, 1, 36, 36}, 173, 34, 1.5f, false},  {{86, 1, 36, 36}, 233, 34, 1.5f, false},
        {{142, 1, 36, 36}, 293, 34, 1.5f, false}, {{198, 1, 36, 36}, 353, 34, 1.5f, false},
        {{246, 1, 36, 36}, 413, 34, 1.5f, false}, {{0, 66, 32, 18}, 475, 27, 1, false},
        {{292, 66, 24, 18}, 479, 51, 1, false}, {{192, 128, 128, 96}, 515, 0, 1, true}};
    const float panel_w = 643, panel_h = 96, margin = 4;
    static uint32_t hud[320 * 240];
    std::copy(std::begin(pixels), std::end(pixels), hud);
    for (const Piece& p : pieces)
        if (p.cut) clear_outside(hud, p.src);
    SDL_UpdateTexture(hud_texture, nullptr, hud, 320 * 4);
    float s = std::min(std::max(1.0f, float(h) / 640), float(w) / (panel_w + 2 * margin));
    float top = std::round(float(h) - (panel_h + 2 * margin) * s);

    // Hor+ field of view: 90 degrees across a 4:3 window, wider for wider ones.
    game::hires::view_tan = std::max(1.0f, float(w) / float(h) * 0.75f);
    draw_view(w, int(top));
    // The sky sprites cover the middle 90 degrees.
    float k = float(w) / (256 * game::hires::shown.tan), v = float(w) / 256;
    SDL_FRect view_src{32, 40, 256, 80};
    SDL_FRect sky_dst{(float(w) - 256 * k) / 2, top / 2 - 40 * k, 256 * k, 80 * k};
    SDL_RenderTexture(renderer, sky_texture, &view_src, &sky_dst);
    SDL_FRect weapon_dst{0, top - 80 * v, float(w), 80 * v};
    SDL_RenderTexture(renderer, weapon_texture, &view_src, &weapon_dst);

    // The panel in the HUD's own colours (sampled from the frame, so fades
    // apply): body, a highlight line along the top, and a recessed slot per piece.
    auto set = [](uint32_t c) { SDL_SetRenderDrawColor(renderer, c >> 16 & 0xFF, c >> 8 & 0xFF, c & 0xFF, 255); };
    uint32_t light = pixels[124 * 320 + 1], dark = pixels[124 * 320 + 2], edge = pixels[124 * 320 + 27];
    uint32_t body = (light >> 1 & 0x7F7F7F) + (dark >> 1 & 0x7F7F7F);
    set(body);
    SDL_FRect panel{0, top, float(w), float(h) - top};
    SDL_RenderFillRect(renderer, &panel);
    set(edge);
    float t = std::round(s); // line width
    SDL_FRect line{0, top, float(w), t};
    SDL_RenderFillRect(renderer, &line);
    float x0 = (float(w) - panel_w * s) / 2, y0 = top + margin * s;
    for (const Piece& p : pieces) {
        float x = std::round(x0 + p.x * s), y = std::round(y0 + p.y * s);
        SDL_FRect dst{x, y, std::round(x0 + (p.x + p.src.w * p.z) * s) - x,
                      std::round(y0 + (p.y + p.src.h * p.z) * s) - y};
        draw_recess(dst, t, dark, edge);
        SDL_RenderTexture(renderer, hud_texture, &p.src, &dst);
    }
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
}

// The PC menu (menu_show) over any screen, a piece of the HUD panel: a screen
// like the message screen's in its font (kMsgFont; tiles 0x16E758 from 0x574,
// 0x573 the blank) and the game palette (panel colours 57/56/50 as
// sampled in present_gameplay), scaled like the panel. The selected item is
// lit on a bar, the others half bright, disabled ones a quarter.
static void draw_menu() {
    auto pal = [](int i) {
        static const uint8_t lv[8] = {0, 0x34, 0x57, 0x74, 0x90, 0xAC, 0xCE, 0xFF};
        uint16_t c = assets::palette("palettes/game.png")[i];
        return 0xFF000000u | uint32_t(lv[c >> 1 & 7]) << 16 | uint32_t(lv[c >> 5 & 7]) << 8 | lv[c >> 9 & 7];
    };
    uint32_t light = pal(57), dark = pal(56), edge = pal(50);
    uint32_t body = (light >> 1 & 0x7F7F7F) + (dark >> 1 & 0x7F7F7F);
    int n = int(menu_items.size());
    size_t longest = menu_title.size();
    for (const std::string& s : menu_items) longest = std::max(longest, s.size());
    const int sw = std::max(128, int(longest) * 8 + 24), sh = 22 + n * 14; // the screen in HUD pixels
    static uint32_t px[320 * 240];
    auto dim = [](uint32_t c, int shift) {
        return 0xFF000000u | (c >> shift & (shift == 0 ? 0xFFFFFF : shift == 1 ? 0x7F7F7F : 0x3F3F3F));
    };
    auto text = [&](const std::string& s, int x0, int y0, int shift) {
        for (size_t k = 0; k < s.size(); k++) {
            uint16_t e = game::kMsgFont[s[k] - 0x20];
            if ((e & 0x7FF) < 0x574) continue; // blank
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    uint8_t b = assets::tiles("screens/tiles/hud_AE60.png")[((e & 0x7FF) - 0x574) * 32 + size_t(y * 4 + x / 2)];
                    int c = x & 1 ? b & 15 : b >> 4;
                    if (c && c != 15) px[(y0 + y) * sw + x0 + int(k) * 8 + x] = dim(pal((e >> 13 & 3) * 16 + c), shift);
                }
        }
    };
    std::fill(px, px + sw * sh, 0xFF000000u);
    text(menu_title, (sw - int(menu_title.size()) * 8) / 2, 6, 0);
    for (int i = 0; i < n; i++) {
        int y0 = 18 + i * 14;
        if (i == menu_selected)
            for (int y = y0 - 3; y < y0 + 11; y++)
                for (int x = 4; x < sw - 4; x++) px[y * sw + x] = dark;
        text(menu_items[size_t(i)], 12, y0, i == menu_selected ? 0 : menu_enabled >> i & 1 ? 1 : 2);
    }
    SDL_Rect area{0, 0, sw, sh};
    SDL_UpdateTexture(menu_texture, &area, px, sw * 4);

    int w, h;
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    float s = std::min(std::max(1.0f, float(h) / 640), float(w) / 651), t = std::round(s);
    const float z = 2, margin = 6;
    float mw = std::round((sw * z + 2 * margin) * s), mh = std::round((sh * z + 2 * margin) * s);
    float mx = std::round((float(w) - mw) / 2), my = std::round((float(h) - mh) / 2);
    auto set = [](uint32_t c) { SDL_SetRenderDrawColor(renderer, c >> 16 & 0xFF, c >> 8 & 0xFF, c & 0xFF, 255); };
    SDL_FRect r{mx - t, my - t, mw + 2 * t, mh + 2 * t};
    set(dark);
    SDL_RenderFillRect(renderer, &r);
    r = {mx, my, mw, mh};
    set(body);
    SDL_RenderFillRect(renderer, &r);
    r = {mx, my, mw, t};
    set(edge);
    SDL_RenderFillRect(renderer, &r);
    SDL_FRect dst{std::round(mx + margin * s), std::round(my + margin * s), std::round(sw * z * s), std::round(sh * z * s)};
    draw_recess(dst, t, dark, edge);
    SDL_FRect src{0, 0, float(sw), float(sh)};
    SDL_RenderTexture(renderer, menu_texture, &src, &dst);
}

// A screen as wide as the window (wide_screen): the game draws the extra
// columns on both sides, then the display is composited wider.
static void present_wide(int w, int h) {
    int ow, oh;
    SDL_GetRenderOutputSize(renderer, &ow, &oh); // not the logical size
    int extra = std::clamp((h * ow / std::max(1, oh) - w) / 2, 0, (1024 - w) / 2);
    wide_screen(extra);
    static uint32_t wide[1024 * 240];
    int ww = w + 2 * extra;
    game::display::compose(wide, ww, -extra, ww);
    SDL_UpdateTexture(wide_texture, nullptr, wide, ww * 4);
    SDL_FRect src{0, 0, float(ww), float(h)};
    SDL_SetRenderLogicalPresentation(renderer, ww, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_RenderClear(renderer);
    SDL_RenderTexture(renderer, wide_texture, &src, &src);
}

void frame() {
    if (vblank_handler && int_mask < 6) {
        int saved = int_mask;
        int_mask = 7;
        vblank_handler();
        int_mask = saved;
    }
    game::display::compose(pixels, 320, 0, 320);
    frame_count++;
    run_plan();

    uint16_t held = plan_held;
    if (!opt.headless) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) quit();
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) any_press = true;
            if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && e.key.scancode == SDL_SCANCODE_RETURN &&
                (e.key.mod & SDL_KMOD_ALT))
                SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
            // Mouse look captures the mouse; Escape releases it, a click takes it
            // back (that click does not fire).
            if (text_on) {
                // The keyboard types instead of working the pad.
                if (e.type == SDL_EVENT_KEY_DOWN && !(e.key.mod & SDL_KMOD_ALT)) {
                    if (char c = us_char(e.key.scancode, e.key.mod)) typed += c;
                    else if (e.key.scancode == SDL_SCANCODE_BACKSPACE) typed += '\b';
                    else if (!e.key.repeat && (e.key.scancode == SDL_SCANCODE_RETURN || e.key.scancode == SDL_SCANCODE_KP_ENTER))
                        typed += '\r';
                    else if (!e.key.repeat && e.key.scancode == SDL_SCANCODE_ESCAPE) typed += '\x1b';
                }
                if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_TEXT_INPUT) continue;
            }
            if (e.type == SDL_EVENT_KEY_DOWN && e.key.scancode == SDL_SCANCODE_ESCAPE) {
                SDL_SetWindowRelativeMouseMode(window, false);
                if (!e.key.repeat) esc_requested = true;
            }
            bool captured = SDL_GetWindowRelativeMouseMode(window);
            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) SDL_SetWindowRelativeMouseMode(window, true);
            if ((e.type != SDL_EVENT_MOUSE_MOTION && e.type != SDL_EVENT_MOUSE_BUTTON_DOWN) || captured)
                input.handle_event(e);
        }
        input.update();
        held |= input.held();
        // Drop mouse motion nobody asked for (menus), so it does not turn the
        // player when gameplay starts.
        if (frame_count - mouse_read_frame > 10) {
            input.take_mouse_x();
            mouse_rest = 0;
        }

        int w = 320, h = 224;
        SDL_UpdateTexture(texture, nullptr, pixels, 320 * 4);
        if (gameplay_screen()) present_gameplay();
        else if (wide_screen) present_wide(w, h);
        else {
            SDL_FRect src{0, 0, float(w), float(h)};
            SDL_SetRenderLogicalPresentation(renderer, 320, h, SDL_LOGICAL_PRESENTATION_LETTERBOX);
            SDL_FRect dst{0, 0, 320, float(h)};
            SDL_RenderClear(renderer);
            SDL_RenderTexture(renderer, texture, &src, &dst);
        }
        if (menu_on) draw_menu();
        if (!shot_path.empty()) {
            if (SDL_Surface* s = SDL_RenderReadPixels(renderer, nullptr)) {
                SDL_SavePNG(s, shot_path.c_str());
                SDL_DestroySurface(s);
            }
            shot_path.clear();
        }
        SDL_RenderPresent(renderer);

        next_tick += 1000000000ull / 60;
        uint64_t now = SDL_GetTicksNS();
        if (next_tick > now) SDL_DelayPrecise(next_tick - now);
        else if (now - next_tick > 100000000ull) next_tick = now;
    }
    if (held & ~last_held) any_press = true;
    last_held = held;
    pad_held = uint8_t(held);
}

uint8_t pad() { return pad_held; }

} // namespace host
