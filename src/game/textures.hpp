// PC: true-colour texture blocks and colour transforms for the 3D view
// (hires.cpp).
//
// A block is 32x32 pixels (wall texture halves, sprite pieces) of a sheet:
// gfx/wallblocks.png (16 per row), view/objects.png and the sprite sets
// view/set_*.png (one per row). A sheet may be replaced by one at any
// multiple of 32 pixels per block, indexed or RGBA.
#pragma once

#include <cstdint>
#include <string>

namespace game::textures {

struct Block {
    const uint32_t* px; // ARGB, alpha 0 = transparent (colour index 0)
    int size;           // pixels per side: 32 * scale
};

// The sheets: walls, map objects, then the sprite sets in the order of
// game::SpriteSet.
enum : uint8_t { kWalls, kObjects, kSets };
void load(const std::string& data);
// Block index of a sheet; transparent when the sheet has no such block.
Block block(int sheet, int index);
// The 64 rows above a backdrop map (data/view/sky_out.png, sky_in.png from
// tools/extend_sky.py): colour of palette line 2 at x 0-1023, row 0-63 =
// map rows -64..-1; 0 = transparent.
int sky(bool indoor, int x, int row);

// out = m * (r, g, b, 1), channels 0-255.
struct Xf {
    float m[3][4];
};
Xf identity();
Xf lerp(const Xf& a, const Xf& b, float t);
Xf compose(const Xf& outer, const Xf& inner); // outer after inner
uint32_t apply(const Xf& x, uint32_t argb);
// What a 256-byte shade table (two pixels per byte) does to the view palette.
const Xf& shade(const uint8_t* table);
// The view palette (line 0) as it is now in CRAM against the game's view
// palette: fades, flashes, goggles.
Xf effect(const uint32_t* cram_line0);

} // namespace game::textures
