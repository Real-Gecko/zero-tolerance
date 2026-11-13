// Zero Tolerance game code translated from the 68000 ROM. Function names are
// the disassembly labels (sub_XXXXXX = ROM address) or names from
// disasm/labels*.txt. Register arguments become parameters.
#pragma once

#include "../md/mem.hpp"

namespace game {

using namespace md;

inline constexpr uint32_t VDP_DATA = 0xC00000, VDP_CTRL = 0xC00004;
inline void vdp_ctrl(uint16_t v) { wr16(VDP_CTRL, v); }
inline void vdp_ctrl32(uint32_t v) { wr32(VDP_CTRL, v); }
inline void vdp_data(uint16_t v) { wr16(VDP_DATA, v); }
inline void vdp_data32(uint32_t v) { wr32(VDP_DATA, v); }
inline bool vdp_dma_busy() { return rd16(VDP_CTRL) & 2; }

// RAM variables (docs/RE_NOTES.md).
inline constexpr U16 vblank_count{0xFF0000};
inline constexpr U16 fb_dma_pending{0xFF0002}; // 2 = DMA the 3D view in the next two VBlanks
inline constexpr U16 warm_boot{0xFF0004};
inline constexpr U32 random_seed{0xFF0006};
inline constexpr uint32_t password = 0xFF000A; // 9 characters
inline constexpr U16 sound_options{0xFF0014};  // bit 0 music, bit 2 FM effects, bit 3 samples
inline constexpr U8 pad_present{0xFF0018};
inline constexpr U8 pad_buttons{0xFF002E};     // active high: bit 7 Start, 6 A, 5 C, 4 B, 3 R, 2 L, 1 D, 0 U
inline constexpr U8 pad_extra{0xFF002F};
inline constexpr U16 pad_prev{0xFF0032};
inline constexpr U32 vdp_queue_ptr{0xFF0036};
inline constexpr uint32_t vdp_queue = 0xFF0544;
inline constexpr U16 demo_mode{0xFF06DC};
inline constexpr U16 player_x{0xFF0DEC};
inline constexpr U16 player_y{0xFF0DEE};
inline constexpr U16 player_angle{0xFF0E04};
inline constexpr U32 cur_map_ptr{0xFF1024};
inline constexpr U16 cur_level{0xFF1028};
inline constexpr U16 last_sound{0xFF2708};
inline constexpr U16 cur_area{0xFF271C};
inline constexpr U16 link_mode{0xFF2912}; // two-console link cable game
inline constexpr U16 link_sr{0xFF291C};
inline constexpr U8 level_id{0xFF28FC};   // bits 4-5 area, bits 0-3 level
inline constexpr uint32_t framebuffer = 0xFF3926;
inline constexpr uint32_t cell_class = 0xFF7BC6;

[[noreturn]] void untranslated(uint32_t addr);
struct Untranslated {
    uint32_t addr;
};
extern bool untranslated_throws; // tests: throw Untranslated instead of exiting

// boot.cpp
[[noreturn]] void Reset();
void VBlank();
uint32_t Random();
void sub_000744(uint32_t src, uint16_t dest, uint16_t len);
void sub_00079A();
void ReadPad();
void sub_000BAE(uint32_t src, int count);
void sub_000BD2(uint32_t stack_top);
void sub_000BF2();
void sub_000C5A();
void sub_000CAA();
void sub_000D36();
void sub_000FC0();
void sub_001D18();
void sub_00D760(uint16_t id);
void sub_058054();
void sub_05755E(uint16_t level);
uint16_t GameLevelLoop(); // d7: 1 = restart from the boot screens

// technopop.cpp, wall.cpp
void WaitVBlank();
void DrawWallSpan(uint32_t left, uint32_t right);

// intro.cpp
void vdp_write_addr(uint16_t a);
uint16_t LoadImageRes(uint16_t vram, uint16_t nt, uint16_t attr, uint32_t res);
uint16_t LoadTiles(uint16_t vram, uint32_t& res);
void sub_002022(uint16_t vram, uint16_t nt, uint16_t attr, uint32_t res);
void sub_01CC04(int count);
void sub_01CDEE();
void sub_01EF50();
void sub_01F578();
void WaitFrames(uint16_t n);
void ClearCram();
void ClearVram();
void LoadPaletteLine(int line, uint32_t src);
void LoadTilesAt(uint16_t tile, uint16_t n, uint32_t src);
void SetVScrollA(uint16_t v);
void SetVScrollB(uint16_t v);
void LoadMap(uint32_t src, uint16_t dest, uint16_t w, uint16_t h, uint16_t pitch, uint16_t attr);
void FadeIn(uint32_t src, uint16_t delay, int count = 64);
void FadeOut(uint32_t src, uint16_t delay, int count = 64);
void FadeFromWhite(uint32_t src, uint16_t delay, int count = 64);
void FadeToWhite(uint32_t src, uint16_t delay, int count = 64);
void FillPlaneA(uint16_t v);

// title.cpp
void sub_01CF36();

// sound.cpp
void sound_init();
void PlaySound(uint16_t id);
void sub_0C615A(uint16_t id);
void sub_0C62E8();
void sub_0C5DAC();
void sub_0C633A();
void sub_0C6344();

// link.cpp
void sub_01BFF0();
void sub_01C03A(uint32_t packet);
void sub_01C9A0(uint32_t a1);

// player.cpp
uint8_t sub_00E238(uint16_t d3);

void sub_05711E();

// level.cpp
uint32_t gameplay_frame();
void sub_000E78();
void sub_000EB6();
void loc_000F2A();
void sub_001A0C();
void sub_001C24();
void sub_001CBC();

// inventory.cpp
void DrawAmmoDigits(uint16_t n);
void sub_010A7A(uint32_t slot);
void sub_0109E6();
void sub_011C02();
void sub_01125E();
uint16_t sub_011286();
uint16_t sub_0112F2();
uint16_t sub_011084(uint16_t weapon);
void sub_010B00();
void sub_010B8C();
uint16_t sub_010BD8(); // return d0
uint16_t sub_010CC6();
void sub_010DC0();
void sub_010EAC();
void sub_010F98();
void sub_057E0C();
void sub_057EC4();
void sub_057F98();
void sub_058720(uint32_t out);

// hud.cpp
void sub_01E00E(uint16_t n);
void sub_01E0C2(uint32_t msg);
void sub_01E0DE();
void sub_01E2CC();
void sub_01E0FA();
void sub_00D98E();

// still to translate
void sub_00B8FC(uint16_t level);
void sub_00B9A6();
void sub_01C9CA();
uint16_t sub_0CAA4E(uint16_t story, uint32_t a1);
void sub_0C6362(uint32_t a1);
uint16_t sub_058A74(uint32_t password); // d7: 0xFFFF = invalid
void sub_10B11A();

// player.cpp (movement)
void loc_00A1D2();
void sub_00DAB8();
void sub_00EC1C();
void sub_00A9CE(uint16_t level);
uint16_t sub_00B33A(uint32_t a0);
void sub_00B35C();
void loc_00B48A();
void PickupItem(uint32_t a0, uint8_t d3);
void CellUseHandler(uint32_t addr, uint32_t a0, uint16_t d3); // targets of table 0xE420
void sub_00D7DC(uint16_t d0, uint16_t d3, uint16_t d4);
void sub_00D800(uint16_t d0, uint16_t d3, uint16_t d4); // player hit
void sub_00D940();

// status.cpp
void sub_0029DA();
void sub_002CDC();
void sub_002CEC();
void sub_002FA0();
uint16_t sub_001D46(uint16_t mode); // returns d0
uint16_t sub_001D66(uint16_t mode);
uint16_t sub_00D494();
uint16_t sub_00D49A();
void sub_0022BE();
void sub_0023C2();

// radar.cpp
void DrawRadar();
void sub_00E3C4(uint32_t a0);
void sub_00E3F8(uint32_t a0);
uint32_t sub_00E966(uint32_t a2, uint16_t d5);
uint32_t sub_00EB5A(uint32_t a2, uint16_t d5);

// enemy.cpp
void sub_013182();
void sub_0131E8();
void sub_0133A4();
void sub_013554(uint32_t a0);
void sub_0149DE();
void sub_0168AC();

// weapon.cpp
void sub_00ECC0();
void sub_00ECE2();

// sky.cpp
void h_002682();
uint32_t sub_002404(uint32_t a2);
void sub_0028B4(uint16_t d0);
void sub_0029BA();

} // namespace game
