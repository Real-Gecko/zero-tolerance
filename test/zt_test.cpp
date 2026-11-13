// Test entry points for tools/difftest.py: runs one translated routine on a
// given machine state so it can be compared with the original 68000 code
// running in Unicorn.
#include "game/enemy.hpp"
#include "game/game.hpp"
#include "game/m68k.hpp"
#include "game/render.hpp"
#include "host.hpp"
#include "md/pad.hpp"
#include "md/vdp.hpp"

#include <cstring>
#include <functional>
#include <map>
#include <vector>

namespace host {
Options opt;
uint32_t frame_count = 0;
// The difftest Unicorn side runs the VBlank handler whenever a wait loop
// would spin; doing the same here keeps the two in step.
void frame() {
    frame_count++;
    game::VBlank();
}
void set_vblank_handler(void (*)()) {}
} // namespace host

namespace {

uint32_t exit_pc = 0;

struct Regs {
    uint32_t d[8], a[8];
};

struct Entry {
    std::function<void(Regs&)> run;
    uint16_t outputs; // bit n = d<n>, bit 8+n = a<n>
};

using namespace game;

const std::map<uint32_t, Entry>& table() {
    static const std::map<uint32_t, Entry> t = {
        {0x000A5C, {[](Regs& r) { r.d[2] = Random(); }, 1 << 2}},
        {0x000744, {[](Regs& r) { sub_000744(r.d[4], uint16_t(r.d[5]), uint16_t(r.d[6])); }, 0}},
        {0x000BD2, {[](Regs& r) { sub_000BD2(r.a[7]); }, 0}},
        {0x000BF2, {[](Regs&) { sub_000BF2(); }, 0}},
        {0x000C5A, {[](Regs&) { sub_000C5A(); }, 0}},
        {0x000CAA, {[](Regs&) { sub_000CAA(); }, 0}},
        {0x000FC0, {[](Regs&) { sub_000FC0(); }, 0}},
        {0x000AA2, {[](Regs&) { VBlank(); }, 0}},
        {0x001F72, {[](Regs& r) { r.d[0] = (r.d[0] & 0xFFFF0000) | LoadImageRes(uint16_t(r.d[0]), uint16_t(r.d[1]), uint16_t(r.d[3]), r.a[0]); }, 0}},
        {0x002022, {[](Regs& r) { sub_002022(uint16_t(r.d[0]), uint16_t(r.d[1]), uint16_t(r.d[3]), r.a[0]); }, 0}},
        {0x01CC04, {[](Regs& r) { sub_01CC04(int(r.d[7] & 0xFFFF) + 1); }, 0}},
        {0x01F578, {[](Regs&) { sub_01F578(); }, 0}},
        {0x01F648, {[](Regs& r) { LoadPaletteLine(int(r.d[0] & 3), r.a[0]); }, 0}},
        {0x01F66C, {[](Regs& r) { LoadTilesAt(uint16_t(r.d[0]), uint16_t(r.d[1]), r.a[0]); }, 0}},
        {0x01F740, {[](Regs& r) { LoadMap(r.a[0], uint16_t(r.a[1] + 0xC000), uint16_t(r.d[0]), uint16_t(r.d[1]), uint16_t(r.d[2]), uint16_t(r.d[3])); }, 0}},
        {0x058A74, {[](Regs& r) { r.d[7] = (r.d[7] & 0xFFFF0000) | sub_058A74(r.a[0]); }, 1 << 7}},
        // The caller's d6 reaches the portal list (0x9D46 st.b d6 keeps its high byte).
        {0x00B9D2, {[](Regs& r) { cr.d6 = uint16_t(r.d[6]); Render3DView(); }, 0}},
        {0x00B9A6, {[](Regs& r) { cr.d6 = uint16_t(r.d[6]); sub_00B9A6(); }, 0}},
        {0x0113B0, {[](Regs& r) { uint32_t a3 = r.a[3]; DrawObjectSprite(a3); r.a[3] = a3; }, 1 << 11}},
        {0x00BB74, {[](Regs& r) {
             cr.d6 = uint16_t(r.d[6]);
             skip_object_pass = true;
             Render3DView();
             skip_object_pass = false;
             exit_pc = md::rd8(0xBB90 + md::rd8(0xFF7BC6 + md::rd8(md::rd32(0xFF0E12)))) ? 0xBB74 : 0xBB8A;
         }, 0}},
        {0x001442, {[](Regs&) { exit_pc = gameplay_frame(); }, 0}},
        {0x00CCB6, {[](Regs& r) { DrawWallSpan(r.a[2], r.a[3]); }, 0}},
        {0x01C03A, {[](Regs& r) { sub_01C03A(r.a[0]); }, 0}},
        {0x00B25E, {[](Regs& r) { CellUseHandler(0xB25E, r.a[0], uint16_t(r.d[3])); }, 0}},
        {0x00B29A, {[](Regs& r) { CellUseHandler(0xB29A, r.a[0], uint16_t(r.d[3])); }, 0}},
        {0x00E63C, {[](Regs& r) { CellUseHandler(0xE63C, r.a[0], uint16_t(r.d[3])); }, 0}},
        {0x00E644, {[](Regs& r) { CellUseHandler(0xE644, r.a[0], uint16_t(r.d[3])); }, 0}},
        {0x00E64E, {[](Regs& r) { CellUseHandler(0xE64E, r.a[0], uint16_t(r.d[3])); }, 0}},
        {0x00E658, {[](Regs& r) { CellUseHandler(0xE658, r.a[0], uint16_t(r.d[3])); }, 0}},
        {0x00E662, {[](Regs& r) { CellUseHandler(0xE662, r.a[0], uint16_t(r.d[3])); }, 0}},
        {0x011AB4, {[](Regs& r) { CellUseHandler(0x11AB4, r.a[0], uint16_t(r.d[3])); }, 0}},
        {0x002682, {[](Regs& r) { CellUseHandler(0x2682, r.a[0], uint16_t(r.d[3])); }, 0}},
        {0x00E7D6, {[](Regs& r) { CellUseHandler(0xE7D6, r.a[0], uint16_t(r.d[3])); }, 0}},
        {0x00E69E, {[](Regs& r) { CellUseHandler(0xE69E, r.a[0], uint16_t(r.d[3])); }, 0}},
        {0x00E6FE, {[](Regs& r) { CellUseHandler(0xE6FE, r.a[0], uint16_t(r.d[3])); }, 0}},
        {0x00E75E, {[](Regs& r) { CellUseHandler(0xE75E, r.a[0], uint16_t(r.d[3])); }, 0}},
        {0x00E634, {[](Regs& r) { CellUseHandler(0xE634, r.a[0], uint16_t(r.d[3])); }, 0}},
        {0x00A1D2, {[](Regs&) { loc_00A1D2(); }, 0}},
        {0x00A9CE, {[](Regs& r) { sub_00A9CE(uint16_t(r.d[0])); }, 0}},
        {0x00B33A, {[](Regs& r) { r.d[3] = (r.d[3] & 0xFFFF0000) | sub_00B33A(r.a[0]); }, 1 << 3}},
        {0x00B35C, {[](Regs&) { sub_00B35C(); }, 0}},
        {0x00DAB8, {[](Regs&) { sub_00DAB8(); }, 0}},
        {0x00EC1C, {[](Regs&) { sub_00EC1C(); }, 0}},
        {0x00D494, {[](Regs& r) { r.d[0] = (r.d[0] & 0xFFFF0000) | sub_00D494(); }, 1 << 0}},
        {0x00D49A, {[](Regs& r) { r.d[0] = (r.d[0] & 0xFFFF0000) | sub_00D49A(); }, 1 << 0}},
        {0x001D66, {[](Regs& r) { r.d[0] = (r.d[0] & 0xFFFF0000) | sub_001D66(uint16_t(r.d[0])); }, 1 << 0}},
        {0x001D46, {[](Regs& r) { r.d[0] = (r.d[0] & 0xFFFF0000) | sub_001D46(uint16_t(r.d[0])); }, 1 << 0}},
        {0x0022BE, {[](Regs&) { sub_0022BE(); }, 0}},
        {0x0023C2, {[](Regs&) { sub_0023C2(); }, 0}},
        {0x002FA0, {[](Regs&) { sub_002FA0(); }, 0}},
        {0x05755E, {[](Regs& r) { sub_05755E(uint16_t(r.d[1])); }, 0}},
        {0x0029DA, {[](Regs&) { sub_0029DA(); }, 0}},
        {0x002CEC, {[](Regs&) { sub_002CEC(); }, 0}},
        {0x00E816, {[](Regs&) { DrawRadar(); }, 0}},
        {0x00E3C4, {[](Regs& r) { sub_00E3C4(r.a[0]); }, 0}},
        {0x00E3F8, {[](Regs& r) { sub_00E3F8(r.a[0]); }, 0}},
        {0x00E966, {[](Regs& r) { r.a[2] = sub_00E966(r.a[2], uint16_t(r.d[5])); }, 1 << 10}},
        {0x00EB5A, {[](Regs& r) { r.a[2] = sub_00EB5A(r.a[2], uint16_t(r.d[5])); }, 1 << 10}},
        {0x010BD8, {[](Regs&) { sub_010BD8(); }, 0}},
        {0x010CC6, {[](Regs&) { sub_010CC6(); }, 0}},
        {0x010DC0, {[](Regs&) { sub_010DC0(); }, 0}},
        {0x010EAC, {[](Regs&) { sub_010EAC(); }, 0}},
        {0x013182, {[](Regs&) { sub_013182(); }, 0}},
        {0x0131E8, {[](Regs&) { sub_0131E8(); }, 0}},
        {0x0133A4, {[](Regs&) { sub_0133A4(); }, 0}},
        {0x013554, {[](Regs& r) { sub_013554(r.a[0]); }, 0}},
        {0x0145AA, {[](Regs& r) { auto m = sub_0145AA(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[1])); r.d[0] = (r.d[0] & 0xFFFF0000) | m.d0; r.d[1] = (r.d[1] & 0xFFFF0000) | m.d1; }, 3}},
        {0x0146FE, {[](Regs& r) { auto m = sub_0146FE(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[1])); r.d[0] = (r.d[0] & 0xFFFF0000) | m.d0; r.d[1] = (r.d[1] & 0xFFFF0000) | m.d1; }, 3}},
        {0x013D42, {[](Regs& r) { r.d[3] = (r.d[3] & 0xFFFF0000) | sub_013D42(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[1])); }, 1 << 3}},
        {0x013CBC, {[](Regs& r) { r.d[3] = (r.d[3] & 0xFFFF0000) | sub_013CBC(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[1])); }, 1 << 3}},
        {0x01642C, {[](Regs& r) { r.a[1] = sub_01642C(r.a[0]); }, 1 << 9}},
        {0x01649C, {[](Regs& r) { r.a[1] = sub_01649C(r.a[0]); }, 1 << 9}},
        {0x0165D8, {[](Regs& r) { r.d[0] = ext_l(uint16_t(r.d[0])) & 0xFFFF0000 | sub_0165D8(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[1]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 1 << 0}},
        {0x0166E8, {[](Regs& r) { r.d[0] = ext_l(uint16_t(r.d[0])) & 0xFFFF0000 | sub_0166E8(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[1]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 1 << 0}},
        {0x016590, {[](Regs& r) { r.d[3] = sub_016590(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[1]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 1 << 3}},
        {0x0166A0, {[](Regs& r) { r.d[3] = sub_0166A0(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[1]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 1 << 3}},
        {0x014454, {[](Regs& r) { h_014454(r.a[0]); }, 0}},
        {0x0144FE, {[](Regs& r) { h_0144FE(r.a[0]); }, 0}},
        {0x014A56, {[](Regs& r) { h_014A56(r.a[0]); }, 0}},
        {0x014B08, {[](Regs& r) { h_014B08(r.a[0]); }, 0}},
        {0x00D740, {[](Regs& r) { sub_00D740(r.a[0], uint16_t(r.d[0])); }, 0}},
        {0x01BA04, {[](Regs& r) { sub_01BA04(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[2]), int16_t(r.d[1]), uint16_t(r.d[5])); }, 0}},
        {0x01B952, {[](Regs& r) { h_01B952(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x0159FE, {[](Regs& r) { h_0159FE(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x016040, {[](Regs& r) { h_016040(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x01B97A, {[](Regs& r) { h_01B97A(r.a[0]); }, 0}},
        {0x0158CC, {[](Regs& r) { h_0158CC(r.a[0]); }, 0}},
        {0x0159EE, {[](Regs& r) { h_0159EE(r.a[0]); }, 0}},
        {0x01B9BC, {[](Regs& r) { h_01B9BC(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x0157CA, {[](Regs& r) { sub_0157CA(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x016084, {[](Regs& r) { loc_016084(r.a[0]); }, 0}},
        {0x0160E8, {[](Regs& r) { loc_0160E8(r.a[0]); }, 0}},
        {0x0161B2, {[](Regs& r) { loc_0161B2(r.a[0]); }, 0}},
        {0x01B8D8, {[](Regs& r) { loc_01B8D8(r.a[0]); }, 0}},
        {0x0149DE, {[](Regs&) { sub_0149DE(); }, 0}},
        {0x0168AC, {[](Regs&) { sub_0168AC(); }, 0}},
        {0x00B48A, {[](Regs&) { loc_00B48A(); }, 0}},
        {0x00D800, {[](Regs& r) { sub_00D800(uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x00D7DC, {[](Regs& r) { sub_00D7DC(uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x00D940, {[](Regs&) { sub_00D940(); }, 0}},
        {0x010F98, {[](Regs&) { sub_010F98(); }, 0}},
        {0x013D48, {[](Regs& r) { r.d[3] = (r.d[3] & 0xFFFF0000) | sub_013D48(uint16_t(r.d[0]), uint16_t(r.d[1])); }, 1 << 3}},
        {0x013C36, {[](Regs& r) { r.d[3] = (r.d[3] & 0xFFFF0000) | sub_013C36(uint16_t(r.d[0]), uint16_t(r.d[1])); }, 1 << 3}},
        {0x016388, {[](Regs& r) { r.a[1] = sub_016388(r.a[0], uint16_t(r.d[5])); }, 1 << 9}},
        {0x0167B0, {[](Regs& r) { auto t = sub_0167B0(uint16_t(r.d[1]), uint16_t(r.d[2])); r.a[1] = t.a1; }, 1 << 9}},
        {0x0108D6, {[](Regs& r) { r.a[2] = sub_0108D6(r.a[2]); }, 1 << 10}},
        {0x011C2C, {[](Regs&) { sub_011C2C(); }, 0}},
        {0x00ECE2, {[](Regs&) { sub_00ECE2(); }, 0}},
        {0x00ECC0, {[](Regs&) { sub_00ECC0(); }, 0}},
        {0x002404, {[](Regs& r) { r.a[2] = sub_002404(r.a[2]); }, 1 << 10}},
        {0x0028B4, {[](Regs& r) { sub_0028B4(uint16_t(r.d[0])); }, 0}},
        {0x012A70, {[](Regs&) { sub_012A70(); }, 0}},
        {0x012AD4, {[](Regs&) { sub_012AD4(); }, 0}},
        {0x00A5D4, {[](Regs&) { sub_00A5D4(); }, 0}},
        {0x015AC4, {[](Regs& r) { h_015AC4(r.a[0]); }, 0}},
        {0x013ADC, {[](Regs& r) { h_013ADC(r.a[0]); }, 0}},
        {0x013E42, {[](Regs& r) { h_013E42(r.a[0]); }, 0}},
        {0x013EC0, {[](Regs& r) { h_013EC0(r.a[0]); }, 0}},
        {0x013F7C, {[](Regs& r) { h_013F7C(r.a[0]); }, 0}},
        {0x014076, {[](Regs& r) { h_014076(r.a[0]); }, 0}},
        {0x014196, {[](Regs& r) { h_014196(r.a[0]); }, 0}},
        {0x01423A, {[](Regs& r) { h_01423A(r.a[0]); }, 0}},
        {0x0142C6, {[](Regs& r) { h_0142C6(r.a[0]); }, 0}},
        {0x013F3E, {[](Regs& r) { h_013F3E(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x01402E, {[](Regs& r) { h_01402E(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x014150, {[](Regs& r) { h_014150(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x0141E2, {[](Regs& r) { h_0141E2(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x014272, {[](Regs& r) { h_014272(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x013AD2, {[](Regs& r) { h_013AD2(r.a[0], uint16_t(r.d[0])); }, 0}},
        {0x00A6BC, {[](Regs& r) { sub_00A6BC(uint16_t(r.d[0]), uint16_t(r.d[1])); }, 0}},
        {0x015AD6, {[](Regs& r) { h_015AD6(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x0182FC, {[](Regs& r) { h_0182FC(r.a[0]); }, 0}},
        {0x0186B8, {[](Regs& r) { h_0186B8(r.a[0]); }, 0}},
        {0x01856C, {[](Regs& r) { h_01856C(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x018660, {[](Regs& r) { h_018660(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x0187C8, {[](Regs& r) { h_0187C8(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x01887A, {[](Regs& r) { h_01887A(r.a[0]); }, 0}},
        {0x018A66, {[](Regs& r) { h_018A66(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x018AC8, {[](Regs& r) { body_frame(r.a[0], 6, uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x018AD0, {[](Regs& r) { h_01B97A(r.a[0]); }, 0}},
        {0x018B12, {[](Regs& r) { h_0187C8(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x018B7C, {[](Regs& r) { h_01B9BC(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x018BC4, {[](Regs& r) { h_018BC4(r.a[0]); }, 0}},
        {0x018EE8, {[](Regs& r) { h_018EE8(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x018F50, {[](Regs& r) { body_walk(r.a[0], 4, uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x019430, {[](Regs& r) { body_walk(r.a[0], 6, uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x019060, {[](Regs& r) { h_019060(r.a[0]); }, 0}},
        {0x0193C8, {[](Regs& r) { h_0193C8(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x01943C, {[](Regs& r) { h_018F5C(r.a[0]); }, 0}},
        {0x01947E, {[](Regs& r) { h_018F9E(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x015F6E, {[](Regs& r) { h_015F6E(r.a[0]); }, 0}},
        {0x015B1E, {[](Regs& r) { h_015B1E(r.a[0]); }, 0}},
        {0x015D84, {[](Regs& r) { h_015D84(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x015DD0, {[](Regs& r) { h_015DD0(r.a[0]); }, 0}},
        {0x015DFC, {[](Regs& r) { h_015DFC(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x015E4E, {[](Regs& r) { h_015E4E(r.a[0]); }, 0}},
        {0x015ED2, {[](Regs& r) { loc_015ED2(r.a[0]); }, 0}},
        {0x00B130, {[](Regs& r) { sub_00B130(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[1])); }, 0}},
        {0x00B168, {[](Regs& r) { sub_00B168(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[1])); }, 0}},
        {0x00B1C4, {[](Regs& r) { sub_00B1C4(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[1])); }, 0}},
        {0x00B202, {[](Regs& r) { sub_00B202(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[1])); }, 0}},
        {0x019540, {[](Regs& r) { h_019540(r.a[0]); }, 0}},
        {0x019A48, {[](Regs& r) { h_019A48(r.a[0]); }, 0}},
        {0x01A324, {[](Regs& r) { h_01A324(r.a[0]); }, 0}},
        {0x01A92A, {[](Regs& r) { h_01A92A(r.a[0]); }, 0}},
        {0x01B078, {[](Regs& r) { h_01B078(r.a[0]); }, 0}},
        {0x014C2A, {[](Regs& r) { h_014C2A(r.a[0]); }, 0}},
        {0x014C40, {[](Regs& r) { h_014C40(r.a[0]); }, 0}},
        {0x01539E, {[](Regs& r) { h_01539E(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x015618, {[](Regs& r) { h_015618(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x0156D2, {[](Regs& r) { h_01B97A(r.a[0]); }, 0}},
        {0x01570C, {[](Regs& r) { h_01570C(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x01575C, {[](Regs& r) { h_01575C(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x01B562, {[](Regs& r) { h_01B562(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x01B68C, {[](Regs& r) { body_frame(r.a[0], 0xB, uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x01B6A2, {[](Regs& r) { h_019FD2(r.a[0]); }, 0}},
        {0x01B824, {[](Regs& r) { h_01B824(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x01B890, {[](Regs& r) { h_01B9BC(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x01AD9A, {[](Regs& r) { h_01AD9A(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x01AE9E, {[](Regs& r) { body_frame(r.a[0], 7, uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x01AEB4, {[](Regs& r) { h_01AEB4(r.a[0]); }, 0}},
        {0x01AFC6, {[](Regs& r) { h_01AFC6(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x01A722, {[](Regs& r) { h_01A722(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x01A7FC, {[](Regs& r) { body_walk(r.a[0], 9, uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x01A808, {[](Regs& r) { h_018F5C(r.a[0]); }, 0}},
        {0x01A84A, {[](Regs& r) { h_01A84A(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x019ECA, {[](Regs& r) { h_019ECA(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x019FCA, {[](Regs& r) { body_frame(r.a[0], 4, uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x019FD2, {[](Regs& r) { h_019FD2(r.a[0]); }, 0}},
        {0x01A154, {[](Regs& r) { h_01A154(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x0198C4, {[](Regs& r) { h_0198C4(r.a[0], uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x01994C, {[](Regs& r) { body_walk(r.a[0], 6, uint16_t(r.d[5]), uint16_t(r.d[1])); }, 0}},
        {0x019958, {[](Regs& r) { h_019958(r.a[0]); }, 0}},
        {0x01999A, {[](Regs& r) { h_01999A(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x018F5C, {[](Regs& r) { h_018F5C(r.a[0]); }, 0}},
        {0x018F9E, {[](Regs& r) { h_018F9E(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x01901C, {[](Regs& r) { h_01901C(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x0194FC, {[](Regs& r) { h_01901C(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x019A04, {[](Regs& r) { h_01901C(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x01A8C8, {[](Regs& r) { h_01901C(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x01A1CC, {[](Regs& r) { h_01B9BC(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x01B030, {[](Regs& r) { h_01B9BC(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
        {0x018832, {[](Regs& r) { h_01B9BC(r.a[0], uint16_t(r.d[0]), uint16_t(r.d[3]), uint16_t(r.d[4])); }, 0}},
    };
    return t;
}

} // namespace

extern "C" {

// rom: whole ROM; ram: 64 KB in/out; regs: d0-d7 a0-a7 in/out; log receives
// VDP port writes as (address << 16 | value) words, returns -1 if the routine
// is not in the table, else the output register mask.
int zt_call(uint32_t addr, const uint8_t* rom, uint32_t rom_size, uint8_t* ram, uint32_t* regs,
            uint32_t* log, uint32_t* log_len) {
    auto it = table().find(addr);
    if (it == table().end()) return -1;
    md::rom = rom;
    md::rom_size = rom_size;
    std::memcpy(md::ram, ram, sizeof md::ram);
    md::vdp::reset();
    std::vector<uint32_t> writes;
    md::port_log = &writes;
    Regs r;
    std::memcpy(&r, regs, sizeof r);
    exit_pc = 0;
    game::untranslated_throws = true;
    try {
        it->second.run(r);
    } catch (const game::Untranslated& u) {
        md::port_log = nullptr;
        exit_pc = 0x80000000u | u.addr;
        return -2;
    }
    md::port_log = nullptr;
    std::memcpy(regs, &r, sizeof r);
    std::memcpy(ram, md::ram, sizeof md::ram);
    uint32_t n = std::min<uint32_t>(*log_len, uint32_t(writes.size()));
    std::memcpy(log, writes.data(), n * 4);
    *log_len = uint32_t(writes.size());
    return it->second.outputs;
}

int zt_has(uint32_t addr) { return table().count(addr) ? 1 : 0; }

void zt_set_pad(int buttons) { md::pad::set_buttons(0, uint16_t(buttons)); }

uint32_t zt_exit_pc() { return exit_pc; }

}
