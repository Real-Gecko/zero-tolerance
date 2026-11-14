// PC: the 3D view at the window's resolution and field of view. Render3DView
// (render.cpp) casts the rays over the window's field of view and
// records the wall spans, portals and sprites with full precision instead of
// drawing the original 128x80 view; the host replays the frame on screen at
// any size.
#pragma once

#include <cstdint>
#include <vector>

namespace game::hires {

struct Cmd {
    enum Kind : uint8_t { Wall, Portal, Fill, Sprite } kind;
    // Wall / Portal: left and right ends in view columns (0-128 across the
    // original 90 degrees) and wall heights in rows, texture u (0-256 across
    // the face) at the ends.
    float xa, xb, ha, hb, ua, ub;
    float door_a, door_b; // door offset at the ends, 1/64 rows (wall(): the door bytes FF1172/3)
    bool door, gradient;  // door offset used; background from the gradient (else colour 1, 0xD074)
    int16_t tilt;         // FF0E1A
    uint16_t strip;  // st.faces word of the face: 4 strips of (top, bottom) block numbers
    bool shaded;     // shaded by height (st.walls_shaded)
    // Columns [lit_a, lit_b) drawn unshaded (goggles); empty when lit_a >= lit_b.
    int16_t lit_a, lit_b;
    // Sprite: DrawSprite's box in view columns/rows, block (textures sheet and
    // index), shade table (render/sprite_shade.txt), depth.
    int16_t col, row, width, height, scale;
    bool mirror;
    uint8_t sheet, block, shades;
    // sprite(): the box above moved and sized from the object's integer
    // column and scale to its exact ones (anchor()), and the exact scale.
    float fx, fy, fw, fh, fscale;
};

struct Frame {
    std::vector<Cmd> cmds;
    uint8_t gradient[0xA0]; // FF6226: background rows of the odd and even columns
    uint8_t shade;          // FF0E8A: first of the wall shade tables by height
    float tan = 1;          // view_tan when recorded
};

// Tangent of half the horizontal field of view (1 = the original 90 degrees).
extern float view_tan;

extern Frame rendered; // the last finished Render3DView
extern Frame shown;    // the one on screen

void begin();
void end();
// Between begin() and end(): pauses (false) and resumes recording.
void record(bool on);
// project_and_draw: the ends of the next span (columns, heights, texture u).
void edges(float xa, float ha, float xb, float hb, float ua, float ub);
void scale_u(float f); // the sliding door halves (render.cpp half())
void wall(const Cmd& c); // ends filled in from edges()
void portal(const Cmd& c);
void fill();
void sprite(const Cmd& c);
// The object or enemy drawn next: its column and scale as the game computed
// them, and exactly.
void anchor(int16_t col, int16_t scale, float fcol, float fscale);
// Exact column and scale of a point at (x, y) (8.8 cells) from the player.
void exact(int16_t x, int16_t y, float& col, float& scale);
// The rendered frame went to VRAM.
void show();

// The backdrop (plane A) at screen position (x, line) of the original
// 320-pixel screen, any x: the whole 1024-pixel panorama of the backdrop map,
// not just the 512 pixels plane A holds, and the sky drawn above it
// (textures::sky). 0 = transparent.
int backdrop(int x, int line);

// The skyline building of 0x2404 without the game's clipping to the 4:3
// view: d2, d5 = its sprite x and the y of its first row. skyline_pixel()
// gives its colour index at (x, line) like backdrop(), 0 outdoors only.
void skyline(uint16_t d2, int16_t d5);
int skyline_pixel(int x, int line);

// Draws the shown frame into w x h ARGB pixels (0 = transparent), pal = the
// 64 CRAM colours. Columns span the recorded field of view; rows keep the
// original aspect.
void draw(uint32_t* out, int w, int h, const uint32_t* pal);

// Whether the last rendered frame has a non-zero colour at view column col,
// row row (the original 128x80 framebuffer pixel there).
bool pixel_set(float col, float row);

} // namespace game::hires
