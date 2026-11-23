// Status display: the HUD set up at level start (0x29DA), the map screen
// (0x2CEC), lighting modes (0x1D46), the backdrop slice (0xD494) and the face
// animation (0x22BE, 0x23C2).
#include "display.hpp"
#include "game.hpp"

namespace game {

namespace {

void copy_tiles(uint16_t tile, const std::vector<uint8_t>& src, int nlongs) {
    display::upload_tiles(tile, uint16_t(nlongs / 8), src.data());
}

void fill_tile_ff(uint16_t tile) {
    for (int i = 0; i < 64; i++) display::tiles[tile & 0x7FF][i] = 15;
}

// Iterations of `subq #1,d7; bmi skip; loop: ... dbra d7,loop` for d7 = n.
int dbra_count(int16_t n) {
    int16_t d7 = int16_t(n - 1);
    return d7 < 0 ? 0 : d7 + 1;
}

void reset_queues() {
    st.dma_count = 0;
    st.sprite_link = 1;
    st.sprite_end = 0;
}

} // namespace

// 0x29DA: draws the HUD and the status area at the start of a level.
void DrawHud() {
    WaitVBlank();
    fb_dma_pending = 0;
    HideSprites();
    const uint16_t* pal = game_palette(st.nv_slot != 0xFFFF, st.indoors);
    for (int i = 0; i < 64; i++) display::pal[i] = pal[i];
    ClearPlaneA();
    ClearPlaneB();
    LoadImageRes(0x6B20, 0xC000, 0xE000, kHudFrame);
    VdpDelay();
    uint16_t d0 = display::planeB[0x265];
    VdpDelay();
    fill_tile_ff(uint16_t(d0 & 0x7FF));
    DrawHealthBar();
    DrawEnemyCounter(st.enemies_left);
    uint16_t face = st.character;
    if (face > 4) face = 0;
    st.character = face;
    LoadImageRes(0x80A0, 0xC830, 0xE000, kPortraits[face]);
    fill_tile_ff(0x573);
    copy_tiles(0x574, assets::tiles("screens/tiles/hud_AE60.png"), 0x140);
    const assets::Tilemap& panorama = assets::tilemap(st.indoors ? "render/panorama_in.png" : "render/panorama_out.png");
    display::upload_tiles(0x141, panorama.count(), panorama.tiles.data());
    uint16_t tile = 0x8001;
    for (uint16_t col = 0xC288; col < 0xC288 + 64; col += 2) {
        for (uint16_t a = col, i = 0; i < 10; i++, a = uint16_t(a + 0x80)) {
            display::write_cell(a, tile++);
        }
    }
    copy_tiles(0x347, assets::tiles("screens/tiles/hud_68E0.png"), 0x90);
    copy_tiles(0x515, assets::tiles("screens/tiles/hud_A2A0.png", {{4, 2}}), 0x180);
    uint16_t d3 = st.backdrop_col, d4 = d3;
    for (int i = 0; i < 0x2A; i++) {
        d3 &= 0xFF;
        d4 &= 0x7F;
        uint16_t dest = uint16_t(0xE000 + d4);
        const uint16_t* a0 = backdrop_map() + d3 / 2;
        d3 = uint16_t(d3 + 2);
        d4 = uint16_t(d4 + 2);
        for (int j = 0; j < 16; j++, a0 += 0x80) display::write_cell(uint16_t(dest + j * 0x80), *a0);
    }
    uint16_t vs = uint16_t(cur_level * 4 + (int16_t(uint16_t(st.stair_height)) >> 5) - 0x20);
    display::vscrollA = vs;
    display::hscrollA[0] = uint16_t(st.backdrop_scroll + 0x10);
    st.radar_redraw = 1;
    uint16_t mode = st.lighting;
    st.lighting = 0xFFFF;
    SetLighting(mode);
    reset_queues();
}

// 0x2CDC: short delay between VDP accesses.
void VdpDelay() {}

// 0x2CEC: map screen (Start during gameplay); Up/Down change the map scale.
// The link-game packet exchange (0x1C0F6, 0x1C52C) is not supported.
void MapScreen() {
    fb_dma_pending = 0;
    WaitVBlank();
    HideSprites();
    display::pal[63] = 0;
    // The original pauses GEMS here (0xC633A); the port keeps the music playing.
    ClearPlaneA();
    ClearPlaneB();
    LoadImageRes(0x2820, 0xE000, 0x6000, kMapScreen);
    copy_tiles(0x19B, assets::tiles("screens/tiles/mapscreen_extra.png"), 0x90);
    copy_tiles(0x1AD, Font16Tiles(), 0x4B0);
    copy_tiles(0x243, assets::tiles("screens/tiles/mapscreen_cells.png"), 0x120);
    uint16_t t = 0xE267;
    for (uint16_t row = 0xC518, i = 0; i < 16; i++, row = uint16_t(row + 0x80)) {
        for (int j = 0; j < 16; j++) display::write_cell(uint16_t(row + j * 2), t++);
    }
    display::vscrollA = 0;
    display::vscrollB = 0xFFFC;
    for (int i = 0; i < 16; i++) display::pal[32 + i] = assets::palette("palettes/menu_font.png")[i];
    DrawLevelMap();
    reset_queues();
    st.map_level = st.pw_level;
    DrawMapLevelLines(st.map_level);
    st.repeat[0] = st.repeat[1] = 0;
    WaitVBlank();
    auto& rep_up = st.repeat[0];
    auto& rep_down = st.repeat[1];
    auto& prev = st.pad_prev;
    do {
        WaitVBlank();
        RunDmaQueue();
        ReadInput();
        bool step = false;
        if (pad_buttons & 1) {
            if (!(prev & 1)) {
                rep_up = 0x10;
                step = true;
            } else {
                rep_up = uint8_t(rep_up - 1);
                if (rep_up == 1) {
                    rep_up = 4;
                    step = true;
                }
            }
            if (step) {
                if (st.map_level == 0)
                    rep_up = 0;
                else {
                    st.map_level = uint16_t(st.map_level - 1);
                    DrawMapLevelLines(st.map_level);
                    PlaySoundLocal(0x6B);
                }
            }
        } else
            rep_up = 0;
        step = false;
        if (pad_buttons & 2) {
            if (!(prev & 2)) {
                rep_down = 0x10;
                step = true;
            } else {
                rep_down = uint8_t(rep_down - 1);
                if (rep_down == 1) {
                    rep_down = 4;
                    step = true;
                }
            }
            if (step) {
                if (st.map_level == st.pw_level)
                    rep_down = 0;
                else {
                    st.map_level = uint16_t(st.map_level + 1);
                    DrawMapLevelLines(st.map_level);
                    PlaySoundLocal(0x6B);
                }
            }
        } else
            rep_down = 0;
        st.sprite_link = 1;
        st.sprite_end = 0;
        if (uint8_t(st.vblank_count) & 0x10) {
            st.sprites[st.sprite_end++] = {uint16_t((int16_t(uint16_t(player_y)) >> 6) + 0xD1), st.sprite_link, 0xE19D,
                                           uint16_t((int16_t(uint16_t(player_x)) >> 6) + 0xDD)};
            st.sprite_link = uint16_t(st.sprite_link + 1);
        }
        QueueSpriteListDma();
        auto& map_flags = st.map_request;
        if ((map_flags & 1) && (((pad_buttons & 0x80) && !(prev & 0x80)) || action_pressed(16))) {
            map_flags = uint8_t(map_flags & ~1);
        }
    } while (st.map_request);
    DrawHud();
}

// 0x2FA0: draws the level map (16x16 cells of 2x2 tiles, 4x4 pixels of
// mapscreen_extra per collision shape, table 0x3372) and the level name.
void DrawLevelMap() {
    static const uint8_t shape_pixels[8] = {0x00, 0x02, 0x10, 0x12, 0x20, 0x22, 0x30, 0x32};
    const uint8_t* pixels = assets::tiles("screens/tiles/mapscreen_extra.png").data();
    uint16_t tile = 0x4CE0 >> 5;
    int wi = 0;
    Cell a0 = cur_map;
    auto cell = [&](Cell a) { return pixels + shape_pixels[CollisionShape(st.cell_class[map_cell(a)])]; };
    for (int y = 0; y < 16; y++, a0 += 0x20) {
        for (int x = 0; x < 16; x++, a0 += 2) {
            for (int row = 0; row < 0x21; row += 0x20) {
                const uint8_t *a1 = cell(a0 + row), *a2 = cell(a0 + row + 1);
                for (int i = 0; i < 4; i++, a1 += 4, a2 += 4) {
                    display::tile_word(tile, wi, be16(a1));
                    if (++wi == 16) {
                        tile++;
                        wi = 0;
                    }
                    display::tile_word(tile, wi, be16(a2));
                    if (++wi == 16) {
                        tile++;
                        wi = 0;
                    }
                }
            }
        }
    }
    const std::string& name = assets::list("text/levelnames.txt")[cur_area * 16 + cur_level];
    for (int i = 0; i < 16; i++) {
        uint8_t c = uint8_t(name[i]);
        uint16_t t;
        if (c < 0x30)
            t = 0x8000;
        else if (c <= 0x39)
            t = uint16_t(c - 0x30 + 0xE243);
        else
            t = uint16_t(uint8_t(c - 0x41) + 0xE24D);
        display::write_cell(uint16_t(0xCD18 + i * 2), t);
    }
}

// 0x1D46: sets the lighting mode (FF0EB2), or only records it while FF105E >= 0.
// Returns d0.
uint16_t SetLighting(uint16_t mode) {
    if (int16_t(uint16_t(st.nv_slot)) >= 0) {
        st.lighting = mode;
        return NightVisionOn();
    }
    if (int16_t(uint16_t(st.light_slot)) < 0) return ApplyLighting(mode);
    uint16_t d0 = ApplyLighting(mode);
    st.visibility = 0x10;
    return d0;
}

// 0x1D66: switches to lighting mode d0 (0-4, others mean 0): wall shades
// FF0E8A-FF0EAA, backdrop FF0EAE, FF0EB4 and FF1096. Returns d0.
uint16_t ApplyLighting(uint16_t mode) {
    if (mode > 4) mode = 0;
    if (mode == st.lighting) return mode;
    st.lighting = mode;
    st.backdrop_image = uint8_t(mode);
    if (mode == 0) {
        st.walls_shaded = 0;
        st.visibility = 0x10;
        st.sprite_shades = 0;
    } else {
        st.wall_shade = mode == 2 ? 8 : 0;
        st.walls_shaded = 1;
        st.visibility = mode == 3 ? 0xC : 5;
        st.sprite_shades = mode == 2 ? 2 : 1;
    }
    return BuildBackdrop();
}

// 0xD494: rebuilds the backdrop (st.gradient, two 80-byte rows) from the
// rows of FF0EAE, shifted by FF0E1A. Returns d0.
uint16_t BuildBackdrop() {
    uint16_t d0 = st.view_z;
    st.backdrop_z = d0;
    const int16_t d5 = 0x28;
    const uint8_t* a5 = assets::hex("render/backgrounds.txt").data() + st.backdrop_image * 0xA0;
    uint8_t* g = st.gradient;
    if (d0 == 0) {
        for (int i = 0; i < 0xA0; i++) g[i] = a5[i];
        return 0;
    }
    int16_t n = int16_t(d0);
    if (n > 0) {
        int a4 = 0, a2 = 0x50;
        const uint8_t* a3 = a5 + 0x50;
        const uint8_t* saved = a5;
        n = int16_t(n + (n >> 2));
        int16_t d1 = int16_t(d5 - n);
        uint8_t d3 = *a5, d4 = *a3;
        for (int i = dbra_count(n); i > 0; i--) {
            g[a4++] = d3;
            g[a2++] = d4;
        }
        {
            int16_t d2 = int16_t(-d1);
            for (int i = dbra_count(d1); i > 0; i--) {
                g[a4++] = *a5++;
                g[a2++] = *a3++;
                d2 = int16_t(d2 + d5 - d1);
                while (d2 >= 0) {
                    a5++;
                    a3++;
                    d2 = int16_t(d2 - d1);
                }
            }
        }
        a5 = saved + 0x28;
        a3 = a5 + 0x50;
        n = int16_t(n + d5);
        int16_t d2 = int16_t(-n);
        for (int i = 0; i < 0x28; i++) {
            g[a4++] = *a5;
            g[a2++] = *a3;
            d2 = int16_t(d2 + d5);
            while (d2 >= 0) {
                a5++;
                a3++;
                d2 = int16_t(d2 - n);
            }
        }
    } else {
        a5 += 0x50;
        int a4 = 0x50, a2 = 0xA0;
        const uint8_t* a3 = a5 + 0x50;
        const uint8_t* saved = a5;
        n = int16_t(-n);
        n = int16_t(n + (n >> 2));
        int16_t d1 = int16_t(d5 - n);
        uint8_t d3 = a5[-1], d4 = a3[-1];
        for (int i = dbra_count(n); i > 0; i--) {
            g[--a4] = d3;
            g[--a2] = d4;
        }
        {
            int16_t d2 = int16_t(-d1);
            for (int i = dbra_count(d1); i > 0; i--) {
                g[--a4] = *--a5;
                g[--a2] = *--a3;
                d2 = int16_t(d2 + d5 - d1);
                while (d2 >= 0) {
                    a5--;
                    a3--;
                    d2 = int16_t(d2 - d1);
                }
            }
        }
        a5 = saved - 0x29;
        a3 = a5 + 0x50;
        n = int16_t(n + d5);
        int16_t d2 = int16_t(-n);
        for (int i = 0; i < 0x28; i++) {
            g[--a4] = *a5;
            g[--a2] = *a3;
            d2 = int16_t(d2 + d5);
            while (d2 >= 0) {
                a5--;
                a3--;
                d2 = int16_t(d2 - n);
            }
        }
    }
    return uint16_t(n);
}

// 0xD49A: BuildBackdrop when FF0E1A changed. Returns d0.
uint16_t UpdateBackdrop() {
    if (st.view_z == st.backdrop_z) return st.view_z;
    return BuildBackdrop();
}

// 0x22BE: animates one of the three HUD faces per call (blinking/eye frames
// of hud_face_frames, 8 tiles per face, timers st.face).
void AnimateFaces() {
    auto& turn = st.face_turn;
    turn = uint8_t(turn - 1);
    const uint8_t* a1 = assets::tiles("screens/tiles/hud_face_frames.png").data();
    uint16_t d4, d3;
    int k;
    if (int8_t(uint8_t(turn)) < 0) {
        a1 += 0x200, d4 = 0x3040, d3 = 0x3D00, k = 0;
        turn = 2;
    } else if (turn == 0) {
        a1 += 0x100, d4 = 0x2E40, d3 = 0x2E60, k = 1;
    } else {
        d4 = 0x2960, d3 = 0x2980, k = 2;
    }
    FaceTimer& f = st.face[k];
    f.time = uint8_t(f.time - 1);
    if (int8_t(f.time) < 0) {
        f.time = uint8_t(f.time + 8);
        f.frame = uint8_t(f.frame + 0x40);
        f.phase = uint16_t(Random() >> 16) & 0x38;
    }
    a1 += f.frame;
    uint16_t d0 = kFaceAnim[uint8_t(f.phase + f.time) & 0x3F];
    a1 += d0;
    display::tile_long(uint16_t(d4 + d0), be32(a1));
    display::tile_long(uint16_t(d3 + d0), be32(a1 + 0x20));
}

// 0x23C2: runs the face animation scripts (st.face_scripts), writing words
// into st.faces. A record: u16 length, timer, reload, u16 offset of the next
// frame: (u16 faces byte offset, u16 word) pairs ending in a negative offset,
// then the offset of the frame after it.
void RunFaceScripts() {
    st.frame = uint16_t(st.frame + 1);
    uint8_t* s = st.face_scripts;
    auto word = [&](int i) { return uint16_t(s[i] << 8 | s[i + 1]); };
    for (int a0 = 0; uint16_t len = word(a0); a0 += int16_t(len)) {
        if (--s[a0 + 2]) continue;
        s[a0 + 2] = s[a0 + 3];
        int a2 = a0 + int16_t(word(a0 + 4));
        for (int16_t off; (off = int16_t(word(a2))) >= 0; a2 += 4) st.faces[off >> 1] = word(a2 + 2);
        s[a0 + 4] = s[a2 + 2];
        s[a0 + 5] = s[a2 + 3];
    }
}

} // namespace game
