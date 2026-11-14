// Scaled sprites in the 3D view: the column scaler (0xEDA0), map object
// sprites (DrawObjectSprite 0x113B0, handlers at 0x113F4) and multi-block
// sprites of enemies and large objects (0x1BD72).
#include "render.hpp"

#include "game.hpp"
#include "hires.hpp"

#include <map>
#include <sstream>

namespace game {

const char* const kSpriteSetFiles[12] = {
    "view/set_1B7B38", "view/set_16EC58", "view/set_1A7828", "view/set_1ACC72",
    "view/set_18D078", "view/set_183B94", "view/set_1784F2", "view/set_17BA80",
    "view/set_193F00", "view/set_19D50C", "view/set_1C258A", "view/set_1CB96A",
};

namespace {

int16_t s16(uint16_t v) { return int16_t(v); }

auto& mirror = st.sprite_mirror;

// 0xED50, 0xED64, 0xED78, 0xED8C: the object shade tables
// (render/sprite_shade.txt) of each FF1096 row, by distance.
const uint8_t kSpriteShades[4][5] = {{0, 0, 0, 0, 0}, {1, 2, 3, 4, 5}, {6, 7, 8, 9, 10}, {11, 11, 11, 11, 11}};

// Parses a view/set_*.txt (format in tools/extract_tables.py write_sprite_sets).
SpriteAnims load_set(const std::string& file) {
    SpriteAnims anims;
    for (const std::string& line : assets::lines(file.c_str())) {
        std::istringstream ss(line);
        std::string kind;
        ss >> kind;
        if (kind == "anim") {
            anims.emplace_back();
        } else if (kind == "view") {
            anims.back().emplace_back();
        } else {
            SpritePart p{};
            int ax, ay, bw, bh;
            char colon;
            ss >> ax >> ay >> bw >> bh >> colon;
            p.ax = uint8_t(ax), p.ay = uint8_t(ay), p.bw = uint8_t(bw), p.bh = uint8_t(bh);
            std::string grid;
            std::getline(ss, grid);
            std::istringstream rows(grid);
            for (std::string row; std::getline(rows, row, '/');) {
                std::istringstream cells(row);
                int x = 0;
                for (std::string c; cells >> c; x++) {
                    p.block[p.rows][x] = uint8_t(std::stoi(c));
                    p.mirror[p.rows][x] = c.back() == 'm';
                }
                if (!x) break;
                p.cols = uint8_t(x);
                p.rows++;
            }
            anims.back().back().push_back(p);
        }
    }
    return anims;
}

} // namespace

const SpriteAnims& sprite_anims(SpriteSet set) {
    static std::map<SpriteSet, SpriteAnims> sets;
    auto it = sets.find(set);
    if (it == sets.end()) it = sets.emplace(set, load_set(std::string(kSpriteSetFiles[int(set)]) + ".txt")).first;
    return it->second;
}

// 0xEDA0: d0 height, d4 width (screen pixels), d1 left column, d2 top row,
// a1 sprite (16 columns of 32 texels). FF1094 scale for the depth test,
// FF1096 the shade tables, FF1088 mirrored.
void DrawSprite(int16_t height, int16_t width, int16_t col, int16_t row, SpriteBlock a1) {
    if (height <= 0 || width <= 0) return;
    int16_t d3 = 0;
    if (st.sprite_shades != 0) {
        d3 = int16_t((s16(st.sprite_scale) >> 3) - 1);
        if (d3 < 0) return;
        d3 = int16_t(4 - d3);
        if (d3 < 0) d3 = 0;
    }
    // PC: recorded for the screen (hires).
    hires::Cmd hc{};
    hc.col = col;
    hc.row = row;
    hc.width = width;
    hc.height = height;
    hc.scale = s16(st.sprite_scale);
    hc.mirror = mirror != 0;
    hc.sheet = a1.sheet;
    hc.block = a1.index;
    hc.shades = kSpriteShades[st.sprite_shades][d3];
    if (s16(st.nv_slot) < 0 && s16(st.light_slot) >= 0) {
        hc.lit_a = st.lit_start;
        hc.lit_b = st.lit_end;
    }
    hires::sprite(hc);
}

// 0x1BD72: multi-block sprite: sprite set, anim (d0), frame (d7), part
// (d3); col, row, scale = d1, d2, d5.
void DrawSpriteSet(SpriteSet set, uint16_t anim, uint16_t frame, uint16_t part, int16_t col, int16_t row, uint16_t scale) {
    const SpriteAnims& anims = sprite_anims(set);
    if (anim >= anims.size()) return;
    DrawSpriteBlocks(anims[anim][frame][part], set, col, row, scale);
}

// 0x1BD9E / 0x1BCCE: the blocks of one sprite part, placed by its anchor at
// (col, row).
void DrawSpriteBlocks(const SpritePart& p, SpriteSet set, int16_t col, int16_t row, uint16_t scale) {
    int16_t bw = int16_t((uint32_t(p.bw) * scale) >> 5);
    int16_t bh = int16_t((uint32_t(p.bh) * scale) >> 4);
    uint16_t w = uint16_t(p.cols * uint16_t(bw));
    if (p.ax) col = int16_t(col - (p.ax == 1 ? int16_t(w) >> 1 : int16_t(w)));
    uint16_t h = uint16_t(p.rows * uint16_t(bh));
    if (p.ay) row = int16_t(row - (p.ay == 1 ? int16_t(h) >> 1 : int16_t(h)));
    mirror = 0;
    for (int y = 0; y < p.rows; y++, row = int16_t(row + bh)) {
        int16_t c = col;
        for (int x = 0; x < p.cols; x++, c = int16_t(c + bw)) {
            set_low_byte(st.sprite_mirror, p.mirror[y][x]);
            DrawSprite(bh, bw, c, row, {uint8_t(textures::kSets + int(set)), p.block[y][x]});
        }
    }
}

namespace {

// Height of the hanging objects (0x1198A, 0x119E8), from the view height.
int16_t eye_height(int16_t d0) {
    int16_t d3 = int16_t(0x20 + s16(st.view_z) - s16(st.stair_height));
    return int16_t((int32_t(d0) * d3) >> 6);
}

} // namespace

// 0x113B0
void DrawObjectSprite(const ViewObject& o) {
    uint8_t cls = o.cls;
    int16_t d5 = o.scale, d1 = o.col;
    st.sprite_scale = uint16_t(d5);
    float fcol, fscale;
    hires::exact(int16_t(((uint16_t(o.x) + view.cell_x) << 8) + 0x80),
                 int16_t(((uint16_t(o.y) + view.cell_y) << 8) + 0x80), fcol, fscale);
    hires::anchor(d1, d5, fcol, fscale);
    int16_t d3 = int16_t(s16(st.view_z) - s16(st.stair_height) + 0x20);
    int16_t d2 = int16_t(((int32_t(d5) * d3) >> 6) + 0x28);
    switch (cls) {
    case 0x18: { // barrel-like animated object
        int16_t d4 = int16_t(d5 >> 1);
        d4 = int16_t(d4 + (d4 >> 2));
        mirror = st.frame & 1;
        DrawSprite(d5, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), object_block(0));
        break;
    }
    case 0x19: case 0x82: { // 0x11A3A (0x82 reads one entry past the table)
        int16_t d0 = int16_t(d5 >> 1), d4 = int16_t(d0 >> 1);
        mirror = 0;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d0), cls == 0x82 ? object_block(60) : object_block(3));
        break;
    }
    case 0x1A: case 0x1B: case 0x1C: case 0x1E: case 0x1F: case 0x20: case 0x21: case 0x22: case 0x23:
    case 0x24: case 0x25: case 0x36: { // 0x11A52
        static const uint8_t spr[] = {8, 10, 11, 0, 4, 13, 14, 7,
                                      5, 6, 12, 18};
        SpriteBlock a1 = cls == 0x36 ? object_block(17) : object_block(spr[cls - 0x1A]);
        int16_t d0 = int16_t(d5 >> 2);
        mirror = 0;
        DrawSprite(d0, d0, int16_t(d1 - (d0 >> 1)), int16_t(d2 - d0), a1);
        break;
    }
    case 0x1D: {
        int16_t d4 = int16_t(d5 >> 1), d0 = int16_t(d4 >> 1);
        mirror = 0;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d0), object_block(15));
        break;
    }
    case 0x26: {
        int16_t d0 = int16_t(d5 >> 1);
        d0 = int16_t(d0 - (d0 >> 2));
        int16_t d4 = int16_t(d0 >> 1);
        mirror = 0;
        SpriteBlock a1 = (uint8_t(st.frame) & 2) ? object_block(20) : object_block(19);
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), a1);
        break;
    }
    case 0x27: case 0x28: break;
    case 0x29: DrawSpriteSet(SpriteSet::Type0, 0, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x2A: DrawSpriteSet(SpriteSet::Type7, 0, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x2B: DrawSpriteSet(SpriteSet::Type2, 0, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x2C: DrawSpriteSet(SpriteSet::Type0, 0xB, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x35: DrawSpriteSet(SpriteSet::Stunned, 0, 0, 2, d1, d2, uint16_t(d5)); break;
    case 0x37: { // 0x11A0E
        int16_t d4 = int16_t(d5 >> 1);
        mirror = 0;
        DrawSprite(d5, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), object_block(46));
        break;
    }
    case 0x5C: {
        int16_t d4 = int16_t(d5 >> 1);
        mirror = 0;
        DrawSprite(d5, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), object_block(47));
        break;
    }
    case 0x60: case 0x61: case 0x76: { // 0x11A24
        SpriteBlock a1 = cls == 0x60 ? object_block(51) : cls == 0x61 ? object_block(54) : object_block(52);
        int16_t d4 = int16_t(d5 >> 2);
        mirror = 0;
        DrawSprite(d5, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), a1);
        break;
    }
    case 0x5D: {
        int16_t d0 = int16_t(d5 - (d5 >> 2));
        int16_t d4 = int16_t(d0 >> 1);
        mirror = 0;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d0), object_block(48));
        break;
    }
    case 0x5F: {
        int16_t d0 = int16_t(d5 >> 1), d4 = int16_t(d0 >> 2);
        mirror = 0;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d0), object_block(55));
        break;
    }
    case 0x5E: {
        int16_t h = int16_t(d5 >> 1);
        int16_t d0 = int16_t(h - (h >> 2));
        mirror = 0;
        DrawSprite(d0, h, int16_t(d1 - (h >> 1)), int16_t(d2 - d0), object_block(55));
        break;
    }
    case 0x63: case 0x62: {
        SpriteBlock a1 = object_block(53);
        if (cls == 0x62 && (uint8_t(st.frame) & 2)) a1 = object_block(49);
        int16_t d0 = int16_t(d5 >> 2), d4 = int16_t(d0 >> 1);
        mirror = 0;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), a1);
        break;
    }
    case 0x64: {
        int16_t d4 = int16_t(d5 >> 2), d0 = int16_t(d4 >> 1);
        mirror = 0;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), object_block(56));
        // 0x1198A: the top part.
        int16_t w = int16_t(d5 >> 1), half = int16_t(w >> 1);
        int16_t hh = eye_height(half);
        mirror = 0;
        DrawSprite(hh, w, int16_t(d1 - half), int16_t(d2 - (hh >> 1)), object_block(50));
        break;
    }
    case 0x75: {
        int16_t d4 = int16_t(d5 >> 1), d0 = int16_t(d4 >> 2);
        mirror = 0;
        DrawSprite(d0, d4, int16_t(d1 - (d4 >> 1)), int16_t(d2 - d5), object_block(57));
        break;
    }
    case 0x6C: DrawSpriteSet(SpriteSet::Type1, 4, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x6D: DrawSpriteSet(SpriteSet::Type2, 6, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x6E: {
        // PC: a random number of its own (render.cpp draws the game's).
        static uint32_t seed;
        uint32_t r = seed = seed * 0x41C64E6D + 0x3039;
        DrawSpriteSet(SpriteSet::Type3, 9, 0, (r & 0x3F00) ? 1 : 0, d1, d2, uint16_t(d5));
        break;
    }
    case 0x6F: DrawSpriteSet(SpriteSet::Type4, 7, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x70: DrawSpriteSet(SpriteSet::Type5, 6, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x71: DrawSpriteSet(SpriteSet::Type6, 6, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x72: DrawSpriteSet(SpriteSet::Type7, 4, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x73: DrawSpriteSet(SpriteSet::Type8, 9, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x74: DrawSpriteSet(SpriteSet::Type9, 4, 0, 0, d1, d2, uint16_t(d5)); break;
    case 0x78: {
        mirror = 0;
        SpriteBlock a1 = object_block(58);
        uint16_t t = st.frame & 3;
        if (t != 0 && t != 3) a1.index++;
        if (t >= 2) mirror = 1;
        int16_t w = int16_t(d5 >> 1), half = int16_t(w >> 1);
        int16_t hh = eye_height(half);
        DrawSprite(hh, w, int16_t(d1 - half), int16_t(d2 - (hh >> 1)), a1);
        break;
    }
    default:
        break; // 0x13298: nothing
    }
}

} // namespace game
