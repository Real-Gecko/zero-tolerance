// Native 2D display: tile store, planes, sprites and palette.
//
// This replaces the Mega Drive VDP hardware. There are no registers, no DMA
// engine, no control/data ports and no status reads: game code calls this
// module explicitly (upload tiles, blit maps, set scroll, show sprites) and
// the host composites the screen from these plain buffers.
//
// Tile and entry numbers match the old VRAM layout, so the game's addresses
// keep working: tiles are 8x8 4bpp indexed by VRAM tile number, nametable
// entries keep the VDP format (priority, palette, flip, tile index).
#pragma once

#include "state.hpp"

#include <cstdint>

namespace game::display {

constexpr int W = 320, H = 224;

// Palette (Mega Drive colour words, what CRAM held).
extern uint16_t pal[64];
// 8x8 tiles by VRAM tile number, nibbles (0 = transparent).
extern uint8_t tiles[2048][64];
// Nametable entries per plane (VDP entry format).
extern uint16_t planeA[128 * 64], planeB[128 * 64];
extern int pwA, phA, pwB, phB; // plane size in cells
extern uint16_t ntA, ntB;        // nametable VRAM addresses (route map writes)
// Per-line horizontal scroll per plane, vertical scroll per plane.
extern uint16_t hscrollA[H], hscrollB[H];
extern int hmode; // 0 = full screen, 2 = 8-line, 3 = per line
extern uint16_t vscrollA, vscrollB;
// Window (plane A) position, raw register values.
extern uint8_t win_h, win_v;

// Clears everything (tiles, planes, scroll, sprites, palette).
void reset();
// Clears tiles, planes, scroll, sprites (what ClearVram did); palette kept.
void clear_vram();
// Fills the first n cells of a plane with an entry.
void fill_a(uint16_t e, int n);
void fill_b(uint16_t e, int n);
// Writes one nametable entry at a VRAM address (routes to a plane).
void write_cell(uint16_t nt, uint16_t e);
// Fills n entries from a VRAM address.
void fill_cells(uint16_t nt, int n, uint16_t e);
// Sets sprite slot i directly (y, size, link, tile entry, x).
void set_sprite(int i, uint16_t y, uint8_t size, uint8_t link, uint16_t entry, uint16_t x);
// Clears the sprite list.
void clear_sprites();
// What compose draws: planes or not, which sprites (by their tile entry;
// all when null), and transparent (alpha 0) instead of the palette-0 colour.
struct Layers {
    bool planes = true;
    bool (*sprite)(uint16_t entry) = nullptr;
    bool transparent = false;
};
// Full screen ARGB8888 composition (planes, window, sprites).
void compose(uint32_t* out, int pitch, int x0, int w, const Layers& layers = {});
// Colour index (palette line * 16 + index, 0 = transparent) of tile entry e
// at pixel (x, y) inside the 8x8 tile.
int tile_pixel(uint16_t e, int x, int y);
// Colour of palette entry i, ARGB8888.
uint32_t argb(int i);
// Decodes one 4-pixel word into a tile.
void tile_word(uint16_t tile, int i, uint16_t w);
// Writes 4 bytes (one 8-pixel row) at a VRAM byte address into the tile store.
void tile_long(uint16_t dest, uint32_t w);
// n tiles in the Mega Drive 4bpp format (32 bytes each) from src.
void upload_tiles(uint16_t tile, uint16_t n, const uint8_t* src);
// Blit a w x h map at a nametable VRAM address, rows stride_cells apart,
// exactly like the hardware writes (cells outside the plane stay invisible).
void blit_linear(uint16_t nt, const uint16_t* src, int w, int h, int stride_cells, uint16_t attr);
// Shows n entries of a sprite list.
void upload_sprites(const SatEntry* list, int n);

} // namespace game::display
