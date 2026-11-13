// 3D view rendering shared between render.cpp, wall.cpp and the sprite code.
#pragma once

#include <cstdint>

namespace game {

// The 68000 registers the ray caster and the cell handlers share.
struct CastRegs {
    uint16_t d0, d1, d2, d3, d4, d5, d6, d7;
    uint32_t a0, a1, a2, a3, a4, a5;
};
extern CastRegs cr;
extern bool skip_object_pass; // tests: stop Render3DView before the sprites

bool CellSpecialDispatch();
void AddVisibleObject();
void CastRay(uint16_t angle);
uint16_t Distance(int16_t x, int16_t y);
void DrawPortalSpan(uint32_t left, uint32_t right);
void Render3DView();
void sub_007F92();
void sub_009D72();
void DrawEnemiesAndObjects(); // 0x1329A
uint32_t sub_013466();        // allocates an enemy slot, 0 when full
void sub_00B4E4(uint8_t cls);
void sub_013526(uint32_t enemy);
// 0x113B0: draws the object entry before a3 and steps a3 back.
void DrawObjectSprite(uint32_t& a3);
// Indirect calls through the handler addresses stored in an enemy.
void CallEnemyDraw(uint32_t addr, uint32_t enemy, uint16_t scale, uint16_t column);
void CallEnemyHandler(uint32_t addr, uint32_t enemy);
void CallEnemyHit(uint32_t addr, uint32_t enemy, uint16_t d0, uint16_t d3, uint16_t d4);
void DrawSprite(int16_t height, int16_t width, int16_t col, int16_t row, uint32_t sprite);
void DrawSpriteSet(uint32_t set, uint16_t anim, uint16_t frame, uint16_t part, int16_t col, int16_t row, uint16_t scale);
void DrawSpriteBlocks(uint32_t part, uint32_t blocks, int16_t col, int16_t row, uint16_t scale);

} // namespace game
