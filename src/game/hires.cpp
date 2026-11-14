#include "hires.hpp"

#include "game.hpp"
#include "display.hpp"
#include "textures.hpp"



#include <algorithm>
#include <cmath>
#include <cstring>

namespace game::hires {

Frame rendered, shown;
float view_tan = 1;

namespace {

Frame building;
bool recording = false;
float edge_xa, edge_ha, edge_xb, edge_hb, edge_ua, edge_ub;
int16_t anchor_col, anchor_scale;
float anchor_fcol, anchor_fscale;

// Shade of a wall wh rows high, smoothed: 0 = Frame::shade[7] (darkest) to
// 8 = unshaded. wall.cpp scale_column picks a table by bands of wh / 2
// (1-4, 5-7, 8-10, 11-12, ... 19-20, then none); this runs linearly between
// the middles of the bands, and the caller blends neighbouring tables.
float shade_level(float wh) {
    static const float mid[9] = {3, 6.5f, 9.5f, 12, 14, 16, 18, 20, 22};
    float i = wh / 2;
    if (i < 1) return 8; // scale_column leaves walls under 2 rows unshaded
    if (i <= mid[0]) return 0;
    for (int d = 0; d < 8; d++)
        if (i < mid[d + 1]) return float(d) + (i - mid[d]) / (mid[d + 1] - mid[d]);
    return 8;
}


uint8_t nibble(uint8_t b, int right) { return right ? b & 15 : b >> 4; }

} // namespace

void begin() {
    building.cmds.clear();
    building.tan = view_tan;
    recording = true;
}

void end() {
    std::copy_n(st.gradient, 0xA0, building.gradient);
    building.shade = st.wall_shade;
    recording = false;
    std::swap(rendered, building);
}

void record(bool on) { recording = on; }

void edges(float xa, float ha, float xb, float hb, float ua, float ub) {
    edge_xa = xa;
    edge_ha = ha;
    edge_xb = xb;
    edge_hb = hb;
    edge_ua = ua;
    edge_ub = ub;
}

void scale_u(float f) {
    edge_ua *= f;
    edge_ub *= f;
}

static void add(Cmd c, Cmd::Kind kind) {
    if (!recording) return;
    c.kind = kind;
    if (kind == Cmd::Wall || kind == Cmd::Portal) {
        c.xa = edge_xa;
        c.ha = edge_ha;
        c.xb = edge_xb;
        c.hb = edge_hb;
        c.ua = edge_ua;
        c.ub = edge_ub;
        // DrawWallSpan 0xD0C4: the door offset runs from byte b0 at u = 0 to
        // b1 at u = 256, times the wall height.
        if (c.door) {
            float b0 = c.door_a, b1 = c.door_b;
            c.door_a = (b0 + (b1 - b0) * c.ua / 256) * c.ha;
            c.door_b = (b0 + (b1 - b0) * c.ub / 256) * c.hb;
        }
    }
    if (kind == Cmd::Sprite) {
        float r = anchor_scale > 0 ? anchor_fscale / anchor_scale : 1;
        c.fx = anchor_fcol + (c.col - anchor_col) * r;
        c.fy = 40 + (c.row - 40) * r;
        c.fw = c.width * r;
        c.fh = c.height * r;
        c.fscale = c.scale * r;
    }
    building.cmds.push_back(c);
}

void wall(const Cmd& c) { add(c, Cmd::Wall); }
void portal(const Cmd& c) { add(c, Cmd::Portal); }
void fill() { add(Cmd{}, Cmd::Fill); }
void sprite(const Cmd& c) { add(c, Cmd::Sprite); }

void anchor(int16_t col, int16_t scale, float fcol, float fscale) {
    anchor_col = col;
    anchor_scale = scale;
    anchor_fcol = fcol;
    anchor_fscale = fscale;
}

void exact(int16_t x, int16_t y, float& col, float& scale) {
    float dx = int16_t(x - player_x), dy = int16_t(y - player_y);
    float cx = int16_t(st.dir_x), cy = int16_t(st.dir_y);
    float depth = dx * cx + dy * cy;
    col = depth > 0 ? (dy * cx - dx * cy) * 64 / depth + 64 : 64;
    scale = depth > 0 ? 4194304.0f / depth : 0;
}
void show() { shown = rendered; }

int backdrop(int x, int line) {
    int px = (x - (display::hscrollA[0] & 0x3FF)) & 0x3FF;
    int py = (line + display::vscrollA) & 0xFF;
    if (py >= 0xC0) { // PC: the sky drawn above the map
        int c = textures::sky(st.backdrop_in, px, py - 0xC0);
        return c ? 32 + c : 0;
    }
    if (py >= 0x80) return 0; // the map has 16 rows
    uint16_t e = backdrop_map()[(px >> 3) + (py >> 3) * 0x80];
    return display::tile_pixel(e, px, py);
}

static int skyline_x, skyline_y;

void skyline(uint16_t d2, int16_t d5) {
    skyline_x = ((d2 - 0x80 + 0x200) & 0x3FF) - 0x200;
    skyline_y = d5 - 0x80;
}

int skyline_pixel(int x, int line) {
    if (st.indoors) return 0;
    int dx = x - skyline_x, dy = line - skyline_y;
    if (dx < 0 || dx >= 160 || dy < 0) return 0;
    // Rows of 16 lines, 4x2-cell sprites of tiles kSkyRows[row].
    uint16_t e = kSkyRows[std::min(dy >> 4, 0x2E)];
    return display::tile_pixel(uint16_t(e + ((dx & 31) >> 3) * 2 + ((dy & 15) >> 3)), dx, dy);
}

void draw(uint32_t* out, int w, int h, const uint32_t* pal) {
    using textures::Xf;
    const Frame& f = shown;
    std::memset(out, 0, size_t(w) * h * 4);
    const float k = float(w) / (256 * f.tan); // output pixels per original pixel
    const float c0 = 64 - 64 * f.tan;         // view column at the left edge
    static std::vector<float> depth; // wall height drawn in each column (FF6126)
    depth.assign(size_t(w), 0.0f);
    auto col_of = [&](int x) { return c0 + (float(x) + 0.5f) / (2 * k); };
    auto row_of = [&](int y) { return 40 + (float(y) + 0.5f - float(h) / 2) / k; };
    auto y_of = [&](float row) { return float(h) / 2 + (row - 40) * k; };
    // The FF6226 gradient without its dither: each row's 2x2 pattern (two
    // columns of two pixels) averaged, then blurred over neighbouring rows.
    float avg[0x50][3], grad[0x50][3];
    bool solid[0x50];
    for (int r = 0; r < 0x50; r++) {
        int n = 0;
        avg[r][0] = avg[r][1] = avg[r][2] = 0;
        for (int i = 0; i < 4; i++) {
            int c = nibble(f.gradient[(i & 2 ? 0x50 : 0) + r], i & 1);
            if (!c) continue;
            n++;
            for (int ch = 0; ch < 3; ch++) avg[r][ch] += float(pal[c] >> (16 - 8 * ch) & 0xFF);
        }
        solid[r] = n > 0;
        for (int ch = 0; ch < 3; ch++) avg[r][ch] /= float(std::max(n, 1));
    }
    for (int r = 0; r < 0x50; r++) {
        float wsum = 0;
        grad[r][0] = grad[r][1] = grad[r][2] = 0;
        for (int d = -3; d <= 3; d++) {
            int q = r + d;
            if (q < 0 || q >= 0x50 || !solid[q]) continue;
            float wt = float(4 - std::abs(d));
            wsum += wt;
            for (int ch = 0; ch < 3; ch++) grad[r][ch] += avg[q][ch] * wt;
        }
        for (int ch = 0; ch < 3; ch++) grad[r][ch] /= std::max(wsum, 1.0f);
    }
    // Background at view row r: the gradient lerped between rows, or the
    // colour 1 fill of 0xD074 (door faces, portals with FF119A clear).
    auto background = [&](bool gradient, float r) -> uint32_t {
        if (!gradient) return 0xFF000000u | pal[1];
        int row = std::clamp(int(r), 0, 0x4F);
        if (!solid[row]) return 0;
        float t = r - (float(row) + 0.5f);
        int other = std::clamp(t < 0 ? row - 1 : row + 1, 0, 0x4F);
        if (!solid[other]) other = row;
        t = std::min(std::abs(t), 1.0f);
        uint32_t p = 0xFF000000u;
        for (int ch = 0; ch < 3; ch++)
            p |= uint32_t(grad[row][ch] + (grad[other][ch] - grad[row][ch]) * t + 0.5f) << (16 - 8 * ch);
        return p;
    };
    auto x_range = [&](float a, float b, int& x0, int& x1) {
        x0 = std::max(0, int((a - c0) * 2 * k + 0.5f));
        x1 = std::min(w, int((b - c0) * 2 * k + 0.5f));
    };
    const Xf effect = textures::effect(pal);
    // Wall shade by level (shade_level): tables f.shade + 7 - d, then none.
    const uint8_t* shades = assets::hex("render/shade.txt").data();
    Xf levels[9];
    for (int d = 0; d < 8; d++) levels[d] = textures::compose(effect, textures::shade(shades + (f.shade + 7 - d) * 0x100));
    levels[8] = effect;

    for (const Cmd& c : f.cmds) {
        switch (c.kind) {
        case Cmd::Wall: {
            int x0, x1;
            x_range(c.xa, c.xb, x0, x1);
            for (int x = x0; x < x1; x++) {
                float col = col_of(x);
                float t = c.xb > c.xa ? (col - c.xa) / (c.xb - c.xa) : 0;
                float wh = c.ha + (c.hb - c.ha) * t;
                if (wh < depth[x] || wh <= 0) continue;
                depth[x] = wh;
                // u / z and 1 / z are linear across the screen; wall height is 1 / z.
                float u = (c.ua * c.ha + (c.ub * c.hb - c.ua * c.ha) * t) / wh;
                // The face: 4 strips of 32 pixels, each a top block over a bottom block.
                float fx = std::clamp(u / 2, 0.0f, 127.99f);
                int strip = int(fx / 32);
                const uint16_t* entry = st.faces + c.strip + strip * 2;
                textures::Block top = textures::block(textures::kWalls, int16_t(entry[0]));
                textures::Block bottom = textures::block(textures::kWalls, int16_t(entry[1]));
                float bx = fx - float(strip * 32);
                bool lit = int(col) >= c.lit_a && int(col) < c.lit_b;
                float level = c.shaded && !lit ? shade_level(wh) : 8;
                int d = int(level);
                Xf xf = d >= 8 ? levels[8] : textures::lerp(levels[d], levels[d + 1], level - float(d));
                float off = (wh * c.tilt + (c.door ? c.door_a + (c.door_b - c.door_a) * t : 0)) / 64;
                float wall_top = 40 + off - wh / 2;
                for (int y = 0; y < h; y++) {
                    float r = row_of(y);
                    float v = (r - wall_top) / wh * 64;
                    uint32_t p;
                    if (v < 0 || v >= 64) p = background(c.gradient, r);
                    else {
                        const textures::Block& b = v < 32 ? top : bottom;
                        float by = v < 32 ? v : v - 32;
                        p = b.px[int(by * float(b.size) / 32) * b.size + int(bx * float(b.size) / 32)];
                        if (p) p = textures::apply(xf, p);
                    }
                    out[y * w + x] = p;
                }
            }
            break;
        }
        case Cmd::Portal: {
            int x0, x1;
            x_range(c.xa, c.xb, x0, x1);
            for (int x = x0; x < x1; x++) {
                float col = col_of(x);
                float t = c.xb > c.xa ? (col - c.xa) / (c.xb - c.xa) : 0;
                float wh = c.ha + (c.hb - c.ha) * t;
                if (wh < depth[x]) continue;
                float mid = 40 + wh * c.tilt / 64;
                for (int y = 0; y < h; y++) {
                    float r = row_of(y);
                    if (r < mid - wh / 2 || r >= mid + wh / 2) out[y * w + x] = background(c.gradient, r);
                }
            }
            break;
        }
        case Cmd::Fill:
            for (int x = 0; x < w; x++)
                if (depth[x] == 0)
                    for (int y = 0; y < h; y++) out[y * w + x] = background(true, row_of(y));
            break;
        case Cmd::Sprite: {
            if (c.fw <= 0 || c.fh <= 0) break;
            float sx = (c.fx - c0) * 2 * k, sw = c.fw * 2 * k, sy = y_of(c.fy), sh = c.fh * k;
            int x0 = std::max(0, int(sx + 0.5f)), x1 = std::min(w, int(sx + sw + 0.5f));
            int y0 = std::max(0, int(sy + 0.5f)), y1 = std::min(h, int(sy + sh + 0.5f));
            textures::Block b = textures::block(c.sheet, c.block);
            const uint8_t* sprite_shades = assets::hex("render/sprite_shade.txt").data();
            Xf shaded = textures::compose(effect, textures::shade(sprite_shades + c.shades * 0x100));
            Xf lit = textures::compose(effect, textures::shade(sprite_shades));
            for (int x = x0; x < x1; x++) {
                if (c.fscale < depth[x]) continue;
                int col = int(std::floor(col_of(x)));
                const Xf& xf = col >= c.lit_a && col < c.lit_b ? lit : shaded;
                int px = std::clamp(int((float(x) + 0.5f - sx) / sw * float(b.size)), 0, b.size - 1);
                if (c.mirror) px = b.size - 1 - px;
                for (int y = y0; y < y1; y++) {
                    int py = std::clamp(int((float(y) + 0.5f - sy) / sh * float(b.size)), 0, b.size - 1);
                    uint32_t p = b.px[py * b.size + px];
                    if (p) out[y * w + x] = textures::apply(xf, p);
                }
            }
            break;
        }
        }
    }
}

bool pixel_set(float col, float row) {
    const Frame& f = rendered;
    float depth = 0; // wall height drawn in the column so far
    bool set = false;
    auto background = [&](bool gradient, float r) {
        if (!gradient) return true; // colour 1 (0xD074)
        int i = std::clamp(int(r), 0, 0x4F);
        return (f.gradient[i] | f.gradient[0x50 + i]) != 0;
    };
    for (const Cmd& c : f.cmds) {
        switch (c.kind) {
        case Cmd::Wall: {
            if (col < c.xa || col >= c.xb) break;
            float t = c.xb > c.xa ? (col - c.xa) / (c.xb - c.xa) : 0;
            float wh = c.ha + (c.hb - c.ha) * t;
            if (wh < depth || wh <= 0) break;
            depth = wh;
            float u = (c.ua * c.ha + (c.ub * c.hb - c.ua * c.ha) * t) / wh;
            float fx = std::clamp(u / 2, 0.0f, 127.99f);
            int strip = int(fx / 32);
            float off = (wh * c.tilt + (c.door ? c.door_a + (c.door_b - c.door_a) * t : 0)) / 64;
            float v = (row - (40 + off - wh / 2)) / wh * 64;
            if (v < 0 || v >= 64) {
                set = background(c.gradient, row);
                break;
            }
            uint16_t entry = st.faces[c.strip + strip * 2 + (v < 32 ? 0 : 1)];
            textures::Block b = textures::block(textures::kWalls, int16_t(entry));
            float by = v < 32 ? v : v - 32, bx = fx - float(strip * 32);
            set = b.px[int(by * float(b.size) / 32) * b.size + int(bx * float(b.size) / 32)] != 0;
            break;
        }
        case Cmd::Portal: {
            if (col < c.xa || col >= c.xb) break;
            float t = c.xb > c.xa ? (col - c.xa) / (c.xb - c.xa) : 0;
            float wh = c.ha + (c.hb - c.ha) * t;
            if (wh < depth) break;
            float mid = 40 + wh * c.tilt / 64;
            if (row < mid - wh / 2 || row >= mid + wh / 2) set = background(c.gradient, row);
            break;
        }
        case Cmd::Fill:
            if (depth == 0) set = background(true, row);
            break;
        case Cmd::Sprite: {
            if (c.fw <= 0 || c.fh <= 0 || c.fscale < depth) break;
            if (col < c.fx || col >= c.fx + c.fw || row < c.fy || row >= c.fy + c.fh) break;
            textures::Block b = textures::block(c.sheet, c.block);
            int px = std::clamp(int((col - c.fx) / c.fw * float(b.size)), 0, b.size - 1);
            if (c.mirror) px = b.size - 1 - px;
            int py = std::clamp(int((row - c.fy) / c.fh * float(b.size)), 0, b.size - 1);
            if (b.px[py * b.size + px]) set = true;
            break;
        }
        }
    }
    return set;
}

} // namespace game::hires
