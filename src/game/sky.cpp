// The sky seen outdoors (FF271A = 0): the moving backdrop sprites (0x2404),
// the lightning flash (0x2682, 0x27F8) that hurts a player standing on the
// open cells 0x27 / 0x28, and the bolt sprite (0x28B4, 0x28F6).
#include "game.hpp"
#include "display.hpp"
#include "hires.hpp"

#include "../host.hpp"

namespace game {

namespace {

int16_t s16(uint16_t v) { return int16_t(v); }

auto& flash = st.sky_flash;
auto& flash_x = st.flash_x;
auto& flash_count = st.flash_count;
auto& flash_time = st.flash_time;
auto& flash_lit = st.flash_lit;
auto& bolt_on = st.bolt_on;
auto& bolt_time = st.bolt_time;
auto& flash_col = st.flash_col;
auto& bolt_x = st.bolt_x;
auto& bolt_y = st.bolt_y;
auto& spr_link = st.sprite_link;

uint16_t put_sprite(uint16_t a2, uint16_t y, uint16_t link, uint16_t attr, uint16_t x) {
    st.sprites[a2] = {y, link, attr, x};
    return uint16_t(a2 + 1);
}

// 0x2794: the cloud lit by the flash, d5 its column.
uint16_t FlashCloudTile(uint16_t& d1, uint16_t& d2, uint16_t d5) {
    d1 = uint16_t(d1 + 0xB0);
    d2 = uint16_t(d2 + 0xE0);
    uint16_t d3 = uint16_t((d1 & 3) + 0xC54C);
    if (s16(d1) >= 0xC6 && s16(d1) <= 0xDA) d3 = 0xC54F;
    if (s16(d2) <= s16(uint16_t(d5 + 0x18)) && s16(d2) >= s16(uint16_t(d5 - 0x18))) d3 = 0xC550;
    if (s16(d1) <= 0xD0) d3 |= 0x1000;
    if (s16(d2) <= s16(d5)) d3 |= 0x800;
    return d3;
}

// 0x271C
uint16_t FlashSparkTile(uint16_t& d1, uint16_t& d2) {
    uint16_t d3 = 0xC551;
    if (d1 < 0x10)
        d3 = uint16_t(d3 + 0x1002);
    else if (d1 >= 0x30)
        d3 = uint16_t(d3 + 2);
    d1 = uint16_t(d1 + 0xB0);
    d2 = uint16_t(d2 + 0xE0);
    return d3;
}

uint16_t flash_column(uint16_t base) {
    return uint16_t((uint16_t(uint16_t(-player_angle) << 1) + base - (s16(player_y) >> 6) + flash_x) & 0x3FF);
}

// 0x26DA: a random spark of the flash, placed by the view direction.
uint16_t FlashSpark(uint16_t a2) {
    uint32_t r = Random();
    uint16_t d1 = uint16_t(r >> 16) & 0x3F, d2 = uint16_t(r >> 8) & 0x7F;
    uint16_t a = player_angle & 0x1FF;
    uint16_t d0 = 0, d3;
    if (a < 0x40 || a >= 0x1C0) {
        d3 = uint16_t(FlashSparkTile(d1, d2) | 0x800);
        d0 = 0x400;
    } else if (a < 0xC0)
        d3 = FlashCloudTile(d1, d2, flash_column(0x220));
    else if (a < 0x140) {
        d3 = FlashSparkTile(d1, d2);
        d0 = 0x400;
    } else
        d3 = uint16_t(FlashCloudTile(d1, d2, flash_column(0x20)) ^ 0x1800);
    a2 = put_sprite(a2, d1, uint16_t(d0 | spr_link), d3, d2);
    spr_link = uint16_t(spr_link + 1);
    return a2;
}

// 0x27F8: the flash: thunder at 0x0A-0x0F, the hit on open cells at 0x0A
// and 0x10, over at 0x20 (or 0x24 counting down FF1077).
void UpdateFlash() {
    flash_time = uint8_t(flash_time + 1);
    uint8_t t = flash_time;
    auto thunder = [] {
        PlaySoundLocal(0x68);
        PlaySoundLocal(0x97);
    };
    auto strike = [] {
        uint8_t cls = st.cell_class[map_cell(st.player_cell)];
        if (cls == 0x28 || (cls == 0x27 && s16(st.view_z) > -10)) HitPlayer(0x64, 0x64, 0);
    };
    if (t == 0x20) {
        flash = 0;
        flash_time = 0;
    } else if (t == 0x24) {
        flash = 0;
        flash_lit = 0;
        flash_count = uint8_t(flash_count - 1);
    } else if (t == 0xA) {
        flash_lit = 1;
        thunder();
        strike();
    } else if (t == 0x10)
        strike();
    else if (t == 0x16)
        flash_lit = 0;
    else if (t > 0xA && t < 0x10)
        thunder();
}

// 0x28F6: the bolt sprite; at count 8 it strikes (sound 0x18).
uint16_t BoltSprite(uint16_t a2) {
    if (bolt_on) {
        bolt_x = uint16_t(s16(uint16_t((bolt_x << 1) + bolt_x + flash_col)) >> 2);
        flash_time = 0x14;
    }
    bolt_time = uint8_t(bolt_time - 1);
    if (bolt_time == 8) {
        PlaySoundLocal(0x18);
        flash_count = 1;
        flash_time = 0x20;
        bolt_on = 0;
    }
    const uint16_t* f = kBoltFrames[bolt_time & 7];
    a2 = put_sprite(a2, uint16_t(bolt_y + f[0]), uint16_t(spr_link | f[1]), f[2], uint16_t(bolt_x + f[3]));
    spr_link = uint16_t(spr_link + 1);
    return a2;
}

} // namespace

// 0x2682: starts a lightning flash (cells 0x27/0x28) unless one is running.
void StartFlash() {
    if (!flash_count || flash) return;
    flash = 0xFF;
    flash_time = 0;
    flash_lit = 0;
    flash_x = uint8_t(uint16_t(Random() >> 16) & 0x78);
    while (fb_dma_pending) host::frame();
    display::upload_tiles(0x545, 46, assets::tiles("screens/tiles/sky_flash_cloud.png",
                                             {{1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1},
                                              {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1}, {1, 1},
                                              {2, 2}, {3, 3}, {4, 4}})
                                   .data());
}

// 0x28B4: starts the bolt at column d0; it hits the flash when that is
// within 0x40 of the middle.
void StartBolt(uint16_t d0) {
    bolt_x = d0;
    bolt_y = 0xD0;
    if (flash) {
        int16_t d = int16_t(flash_col - 0x120);
        if (d < 0) d = int16_t(-d);
        if (uint16_t(d) < 0x40) {
            bolt_on = 1;
            bolt_time = 0x10;
            flash_time = 0x14;
            return;
        }
    }
    bolt_on = 0;
    bolt_time = 8;
}

// 0x2404: the sky sprites (FF003E list a2), returns the new end.
uint16_t SkySprites(uint16_t a2) {
    if (flash) {
        UpdateFlash();
        if (flash_lit) a2 = FlashSpark(a2);
    }
    if (bolt_time) a2 = BoltSprite(a2);
    uint16_t d2 = uint16_t((uint16_t(uint16_t(-player_angle) << 1) + 0x210 - (s16(player_y) >> 6)) & 0x3FF);
    int16_t d5 = int16_t(0xCF - int16_t(uint16_t((cur_level << 3) + (s16(st.stair_height) >> 4))));
    hires::skyline(d2, d5);
    if (d2 == 0 || d2 >= 0x1A0) return a2;
    flash_col = 0;
    if (flash) {
        uint16_t x = uint16_t(flash_x + 0x10 + d2);
        a2 = put_sprite(a2, 0xC9, spr_link, kFlashAttr[(flash_time & 0xFE) >> 1], x);
        spr_link = uint16_t(spr_link + 1);
        flash_col = x;
    }
    uint16_t d3 = 0x13;
    if (d2 < 0xA0) {
        d3 = uint16_t(d2 >> 3);
        d2 = uint16_t((d2 & 7) + 0x98);
    } else if (d2 > 0x100)
        d3 = uint16_t(s16(uint16_t(0x1A0 - d2)) >> 3);
    const uint16_t* a0 = kSkySprites[d3];
    uint16_t d1 = spr_link;
    const uint16_t* a1 = kSkyRows;
    while (d5 <= 0x98) {
        d5 = int16_t(d5 + 0x10);
        a1++;
    }
    do {
        uint16_t d6 = *a1++;
        uint16_t d7 = d2;
        for (const uint16_t* a3 = a0;; a3++) {
            uint16_t d0 = *a3;
            if (s16(d0) < 0) break;
            a2 = put_sprite(a2, uint16_t(d5), uint16_t(d0 | d1), d6, d7);
            d1 = uint16_t(d1 + 1);
            d7 = uint16_t(d7 + 0x20);
        }
        d5 = int16_t(d5 + 0x10);
    } while (d5 < 0xF8);
    spr_link = d1;
    return a2;
}

} // namespace game
