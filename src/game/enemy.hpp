// Enemy helpers shared by the enemy types (enemy.cpp). An enemy is a 0x4C
// byte struct at FF11E8 + 0x4C * n, linked from FF24EA; FF119C is the player.
#pragma once
#include "game.hpp"

namespace game {

struct EnemyMove {
    uint16_t d0, d1;
};

struct TargetResult {
    uint32_t a1;
    uint16_t d0;
};

uint32_t enemy_map(uint32_t a0); // 0x1657A
void sub_00D740(uint32_t a0, uint16_t id);
EnemyMove sub_0145AA(uint32_t a0, uint16_t d0, uint16_t d1);
EnemyMove sub_0146FE(uint32_t a0, uint16_t d0, uint16_t d1);
uint8_t sub_013D42(uint32_t a0, uint16_t d0, uint16_t d1);
uint16_t sub_013CBC(uint32_t a0, uint16_t d0, uint16_t d1);
uint8_t sub_013D48(uint16_t d0, uint16_t d1);
uint16_t sub_013C36(uint16_t d0, uint16_t d1);
uint8_t sub_00A8B6();
uint32_t sub_016388(uint32_t a0, uint16_t d5);
TargetResult sub_0167B0(uint16_t d1, uint16_t d2);
uint32_t sub_01642C(uint32_t a0);
uint32_t sub_01649C(uint32_t a0);
uint16_t sub_0165D8(uint32_t a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4);
uint16_t sub_0166E8(uint32_t a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4);
bool sub_016590(uint32_t a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4);
bool sub_0166A0(uint32_t a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4);
void h_014454(uint32_t a0);
void h_0144FE(uint32_t a0);
void h_014A56(uint32_t a0);
void h_014B08(uint32_t a0);
void sub_01BA04(uint32_t a0, uint16_t d0, uint16_t d2, int16_t col, uint16_t scale);
void loc_016084(uint32_t a0);
uint32_t loc_0160C4(uint32_t a0);
void loc_0160E8(uint32_t a0);
void loc_0161B2(uint32_t a0);
void slow_down(uint32_t a0);
EnemyMove knock_direction(uint16_t d3, uint16_t d4);
void set_knock(uint32_t a0, EnemyMove dir, int16_t strength);
void loc_01B8D8(uint32_t a0);
void h_01B952(uint32_t a0, uint16_t d5, uint16_t d1);
void h_01B97A(uint32_t a0);
void h_01B9BC(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4);
void sub_0157CA(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4);
void h_0158CC(uint32_t a0);
void h_0159EE(uint32_t a0);
void h_0159FE(uint32_t a0, uint16_t d5, uint16_t d1);
void h_016040(uint32_t a0, uint16_t d5, uint16_t d1);
void h_015AC4(uint32_t a0);
void h_015AD6(uint32_t a0, uint16_t d5, uint16_t d1);
void sub_00A6BC(uint16_t d0, uint16_t d1);
void sub_015F38(uint32_t a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4, uint16_t d5, uint16_t d6);
void h_015F6E(uint32_t a0);
void h_015B1E(uint32_t a0);
void h_015D84(uint32_t a0, uint16_t d5, uint16_t d1);
void h_015DD0(uint32_t a0);
void h_015DFC(uint32_t a0, uint16_t d5, uint16_t d1);
void h_015E4E(uint32_t a0);
void loc_015ED2(uint32_t a0);
void h_013AD2(uint32_t a0, uint16_t d0);
void h_013ADC(uint32_t a0);
void h_013E42(uint32_t a0);
void h_013EC0(uint32_t a0);
void h_013F3E(uint32_t a0, uint16_t d5, uint16_t d1);
void h_013F7C(uint32_t a0);
void h_01402E(uint32_t a0, uint16_t d5, uint16_t d1);
void h_014076(uint32_t a0);
void h_014150(uint32_t a0, uint16_t d5, uint16_t d1);
void h_014196(uint32_t a0);
void h_0141E2(uint32_t a0, uint16_t d5, uint16_t d1);
void h_01423A(uint32_t a0);
void h_014272(uint32_t a0, uint16_t d5, uint16_t d1);
void h_0142C6(uint32_t a0);

// player.cpp
void sub_00B1C4(uint32_t a0, uint16_t d0, uint16_t d1);
void sub_00B202(uint32_t a0, uint16_t d0, uint16_t d1);
void sub_00B130(uint32_t a0, uint16_t d0, uint16_t d1);
void sub_00B168(uint32_t a0, uint16_t d0, uint16_t d1);

// weapon.cpp
uint32_t sub_0108D6(uint32_t a2);
void sub_011C2C();
void sub_012A70();
void sub_012AD4();
void sub_00A5D4();

// enemy_types.cpp
void h_0182FC(uint32_t a0);
void h_01856C(uint32_t a0, uint16_t d5, uint16_t d1);
void h_018660(uint32_t a0, uint16_t d5, uint16_t d1);
void h_0186B8(uint32_t a0);
void h_0187C8(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4);
void h_01887A(uint32_t a0);
void h_018A66(uint32_t a0, uint16_t d5, uint16_t d1);
void body_frame(uint32_t a0, uint16_t anim, uint16_t d5, uint16_t d1);
void h_018BC4(uint32_t a0);
void h_018EE8(uint32_t a0, uint16_t d5, uint16_t d1);
void body_walk(uint32_t a0, uint16_t anim, uint16_t d5, uint16_t d1);
void h_019060(uint32_t a0);
void h_019540(uint32_t a0);
void h_019A48(uint32_t a0);
void h_01A324(uint32_t a0);
void h_01A92A(uint32_t a0);
void h_01B078(uint32_t a0);
void h_014C2A(uint32_t a0);
void h_014C40(uint32_t a0);
void h_01539E(uint32_t a0, uint16_t d5, uint16_t d1);
void h_015618(uint32_t a0, uint16_t d5, uint16_t d1);
void h_01570C(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4);
void h_01575C(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4);
void h_01B562(uint32_t a0, uint16_t d5, uint16_t d1);
void h_01B824(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4);
void h_01AD9A(uint32_t a0, uint16_t d5, uint16_t d1);
void h_01AEB4(uint32_t a0);
void h_01AFC6(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4);
void h_01A722(uint32_t a0, uint16_t d5, uint16_t d1);
void h_01A84A(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4);
void h_019ECA(uint32_t a0, uint16_t d5, uint16_t d1);
void h_019FD2(uint32_t a0);
void h_01A154(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4);
void h_0198C4(uint32_t a0, uint16_t d5, uint16_t d1);
void h_019958(uint32_t a0);
void h_01999A(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4);
void h_0193C8(uint32_t a0, uint16_t d5, uint16_t d1);
void h_018F5C(uint32_t a0);
void h_018F9E(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4);
void h_01901C(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4);

} // namespace game
