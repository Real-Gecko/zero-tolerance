#include "game.hpp"
#include "display.hpp"
#include "hires.hpp"
#include "savegame.hpp"

#include "../host.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace game {

void untranslated(uint32_t addr) {
    std::fprintf(stderr, "reached untranslated code at 0x%06X (frame %u)\n", addr, host::frame_count);
    std::exit(2);
}

// 0x200: power-on hardware setup, then the region check at 0x2FA. Our console
// reports overseas NTSC, which the header ("JUE") accepts, so the
// "DEVELOPED FOR USE ONLY WITH..." screen at 0x332 is never shown.
static void power_on() {
    display::reset();
    std::memset(&st, 0, sizeof st);
    int_mask = 7;
}

// The VDP's HV counter as the seed loops read it: the hardware's sequence.
uint16_t ReadHvCounter() {
    static uint16_t hv = 0;
    return hv = uint16_t(hv * 0x6255 + 0x3619);
}

// 0x976
static void main_init() {
    ClearRam();
    // 0xA3C: the seed sums 10000 reads of the HV counter.
    uint32_t seed = 0x82647359;
    for (int i = 0; i < 10000; i++) seed += uint32_t(int32_t(int16_t(ReadHvCounter())));
    random_seed = host::opt.seed >= 0 ? uint32_t(host::opt.seed) : seed;
    for (int i = 0; i < 0x40; i++) display::pal[i] = 0;
    display::clear_vram();
    // 0x8F8 checks for a pad in port 1: the host always has one.
    SetGameVdpRegs();
    fb_dma_pending = 0;
    display::hmode = 0; // 0x1BFF0 (no link cable): full screen scroll
    int_mask = 5;
    sound_init();
    PlaySound(3);
    SpinningSegaLogo();
    BootLogo();
}

// 0xD98: title, story, character select and the game, until a full restart.
static void main_loop() {
    for (;;) {
        uint16_t s = last_sound;
        if (s <= 2 || s == 0x88) StopMusic();
        st.indoors = 1;
        cur_area = 0;
        TitleMenu();
        if (!demo_mode) {
            if (int slot = take_load(); slot >= 0) { // title LOAD: snapshot has the character already
                request_load(slot); // forward to GameLevelLoop (take_load consumes)
                if (GameLevelLoop() == 3) continue; // quit to title
                return;
            }
            uint16_t story = 0;
            bool show = true;
            if (st.start_level) {
                story = 1;
                if (st.start_level != 0x10) {
                    story = 2;
                    if (st.start_level != 0x20) show = false;
                }
            }
            if (show) {
                StoryScreen(story, 0x13298);
                SetGameVdpRegs();
            }
            NewGame();
            st.character = 0;
            CharacterSelect(0x13298);
            if (GameLevelLoop() == 3) continue; // Esc menu: quit to title
            return;
        }
        // Attract mode: the demo story screen, then a recorded game.
        uint16_t d7 = StoryScreen(3, 0x13298);
        SetGameVdpRegs();
        if (d7) continue;
        uint16_t area = cur_area;
        NewGame();
        level_id = uint8_t(area << 4);
        uint32_t r = Random();
        st.character = uint16_t((r & 0x400) / 0xCD);
        StopAllSound();
        if (GameLevelLoop() == 1) return;
    }
}

[[noreturn]] void Reset() {
    power_on();
    // 0x690
    if (!st.warm_boot) {
        ClearRam();
        st.warm_boot = 0xFFFF;
        for (int i = 0; i < 9; i++) st.password[i] = 0x5F;
        st.password[9] = 0;
        sound_options = 0x0F;
    }
    host::set_vblank_handler(VBlank);
    for (;;) {
        main_init();
        main_loop();
    }
}

// 0xA5C: returns the new seed (d2).
uint32_t Random() {
    uint32_t d0 = random_seed;
    uint32_t d2 = (d0 & 0xFFFF) * 0xE673u;
    uint32_t d1 = ((d0 >> 16) * 0xE673u) << 16;
    d2 += d1;
    d2 += ((d0 & 0xFFFF) * 0xBB40u) << 16;
    d2 += 1;
    random_seed = d2;
    return d2;
}

// 0xAA2
void VBlank() {
    vblank_count += 1;
    st.speech_timer -= 1;
    if (st.dac_hold) st.dac_hold -= 1;
    if (!fb_dma_pending) return;
    fb_dma_pending -= 1;
    if (fb_dma_pending == 0) {
        // Second half of the 3D view (was DMA'd to VRAM, never shown).
    } else {
        // First half: swap in the recorded high-resolution view.
        hires::show();
        auto& flash = st.hit_flash;
        if (flash) {
            if (st.dying) {
                if (flash > 0xF) flash -= 0x110;
                else {
                    flash -= 1;
                    if (int16_t(uint16_t(flash)) < 0) flash = 0;
                }
            } else {
                flash -= 3;
                if (int16_t(uint16_t(flash)) < 0) flash = 0;
            }
            display::pal[63] = flash;
        }
    }
}

// 0x744
void QueueVramDma(uint8_t src) { st.dma_queue[st.dma_count++] = src; }

// 0x79A: runs the queued DMAs into the display buffers.
void RunDmaQueue() {
    int saved = int_mask;
    int_mask = 7;
    for (int i = 0; i < st.dma_count; i++) {
        uint8_t src = st.dma_queue[i];
        if (src == kTitleHscroll)
            for (int k = 0; k < 0x1C0; k++) (k & 1 ? display::hscrollB : display::hscrollA)[(k >> 1) % display::H] = st.title_hscroll[k];
        else
            display::upload_tiles(0x500, 21, assets::tiles(kHandTiles[src].file, kHandTiles[src].cells).data());
    }
    st.dma_count = 0;
    int_mask = saved;
}

// 0x804: reads the pad in port 1 into pad_buttons (the six-button pad's
// X/Y/Z/Mode, which nothing uses, are left out).
void ReadPad() {
    st.pad_prev = st.pad_buttons;
    pad_buttons = host::pad();
}

// 0xBAE: waits for the 3D view DMA, then writes count+1 words to the palette from src.
void WriteCram(const uint16_t* src, int count) {
    while (fb_dma_pending) host::frame();
    int saved = int_mask;
    int_mask = 7;
    for (int i = 0; i <= count; i++) display::pal[i] = src[i];
    int_mask = saved;
}

// 0xBD2: clears RAM from FF0018 up to the stack (FFEFFC) and FFF000-FFFFFF:
// all the state but the variables before the pad's.
void ClearRam() {
    uint16_t vblank = st.vblank_count, dma = st.fb_dma_pending, warm = st.warm_boot, sound = st.sound_options;
    uint32_t seed = st.random_seed;
    uint8_t password[sizeof st.password];
    std::memcpy(password, st.password, sizeof password);
    std::memset(&st, 0, sizeof st);
    st.vblank_count = vblank;
    st.fb_dma_pending = dma;
    st.warm_boot = warm;
    st.random_seed = seed;
    std::memcpy(st.password, password, sizeof password);
    st.sound_options = sound;
}

// 0xBF2: display mode for the game screens (register table 0x2272; 0x2298
// with a link cable): plane A at 0xE000, plane B at 0xC000, 64x32 cells,
// full screen scroll, no window.
void SetGameVdpRegs() {
    display::ntA = 0xE000;
    display::ntB = 0xC000;
    display::pwA = display::pwB = 64;
    display::phA = display::phB = 32;
    display::hmode = 0;
    display::win_h = display::win_v = 0;
    display::clear_sprites();
    display::hscrollA[0] = display::hscrollB[0] = 0;
    display::vscrollA = display::vscrollB = 0;
}

// 0xC5A
void ClearPlaneB() {
    display::fill_b(0, 0x700);
    display::hscrollA[0] = display::hscrollB[0] = 0;
    display::vscrollA = display::vscrollB = 0;
}

// 0xCAA
void ClearPlaneA() {
    std::memset(display::tiles[0], 0, sizeof display::tiles[0]);
    display::fill_a(0x8000, 0x800);
    display::hscrollA[0] = display::hscrollB[0] = 0;
    display::vscrollA = display::vscrollB = 0;
}

// 0xD36: reads the pad, or replays/records the demo input (demo_mode 1 / 2).
void ReadInput() {
    uint16_t mode = demo_mode;
    if (!mode) {
        ReadPad();
        return;
    }
    auto& left = st.demo_left;
    if (mode != 1) { // records into the demo's ROM bytes, which keep nothing
        left -= 1;
        if (!left) st.level_exit = 1;
        ReadPad();
        st.demo_pos += 1;
        return;
    }
    left -= 1;
    if (!left) st.level_exit = 1;
    ReadPad();
    if (pad_buttons & 0x80) {
        st.level_exit = 1;
        demo_mode = 3;
    }
    pad_buttons = demo_inputs()[st.demo_pos++];
}

// 0xFC0: new game: health, lives and inventory, then the password.
void NewGame() {
    st.area_password[0] = 0;
    level_id = 0;
    st.carry_health = 0x64;
    for (uint8_t& a : st.alive) a = 1;
    for (int i = 0; i < 5; i++) st.carry_weapon[i] = st.carry_ammo[i] = 0;
    if (demo_mode) return;
    DecodePassword(st.password);
    level_id = st.start_level;
}

} // namespace game

namespace game {

// 0x1D18: terminates the sprite list and shows it (the original queues its
// DMA to 0xB800).
void QueueSpriteListDma() {
    uint16_t n = st.sprite_end;
    if (n) set_low_byte(st.sprites[n - 1].link, 0);
    else st.sprites[n++] = {};
    display::upload_sprites(st.sprites, n);
}

} // namespace game
