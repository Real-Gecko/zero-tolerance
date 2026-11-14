// The game's data files (data/), loaded by name on first use.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace assets {

// Big-endian values in asset bytes.
inline uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
inline uint32_t be32(const uint8_t* p) { return uint32_t(be16(p)) << 16 | be16(p + 2); }

// The data directory the files below are read from.
void init(const std::string& data);
// The lines of a text file, without empty and # comment lines.
const std::vector<std::string>& lines(const char* file);
// The quoted text of each line of a text file.
const std::vector<std::string>& list(const char* file);
// A screen text by name (text/strings.txt).
const char* text(const char* name);
// Colour words of a palette swatch PNG (16 per row), loaded on first use.
const uint16_t* palette(const std::string& file);
// The hex bytes of a text file (whitespace separated), loaded on first use.
const std::vector<uint8_t>& hex(const char* file);
// The bytes of a binary file, loaded on first use.
const std::vector<uint8_t>& bytes(const std::string& file);
// An indexed PNG (index = palette line * 16 + colour, colour 0 transparent)
// cut into 8x8 cells: the distinct tiles in the Mega Drive 4bpp format, 32
// bytes each, flipped repeats sharing one, and the map of tile entries (tile,
// flips, palette line), row by row. Loaded on first use.
struct Tilemap {
    int w = 0, h = 0; // cells
    std::vector<uint8_t> tiles;
    std::vector<uint16_t> map;
    uint16_t count() const { return uint16_t(tiles.size() / 32); }
};
const Tilemap& tilemap(const char* file);
// The 8x8 tiles of an indexed PNG (width / 8 per row) in the Mega Drive 4bpp
// format, 32 bytes each, loaded on first use.
const std::vector<uint8_t>& tiles(const std::string& file);
// The tiles of an indexed PNG of sprites side by side, tops aligned, of the
// sizes in cells (repeated to the image's right edge), each column by column
// as the VDP reads a sprite. Loaded on first use.
struct Cell {
    int w, h; // tiles
};
const std::vector<uint8_t>& tiles(const std::string& file, const std::vector<Cell>& cells);
// The 32x32 blocks of an indexed PNG (cols per row) as 16 byte columns of 32
// rows each (two pixels per byte), loaded on first use.
const std::vector<uint8_t>& blocks(const std::string& file, int cols);

} // namespace assets
