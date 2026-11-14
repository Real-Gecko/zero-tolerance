// Title screen and its menu (0x1C9CA, 0x1CF36).
#include "display.hpp"
#include "game.hpp"
#include "m68k.hpp"
#include "savegame.hpp"

#include "../host.hpp"

#include <utility>

namespace game {

namespace {

auto& menu_choice = st.menu_choice; // 0 = "press start", 1 = START, 2 = OPTIONS

// 0x1D472: restarts the logo strip scroll.
void ResetLogoScroll() {
    if (!menu_choice) menu_choice = 1;
    st.strip_pos = 0;
    st.strip_tick = 0;
    st.title_scroll = 0;
    st.light_timer[0] = 0;
    st.light_timer[1] = 0;
    st.light_step[0] = 0;
    st.light_step[1] = 0;
    st.light_pal[0] = 0;
    st.light_pal[1] = 0;
    display::fill_a(0, 0xC00); // logo strip area, plane A at 0xE000
}

// Palette for one of the two flashing lights, from its timer.
uint8_t light_palette(uint16_t t) {
    int16_t d0 = int16_t(t);
    if (d0 > 0x40) d0 = 0x40;
    d0 = int16_t(d0 - 0x20);
    if (d0 < 0) d0 = int16_t(-d0);
    if (uint16_t(d0) >= 0x20) d0 = 0x1F;
    return uint8_t(0x1F - d0);
}

// 0x1D4F6: steps the two light timers through their tables.
void StepLightTimers() {
    for (int i = 0; i < 2; i++) {
        uint16_t& t = st.light_timer[i];
        t -= 1;
        if (int16_t(uint16_t(t)) < 0) t = uint16_t(kLightTimes[i][st.light_step[i]++] - 1);
    }
    st.light_pal[0] = light_palette(st.light_timer[0]);
    st.light_pal[1] = light_palette(st.light_timer[1]);
}

// 0x1D7D2: sprite palette (title_sprite_pal) for the light timers.
void LightSpritePalette() {
    for (uint16_t& c : st.title_sprite_pal) c = 0;
    static const int dest[5] = {13, 10, 7, 4, 1};
    for (uint16_t t : st.light_timer) {
        if (t >= 0x40) continue;
        for (int i = 0; i < 5; i++) {
            uint8_t k = kLightLevels[t][i];
            if (!k) continue;
            for (int j = 0; j < 3; j++) st.title_sprite_pal[dest[i] + j] = kLightColours[k - 1][j];
        }
    }
}

// Appends n sprites from a list (y, size, attr, x) with consecutive links.
uint16_t add_sprites(uint16_t a2, const uint16_t (*src)[4], int n, uint16_t& link) {
    for (int i = 0; i < n; i++, src++)
        st.sprites[a2++] = {(*src)[0], uint16_t((*src)[1] | link++), (*src)[2], (*src)[3]};
    return a2;
}

// The LOAD label for the title menu (4x2 sprites at VRAM 0x8800, tile 0x440),
// built from the options font like the START/OPTIONS sprites: the 16 rows of
// each letter shifted up one, colours 0/8/14/9 mapped to 0/12/7/1.
void build_load_tiles() {
    for (int i = 0; i < 4; i++) {
        char c = "LOAD"[i];
        uint8_t rows[16][8];
        for (int half = 0; half < 2; half++) {
            uint16_t w = kFont16[c - 0x20][half];
            uint32_t t = (w & 0x7FF) - 0x4B0;
            for (int r = 0; r < 8; r++)
                for (int k = 0; k < 4; k++) {
                    uint8_t b = Font16Tiles()[t * 32 + r * 4 + k];
                    rows[half * 8 + r][k * 2] = (b >> 4) & 15;
                    rows[half * 8 + r][k * 2 + 1] = b & 15;
                }
        }
        for (int half = 0; half < 2; half++) {
            uint16_t tile = uint16_t(0x440 + i * 2 + half);
            int wi = 0;
            for (int r = 0; r < 8; r++) {
                for (int w = 0; w < 2; w++) {
                    uint16_t v = 0;
                    for (int k = 0; k < 4; k++) {
                        uint8_t p = (r + half * 8 + 1 < 16) ? rows[r + half * 8 + 1][w * 4 + k] : 0;
                        v = uint16_t(v << 4 | (p == 8 ? 12 : p == 14 ? 7 : p == 9 ? 1 : 0));
                    }
                    display::tile_word(tile, wi++, v);
                }
            }
        }
    }
}

// The logo strip map from row 1 at byte column d2 (0x119B8E).
const uint16_t* strip_map(uint16_t d2) { return assets::tilemap(kTitleStrip).map.data() + 0x80 + d2 / 2; }

// Wide windows: writes now the logo strip columns the loop below would write
// over the next frames, so the strip enters at the window's right edge
// instead of the original screen's.
void write_strip_ahead(int extra) {
    uint16_t d2 = st.strip_pos;
    for (int col = 0; col < extra / 8 + 2; col++, d2 = uint16_t(d2 + 2)) {
        uint16_t d3 = uint16_t(((d2 + 0x54) & 0xFF) + 0xE000);
        const uint16_t* a0 = strip_map(d2);
        for (int row = 0; row < 12; row++, d3 = uint16_t(d3 + 0x100), a0 += 0x80) {
            uint16_t v = d2 < 0x100 ? uint16_t(*a0 + 1) : 0;
            display::write_cell(d3, v);
        }
    }
}

} // namespace

// 0x1CF36: returns with menu_choice set, or with demo_mode = 1 when the
// attract timer runs out.
void TitleScreen() {
    WaitVBlank();
    display::clear_vram();
    display::hscrollA[0] = display::hscrollB[0] = 0;
    display::vscrollA = display::vscrollB = 0;
    display::clear_sprites();
    // Title registers (0x1DD14): plane A at 0xE000, plane B at 0xC000,
    // 128x32 cells, per-line horizontal scroll.
    display::ntA = 0xE000;
    display::ntB = 0xC000;
    display::pwA = display::pwB = 128;
    display::phA = display::phB = 32;
    display::hmode = 3;
    display::win_h = display::win_v = 0;
    for (int i = 0; i < 16; i++) display::pal[i] = assets::palette("palettes/title.png")[i];
    for (int i = 16; i < 40; i++) display::pal[i] = 0;
    display::fill_a(0, 0x800);
    display::fill_b(0, 0x800);
    st.dma_count = 0;
    for (int i = 0; i < 64; i++) display::tiles[0][i] = 0;
    uint16_t tile = 1;
    for (const char* image : {kTitleStrip, kTitleFloor}) {
        const assets::Tilemap& t = assets::tilemap(image);
        display::upload_tiles(tile, t.count(), t.tiles.data());
        tile = uint16_t(tile + t.count());
    }
    for (int i = 0; i < 0x780; i++) {
        uint16_t w = assets::tilemap(kTitleFloor).map[size_t(i)];
        display::write_cell(uint16_t(0xED00 + i * 2), uint16_t(w + 0x4090));
        display::write_cell(uint16_t(0xCD00 + i * 2), uint16_t(w + 0x6090));
    }
    // The two figures (kTitleSprites) and the menu sprites.
    const uint8_t* sprites = assets::tiles("screens/tiles/title_sprites.png",
                                           {{4, 4}, {4, 3}, {4, 3}, {4, 3}, {3, 4}, {3, 3}, {3, 3}, {3, 3},
                                            {4, 4}, {4, 3}, {4, 3}, {4, 3}, {3, 4}, {3, 3}, {3, 3}, {3, 3},
                                            {2, 1}, {3, 2}, {2, 2}, {4, 2}, {3, 2}})
                                 .data();
    display::upload_tiles(0x355, 182, sprites);
    display::upload_tiles(0x40B, 26, sprites + 182 * 32);
    build_load_tiles();
    auto& strip_pos = st.strip_pos;
    auto& strip_tick = st.strip_tick;
    auto& scroll = st.title_scroll;
    strip_pos = 0;
    strip_tick = 0;
    scroll = 0;
    st.light_timer[0] = 0;
    st.light_timer[1] = 0;
    st.light_step[0] = 0;
    st.light_step[1] = 0;
    st.light_pal[0] = 0;
    st.light_pal[1] = 0;
    WaitVBlank();
    menu_choice = 0;
    auto& attract = st.attract_timer;
    attract = 0x564;
    demo_mode = 0;
    host::wide_screen = write_strip_ahead;

    for (;;) {
        WaitVBlank();
        RunDmaQueue();
        const uint16_t* lights = assets::palette("palettes/title_lights.png");
        const uint16_t* pals[3] = {st.title_sprite_pal, lights + st.light_pal[0] * 16, lights + st.light_pal[1] * 16};
        int pi = 16;
        for (const uint16_t* src : pals)
            for (int i = 0; i < 16; i++, pi++) display::pal[pi] = src[i];
        ReadPad();
        uint16_t link = 1;
        st.sprite_link = 1;
        st.sprite_end = 0;
        StepLightTimers();
        LightSpritePalette();

        // The logo strip scrolls in one column every 4 frames.
        strip_tick += 1;
        if (strip_tick == 4) {
            strip_tick = 0;
            uint16_t d2 = strip_pos;
            uint16_t d3 = uint16_t(((d2 + 0x54) & 0xFF) + 0xE000);
            strip_pos += 2;
            const uint16_t* a0 = strip_map(d2);
            for (int row = 0; row < 12; row++, d3 = uint16_t(d3 + 0x100), a0 += 0x80)
                display::write_cell(d3, d2 < 0x100 ? uint16_t(*a0 + 1) : 0);
        }

        // Line scroll: the top lines scroll left, the floor gets its perspective.
        scroll -= 2;
        uint16_t* h = st.title_hscroll;
        for (int i = 0; i < 0xD0; i++) *h++ = scroll;
        auto clamp = [](uint16_t t) { return int32_t(int16_t(uint16_t((t >= 0x40 ? 0x3F : t) - 0x20))); };
        int32_t d0 = clamp(st.light_timer[0]), d4 = clamp(st.light_timer[1]);
        int32_t d2 = int32_t(0xFFFFE9E0), d3 = d2;
        for (int i = 0; i < 0x78; i++) {
            d2 -= d0;
            d3 -= d4;
            *h++ = uint16_t(lo(uint32_t(d2 >> 4)) & ~1);
            *h++ = uint16_t(lo(uint32_t(d3 >> 4)) | 1);
        }

        uint16_t a2 = add_sprites(0, kTitleSprites, 16, link);
        if (menu_choice) {
            st.sprites[a2++] = {uint16_t((menu_choice - 1) * 24 + 0x112), uint16_t(0x400 | link++), 0x840B, 0xFC};
            a2 = add_sprites(a2, kTitleMenuSprites, 2, link); // START
            // LOAD (custom 4x2 sprite, tiles built above at 0x8800).
            st.sprites[a2++] = {0x128, uint16_t(0xD00 | link++), 0x8440, 0x110};
            uint16_t opt = a2;
            a2 = add_sprites(a2, kTitleMenuSprites + 2, 2, link); // OPTIONS, shifted down
            for (uint16_t s = opt; s < a2; s++) st.sprites[s].y = uint16_t(st.sprites[s].y + 24);
        }
        st.sprite_end = a2;
        st.sprite_link = link;
        QueueVramDma(kTitleHscroll);
        QueueSpriteListDma();

        attract -= 1;
        if (!attract) {
            menu_choice = 1;
            uint32_t r = Random();
            r = (r >> 16 | r << 16) & 0xFF;
            cur_area = lo(divu(r, 0x56));
            if (!cur_area) st.indoors = 1;
            demo_mode = 1;
            break;
        }
        if (scroll == 0xFAA0) ResetLogoScroll();
        uint8_t pressed = pad_buttons & 0xF0;
        if (pressed && (st.pad_prev & pressed) != pressed) {
            if (!menu_choice) {
                menu_choice = 1;
                continue;
            }
            if (menu_choice == 2) { // LOAD: pick a slot over the title
                if (!any_save()) continue;
                int slot = choose_load_slot();
                if (slot < 0) continue;
                request_load(slot);
            }
            break;
        }
        auto newly = [](int bit) { return (pad_buttons >> bit & 1) && !(st.pad_prev >> bit & 1); };
        if (newly(0) && menu_choice) menu_choice = menu_choice <= 1 ? 1 : menu_choice - 1;
        if (newly(1) && menu_choice) menu_choice = menu_choice >= 3 ? 3 : menu_choice + 1;
    }
    host::wide_screen = nullptr;

    WaitVBlank();
    SetGameVdpRegs();
    for (int i = 0; i < 32; i++) display::pal[i] = 0;
    display::hscrollA[0] = display::hscrollB[0] = 0;
    display::vscrollA = display::vscrollB = 0;
    display::clear_sprites();
    WaitVBlank();
}

// 0x1C9CA: title, options, then the password check of a new game.
void TitleMenu() {
    for (;;) {
        if (last_sound != 3) PlaySound(3);
        TitleScreen();
        if (menu_choice == 3) {
            OptionsScreen();
            continue;
        }
        if (demo_mode) {
            st.start_level = 0;
            return;
        }
        if (menu_choice == 2) { // LOAD: resume the chosen save in GameLevelLoop
            StopMusic();
            return;
        }
        st.start_level = 0;
        if (DecodePassword(st.password) != 0xFFFF) st.start_level = st.level_id;
        StopMusic();
        return; // the two-console link game (0x1CA32) is not supported
    }
}

} // namespace game
