// Story, briefing, game-over and ending screens (0xCAA4E): a picture on
// plane B with text scrolling up on plane A.
#include "display.hpp"
#include "game.hpp"

namespace game {

namespace {

uint16_t* const target_pal = st.pal_buf; // 4 palette lines; line 3 is the text colours
constexpr const char* text_pal = "palettes/story_text.png";

auto& scroll = st.menu_value;

struct Picture {
    const char* image;
    const char* palette;
    int palette_lines;
};

const Picture pic_a{"story/pic_a.png", "story/pic_a.pal.png", 1};
const Picture pic_b{"story/pic_b.png", "story/pic_b.pal.png", 1};
const Picture pic_c{"story/pic_c.png", "story/pic_c.pal.png", 1};
const Picture pic_d{"story/pic_d.png", "story/pic_d.pal.png", 2};
const Picture pic_ending{"story/pic_ending.png", "story/pic_ending.pal.png", 2};
const Picture pic_basement{"story/pic_basement.png", "story/pic_basement.pal.png", 1};

// 0x1F84E: copies palette line n of the file into line of pal_buf.
void copy_line(const char* file, int line, int n = 0) {
    for (int i = 0; i < 16; i++) target_pal[line * 16 + i] = assets::palette(file)[n * 16 + i];
}

void load_picture(const char* image) {
    const assets::Tilemap& t = assets::tilemap(image);
    LoadTilesAt(1, t.count(), t.tiles.data());
    LoadMap(t.map.data(), 0xE000, 0x28, 0x1C, 0x80, 1);
}

void show_picture(const Picture& p) {
    load_picture(p.image);
    for (int i = 0; i < p.palette_lines; i++) copy_line(p.palette, i, i);
    copy_line(text_pal, 3);
    FadeIn(target_pal, 4);
}

// 0xCB2F6: one line of text in two tile rows.
void draw_line(uint16_t nt, const std::string& text) {
    for (int row = 0; row < 2; row++, nt = uint16_t(nt + 0x100))
        for (size_t i = 0; i < text.size(); i++) display::write_cell(uint16_t(nt + i * 2), kStoryFont[text[i] - 0x20][row]);
}

// A block of text/story.txt: the quoted lines after the line [name].
std::vector<std::string> story_lines(const char* name) {
    const std::vector<std::string>& l = assets::lines("text/story.txt");
    const std::vector<std::string>& q = assets::list("text/story.txt");
    std::vector<std::string> v;
    size_t i = 0;
    while (l[i] != std::string("[") + name + "]") i++;
    while (++i < l.size() && l[i][0] != '[') v.push_back(q[i]);
    return v;
}

bool newly(int bit) { return (pad_buttons >> bit & 1) && !(st.pad_prev >> bit & 1); }

// 0xCB1E4: scrolls a block of text up, then the blank [tail] block. Up
// speeds it up, Down or A pause it, Start skips.
void scroll_text(const char* name) {
    std::vector<std::string> text = story_lines(name);
    bool tail = false;
    size_t line = 0;
    FillPlaneA(0);
    scroll = 0;
    SetVScrollA(0);
    for (;;) {
        int frames = (pad_buttons & 1) ? 1 : 4;
        for (int i = 0; i < frames; i++) WaitVBlank();
        for (;;) {
            ReadPad();
            if (newly(7)) return;
            if (pad_buttons & 0x42) break;
            uint16_t s = scroll;
            if (!(s & 0xF)) {
                draw_line(uint16_t((((s & 0xFF) << 5) + 4) | 0xC000), text[line]);
                if (++line == text.size()) {
                    if (tail) return;
                    text = story_lines("tail"); // blank lines until the text has scrolled off
                    tail = true;
                    line = 0;
                    continue;
                }
            }
            scroll += 1;
            SetVScrollA(scroll);
            break;
        }
    }
}

void fade_out() { FadeOut(target_pal, 4); }

// 0xCB0BE: the last picture of the game-over sequence, flashing in from white.
void final_picture() {
    load_picture("story/final.png");
    FillPlaneA(0);
    for (int i = 0; i < 34; i++) target_pal[i] = 0;
    FadeToWhite(target_pal, 1);
    GemsPlaySound(0x98);
    copy_line("story/final.pal.png", 0);
    copy_line(text_pal, 3);
    FadeFromWhite(target_pal, 4);
    LoadPaletteLine(0, target_pal);
    LoadPaletteLine(3, target_pal + 16);
    GemsPlaySound(0x98);
    WaitFrames(10);
    GemsPlaySound(0x98);
    for (int i = 0; i <= 0x384; i++) {
        WaitVBlank();
        ReadPad();
        if (newly(7)) break;
    }
}

} // namespace

// 0xCAA4E: d0 = story: 0 opening, 1 high-rise, 2 basement, 3 attract mode (returns 1
// if Start was pressed), 4 ending, 5 game over.
uint16_t StoryScreen(uint16_t story, uint32_t) {
    ClearCram();
    ClearVram();
    SetIntroVdpRegs();
    // 0x9003: 128x32 planes for the picture and the scrolling text.
    display::pwA = display::pwB = 128;
    display::phA = display::phB = 32;
    // 0x8B03 (0x8B0B with a link cable): per-line scroll hides text lines
    // 0-24 and 201-255 of plane A.
    display::hmode = 3;
    for (int line = 0xFF; line >= 0; line--) {
        uint16_t v = line <= 0x18 || line > 0xC8 ? 0x140 : 0;
        display::hscrollA[line % display::H] = v;
        display::hscrollB[line % display::H] = v;
    }
    display::upload_tiles(0x4B0, 0x4B0 / 8, Font16Tiles().data());

    switch (story & 0xFF) {
    case 0:
        PlaySound(3);
        show_picture(pic_a);
        scroll_text("briefing");
        if (!(pad_buttons & 0x80)) {
            fade_out();
            show_picture(pic_b);
            scroll_text("mission");
        }
        fade_out();
        StopMusic();
        return 0;
    case 1:
        PlaySound(3);
        show_picture(pic_b);
        scroll_text("station_won");
        if (!(pad_buttons & 0x80)) {
            fade_out();
            show_picture(pic_c);
            scroll_text("high_rise");
        }
        fade_out();
        StopMusic();
        return 0;
    case 2:
        PlaySound(3);
        show_picture(pic_d);
        scroll_text("basement");
        fade_out();
        StopMusic();
        return 0;
    case 3: {
        show_picture(pic_a);
        scroll_text("briefing");
        uint16_t skipped = (pad_buttons & 0x80) ? 1 : 0;
        fade_out();
        return skipped;
    }
    case 4:
        PlaySound(0x89);
        show_picture(pic_ending);
        scroll_text("ending");
        fade_out();
        StopMusic();
        return 0;
    case 5:
        PlaySound(0x87);
        if (cur_area == 1 || cur_area == 2) {
            show_picture(cur_area == 1 ? pic_c : pic_basement);
            scroll_text("hq_lost");
            fade_out();
            final_picture();
        } else {
            show_picture(pic_b);
            scroll_text("station_lost");
        }
        fade_out();
        StopMusic();
        return 0;
    }
    untranslated(0x0CAA4E);
}

} // namespace game
