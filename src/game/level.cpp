// The game: level start, the gameplay loop and what happens on death or
// when an area is finished (GameLevelLoop 0x102C).
#include "display.hpp"
#include "game.hpp"
#include "hires.hpp"
#include "savegame.hpp"

#include "../host.hpp"

#include <cstring>
#include <utility>

namespace game {

namespace {


void clear_cram() {
    for (int i = 0; i < 0x40; i++) display::pal[i] = 0;
}

// Loads the area blob (layout in docs/RE_NOTES.md).
void load_area() {
    const char* files[3] = {"levels/area0.bin", "levels/area1.bin", "levels/area2.bin"};
    const uint8_t* a0 = assets::bytes(files[cur_area == 0 ? 0 : cur_area == 1 ? 1 : 2]).data();
    for (uint16_t& w : st.faces) w = be16(std::exchange(a0, a0 + 2));
    for (uint16_t& w : st.cell_faces) w = be16(std::exchange(a0, a0 + 2));
    for (uint8_t& b : st.cell_class) b = *a0++;
    for (auto& map : st.maps)
        for (uint8_t& b : map) b = *a0++;
    for (uint8_t& b : st.level_light) b = *a0++;
    uint8_t* a1 = st.face_scripts;
    for (;;) {
        uint16_t len = be16(a0);
        a0 += 2;
        *a1++ = uint8_t(len >> 8);
        *a1++ = uint8_t(len);
        if (!len) break;
        for (int i = 0; i <= int16_t(len - 3); i++) *a1++ = *a0++;
    }
    for (int cls = 0xFF; cls >= 0; cls--) {
        uint8_t v = 0;
        while (st.cell_class[v] != cls) {
            v++;
            if (!v) break;
        }
        st.class_cell[cls] = v;
    }
}

// Player start: the cell of class 0x77, or cell 0x288.
void place_player() {
    uint16_t d0 = 0;
    for (; d0 < 0x400; d0++)
        if (st.cell_class[map_cell(cur_map + d0)] == 0x77) break;
    if (d0 == 0x400) d0 = 0x288;
    player_x = uint16_t(((d0 & 0x1F) << 8) + 0x80);
    player_y = uint16_t(((d0 << 3) & 0xFF00) + 0x80);
}

// State shared by the level start (0x11C8) and a respawn (0x1848).
void reset_view() {
    st.dma_count = 0;
    player_angle = 0;
    for (uint16_t* v : {&st.view_z, &st.backdrop_z, &st.view_z_target, &st.jump_speed, &st.crouch_time, &st.turn,
                        &st.turn_target, &st.speed, &st.speed_target, &st.push_x, &st.push_y})
        *v = 0;
}

void demo_setup() {
    st.attract_timer = 0;
    st.demo_left = 0xC8;
    // The seed went to the demo's ROM bytes when recording, which kept nothing.
    st.demo_pos = 0;
    if (demo_mode == 1) random_seed = be32(demo_inputs().data());
    if (demo_mode == 1 || demo_mode == 2) st.demo_pos = 4;
    st.dir_x = kDirVectors[player_angle][0];
    st.dir_y = kDirVectors[player_angle][1];
    st.backdrop_scroll = 0;
    st.backdrop_fine = 0;
    st.backdrop_col = 0;
    st.footstep = 0;
}

void show_view(bool level_start) {
    clear_cram();
    fb_dma_pending = 2;
    if (level_start) HideSprites();
    WaitVBlank();
    WaitVBlank();
    FadeInGamePalette();
}

// Writes the backdrop scroll: hscroll of plane A from the angle, vscroll
// from the level and the view tilt.
void stream_backdrop(uint16_t d3);

void write_scroll(uint16_t angle_scroll, bool with_h, int stream, bool wait) {
    if (wait) WaitVBlank();
    int saved = int_mask;
    int_mask = 7;
    if (with_h) display::hscrollA[0] = uint16_t(angle_scroll + 0x10);
    uint16_t v = uint16_t(cur_level * 4 + (int16_t(uint16_t(st.stair_height)) >> 5) - 0x20);
    display::vscrollA = v;
    if (stream >= 0) stream_backdrop(uint16_t(stream));
    int_mask = saved;
}

// Streams 4 columns of the backdrop map into plane A when the
// view turns past an 8-pixel column.
void stream_backdrop(uint16_t d3) {
    for (int i = 0; i < 4; i++, d3 = uint16_t(d3 + 2)) {
        uint16_t d4 = uint16_t(d3 + st.backdrop_col);
        const uint16_t* a0 = backdrop_map() + (d4 & 0xFF) / 2;
        uint16_t dest = uint16_t(0xE000 + (d4 & 0x7E));
        for (int k = 0; k < 16; k++) display::write_cell(uint16_t(dest + k * 0x80), a0[k * 0x80]);
    }
}

} // namespace

// 0xE78: the alive flags and inventory carried to the next area.
void CarryInventory() {
    for (int i = 0; i < 5; i++) {
        st.carry_weapon[i] = uint8_t(st.slots[i].weapon);
        st.carry_ammo[i] = uint8_t(st.slots[i].ammo >> 8);
    }
}

// 0xEB6: inventory from the carried weapons.
void RestoreCarriedInventory() {
    ClearInventory();
    for (int i = 0; i < 5; i++) {
        st.add_ammo = uint16_t(st.carry_ammo[i] << 8);
        uint8_t w = st.carry_weapon[i];
        if (w) AddWeapon(w);
    }
    st.add_ammo = 0;
}

// 0xF2A: the character's starting equipment.
void GiveStartEquipment() {
    if (cur_area == 1 && cur_level == 0xF) return; // the boxing match: a link game removes one boxer
    for (const uint16_t* e : kStartEquipment[st.character]) {
        if (!e[0]) break;
        st.add_ammo = e[1];
        AddWeapon(e[0]);
    }
    st.add_ammo = 0;
}

// 0x1A0C: hides the sprite table.
void HideSprites() {
    int saved = int_mask;
    int_mask = 7;
    display::clear_sprites();
    int_mask = saved;
}

// 0x1C24: fades the game palette in.
void FadeInGamePalette() {
    for (int i = 0; i < 64; i++) st.fade_pal[i] = 0;
    const uint16_t* target = game_palette(st.nv_slot != 0xFFFF, st.indoors);
    st.fade_comp = 0;
    st.fade_idle = 0;
    do {
        WaitVBlank();
        if (!st.fade_idle)
            for (int i = 0; i < 64; i++) display::pal[i] = st.fade_pal[i];
        StepPaletteFade(target, 64);
    } while (st.fade_idle != 2);
}

// 0x1CBC: fades the game palette out.
void FadeOutGamePalette() {
    st.fade_comp = 0;
    st.fade_idle = 0;
    do {
        WaitVBlank();
        if (!st.fade_idle)
            for (int i = 0; i < 64; i++) display::pal[i] = st.fade_pal[i];
        StepPaletteFade(kBlack, 64);
    } while (st.fade_idle != 2);
}

// One pass of the gameplay loop (0x1442-0x1784). Returns where the original
// continues: 0x1442 (next frame), 0x1788 (dead) or 0x1A40 (level left).
// The backdrop (plane A) scroll for player_angle, streaming the backdrop
// columns the view turned onto.
void scroll_backdrop(bool wait) {
    uint16_t d2 = uint16_t((-player_angle * 2) & 0x3FF);
    if (d2 == st.backdrop_scroll) {
        write_scroll(d2, false, -1, wait);
    } else {
        int16_t d1 = int16_t(st.backdrop_scroll - d2);
        st.backdrop_scroll = d2;
        d1 = int16_t(int16_t(d1 << 6) >> 6);
        auto& fine = st.backdrop_fine;
        auto& col = st.backdrop_col;
        st.bolt_x -= uint16_t(d1 * 2);
        fine += uint16_t(d1);
        // PC: the original turns at most one column per frame; mouse turns can
        // cross several, so the earlier ones are streamed here.
        int stream = -1;
        if (d1 >= 0) {
            while (int16_t(uint16_t(fine)) >= 0x20) {
                if (stream >= 0) stream_backdrop(uint16_t(stream));
                fine -= 0x20;
                col += 8;
                stream = 0x48;
            }
        } else {
            while (int16_t(uint16_t(fine)) < 0) {
                if (stream >= 0) stream_backdrop(uint16_t(stream));
                fine += 0x20;
                col -= 8;
                stream = 0;
            }
        }
        write_scroll(d2, true, stream, wait);
    }
}

// PC: the game draws a frame every 5 VBlanks. In between, the port shows views
// with the camera and enemies part way from the last drawn frame to the next
// one. They are drawn from a copy of the state, so the game is untouched.
State drawn_prev, drawn_cur; // the state when the last two frames were drawn
int drawn_count = 0;

// v (from drawn_cur) num/5 of the way from p (drawn_prev).
void interpolate(uint16_t& v, uint16_t p, int num, int limit, bool angle = false) {
    int d = int16_t(uint16_t(v - p));
    if (angle) d = ((d + 0x100) & 0x1FF) - 0x100;
    if (d > limit || d < -limit) return;
    uint16_t r = uint16_t(p + d * num / 5);
    v = angle ? r & 0x1FF : r;
}

// Shows the view num/5 of the way between the last two drawn frames.
void draw_between(int num) {
    if (drawn_count < 2 || fb_dma_pending) return;
    static State saved;
    saved = st;
    st = drawn_cur;
    const State& prev = drawn_prev;
    // A long jump of the player between the two frames (a teleport): no in-between.
    auto far = [](uint16_t v, uint16_t p) {
        int d = int16_t(uint16_t(v - p));
        return d > 0x180 || d < -0x180;
    };
    if (far(player_x, prev.player_x) || far(player_y, prev.player_y)) {
        st = saved;
        return;
    }
    sound_muted = true;
    fb_dma_pending = 0; // the view goes to VRAM below, not by VBlank DMA
    interpolate(player_x, prev.player_x, num, 0x180);
    interpolate(player_y, prev.player_y, num, 0x180);
    interpolate(player_angle, prev.player_angle, num, 0x100, true);
    interpolate(st.view_z, prev.view_z, num, 0x100);
    interpolate(st.stair_height, prev.stair_height, num, 0x100);
    st.dir_x = kDirVectors[player_angle][0];
    st.dir_y = kDirVectors[player_angle][1];
    Enemy* e = first_object();
    for (uint16_t n = st.object_count; n; n--, e = next_object(e)) {
        interpolate(e->x, prev.objects[slot_of(e)].x, num, 0x100);
        interpolate(e->y, prev.objects[slot_of(e)].y, num, 0x100);
    }
    uint16_t x = player_x, y = player_y, angle = player_angle, height = st.stair_height;
    hires::Frame next = hires::rendered;
    RenderFromBehind();
    // The in-between view goes straight to the high-resolution output.
    hires::show();
    hires::rendered = std::move(next);

    // The backdrop scroll and the sky sprites go with the game state after the
    // last frame, with the in-between camera.
    st = saved;
    player_x = x;
    player_y = y;
    player_angle = angle;
    st.stair_height = height;
    scroll_backdrop(false);
    if (!st.indoors) {
        st.sprite_link = sky_link;
        uint16_t a2 = SkySprites(sky_sprites);
        if (a2) set_low_byte(st.sprites[a2 - 1].link, 0);
        display::upload_sprites(st.sprites, a2);
    }
    st = saved;
    sound_muted = false;
}

uint32_t gameplay_frame() {
    if (!st.cleared_timer && !(st.frame & 0x3F)) CountEnemiesLeft();
    if (!st.indoors) AnimateFaces();
    UpdateMessages();
    fb_dma_pending = 2;
    scroll_backdrop(true);

    RunDmaQueue();
    ReadInput();
    st.sprite_link = 1;
    st.sprite_end = 0;
    RunFaceScripts();
    if ((pad_buttons & 0x80) && !(st.pad_prev & 0x80)) st.map_request = st.map_request | 1;
    if (st.map_request) MapScreen();
    PlayerControls();
    RunEnemies();
    HeadBob();
    UpdateBackdrop();
    drawn_prev = drawn_cur;
    drawn_cur = st;
    drawn_count += 1;
    RenderFromBehind();
    DrawRadar();
    StatusSprites();
    UndoHeadBob();
    UpdateDoors();
    UpdateItemCharges();
    QueueSpriteListDma();

    auto& exit_timer = st.exit_timer;
    if (exit_timer) {
        exit_timer -= 1;
        if (!exit_timer) st.level_exit = 1;
    }
    if (st.level_exit) return 0x1A40;
    auto& dying = st.dying;
    if (st.health <= 1) {
        dying += 1;
        if (dying == 1) st.hit_flash = 0xFFF;
    }
    return dying < 0x1E ? 0x1442 : 0x1788;
}

uint16_t GameLevelLoop() {
    host::take_menu_request(); // drop a stale Esc (e.g. from the title screen)
    if (int slot = take_load(); slot >= 0 && load_snapshot(slot)) goto loaded;
restart:
    {
    drawn_count = 0;
    st.exit_timer = st.pw_level = st.cleared_timer = st.enemies_left = 0;
    st.counter_shown = 0xFFFF;
    st.blast_near = 0;
    st.burning = 0;
    st.footstep = 0x65;
    st.pickup_check = 0;
    st.sky_flash = 0;
    st.flash_count = 3;
    st.indoors = 0;
    cur_area = uint16_t((uint8_t(level_id) >> 4) & 3);
    if (cur_area != 1) st.indoors = 1;
    uint16_t lvl = level_id & 0xF;
    cur_level = lvl;
    st.pw_level = lvl;
    // Passwords of the levels before the start one are "*SECURED*".
    uint8_t* a0 = st.level_passwords;
    for (uint16_t i = lvl; i; i--)
        for (char c : {'*', 'S', 'E', 'C', 'U', 'R', 'E', 'D', '*'}) *a0++ = uint8_t(c);
    load_area();
    for (uint16_t l = 0; l != cur_level; l++) ApplyLevelCellVariants(l);
    clear_cram();
    st.health = st.carry_health;
    st.dying = 0;
    ClearInventory();
    PlaySound(cur_area);
    SetGameVdpRegs();
    // 0xA3C: reseeds from the HV counter.
    uint32_t seed = 0x82647359;
    for (int i = 0; i < 10000; i++) seed += uint32_t(int32_t(int16_t(ReadHvCounter())));
    random_seed = seed;
    st.backdrop_in = st.indoors != 0;
    reset_view();
    st.door_count = 0;
    demo_setup();
    cur_map = level_map(cur_level);
    ClearEnemies();
    ChangeLevel(cur_level);
    ClearEnemies();
    st.hit_flash = 0;
    st.stair_height = 0;
    st.lift = 0;
    ClearMessages();
    place_player();
    st.map_request = 0;
    for (int i = 0; i < 5; i++) {
        uint8_t w = st.carry_weapon[i];
        if (!w) continue;
        st.add_ammo = uint16_t(st.carry_ammo[i] << 8);
        AddWeapon(w);
    }
    st.add_ammo = 0;
    GiveStartEquipment();
    for (uint8_t& b : st.slot_blink) b = 0;
    st.last_cell = kNoCell;
    st.radar_redraw = 1;
    DrawHud();
    RenderFromBehind();
    DrawRadar();
    show_view(true);
    }

loaded:
    uint16_t last = vblank_count;
    for (;;) {
        // The original draws as fast as the 68000 allows, about 5 VBlanks
        // per frame; the port draws instantly, so it waits for that pace.
        while (uint16_t(vblank_count - last) < 5) {
            // The frame drawn last tick is fully shown 2 VBlanks into this one.
            draw_between(uint16_t(vblank_count - last) - 1);
            host::frame();
        }
        // PC: Esc opens the save/load menu (not in attract mode).
        if (!demo_mode && host::take_menu_request()) {
            if (run_pause_menu() == 1) { // quit to title
                StopMusic();
                return 3;
            }
        }
        last = vblank_count;
        uint32_t next = gameplay_frame();
        if (next == 0x1442) continue;
        if (next == 0x1A40) break;

        // 0x1788: dead. Enemies forget the player, the character is lost.
        uint16_t n = st.object_count;
        Enemy* e = first_object();
        // The original tests FF8004 (a6-relative, where the enemy's flags were
        // probably meant): level-0 map cells (14, 25) and (15, 25).
        uint16_t w = uint16_t(st.maps[0][0x32E] << 8 | st.maps[0][0x32F]);
        for (int i = 0; i < n; i++, e = next_object(e))
            if ((w & 0x14) == 0x14) e->count = 1;
        StopMusic();
        st.alive[st.character] = 0;
        if (!(st.alive[0] | st.alive[1] | st.alive[2] | st.alive[3] | st.alive[4])) {
            // 0x19D8: everyone is dead.
            StoryScreen(5, 0x13298);
            SetGameVdpRegs();
            return 0;
        }
        CarryInventory();
        CharacterSelect(0x13298);
        clear_cram();
        PlaySound(cur_area);
        SetGameVdpRegs();
        uint32_t s = 0x82647359;
        for (int i = 0; i < 10000; i++) s += uint32_t(int32_t(int16_t(ReadHvCounter())));
        random_seed = s;
        st.health = 0x64;
        DropItemNearPlayer();
        reset_view();
        demo_setup();
        st.face_turn = 0;
        for (FaceTimer& f : st.face) f = {};
        st.hit_flash = 0;
        st.stair_height = 0;
        st.lift = 0;
        ClearMessages();
        place_player();
        drawn_count = 0;
        RestoreCarriedInventory();
        GiveStartEquipment();
        DrawHud();
        RenderFromBehind();
        DrawRadar();
        st.dying = 0;
        show_view(false);
        for (uint8_t& b : st.slot_blink) b = 0;
        last = vblank_count;
    }

    // 0x1A40: the level exit was reached.
    st.level_exit = 0;
    FadeOutGamePalette();
    if (demo_mode == 3) {
        StopMusic();
        KillAllEnemies();
        return 2;
    }
    if (demo_mode) {
        StopMusic();
        KillAllEnemies();
        return 1;
    }
    StoreAreaPassword();
    StopMusic();
    uint16_t story = cur_area == 0 ? 1 : cur_area == 1 ? 2 : 4;
    StoryScreen(story, 0x13298);
    SetGameVdpRegs();
    uint16_t next = uint16_t(cur_area + 1);
    if (next == 3) return 0; // game finished: back to the boot screens
    cur_area = next;
    level_id = uint8_t(next << 4);
    st.carry_health = uint8_t(st.health);
    CarryInventory();
    goto restart;
}

} // namespace game
