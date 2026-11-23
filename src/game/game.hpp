// Zero Tolerance game code translated from the 68000 ROM. Functions are named
// by what they do; the ROM address is in a comment at each definition.
// Register arguments become parameters.
#pragma once

#include "../assets.hpp"
#include "../host.hpp"
#include "state.hpp"
#include "tables.hpp"

#include <map>
#include <vector>

namespace game {

using assets::be16;
using assets::be32;
using host::int_mask;

// Short names for the most used state variables.
inline auto& vblank_count = st.vblank_count;
inline auto& fb_dma_pending = st.fb_dma_pending;
inline auto& random_seed = st.random_seed;
inline auto& sound_options = st.sound_options;
inline auto& pad_buttons = st.pad_buttons;
// PC: host::action_keys() at the last pad read and the read before.
inline uint16_t actions = 0, actions_prev = 0;
inline bool action_pressed(uint16_t bit) { return (actions & bit) && !(actions_prev & bit); }
// PC: set while gameplay runs (not the attract demo); the gamepad's Start then
// opens and closes the pause menu instead of pressing the pad's Start.
inline bool start_is_menu = false;
inline auto& demo_mode = st.demo_mode;
inline auto& player_x = st.player_x;
inline auto& player_y = st.player_y;
inline auto& player_angle = st.player_angle;
inline auto& cur_map = st.cur_map;
inline auto& cur_level = st.cur_level;
inline auto& last_sound = st.last_sound;
inline auto& cur_area = st.cur_area;
inline auto& level_id = st.level_id;
// The low byte of a word (move.b to the odd address).
inline void set_low_byte(uint16_t& w, uint8_t b) { w = uint16_t((w & 0xFF00) | b); }
inline uint8_t& low_byte(uint16_t& w) { return reinterpret_cast<uint8_t*>(&w)[0]; }

// 64 black colours.
inline constexpr uint16_t kBlack[64] = {};
// The gameplay palette for night vision (goggles) on or off.
inline const uint16_t* game_palette(bool goggles, bool indoors) {
    return assets::palette(goggles ? (indoors ? "palettes/game_indoor_goggles.png" : "palettes/game_goggles.png")
                                   : (indoors ? "palettes/game_indoor.png" : "palettes/game.png"));
}
// Map cells. Like the original's pointers, cells past level 15 read on into
// face_scripts (the area-0 level-15 lift up leads there), and writes outside
// the 16 maps go to off_map_cell.
constexpr Cell kOffMap = 0x5000; // off_map_cell
constexpr Cell kNoCell = INT32_MIN;
inline Cell level_map(uint16_t level) { return int16_t(uint16_t(level << 10)); }
inline Cell on_map(Cell c) { return c >= 0 && c < 0x4000 ? c : kOffMap; }
inline uint8_t map_cell(Cell c) {
    if (c >= 0 && c < 0x4000) return st.maps[c >> 10][c & 0x3FF];
    if (c >= 0x4000 && c < kOffMap) return st.face_scripts[c - 0x4000];
    if (c == kOffMap) return st.off_map_cell;
    // Before the maps lie level_light and cell_class; the RAM after them is
    // never written, and nothing reachable reads further.
    if (c >= -0x10 && c < 0) return st.level_light[c + 0x10];
    if (c >= -0x110 && c < -0x10) return st.cell_class[c + 0x110];
    return 0;
}
inline void put_cell(Cell c, uint8_t v) {
    c = on_map(c);
    (c == kOffMap ? st.off_map_cell : st.maps[c >> 10][c & 0x3FF]) = v;
}

// The object list (st.object_list, linked by next; links are slot + 1).
inline Enemy* object_at(uint8_t link) { return link ? &st.objects[link - 1] : nullptr; }
inline uint8_t link_of(const Enemy* e) { return e ? uint8_t(e - st.objects + 1) : 0; }
inline uint8_t slot_of(const Enemy* e) { return uint8_t(e - st.objects); }
inline Enemy* first_object() { return object_at(st.object_list); }
inline Enemy* next_object(const Enemy* e) { return object_at(e->next); }
// Target address 0 read as an object: the ROM vectors (x 0, y 0xA88, level 0x10).
inline Enemy rom_object = [] {
    Enemy e{};
    e.y = 0xA88;
    e.level = 0x10;
    return e;
}();
// The target (+6) as an object; nullptr for kNoTarget.
inline Enemy* target_of(const Enemy* e) {
    if (e->target < 64) return &st.objects[e->target];
    return e->target == kPlayerTarget ? &st.player_obj : e->target == kPlayerPos ? &rom_object : nullptr;
}
inline void set_target(Enemy* e, const Enemy* t) {
    e->target = !t ? kPlayerPos : t == &st.player_obj ? kPlayerTarget : uint8_t(slot_of(t));
}
inline Enemy* const player_struct = &st.player_obj; // FF119C
[[noreturn]] void untranslated(uint32_t addr);
extern bool sound_muted; // PC: the in-between views (level.cpp) play no sounds

// boot.cpp
[[noreturn]] void Reset();
void VBlank();
uint32_t Random();
uint16_t ReadHvCounter();
// 0x744: queues an upload for the next VBlank: in-hand weapon tiles
// (kHandTiles index) or the title's line scroll.
void QueueVramDma(uint8_t src);
inline constexpr uint8_t kTitleHscroll = 0xFF;
// The in-hand weapon tiles (screens/tiles) and their sprite sizes, uploaded
// to tile 0x500.
struct HandTiles {
    const char* file;
    std::vector<assets::Cell> cells;
};
extern const HandTiles kHandTiles[10];
// The tiles of the 8x16 font (kFont16), top and bottom per character.
inline const std::vector<uint8_t>& Font16Tiles() { return assets::tiles("screens/tiles/font8x16.png", {{1, 2}}); }
void RunDmaQueue();
void ReadPad();
void WriteCram(const uint16_t* src, int count);
void ClearRam();
void SetGameVdpRegs();
void ClearPlaneB();
void ClearPlaneA();
void ReadInput();
void NewGame();
void QueueSpriteListDma();
void PlaySoundLocal(uint16_t id);
void DrawMapLevelLines(uint16_t level);
uint16_t GameLevelLoop(); // d7: 1 = restart from the boot screens

// sega_logo_spin.cpp, wall.cpp
void WaitVBlank();
void DrawWallSpan();

// intro.cpp
// Image resources (assets::tilemap).
inline constexpr const char* kBootLogo = "image/boot_logo.png";
inline constexpr const char* kTitleStrip = "image/title.png";
inline constexpr const char* kTitleFloor = "image/title_floor.png";
inline constexpr const char* kHudFrame = "image/hud_frame.png";
inline constexpr const char* kMapScreen = "hud/mapscreen.png";
inline constexpr const char* kPortraits[5] = {
    "portraits/face0.png", "portraits/face1.png", "portraits/face2.png", "portraits/face3.png", "portraits/face4.png",
};
uint16_t LoadImageRes(uint16_t vram, uint16_t nt, uint16_t attr, const char* image);
uint16_t LoadTiles(uint16_t vram, const char* image);
void LoadImageMap(uint16_t vram, uint16_t nt, uint16_t attr, const char* image);
void StepPaletteFade(const uint16_t* target, int count);
void BootLogo();
void SegaLogoAndIntro();
void SetIntroVdpRegs();
void WaitFrames(uint16_t n);
void ClearCram();
void ClearVram();
void LoadPaletteLine(int line, const uint16_t* src);
void LoadTilesAt(uint16_t tile, uint16_t n, const uint8_t* src);
void SetVScrollA(uint16_t v);
void SetVScrollB(uint16_t v);
void LoadMap(const uint16_t* src, uint16_t dest, uint16_t w, uint16_t h, uint16_t pitch, uint16_t attr);
void FadeIn(const uint16_t* src, uint16_t delay, int count = 64);
void FadeOut(const uint16_t* src, uint16_t delay, int count = 64);
void FadeFromWhite(const uint16_t* src, uint16_t delay, int count = 64);
void FadeToWhite(const uint16_t* src, uint16_t delay, int count = 64);
void FillPlaneA(uint16_t v);

// title.cpp
void TitleScreen();

// sound.cpp
void sound_init();
void PlaySound(uint16_t id);
void GemsPlaySound(uint16_t id);
void StopMusic();
void StopAllSound();


// player.cpp
uint8_t CollisionShape(uint16_t d3);

void SpinningSegaLogo();

// level.cpp
uint32_t gameplay_frame();
void CarryInventory();
void RestoreCarriedInventory();
void GiveStartEquipment();
extern int drawn_count;
void GiveStartEquipment();
void HideSprites();
void FadeInGamePalette();
void FadeOutGamePalette();

// inventory.cpp
void DrawAmmoDigits(uint16_t n, uint16_t dest);
void DrawInventorySlot(const WeaponSlot& slot, uint16_t dest);
void ClearInventory();
void SetFireAnim();
void NextWeapon();
uint16_t NightVisionOn();
uint16_t NightVisionOff();
uint16_t AddWeapon(uint16_t weapon);
void UseItemCharge();
void UseUpItem();
uint16_t NightVisionCharge(); // return d0
uint16_t LightCharge();
void ScannerCharge();
void ArmourCharge();
void ShieldCharge();
void CountEnemiesLeft();
void StoreLevelPassword();
void StoreAreaPassword();
void EncodePassword(uint8_t* out);

// hud.cpp
void DrawEnemyCounter(uint16_t n);
void QueueMessage(Msg msg);
void queue_saved_message(); // PC: "GAME SAVED" in the message box
void ClearMessages();
void PlaySpeech();
void UpdateMessages();
void DrawHealthBar();

// still to translate
void ChangeLevel(uint16_t level);
void RenderFromBehind();
void TitleMenu();
uint16_t StoryScreen(uint16_t story, uint32_t a1);
void CharacterSelect(uint32_t a1);
uint16_t DecodePassword(const uint8_t* password); // d7: 0xFFFF = invalid
void OptionsScreen();

// player.cpp (movement)
void FloorCellUnderPlayer();
void PlayerControls();
void HeadBob();
void ApplyLevelCellVariants(uint16_t level);
uint16_t DoorOpening(Cell a0);
void UpdateDoors();
void ResetDoors();
void PickupItem(Cell a0, uint8_t d3);
void CellUseHandler(CellUse use, Cell a0, uint16_t d3);
void HitPlayerIgnoreArmour(uint16_t d0, uint16_t d3, uint16_t d4);
void HitPlayer(uint16_t d0, uint16_t d3, uint16_t d4); // player hit
void HealthWarnings();

// status.cpp
void DrawHud();
void VdpDelay();
void MapScreen();
void DrawLevelMap();
uint16_t SetLighting(uint16_t mode); // returns d0
uint16_t ApplyLighting(uint16_t mode);
uint16_t BuildBackdrop();
// The attract demo of the area (tables/demo_N.txt): the random seed, then
// the pad bits of each frame.
inline const std::vector<uint8_t>& demo_inputs() {
    static const char* files[3] = {"tables/demo_0.txt", "tables/demo_1.txt", "tables/demo_2.txt"};
    return assets::hex(files[st.cur_area == 0 ? 0 : st.cur_area == 1 ? 1 : 2]);
}
// The backdrop map (st.backdrop_in): 128x16 cells, tiles at 0x141.
inline const uint16_t* backdrop_map() {
    static std::vector<uint16_t> maps[2];
    std::vector<uint16_t>& m = maps[st.backdrop_in ? 1 : 0];
    if (m.empty())
        for (uint16_t e : assets::tilemap(st.backdrop_in ? "render/panorama_in.png" : "render/panorama_out.png").map)
            m.push_back(uint16_t(e + 0x141));
    return m.data();
}
uint16_t UpdateBackdrop();
void AnimateFaces();
void RunFaceScripts();

// radar.cpp
void DrawRadar();
void UsePickupCell(Cell a0);
void UseCell(Cell a0);
uint16_t RadarPlayerDots(uint16_t a2, uint16_t d5);
uint16_t RadarEnemyDots(uint16_t a2, uint16_t d5);

// enemy.cpp
void UpdateItemCharges();
void RunEnemies();
void ClearEnemies();
void RemoveEnemy(Enemy* a0);
void DropItemNearPlayer();
void KillAllEnemies();

// weapon.cpp
void UndoHeadBob();
void StatusSprites();

// sky.cpp
void StartFlash();
uint16_t SkySprites(uint16_t a2);
// PC: the sprite list end and link where 0xECE2 put the sky sprites, so the
// in-between views (level.cpp) can redo them.
extern uint16_t sky_sprites;
extern uint16_t sky_link;
void StartBolt(uint16_t d0);

} // namespace game
