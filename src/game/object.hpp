// The objects of the enemy list: enemies, their shots and bodies, blood,
// explosions and the weapons' projectiles. An object's behaviour is four
// handlers, which the original kept as code addresses.
#pragma once

#include <cstdint>

namespace game {

// AI (run every frame when the delay is out) and death handlers.
enum class Act : uint8_t {
    None,              // 0x13298
    Unlink,            // 0x13526
    BombAi,            // 0x13ADC
    ExplodeOnPlayer,   // 0x13E42
    ExplodeOnPlayerShielded, // 0x13EC0
    MineAi,            // 0x13F7C
    GrenadeAi,         // 0x14076
    ExplosionAi,       // 0x14196
    FireAi,            // 0x1423A
    FireBallAi,        // 0x142C6
    DieDropItem47,     // 0x14454
    DieDropItem47Keep, // 0x144FE
    DieDropItem46Counted, // 0x14A56
    DieDropItem46Keep, // 0x14B08
    BloodAi,           // 0x158CC
    RemoveBlood,       // 0x159EE
    SparkAi,           // 0x15AC4
    Type1Ai,           // 0x182FC
    Type1BodyAi,       // 0x186B8
    Type2Ai,           // 0x1887A
    Type9Ai,           // 0x18BC4
    Type5Ai,           // 0x19060
    Type6Ai,           // 0x19540
    BodyAiWide,        // 0x19958
    Type7Ai,           // 0x19A48
    Type8Ai,           // 0x1A324
    Type4Ai,           // 0x1A92A
    Type4BodyAi,       // 0x1AEB4
    Type7BodyAi,       // 0x19FD2, 0x1B6A2
    Type0Ai,           // 0x1B078
    Type3Start,        // 0x14C2A
    Type3Ai,           // 0x14C40
    EnemyShotAi,       // 0x15F6E
    BossCellAi,        // 0x15B1E
    BossCellBlastAi,   // 0x15DD0
    BossCellDeath,     // 0x15ED2
    BodyAiBadClamp,    // 0x18F5C, 0x1943C, 0x1A808
    BodyAi,            // 0x1B97A, 0x18AD0, 0x156D2
};

// Drawing handlers.
enum class Draw : uint8_t {
    None,              // 0x13298
    Bomb,              // 0x13F3E
    Mine,              // 0x1402E
    Grenade,           // 0x14150
    Explosion,         // 0x141E2
    Fire,              // 0x14272
    Blood,             // 0x159FE
    Spark,             // 0x15AD6
    EnemyShot,         // 0x16040
    Type1,             // 0x1856C
    Type1Body,         // 0x18660
    Type2,             // 0x18A66
    BodyFrame6,        // 0x18AC8
    Type9,             // 0x18EE8
    BodyWalk4,         // 0x18F50
    Type5,             // 0x193C8
    BodyWalk6,         // 0x19430, 0x1994C
    Type6,             // 0x198C4
    Type7,             // 0x19ECA
    BodyFrame4,        // 0x19FCA
    Type8,             // 0x1A722
    BodyWalk9,         // 0x1A7FC
    Type4,             // 0x1AD9A
    BodyFrame7,        // 0x1AE9E
    Type0,             // 0x1B562
    Type3,             // 0x1539E
    Type3Body,         // 0x15618
    BossCell,          // 0x15D84
    BossCellBlast,     // 0x15DFC
    BodyFrame11,       // 0x1B68C
    Stunned,           // 0x1B952
};

// Hit handlers: distance d0 and direction d3/d4 to the shooter.
enum class Hit : uint8_t {
    None,              // 0x13298
    Player,            // 0xD800
    Bomb,              // 0x13AD2
    Type6,             // 0x1999A
    Type7,             // 0x1A154
    Type8,             // 0x1A84A
    Type4,             // 0x1AFC6
    Type0,             // 0x1B824
    Type3Body,         // 0x1570C
    BossCell,          // 0x15E4E
    Type3,             // 0x1575C
    Walker,            // 0x18F9E, 0x1947E
    Body,              // 0x1901C, 0x194FC, 0x19A04, 0x1A8C8
    Type1,             // 0x187C8, 0x18B12
    BodyFlip,          // 0x1B9BC, 0x18832, 0x18B7C, 0x1A1CC, 0x1B030, 0x1B890
};

// Sprite sets (data/view/set_*.txt and .png): those of the enemy types, the
// second look of type 1, and the stunned enemy.
enum class SpriteSet : uint8_t {
    Type0, Type1, Type2, Type3, Type4, Type5, Type6, Type7, Type8, Type9, Type1Alt, Stunned,
};
// Their files without the extension (sprites.cpp).
extern const char* const kSpriteSetFiles[12];

// Who an object goes after (+6): an object slot, the player's object
// (FF119C), the player by its own position (address 0; also "no target" of
// 0x1642C) or nobody (0xFFFFFFFF).
constexpr uint8_t kPlayerTarget = 64, kPlayerPos = 65, kNoTarget = 0xFF;

// An object (0x4C bytes at FF11E8 + 0x4C * n in the original; FF119C held
// the player in the same shape). Offsets in the original are in comments.
struct Enemy {
    uint8_t next;      // +00: next object in the list as slot + 1, 0 at the end
    uint16_t flags;    // +04: 0 = free slot; 0x10 enemy, 0x04 counted, 0x40/0x80/0x08 hit at
                       //      view height / jumping / crouching, 0x20 kept off the map
    uint8_t target;    // +06
    Hit hit;           // +0A
    Draw draw;         // +0E
    Act ai;            // +12
    Act death;         // +16
    uint8_t step;      // +1E
    uint8_t delay;     // +1F: frames until the AI runs again
    uint16_t x, y, z;  // +20: 8.8 map position, height
    uint16_t vx, vy, vz; // +2A
    uint8_t stun;      // +30: killed by a near blast
    uint8_t level;     // +32
    uint8_t state;     // +34
    uint8_t count;     // +35
    uint16_t health;   // +36
    uint16_t dest_x, dest_y; // +38: where it walks to
    uint16_t walk;     // +3C: distance walked (walking frames)
    uint8_t id;        // +3E: slot + 1
    uint8_t timer;     // +41
    SpriteSet sprites; // +42
    uint8_t item_a, item_b; // +46: dropped when it dies
    uint16_t home_x, home_y; // +48
};

// What a spawned enemy starts with (0x26 byte structs at 0xAB0C, by type).
struct EnemyTemplate {
    Act ai;            // +00
    Draw draw;         // +04
    Hit hit;           // +08
    Act death;         // +0C
    uint16_t flags;    // +10
    uint16_t health;   // +12
    SpriteSet sprites; // +14
    uint8_t item_a, item_b; // +18
    uint16_t z;        // +1A
    uint16_t sound;    // +24
};
extern const EnemyTemplate kEnemyTemplates[10];

} // namespace game
