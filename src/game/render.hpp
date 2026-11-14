// 3D view rendering shared between render.cpp, wall.cpp and the sprite code.
#pragma once

#include "object.hpp"
#include "state.hpp"
#include "textures.hpp"

#include <cstdint>
#include <vector>

namespace game {

// The 68000 registers the ray caster and the cell handlers share.
struct CastRegs {
    uint16_t d0, d1, d2, d3, d4, d5, d6, d7;
    Cell a0, a1;  // map cells
    uint16_t a4;  // angle of the ray's slopes (kRaySlopes)
};
extern CastRegs cr;
extern bool skip_object_pass; // tests: stop Render3DView before the sprites

// A wall end relative to the view: distance along it and across (x 64).
struct Edge {
    int32_t depth, lateral;
};

// A map object a ray met, to be drawn after the walls.
struct ViewObject {
    int16_t y, x; // cell relative to the player's
    uint8_t cls;
    int16_t scale, col;
};

// A stair opening: its two corners (cells relative to the player's) and
// whether the background shows through (FF119A).
struct Portal {
    int8_t x0, y0, x1, y1;
    uint16_t gradient;
};

// The ray caster's variables (FF0E06-FF119A, FF6126), set up by each Render3DView.
struct View {
    uint16_t cell_x, cell_y;          // FF0E06: the player's cell
    uint16_t x_rest, y_rest;          // FF0E0A: 0xFF - the position in the cell
    uint16_t x_in, y_in;              // FF0E0E: the position in the cell
    int16_t north, south, east, west; // FF0E34-FF0E3A: the farthest cells cast, relative
    Edge a, b;                        // FF0E3C / FF0E4C: the ends of the next span
    uint16_t b_x, b_y;                // FF0E4C: the point of b, reused as the next a
    uint16_t faces;                   // FF0E6C: first cell_faces entry of the hit cell
    uint16_t strip;                   // FF0E70: faces word of the face drawn
    int16_t first_ray, right_col;     // FF0E74 / FF0E76
    Cell last_cell;                   // FF0E78
    bool door_hit;                    // FF0E7C: the ray met a door (no spawning)
    Cell seen_y[64], seen_x[64];      // FF0FA6 / FF0FCE: door cells drawn
    uint8_t seen_y_n, seen_x_n;       // FF0EBA / FF0EBE
    ViewObject objects[20];           // FF10A2
    uint8_t object_n;                 // FF116A
    uint8_t door[2];                  // FF1172: door offsets at the face's ends
    uint8_t sides[8];                 // FF1174: door offsets of the S, N, W and E faces
    Portal portals[64];               // FF1182
    uint16_t portal_n;                // FF1180
    uint16_t portal_gradient;         // FF119A
    uint16_t depth[0x80];             // FF6126: wall height drawn in each column
};
extern View view;

bool CellSpecialDispatch();
void AddVisibleObject();
void CastRay(uint16_t angle);
uint16_t Distance(int16_t x, int16_t y);
void DrawPortalSpan();
void Render3DView();
void WideWallDepth(); // PC: the depth buffer and FF0E76 from the last wide wall span
void FillBackground();
void DrawPortals();
void DrawEnemiesAndObjects(); // 0x1329A
Enemy* AllocEnemy();          // allocates an enemy slot, nullptr when full
void StandOnCell(uint8_t cls);
void UnlinkEnemy(Enemy* e);
// 0x113B0: draws a map object of the view.
void DrawObjectSprite(const ViewObject& o);
// Calls through the handlers of an object: AI/death, drawing (scale, column),
// hit (distance d0, direction d3/d4 to the shooter).
void RunAct(Act act, Enemy* e);
void DrawObject(Enemy* e, uint16_t scale, uint16_t column);
void HitObject(Enemy* e, uint16_t d0, uint16_t d3, uint16_t d4);
// A 32x32 block of a sheet (textures::block).
struct SpriteBlock {
    uint8_t sheet, index;
};
// Block n of the map objects (view/objects.png; 0x10E9BE + n * 0x200).
constexpr SpriteBlock object_block(int n) { return {textures::kObjects, uint8_t(n)}; }
// One frame of a view of a sprite set animation (sprites.cpp): its anchor
// (0 left/top, 1 centre, 2 right/bottom), block size and blocks.
struct SpritePart {
    uint8_t ax, ay, bw, bh, cols, rows;
    uint8_t block[4][4];
    bool mirror[4][4];
};
// The animations of a sprite set: views of parts.
using SpriteAnims = std::vector<std::vector<std::vector<SpritePart>>>;
const SpriteAnims& sprite_anims(SpriteSet set);
void DrawSprite(int16_t height, int16_t width, int16_t col, int16_t row, SpriteBlock block);
void DrawSpriteSet(SpriteSet set, uint16_t anim, uint16_t frame, uint16_t part, int16_t col, int16_t row, uint16_t scale);
void DrawSpriteBlocks(const SpritePart& part, SpriteSet set, int16_t col, int16_t row, uint16_t scale);

} // namespace game
