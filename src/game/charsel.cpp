// Character select (0xC6362). Up/Down scroll between two nametable pages
// (0xE082 / 0xF082 on a 64x64 plane); FF1030 is the chosen character.
#include "game.hpp"
#include "display.hpp"

namespace game {

namespace {

auto& character = st.character;
auto& page = st.menu_row;
auto& scroll = st.menu_value;

void draw_text(uint16_t nt, const std::string& text) {
    for (int row = 0; row < 2; row++, nt = uint16_t(nt + 0x80))
        for (size_t i = 0; i < text.size(); i++) display::write_cell(uint16_t(nt + i * 2), kFont16[text[i] - 0x20][row]);
}

// Line n of the character's select page (text/characters.txt).
const std::string& page_line(int n) { return assets::list("text/characters.txt")[character * 12 + n]; }

// One palette line into pal_buf.
void copy_line(const char* file, int line) {
    for (int i = 0; i < 16; i++) st.pal_buf[line * 16 + i] = assets::palette(file)[i];
}

// 0xC6686: the frame sprites, moved with the scroll.
void DrawCharSelFrame() {
    uint16_t d5 = uint16_t(0x1FF - (scroll & 0x1FF));
    static const struct { uint16_t size_link, x, y, attr; } s[14] = {
        {0xB01, 0x1A0, 0x0A8, 0xA546}, {0xB02, 0x1A0, 0x110, 0xB546}, {0xB03, 0x1A0, 0x1A8, 0xA546},
        {0xB04, 0x1A0, 0x210, 0xB546}, {0xF05, 0x19C, 0x0DC, 0xA552}, {0xF06, 0x19C, 0x1DC, 0xA552},
        {0x007, 0x088, 0x089, 0xE562}, {0x008, 0x088, 0x0E1, 0xF562}, {0x009, 0x100, 0x089, 0xED62},
        {0x00A, 0x100, 0x0E1, 0xFD62}, {0x00B, 0x088, 0x189, 0xE562}, {0x00C, 0x088, 0x1E1, 0xF562},
        {0x00D, 0x100, 0x189, 0xED62}, {0x000, 0x100, 0x1E1, 0xFD62},
    };
    for (int i = 0; i < 14; i++)
        display::set_sprite(i, uint16_t((s[i].y + d5) & 0x1FF), uint8_t(s[i].size_link >> 8),
                            uint8_t(s[i].size_link), s[i].attr, s[i].x);
}

// 0xC6520
void LoadCharSelGraphics() {
    copy_line("palettes/charsel_0.png", 0);
    copy_line("palettes/charsel_font.png", 1);
    copy_line("palettes/charsel_story_font.png", 2);
    copy_line("palettes/charsel_3.png", 3);
    uint16_t vram = 0x20;
    for (int i = 0; i < 5; i++) {
        st.portrait_vram[i] = vram;
        vram = LoadTiles(vram, kPortraits[i]);
    }
    display::upload_tiles(0x4B0, 0x4B0 / 8, Font16Tiles().data());
    const assets::Tilemap& killed = assets::tilemap("image/charsel_killed.png");
    display::upload_tiles(0x3A2, killed.count(), killed.tiles.data());
    // The frame sprites (tile lists 0xC76A2 / 0xC76F2 of 0x1F7DA).
    display::upload_tiles(0x546, 28, assets::tiles("screens/tiles/charsel_frame_sprites.png", {{3, 4}, {4, 4}}).data());
    display::upload_tiles(0x562, 1, assets::tiles("screens/tiles/charsel_AC40.png").data());
    DrawCharSelFrame();
    SetVScrollA(0);
    SetVScrollB(0);
    draw_text(0xEE00, assets::text("charsel.dashes"));
    draw_text(0xFE00, assets::text("charsel.dashes"));
}

void draw_portrait(uint16_t nt) {
    LoadImageMap(st.portrait_vram[character], nt, 0, kPortraits[character]);
}

// 0xC6934: name and description of the character.
void DrawCharacterInfo() {
    uint16_t nt = page == 0xE082 ? 0xE0A4 : 0xF0A4;
    for (int i = 0; i < 6; i++, nt = uint16_t(nt + 0x100)) draw_text(nt, page_line(i));
    nt = page == 0xE082 ? 0xE682 : 0xF682;
    for (int i = 6; i < 12; i++, nt = uint16_t(nt + 0x100)) draw_text(nt, page_line(i));
}

// 0xC6A16: overlay on a character who has been killed (the image holds
// the palette line of the original's 0x43A2).
void DrawKilledOverlay() {
    LoadMap(assets::tilemap("image/charsel_killed.png").map.data(), page == 0xE082 ? 0xC000 : 0xD000, 0x28, 0x1C, 0x40, 0x3A2);
}

// 0xC6A50: clears the page scrolled away from. For page 0xF082 the original
// computes the address from leftover registers, which always gives 0xC004.
void ClearHiddenPage() {
    display::fill_cells(page == 0xE082 ? 0xD000 : 0xC004, 0x800, 0);
}

// 0xC683C / 0xC6896: draws the new character on the other page and scrolls to it.
void change_character(int16_t step) {
    page = page == 0xE082 ? 0xF082 : 0xE082;
    draw_portrait(page);
    DrawCharacterInfo();
    if (st.alive[character] != 1) DrawKilledOverlay();
    for (int i = 0; i < 0x20; i++) {
        WaitVBlank();
        scroll += uint16_t(step);
        SetVScrollB(scroll);
        SetVScrollA(scroll);
        DrawCharSelFrame();
    }
    ClearHiddenPage();
}

bool newly(int bit) { return (pad_buttons >> bit & 1) && !(st.pad_prev >> bit & 1); }

} // namespace

// 0xC6362
void CharacterSelect(uint32_t) {
    ClearCram();
    ClearVram();
    SetIntroVdpRegs();
    // 0x9011: 64x64 planes (already set by SetIntroVdpRegs).
    scroll = 0;
    LoadCharSelGraphics();
    page = 0xE082;
    draw_portrait(0xE082);
    DrawCharacterInfo();
    if (st.alive[character] != 1) DrawKilledOverlay();
    // 0xC69B8
    draw_text(page == 0xE082 ? 0xEB82 : 0xFB82, page_line(11));
    FadeIn(st.pal_buf, 4);
    for (;;) {
        WaitVBlank();
        ReadPad();
        if (newly(0)) {
            GemsPlaySound(0x6B);
            character += 1;
            if (character >= 5) character = 0;
            change_character(8);
            continue;
        }
        if (newly(1)) {
            GemsPlaySound(0x6B);
            character -= 1;
            if (int16_t(uint16_t(character)) < 0) character = 4;
            change_character(-8);
            continue;
        }
        if (!(newly(6) || newly(4) || newly(5) || newly(7))) continue;
        if (st.alive[character] != 1) {
            GemsPlaySound(0x69);
            continue;
        }
        GemsPlaySound(0x6B);
        FadeOut(st.pal_buf, 4);
        return;
    }
}

} // namespace game
