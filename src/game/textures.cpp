#include "textures.hpp"

#include "game.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

namespace game::textures {

namespace {

struct Sheet {
    int cols, count, size; // size = block side in pixels
    int pitch;             // pixels per sheet row
    std::vector<uint32_t> px;
    std::vector<std::vector<uint32_t>> blocks; // cut out on first use
};
std::vector<Sheet> sheets;
std::vector<uint8_t> skies[2]; // outdoors, indoors: 1024x64 colour indices

uint32_t md_color(uint16_t c) {
    static const uint8_t lv[8] = {0, 0x34, 0x57, 0x74, 0x90, 0xAC, 0xCE, 0xFF};
    return 0xFF000000u | uint32_t(lv[(c >> 1) & 7]) << 16 | uint32_t(lv[(c >> 5) & 7]) << 8 | lv[(c >> 9) & 7];
}

uint32_t base(int i) { return md_color(assets::palette("palettes/game.png")[i]); }

// The PNG as ARGB; in an indexed PNG colour 0 is transparent.
bool load_png(const std::string& path, int& w, int& h, std::vector<uint32_t>& out) {
    SDL_Surface* s = SDL_LoadPNG(path.c_str());
    if (!s) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), SDL_GetError());
        return false;
    }
    w = s->w;
    h = s->h;
    out.resize(size_t(w) * h);
    if (SDL_Palette* pal = SDL_GetSurfacePalette(s); pal && s->format == SDL_PIXELFORMAT_INDEX8) {
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint8_t i = static_cast<uint8_t*>(s->pixels)[y * s->pitch + x];
                SDL_Color c = i < pal->ncolors ? pal->colors[i] : SDL_Color{};
                out[size_t(y) * w + x] = i ? 0xFF000000u | uint32_t(c.r) << 16 | uint32_t(c.g) << 8 | c.b : 0;
            }
    } else {
        SDL_Surface* c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_ARGB8888);
        if (c) {
            for (int y = 0; y < h; y++)
                std::memcpy(&out[size_t(y) * w], static_cast<uint8_t*>(c->pixels) + y * c->pitch, size_t(w) * 4);
            SDL_DestroySurface(c);
        }
    }
    SDL_DestroySurface(s);
    return true;
}

// Least squares fit of dst = m * (src, 1) per channel.
struct Fit {
    double a[4][4] = {}, b[3][4] = {};
    void add(uint32_t src, uint32_t dst) {
        double x[4] = {double(src >> 16 & 255), double(src >> 8 & 255), double(src & 255), 1};
        double y[3] = {double(dst >> 16 & 255), double(dst >> 8 & 255), double(dst & 255)};
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) a[i][j] += x[i] * x[j];
            for (int c = 0; c < 3; c++) b[c][i] += x[i] * y[c];
        }
    }
    Xf solve() const {
        Xf r{};
        for (int c = 0; c < 3; c++) {
            double m[4][5];
            for (int i = 0; i < 4; i++) {
                for (int j = 0; j < 4; j++) m[i][j] = a[i][j] + (i == j ? 1e-3 : 0);
                m[i][4] = b[c][i];
            }
            for (int i = 0; i < 4; i++) {
                int p = i;
                for (int k = i + 1; k < 4; k++)
                    if (std::abs(m[k][i]) > std::abs(m[p][i])) p = k;
                std::swap(m[i], m[p]);
                for (int k = 0; k < 4; k++) {
                    if (k == i || m[i][i] == 0) continue;
                    double f = m[k][i] / m[i][i];
                    for (int j = i; j < 5; j++) m[k][j] -= f * m[i][j];
                }
            }
            for (int i = 0; i < 4; i++) r.m[c][i] = m[i][i] != 0 ? float(m[i][4] / m[i][i]) : 0;
        }
        return r;
    }
};

} // namespace

void load(const std::string& data) {
    std::vector<std::pair<std::string, int>> files = {{"gfx/wallblocks.png", 16}, {"view/objects.png", 1}};
    for (const char* set : kSpriteSetFiles) files.push_back({std::string(set) + ".png", 1});
    for (const auto& [file, cols] : files) {
        Sheet sh;
        sh.cols = cols;
        int w, h;
        if (!load_png(data + "/" + file, w, h, sh.px)) std::exit(1);
        sh.size = w / sh.cols;
        sh.pitch = w;
        sh.count = sh.cols * (h / sh.size);
        sh.blocks.resize(size_t(sh.count));
        sheets.push_back(std::move(sh));
    }
    for (int i = 0; i < 2; i++) {
        std::string path = data + (i ? "/view/sky_in.png" : "/view/sky_out.png");
        SDL_Surface* s = SDL_LoadPNG(path.c_str());
        if (!s || s->format != SDL_PIXELFORMAT_INDEX8 || s->w != 1024 || s->h != 64) {
            std::fprintf(stderr, "%s: not a 1024x64 indexed PNG\n", path.c_str());
            if (s) SDL_DestroySurface(s);
            continue;
        }
        skies[i].resize(1024 * 64);
        for (int y = 0; y < 64; y++)
            std::memcpy(&skies[i][size_t(y) * 1024], static_cast<uint8_t*>(s->pixels) + y * s->pitch, 1024);
        SDL_DestroySurface(s);
    }
}

int sky(bool indoor, int x, int row) {
    const std::vector<uint8_t>& s = skies[indoor];
    return s.empty() ? 0 : s[size_t(row) * 1024 + size_t(x)] & 15;
}

Block block(int sheet, int index) {
    static const uint32_t blank[32 * 32] = {};
    Sheet& s = sheets[size_t(sheet)];
    if (index < 0 || index >= s.count) return {blank, 32};
    std::vector<uint32_t>& px = s.blocks[size_t(index)];
    if (px.empty()) {
        int x0 = index % s.cols * s.size, y0 = index / s.cols * s.size;
        px.resize(size_t(s.size) * s.size);
        for (int y = 0; y < s.size; y++)
            std::memcpy(&px[size_t(y) * s.size], &s.px[size_t(y0 + y) * s.pitch + x0], size_t(s.size) * 4);
    }
    return {px.data(), s.size};
}

Xf identity() {
    Xf r{};
    r.m[0][0] = r.m[1][1] = r.m[2][2] = 1;
    return r;
}

Xf lerp(const Xf& a, const Xf& b, float t) {
    Xf r;
    for (int c = 0; c < 3; c++)
        for (int i = 0; i < 4; i++) r.m[c][i] = a.m[c][i] + (b.m[c][i] - a.m[c][i]) * t;
    return r;
}

Xf compose(const Xf& o, const Xf& n) {
    Xf r;
    for (int c = 0; c < 3; c++) {
        for (int i = 0; i < 4; i++) {
            float v = 0;
            for (int k = 0; k < 3; k++) v += o.m[c][k] * n.m[k][i];
            r.m[c][i] = v;
        }
        r.m[c][3] += o.m[c][3];
    }
    return r;
}

uint32_t apply(const Xf& x, uint32_t argb) {
    float r = float(argb >> 16 & 255), g = float(argb >> 8 & 255), b = float(argb & 255);
    auto ch = [&](int c) {
        float v = x.m[c][0] * r + x.m[c][1] * g + x.m[c][2] * b + x.m[c][3];
        return uint32_t(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
    };
    return 0xFF000000u | ch(0) << 16 | ch(1) << 8 | ch(2);
}

const Xf& shade(const uint8_t* table) {
    static std::unordered_map<const uint8_t*, Xf> fits;
    auto it = fits.find(table);
    if (it != fits.end()) return it->second;
    Fit fit;
    for (int b = 1; b < 256; b++) {
        uint8_t t = table[b];
        if (b >> 4 && t >> 4) fit.add(base(b >> 4), base(t >> 4));
        if (b & 15 && t & 15) fit.add(base(b & 15), base(t & 15));
    }
    return fits.emplace(table, fit.solve()).first->second;
}

Xf effect(const uint32_t* cram) {
    static uint32_t last[16];
    static bool valid = false;
    static Xf xf = identity();
    if (valid && !std::memcmp(last, cram, sizeof last)) return xf;
    std::memcpy(last, cram, sizeof last);
    valid = true;
    bool same = true;
    for (int i = 1; i < 16; i++) same &= (cram[i] | 0xFF000000u) == base(i);
    if (same) return xf = identity();
    Fit fit;
    for (int i = 1; i < 16; i++) fit.add(base(i), cram[i]);
    return xf = fit.solve();
}

} // namespace game::textures
