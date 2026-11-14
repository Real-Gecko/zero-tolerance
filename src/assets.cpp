#include "assets.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>

namespace assets {

namespace {

std::string dir;

// The pixels of an indexed PNG (the index bits in mask).
bool indexed_png(const std::string& path, int& w, int& h, std::vector<uint8_t>& px, uint8_t mask = 15) {
    SDL_Surface* s = SDL_LoadPNG(path.c_str());
    if (!s) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), SDL_GetError());
        return false;
    }
    bool ok = s->format == SDL_PIXELFORMAT_INDEX8;
    if (ok) {
        w = s->w;
        h = s->h;
        px.resize(size_t(w) * h);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) px[size_t(y) * w + x] = static_cast<uint8_t*>(s->pixels)[y * s->pitch + x] & mask;
    } else {
        std::fprintf(stderr, "%s: not an indexed PNG\n", path.c_str());
    }
    SDL_DestroySurface(s);
    return ok;
}

// A palette swatch: 16 colours per row, each channel = colour nibble * 17.
bool palette_png(const std::string& path, std::vector<uint8_t>& b) {
    SDL_Surface* s = SDL_LoadPNG(path.c_str());
    if (!s) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), SDL_GetError());
        return false;
    }
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            uint8_t r, g, bl;
            SDL_ReadSurfacePixel(s, x, y, &r, &g, &bl, nullptr);
            uint16_t w = uint16_t((bl + 8) / 17 << 8 | (g + 8) / 17 << 4 | (r + 8) / 17);
            b.push_back(uint8_t(w >> 8));
            b.push_back(uint8_t(w));
        }
    SDL_DestroySurface(s);
    return true;
}

} // namespace

void init(const std::string& data) { dir = data; }

const std::vector<std::string>& lines(const char* file) {
    static std::map<std::string, std::vector<std::string>> cache;
    auto it = cache.find(file);
    if (it != cache.end()) return it->second;
    std::ifstream f(dir + "/" + file);
    if (!f) {
        std::fprintf(stderr, "cannot read %s/%s\n", dir.c_str(), file);
        std::exit(1);
    }
    std::vector<std::string>& v = cache[file];
    for (std::string line; std::getline(f, line);)
        if (!line.empty() && line[0] != '#') v.push_back(line);
    return v;
}

const std::vector<std::string>& list(const char* file) {
    static std::map<std::string, std::vector<std::string>> cache;
    auto it = cache.find(file);
    if (it != cache.end()) return it->second;
    std::vector<std::string>& v = cache[file];
    for (const std::string& line : lines(file)) {
        size_t a = line.find('"'), b = line.rfind('"');
        v.push_back(line.substr(a + 1, b - a - 1));
    }
    return v;
}

const char* text(const char* name) {
    static std::map<std::string, std::string> texts;
    if (texts.empty()) {
        const std::vector<std::string>& l = lines("text/strings.txt");
        const std::vector<std::string>& q = list("text/strings.txt");
        for (size_t i = 0; i < l.size(); i++) texts[l[i].substr(0, l[i].find(' '))] = q[i];
    }
    auto it = texts.find(name);
    if (it == texts.end()) {
        std::fprintf(stderr, "no text %s in text/strings.txt\n", name);
        std::exit(1);
    }
    return it->second.c_str();
}

const uint16_t* palette(const std::string& file) {
    static std::map<std::string, std::vector<uint16_t>> cache;
    std::vector<uint16_t>& v = cache[file];
    if (v.empty()) {
        std::vector<uint8_t> b;
        if (!palette_png(dir + "/" + file, b)) std::exit(1);
        for (size_t i = 0; i + 1 < b.size(); i += 2) v.push_back(uint16_t(b[i] << 8 | b[i + 1]));
    }
    return v.data();
}

const std::vector<uint8_t>& hex(const char* file) {
    static std::map<std::string, std::vector<uint8_t>> cache;
    auto it = cache.find(file);
    if (it != cache.end()) return it->second;
    std::vector<uint8_t>& v = cache[file];
    for (const std::string& line : lines(file)) {
        std::istringstream ss(line);
        for (std::string tok; ss >> tok;) v.push_back(uint8_t(std::stoul(tok, nullptr, 16)));
    }
    return v;
}

const std::vector<uint8_t>& bytes(const std::string& file) {
    static std::map<std::string, std::vector<uint8_t>> cache;
    auto it = cache.find(file);
    if (it != cache.end()) return it->second;
    std::ifstream in(dir + "/" + file, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot read %s/%s\n", dir.c_str(), file.c_str());
        std::exit(1);
    }
    return cache[file] = std::vector<uint8_t>(std::istreambuf_iterator<char>(in), {});
}

const Tilemap& tilemap(const char* file) {
    static std::map<std::string, Tilemap> cache;
    auto it = cache.find(file);
    if (it != cache.end()) return it->second;
    int w, h;
    std::vector<uint8_t> px;
    if (!indexed_png(dir + "/" + file, w, h, px, 0xFF)) std::exit(1);
    Tilemap& t = cache[file];
    t.w = w / 8;
    t.h = h / 8;
    std::map<std::vector<uint8_t>, uint16_t> seen; // tile pixels -> tile entry
    for (int cy = 0; cy < t.h; cy++)
        for (int cx = 0; cx < t.w; cx++) {
            std::vector<uint8_t> cell(64);
            uint16_t line = 0;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    uint8_t c = px[size_t(cy * 8 + y) * w + size_t(cx * 8 + x)];
                    cell[size_t(y * 8 + x)] = c & 15;
                    if (c & 15) line = c >> 4;
                }
            auto found = seen.find(cell);
            if (found == seen.end()) {
                uint16_t n = t.count();
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x += 2) t.tiles.push_back(uint8_t(cell[size_t(y * 8 + x)] << 4 | cell[size_t(y * 8 + x + 1)]));
                // The flipped copies share the tile; the first match wins as
                // in the original order (none, vertical, horizontal, both).
                for (uint16_t f : {0x1800, 0x0800, 0x1000, 0}) {
                    std::vector<uint8_t> k(64);
                    for (int y = 0; y < 8; y++)
                        for (int x = 0; x < 8; x++)
                            k[size_t(y * 8 + x)] = cell[size_t((f & 0x1000 ? 7 - y : y) * 8 + (f & 0x0800 ? 7 - x : x))];
                    seen[k] = uint16_t(n | f);
                }
                found = seen.find(cell);
            }
            t.map.push_back(uint16_t(found->second | line << 13));
        }
    return t;
}

const std::vector<uint8_t>& tiles(const std::string& file) {
    static std::map<std::string, std::vector<uint8_t>> cache;
    auto it = cache.find(file);
    if (it != cache.end()) return it->second;
    int w, h;
    std::vector<uint8_t> px;
    if (!indexed_png(dir + "/" + file, w, h, px)) std::exit(1);
    std::vector<uint8_t>& b = cache[file];
    int cols = w / 8;
    for (int t = 0; t < cols * (h / 8); t++)
        for (int y = 0; y < 8; y++)
            for (int x = 0; x < 8; x += 2) {
                size_t p = size_t((t / cols) * 8 + y) * w + size_t((t % cols) * 8 + x);
                b.push_back(uint8_t(px[p] << 4 | px[p + 1]));
            }
    return b;
}

const std::vector<uint8_t>& tiles(const std::string& file, const std::vector<Cell>& cells) {
    static std::map<std::string, std::vector<uint8_t>> cache;
    auto it = cache.find(file);
    if (it != cache.end()) return it->second;
    int w, h;
    std::vector<uint8_t> px;
    if (!indexed_png(dir + "/" + file, w, h, px)) std::exit(1);
    std::vector<uint8_t>& b = cache[file];
    for (size_t i = 0, x0 = 0; int(x0) < w; x0 += size_t(cells[i].w * 8), i = (i + 1) % cells.size())
        for (int cx = 0; cx < cells[i].w; cx++)
            for (int cy = 0; cy < cells[i].h; cy++)
                for (int y = 0; y < 8; y++)
                    for (int x = 0; x < 8; x += 2) {
                        size_t p = size_t(cy * 8 + y) * w + x0 + size_t(cx * 8 + x);
                        b.push_back(uint8_t(px[p] << 4 | px[p + 1]));
                    }
    return b;
}

const std::vector<uint8_t>& blocks(const std::string& file, int cols) {
    static std::map<std::string, std::vector<uint8_t>> cache;
    auto it = cache.find(file);
    if (it != cache.end()) return it->second;
    int w, h;
    std::vector<uint8_t> px;
    if (!indexed_png(dir + "/" + file, w, h, px)) std::exit(1);
    std::vector<uint8_t>& b = cache[file];
    for (int n = 0; n < cols * (h / 32); n++)
        for (int c = 0; c < 16; c++)
            for (int y = 0; y < 32; y++) {
                size_t p = size_t((n / cols) * 32 + y) * w + size_t((n % cols) * 32 + 2 * c);
                b.push_back(uint8_t(px[p] << 4 | px[p + 1]));
            }
    return b;
}

} // namespace assets
