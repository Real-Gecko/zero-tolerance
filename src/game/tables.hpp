// Lookup tables of the game code (tables.cpp).
#pragma once

#include <cstdint>

namespace game {

// What using a cell does, by cell class (the handlers of table 0xE420).
enum class CellUse : uint8_t {
    None, DoorA, DoorB, Light0, Light1, Light2, Light3, Hazard, Pickup, Flash,
    LiftStop, LiftDown, LiftUp, LiftContinue, Exit,
};

// By cell class.
extern const uint8_t kEnemyBlocked[0x100];   // blocks patrolling enemies
extern const uint8_t kStopsShots[0x100];     // the player's cell stops enemy shots
extern const uint8_t kSpawnType[0x100];      // enemy type spawned by the cell, 0 none
extern const uint8_t kFloorKind[0x100];      // floor effect under the player (1-12), 0 none
extern const uint8_t kLevelVariant[0x100];   // class the cell becomes on a level, 0xFF none
extern const uint8_t kOwnSprite[0x100];      // the cell is drawn as a sprite of its own
extern const uint8_t kCollisionShape[0x100]; // 0-7
extern const CellUse kCellUse[0x83];         // classes 6-0x82 are used

// By weapon id.
extern const uint8_t kFireAnim[15];
extern const uint16_t kStartAmmo[15];
extern const uint16_t kMaxAmmo[15];
extern const int8_t kRecoil[5]; // by fire step 1-4
extern const uint16_t kStartEquipment[5][2][2]; // by character: weapon (0 none), ammo
extern const uint16_t kHandSprites[24][4]; // punch sprites y, size, tile, x by kind and step
extern const uint16_t kThrowSprites[5][4]; // weapon 7 throw sprites by step

extern const uint8_t kShotShift[16];    // explosion size shift by step
extern const uint8_t kBlastShift[6];    // boss cell blast size shift by count / 2
extern const uint8_t kBlinkFrame[16];   // type 1 body frame
extern const uint8_t kAnimPart[6][32];  // walk animation part by frame count - 1
extern const uint16_t kJump[16][2];     // hit push: jump speed, push strength
extern const uint16_t kFloorDigits[10]; // lift floor digit faces
extern const uint8_t kFaceAnim[0x40];   // HUD face tile offsets
extern const uint8_t kFootstep[16];     // footstep sound by bob phase
extern const uint16_t kBob[8][2];       // head bob lift, turn
extern const uint16_t kRadarBlinkA[8];  // radar player dot sprite tiles
extern const uint16_t kRadarBlinkB[8];
extern const uint16_t kSkySprites[0x14][6]; // skyline sprite rows, ending with a negative word
extern const uint16_t kSkyRows[0x2F];       // skyline sprite tiles per row of 16 lines
extern const uint16_t kFlashAttr[0x14];     // lightning flash sprite tiles
extern const uint16_t kBoltFrames[8][4];    // bolt sprite y, size, tile, x
extern const uint16_t kDirVectors[0x200][2]; // view direction x, y by angle
extern const uint16_t kRaySlopes[0x200][2];  // ray slopes by angle
extern const int16_t kRayAngles[0x81];       // angle step of each original ray

extern const uint16_t kLogoCycle[33]; // SEGA logo colour cycle
extern const uint16_t kMsgFont[96];   // HUD message font tiles, characters 0x20-0x7F
extern const uint16_t kFont16[96][2];    // 8x16 font: top and bottom tile, characters 0x20-0x7F
extern const uint16_t kMapFont16[96][2]; // the map screen's 8x16 font
extern const uint16_t kStoryFont[91][2]; // story text font: top and bottom tile, characters 0x20-0x7A

// GEMS sounds by id: type (0 music, 1 FM/PSG effect, 2-3 samples), then
// sequence and tempo, program and note, or sample and rate.
extern const uint8_t kSounds[0xA2][3];

// Title screen.
extern const uint16_t kTitleMenuSprites[4][4]; // START, OPTIONS: y, size, tile, x
extern const uint16_t kTitleSprites[16][4];    // background sprites
extern const uint16_t kLightColours[14][3];    // light sprite colours by level - 1
extern const uint8_t kLightLevels[0x40][5];    // light sprite levels by timer, 0 = off
extern const uint16_t kLightTimes[2][8];       // frames of each step of the two lights; the last lasts

// Passwords.
extern const char kPwAlphabet[0x41]; // the 64 characters by 6-bit value
extern const uint8_t kPwShuffle[0x38];
extern const uint8_t kValidLevels[0x40]; // levels a password may lead to

} // namespace game
