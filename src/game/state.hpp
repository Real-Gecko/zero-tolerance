// The game state: everything the game code keeps between calls, as native
// variables. Comments give the work RAM address each one had on the Mega
// Drive (docs/RE_NOTES.md).
#pragma once

#include "object.hpp"

#include <cstdint>

namespace game {

struct WeaponSlot {
    uint16_t weapon; // 0 = empty
    uint16_t ammo;   // rounds * 256
};

struct FaceTimer {
    uint8_t time, frame;
    uint16_t phase;
};

// A map cell: an index into st.maps taken as one array, 0x400 per level
// (the original's pointers into FF7CD6).
using Cell = int32_t;

// A sprite attribute table entry: y, size << 8 | link, tile entry, x.
struct SatEntry {
    uint16_t y, link, attr, x;
};

struct Door {
    Cell cell;
    uint16_t opening;  // 0..0x80
    uint8_t restore;   // cell value put back when it closes
    uint16_t x, y;     // the cell's position
    uint8_t mode;      // 0 closes when free, 1 stays, 2 opens fully
    uint8_t level;
};

struct Speech {
    uint16_t time, sound;
};

// HUD messages, in the order of data/text/messages.txt.
enum class Msg : uint8_t {
    Mine, Vest, Extinguisher, FireSuit, FlashLight, HandGrenade, Handgun, NightVision, LaserGun,
    RocketLauncher, Shotgun, FlameThrower, PulseLaser, BioScanner, Medipack, StepUp, StepDown,
    FloorSecured, FloorNotSecured, Blank, HealthLow, HealthCritical, AmmoLow, ZeroEnemies, ProceedNext,
    Level,             // 32: "ENTERING ..." by area * 16 + level
    Saved = Level + 32, // PC: "GAME SAVED", no speech
};

struct State {
    // System.
    uint16_t vblank_count;   // FF0000
    uint16_t fb_dma_pending; // FF0002: 2 = the 3D view goes out over the next two VBlanks
    uint16_t warm_boot;      // FF0004
    uint32_t random_seed;    // FF0006
    uint8_t password[10];    // FF000A: 9 characters
    uint16_t sound_options;  // FF0014: bit 0 music, 1 voice, 2 FM effects, 3 samples

    // Pad, active high: bit 7 Start, 6 A, 5 C, 4 B, 3 R, 2 L, 1 D, 0 U.
    uint8_t pad_buttons;    // FF002E
    uint8_t pad_prev;       // FF0032: pad_buttons of the read before

    uint8_t dma_count;      // FF0036: end of the DMA queue
    uint16_t sprite_end;    // FF003E: end of the sprite list being built
    uint16_t sprite_link;   // FF0042: link number of the next sprite
    SatEntry sprites[0xA0]; // FF0044: the sprite list, copied to the VDP each frame
    uint8_t dma_queue[0x1C]; // FF0544: QueueVramDma sources

    uint8_t map_request;     // FF06D6: Start pressed in the game
    uint16_t attract_timer;  // FF06D8
    uint16_t demo_left;      // FF06DA
    uint16_t demo_mode;      // FF06DC: 1 playing back, 2 recording, 3 stopped by Start
    uint16_t demo_pos;       // FF06DE: next byte of demo_inputs()

    // Palette fades.
    uint16_t fade_pal[64];   // FF06F6: colours being faded
    uint16_t pal_buf[64];    // FF0776: palettes the screens fade to
    uint16_t fade_idle;      // FF07F6: steps since the last change
    uint8_t fade_comp;       // FF07FC: component stepped last
    uint8_t fade_toggle;     // FF07FD
    uint16_t fade_frames;    // FF07FE

    // Title and boot logo.
    uint16_t menu_choice;    // FF0872: 0 "press start", 1 START, 2 LOAD, 3 OPTIONS
    uint16_t logo_angle;     // FF0874
    uint16_t logo_dist;      // FF0876
    uint16_t light_timer[2]; // FF0878
    uint8_t light_step[2];   // FF087C: kLightTimes step
    uint8_t light_pal[2];    // FF0884: row of palettes/title_lights.png
    uint16_t strip_pos;      // FF0912
    uint16_t strip_tick;     // FF0914
    uint16_t title_scroll;   // FF0916
    uint16_t title_sprite_pal[16]; // FF0918
    uint16_t title_hscroll[0x1C0]; // FF0938

    // Messages.
    Speech speech[16];       // FF088C: (time, sound) list ending in time 0xFFFF
    uint16_t speech_timer;   // FF08CC: counted down by VBlank
    Msg msg_queue[16];       // FF08CE
    uint16_t msg_count;      // FF090E
    uint16_t msg_timer;      // FF0910: frames left for the shown message

    // HUD faces and backdrop.
    uint8_t face_turn;       // FF0DD0
    FaceTimer face[3];       // FF0DD2
    uint16_t backdrop_col;   // FF0DDE
    uint16_t backdrop_fine;  // FF0DE0

    // Player.
    uint16_t dying;          // FF0DE2
    uint16_t health;         // FF0DE4
    uint16_t prev_health;    // FF0DE6
    uint16_t radar_x, radar_y; // FF0DE8
    uint16_t player_x, player_y; // FF0DEC: 8.8, 256 units per map cell
    uint16_t push_x, push_y; // FF0DF0: knock-back
    uint16_t dir_x, dir_y;   // FF0E00: view direction
    uint16_t player_angle;   // FF0E04: 512 per turn
    Cell player_cell;        // FF0E12
    Cell last_cell;          // FF0E16: kNoCell after a level start
    uint16_t view_z;         // FF0E1A: jump / crouch height
    uint16_t view_z_target;  // FF0E1C
    uint16_t backdrop_z;     // FF0E1E: view_z the backdrop was built for
    uint16_t jump_speed;     // FF0E20
    uint16_t backdrop_scroll; // FF0E22
    uint16_t crouch_time;    // FF0E24
    uint16_t bob;            // FF0E26
    uint16_t bob_turn;       // FF0E28: head bob for this frame
    uint16_t bob_lift;       // FF0E2A
    uint16_t bob_angle, bob_dir_x, bob_dir_y, bob_z; // FF0E2C: undone after drawing
    uint16_t turn, turn_target;     // FF0E7E
    uint16_t speed, speed_target;   // FF0E82
    uint16_t strafe, strafe_target; // FF0E86

    // Lighting.
    uint8_t wall_shade;      // FF0E8A: first of the 8 wall shade tables by height (render/shade.txt)
    uint8_t walls_shaded;    // FF0EAA: wall scaler table 0x4392 (shaded by height), else 0x4436
    uint8_t backdrop_image;  // FF0EAE: lighting mode of the render/backgrounds.txt rows
    uint16_t lighting;       // FF0EB2: 0-4
    uint16_t visibility;     // FF0EB4: cells
    uint16_t frame;          // FF0EB6: frame counter
    uint16_t radar_redraw;   // FF0EB8
    uint8_t gradient[0xA0];  // FF6226: the view's background, rows of the odd and even columns

    Door doors[16];          // FF0EC6
    uint8_t door_count;      // FF0EC2: end pointer
    uint16_t blood_count;    // FF0FF6
    Cell cur_map;            // FF1024: first cell of the level
    uint16_t cur_level;      // FF1028
    uint16_t map_level;      // FF102A: level shown on the map screen
    uint8_t repeat[4];       // FF102C: auto-repeat counters

    // Inventory.
    uint16_t character;      // FF1030
    uint16_t add_ammo;       // FF1034: ammo for AddWeapon, 0 = the weapon's default
    WeaponSlot slots[5];     // FF1036
    uint8_t slot_order[5];   // FF104A: selected slot first
    uint8_t slot_count;      // FF104F
    uint8_t weapon;          // FF1050: weapon in hand
    uint8_t next_weapon;     // FF1051
    uint8_t slot_blink[5];   // FF1052
    uint16_t lower;          // FF1058: weapon lowered while changing
    uint16_t fire_step;      // FF105A
    uint16_t fire_kind;      // FF105C
    uint16_t nv_slot;        // FF105E: night vision active (slot, or -1)
    uint16_t light_slot;     // FF1060
    uint16_t scanner_slot;   // FF1062
    uint16_t armour_slot;    // FF1064
    uint16_t shield_slot;    // FF1066
    int16_t lit_start, lit_end; // FF106A: columns lit by the light (depth buffer addresses)
    uint16_t hit_flash;      // FF1072

    // Sky.
    uint8_t sky_flash;       // FF1074
    uint8_t flash_x;         // FF1075
    uint8_t flash_count;     // FF1077
    uint8_t flash_time;      // FF1078
    uint8_t flash_lit;       // FF1079
    uint16_t flash_col;      // FF107A
    uint8_t bolt_on;         // FF107C
    uint8_t bolt_time;       // FF107D
    uint16_t bolt_x, bolt_y; // FF107E

    // Sprite scaler.
    uint16_t sprite_mirror;  // FF1088
    uint16_t sprite_scale;   // FF1094
    uint8_t sprite_shades;   // FF1096: kSpriteShades row

    uint16_t stair_height;   // FF116E: view height on stairs, -0x40..0x40
    uint16_t lift;           // FF1170: 1 down, -1 up, 2 / -2 arrived

    // Objects.
    Enemy player_obj;        // FF119C: the player as a target
    Enemy objects[64];       // FF11E8
    uint16_t object_count;   // FF24E8
    uint8_t object_list;     // FF24EA: first object as slot + 1, 0 none

    uint8_t blast_near;      // FF26F0
    uint8_t burning;         // FF26F1
    uint8_t spawn_template;  // FF26F2: kEnemyTemplates index
    uint16_t portrait_vram[5]; // FF26F6
    uint16_t menu_row;       // FF2704: options row / character select page
    uint16_t menu_value;     // FF2706: sound test / character select and story scroll

    // Sound.
    uint16_t last_sound;     // FF2708
    uint16_t fm_channel;     // FF270A
    int32_t fm_note[2];      // FF270C
    uint16_t speech_on;      // FF2716
    uint16_t dac_hold;       // FF2718: frames samples are held off

    // Level.
    uint16_t indoors;        // FF271A: no sky
    uint16_t cur_area;       // FF271C
    uint8_t backdrop_in;     // FF271E: backdrop render/panorama_in.png, else _out
    uint16_t level_exit;     // FF2722
    uint16_t exit_timer;     // FF2724
    uint8_t footstep;        // FF2726
    uint16_t pickup_check;   // FF2728
    uint16_t pw_level;       // FF272A: level the passwords have reached
    uint16_t cleared_timer;  // FF272C
    uint16_t enemies_left;   // FF272E
    uint16_t counter_shown;  // FF2730
    uint8_t area_password[10]; // FF2732
    uint8_t level_passwords[0x1B0]; // FF273C: 9 characters per level

    // Carried between areas and in passwords.
    uint8_t alive[5];        // FF28EC: characters still available
    uint8_t carry_weapon[5]; // FF28F1
    uint8_t carry_ammo[5];   // FF28F6
    uint8_t carry_health;    // FF28FB
    uint8_t level_id;        // FF28FC: bits 4-5 area, bits 0-3 level
    uint8_t start_level;     // FF28FD
    uint8_t pw_data[10];     // FF28FE
    uint8_t pw_bits[10];     // FF2908

    // The area, from its blob (load_area) and changed by the game.
    uint8_t class_cell[0x100]; // FF62C6: for each class, the first cell value of it
    uint16_t faces[0x800];   // FF63C6: wall faces, 8 words each (texture strip words)
    uint16_t cell_faces[0x400]; // FF73C6: 4 face numbers per cell value
    uint8_t cell_class[0x100]; // FF7BC6
    uint8_t level_light[16]; // FF7CC6: lighting of each level
    uint8_t maps[16][0x400]; // FF7CD6: 32x32 cell values per level
    uint8_t face_scripts[0x1000]; // FFBCD6: face animation records (u16 length, ...), 0 ends
    uint8_t off_map_cell;    // FFCCD6: written instead of cells outside the maps
};

extern State st;


} // namespace game
