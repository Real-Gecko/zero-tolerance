// Boot screens: logo 2, the SEGA logo and the intro animation (0x1CDEE,
// 0x1EF50, 0x52C7E), and the display helpers they share.
#include "game.hpp"
#include "display.hpp"

#include "../host.hpp"

namespace game {

// 0x1F72: image resource with tiles at tile index vram >> 5 and the map at
// nametable address nt (64 cells per row); attr is added to every map entry.
// Returns the VRAM address after the tiles.
uint16_t LoadImageRes(uint16_t vram, uint16_t nt, uint16_t attr, const char* image) {
    LoadImageMap(vram, nt, attr, image);
    return LoadTiles(vram, image);
}

// 0x1FC0
uint16_t LoadTiles(uint16_t vram, const char* image) {
    const assets::Tilemap& t = assets::tilemap(image);
    uint16_t n = t.count();
    display::upload_tiles(uint16_t(vram >> 5), n, t.tiles.data());
    return uint16_t(vram + (n << 5));
}

// 0x2022: only the map of an image resource.
void LoadImageMap(uint16_t vram, uint16_t nt, uint16_t attr, const char* image) {
    const assets::Tilemap& t = assets::tilemap(image);
    uint16_t base = uint16_t((vram >> 5) + attr);
    display::blit_linear(nt, t.map.data(), t.w, t.h, 64, base);
}

// 0x1CC04: steps one colour component of the count colours of fade_pal by 2
// towards the palette target, every other call. fade_idle counts the
// calls since the last change, up to 20.
void StepPaletteFade(const uint16_t* target, int count) {
    bool changed = false;
    auto& idle = st.fade_idle;
    uint8_t toggle = st.fade_toggle;
    st.fade_toggle = toggle ^ 2;
    if (!(toggle & 2) && idle != 0x14) {
        uint8_t comp = uint8_t(st.fade_comp + 1);
        if (comp == 3) comp = 0;
        st.fade_comp = comp;
        // Component 0 = bits 1-3, 1 = bits 5-7, 2 = bits 9-11; the other two are kept.
        static const int shift[3] = {0, 4, 8};
        static const uint16_t keep[3] = {0xEE0, 0xE0E, 0x0EE};
        int k = comp == 1 ? 1 : comp == 2 ? 2 : 0;
        int s = shift[k];
        for (int i = 0; i < count; i++) {
            uint16_t c = st.fade_pal[i];
            int16_t t = int16_t((target[i] >> s) & 0xE), v = int16_t((c >> s) & 0xE);
            if (v != t) {
                changed = true;
                v = int16_t(v > t ? v - 2 : v + 2);
            }
            st.fade_pal[i] = uint16_t((c & keep[k]) | (uint16_t(v) << s));
        }
    }
    if (changed) idle = 0;
    else if (idle != 0x14) idle += 1;
}

// 0x1CDEE: second boot logo, fading in and out, then the SEGA logo and intro.
void BootLogo() {
    WaitVBlank();
    display::clear_sprites();
    display::hscrollA[0] = display::hscrollB[0] = 0;
    display::vscrollA = display::vscrollB = 0;
    SetGameVdpRegs();
    for (int i = 0; i < 0x40; i++) display::pal[i] = 0;
    display::fill_b(0, 0x1000);
    display::fill_a(0, 0x1000);
    LoadImageRes(0, 0xE000, 0x8000, kBootLogo);
    for (int i = 0; i < 16; i++) st.fade_pal[i] = 0;
    auto fade = [](const uint16_t* palette, uint16_t frames) {
        if (host::any_pressed()) return false;
        st.fade_comp = 0;
        st.fade_idle = 0;
        auto& left = st.fade_frames;
        left = frames;
        do {
            WaitVBlank();
            if (!st.fade_idle)
                for (int i = 0; i < 16; i++) display::pal[i] = st.fade_pal[i];
            StepPaletteFade(palette, 16);
            left -= 1;
            if (host::any_pressed()) return false;
        } while (left);
        return true;
    };
    if (fade(assets::palette("palettes/boot_logo.png"), 0xB4)) fade(kBlack, 0x3C);
    SegaLogoAndIntro();
}

// 0x1F578: display mode for the intro screens (64x64 planes).
void SetIntroVdpRegs() {
    display::ntA = 0xC000;
    display::ntB = 0xE000;
    display::pwA = display::pwB = 64;
    display::phA = display::phB = 64;
    display::hmode = 0;
    display::win_h = display::win_v = 0;
}

// 0x1F5FE: waits n frames.
void WaitFrames(uint16_t n) {
    uint16_t target = uint16_t(vblank_count + n);
    while (int16_t(target) > int16_t(uint16_t(vblank_count))) host::frame();
}

// 0x1F610
void ClearCram() {
    for (int i = 0; i < 0x40; i++) display::pal[i] = 0;
}

// 0x1F62C
void ClearVram() { display::clear_vram(); }

// 0x1F648: 16 colours from src to palette line.
void LoadPaletteLine(int line, const uint16_t* src) {
    for (int i = 0; i < 16; i++) display::pal[line * 16 + i] = src[i];
}

// 0x1F66C: n tiles from src to tile index tile.
void LoadTilesAt(uint16_t tile, uint16_t n, const uint8_t* src) { display::upload_tiles(tile, n, src); }

// 0x1F698 / 0x1F6AA: vertical scroll of plane A / B.
void SetVScrollA(uint16_t v) { display::vscrollA = v; }
void SetVScrollB(uint16_t v) { display::vscrollB = v; }

// 0x1F70A: w x h map from src to nametable dest, pitch cells per row.
void LoadMap(const uint16_t* src, uint16_t dest, uint16_t w, uint16_t h, uint16_t pitch, uint16_t attr) {
    display::blit_linear(dest, src, w, h, pitch, attr);
}

namespace {

// Loads the palette lines of the count colours at buf.
void load_palette_lines(const uint16_t* buf, int count) {
    int left = count - 1;
    for (int line = 0;; line++) {
        LoadPaletteLine(line, buf + line * 16);
        left -= 0x10;
        if (left < 0) break;
    }
}

// The fades to and from white (0x1F9F6, 0x1FAC8) worked in the area maps'
// RAM at FF86F6, which a new game reloads.
uint16_t white_fade[64];

} // namespace

// 0x1F870: fades fade_pal from black up to the palette at src (count
// colours), one step every delay frames. Like the original it returns before
// showing the final step.
void FadeIn(const uint16_t* src, uint16_t delay, int count) {
    LoadPaletteLine(0, kBlack);
    for (uint16_t& c : st.fade_pal) c = 0;
    for (;;) {
        WaitFrames(delay);
        bool done = true;
        for (int i = 0; i < count; i++) {
            uint16_t& c = st.fade_pal[i];
            uint16_t t = src[i];
            for (uint16_t m : {0xE00, 0x0E0, 0x00E})
                if ((t & m) != (c & m)) c = uint16_t(c + (m & -m));
            if (c != t) done = false;
        }
        if (done) return;
        load_palette_lines(st.fade_pal, count);
    }
}

// 0x1F93A: shows the palette at src, then fades it to black.
void FadeOut(const uint16_t* src, uint16_t delay, int count) {
    LoadPaletteLine(0, src);
    for (int i = 0; i < 64; i++) st.fade_pal[i] = src[i];
    for (;;) {
        WaitFrames(delay);
        bool done = true;
        for (int i = 0; i < count; i++) {
            uint16_t& c = st.fade_pal[i];
            for (uint16_t m : {0xE00, 0x0E0, 0x00E})
                if (c & m) c = uint16_t(c - (m & -m));
            if (c) done = false;
        }
        if (done) {
            ClearCram();
            return;
        }
        load_palette_lines(st.fade_pal, count);
    }
}

// 0x1F9F6: fades from white to the palette at src.
void FadeFromWhite(const uint16_t* src, uint16_t delay, int count) {
    LoadPaletteLine(0, assets::palette("palettes/intro.png"));
    for (uint16_t& c : white_fade) c = 0x0EEE;
    for (;;) {
        WaitFrames(delay);
        bool done = true;
        for (int i = 0; i < count; i++) {
            uint16_t& c = white_fade[i];
            uint16_t t = src[i];
            for (uint16_t m : {0xE00, 0x0E0, 0x00E})
                if ((t & m) != (c & m)) c = uint16_t(c - (m & -m));
            if (c != t) done = false;
        }
        if (done) return;
        load_palette_lines(white_fade, count);
    }
}

// 0x1FAC8: fades the palette at src to white.
void FadeToWhite(const uint16_t* src, uint16_t delay, int count) {
    LoadPaletteLine(0, src);
    for (int i = 0; i < 64; i++) white_fade[i] = src[i];
    for (;;) {
        WaitFrames(delay);
        bool done = true;
        for (int i = 0; i < count; i++) {
            uint16_t& c = white_fade[i];
            for (uint16_t m : {0xE00, 0x0E0, 0x00E})
                if ((c & m) != m) c = uint16_t(c + (m & -m));
            if (c != 0xEEE) done = false;
        }
        if (done) return;
        load_palette_lines(white_fade, count);
    }
}

// 0x1F75E: fills the plane A nametable with v.
void FillPlaneA(uint16_t v) { display::fill_a(v, 0x1000); }

namespace {

// 0x1EF76
void ResetVdp() {
    ClearCram();
    ClearVram();
    SetIntroVdpRegs();
}

// One frame of the VDP status VBlank flag going high (0x52D66).
void wait_vblank_flag() { host::frame(); }

// 0x52C7E: SEGA logo, 15 animation frames of 5 VBlanks.
void sega_logo(uint16_t vscroll) {
    display::vscrollA = display::vscrollB = vscroll;
    display::clear_vram();
    const uint16_t* pal = assets::palette("palettes/sega_logo.png");
    for (int i = 0; i < 16; i++) display::pal[i] = pal[i];
    display::ntA = 0x8000;
    display::ntB = 0xA000;
    display::pwA = display::pwB = 64;
    display::phA = display::phB = 64;
    display::hmode = 0;
    display::win_h = display::win_v = 0;
    // The 15 frames are stacked in the image, 32x4 cells each.
    const assets::Tilemap& logo = assets::tilemap("intro/sega_logo.png");
    display::upload_tiles(0, logo.count(), logo.tiles.data());
    wait_vblank_flag();
    for (uint32_t frame = 0; frame < 15; frame++) {
        LoadMap(logo.map.data() + frame * 32 * 4, 0x8508, 32, 4, 64, 0);
        for (int i = 0; i < 5; i++) wait_vblank_flag();
        if (host::any_pressed()) return;
    }
    for (int i = 0; i < 61 && !host::any_pressed(); i++) wait_vblank_flag();
}

enum Op { Wait, Frame };
struct Step {
    Op op;
    uint16_t a;       // frames to wait, or the first tile of the frame
    const char* file; // the frame's image
};

// 0x1F016-0x1F576: the intro frames, double-buffered at tiles 1 and 0x2BC.
const Step intro_steps[] = {
    {Wait, 15, nullptr},
    {Wait, 6, nullptr},
    {Wait, 6, nullptr},
    {Frame, 700, "intro/frame_01.png"},
    {Wait, 6, nullptr},
    {Frame, 1, "intro/frame_02.png"},
    {Wait, 6, nullptr},
    {Frame, 700, "intro/frame_03.png"},
    {Wait, 6, nullptr},
    {Frame, 1, "intro/frame_04.png"},
    {Wait, 6, nullptr},
    {Frame, 700, "intro/frame_05.png"},
    {Wait, 6, nullptr},
    {Frame, 1, "intro/frame_06.png"},
    {Wait, 6, nullptr},
    {Frame, 700, "intro/frame_07.png"},
    {Wait, 6, nullptr},
    {Frame, 1, "intro/frame_08.png"},
    {Wait, 6, nullptr},
    {Frame, 700, "intro/frame_09.png"},
    {Wait, 6, nullptr},
    {Frame, 1, "intro/frame_10.png"},
    {Wait, 6, nullptr},
    {Frame, 700, "intro/frame_11.png"},
    {Wait, 6, nullptr},
    {Frame, 1, "intro/frame_12.png"},
    {Wait, 6, nullptr},
    {Frame, 700, "intro/frame_13.png"},
    {Wait, 6, nullptr},
    {Frame, 1, "intro/frame_14.png"},
    {Wait, 6, nullptr},
    {Wait, 6, nullptr},
    {Wait, 6, nullptr},
    {Wait, 6, nullptr},
    {Wait, 6, nullptr},
    {Wait, 6, nullptr},
    {Frame, 700, "intro/frame_15.png"},
    {Wait, 6, nullptr},
    {Frame, 1, "intro/frame_16.png"},
    {Wait, 6, nullptr},
    {Frame, 700, "intro/frame_17.png"},
    {Wait, 6, nullptr},
    {Frame, 1, "intro/frame_18.png"},
    {Wait, 6, nullptr},
    {Frame, 700, "intro/frame_19.png"},
    {Wait, 6, nullptr},
    {Frame, 1, "intro/frame_20.png"},
    {Wait, 6, nullptr},
    {Frame, 700, "intro/frame_21.png"},
    {Wait, 6, nullptr},
    {Wait, 6, nullptr},
};

// Tiles of a 40x32 frame at tile first, its map to plane A.
void load_frame(uint16_t first, const char* image) {
    const assets::Tilemap& t = assets::tilemap(image);
    LoadTilesAt(first, t.count(), t.tiles.data());
    LoadMap(t.map.data(), 0xC000, 0x28, 0x20, 0x40, first);
}

// 0x1EF94
void intro() {
    ClearCram();
    ClearVram();
    SetVScrollA(0xFFF0);
    // 0x1EFD2
    load_frame(1, "intro/frame_00.png");
    FadeIn(assets::palette("palettes/intro_fade.png"), 5);
    for (const Step& s : intro_steps) {
        if (host::any_pressed()) {
            ClearCram();
            return;
        }
        switch (s.op) {
        case Wait: WaitFrames(s.a); break;
        case Frame: load_frame(s.a, s.file); break;
        }
    }
    for (int i = 0; i < 0x78; i++) {
        if (host::any_pressed()) {
            ClearCram();
            return;
        }
        WaitFrames(1);
    }
    FadeOut(assets::palette("palettes/intro_fade.png"), 5);
}

} // namespace

// 0x1EF50
void SegaLogoAndIntro() {
    ResetVdp();
    sega_logo(0x0002); // d0 still holds the low word of the command from 0x1F5D6
    ResetVdp();
    host::wide_screen = [](int) {}; // PC: the backdrop (#000034) fills the sides of a wide window
    intro();
    host::wide_screen = nullptr;
    ClearVram();
}

} // namespace game
