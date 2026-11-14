// Enemy helpers shared by the enemy types (enemy.cpp). The objects are
// st.objects (object.hpp); st.player_obj is the player as a target.
#pragma once
#include "game.hpp"

namespace game {

struct EnemyMove {
    uint16_t d0, d1;
};

struct TargetResult {
    Enemy* a1;
    uint16_t d0;
};

Cell enemy_map(const Enemy* a0); // 0x1657A
void EnemySound(Enemy* a0, uint16_t id);
EnemyMove MoveEnemyNarrow(Enemy* a0, uint16_t d0, uint16_t d1);
EnemyMove MoveEnemyWide(Enemy* a0, uint16_t d0, uint16_t d1);
uint8_t EnemyCollisionShape(Enemy* a0, uint16_t d0, uint16_t d1);
uint16_t EnemyMapShotPasses(Enemy* a0, uint16_t d0, uint16_t d1);
uint8_t MapCollisionShape(uint16_t d0, uint16_t d1);
uint16_t MapShotPasses(uint16_t d0, uint16_t d1);
uint8_t PlayerCellStopsShots();
uint8_t NearestEnemy(Enemy* a0, uint16_t d5);
TargetResult FindAimTarget(uint16_t d1, uint16_t d2);
Enemy* FindTarget(Enemy* a0);
Enemy* FindVisibleTarget(Enemy* a0);
uint16_t LineBlocked(Enemy* a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4);
uint16_t EnemyLineBlocked(Enemy* a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4);
bool EnemySightBlocked(Enemy* a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4);
void DieDropItem47(Enemy* a0);
void DieDropItem47Keep(Enemy* a0);
void DieDropItem46Counted(Enemy* a0);
void DieDropItem46Keep(Enemy* a0);
void DrawEnemySprite(Enemy* a0, uint16_t d0, uint16_t d2, int16_t col, uint16_t scale);
void TrackTarget(Enemy* a0);
void TrackVisibleTarget(Enemy* a0);
void TrackNearTarget(Enemy* a0);
void TrackNearTargetBadClamp(Enemy* a0);
void slow_down(Enemy* a0);
EnemyMove knock_direction(uint16_t d3, uint16_t d4);
void set_knock(Enemy* a0, EnemyMove dir, int16_t strength);
void StunEnemy(Enemy* a0);
void StunnedDraw(Enemy* a0, uint16_t d5, uint16_t d1);
void BodyAi(Enemy* a0);
void BodyHitFlip(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4);
void SpawnBlood(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4);
void BloodAi(Enemy* a0);
void RemoveBlood(Enemy* a0);
void BloodDraw(Enemy* a0, uint16_t d5, uint16_t d1);
void EnemyShotDraw(Enemy* a0, uint16_t d5, uint16_t d1);
void SparkAi(Enemy* a0);
void SparkDraw(Enemy* a0, uint16_t d5, uint16_t d1);
void BlastBreakCell(uint16_t d0, uint16_t d1);
void InitEnemyShot(Enemy* a0, uint16_t d0, uint16_t d1, uint16_t d3, uint16_t d4, uint16_t d5, uint16_t d6);
void EnemyShotAi(Enemy* a0);
void BossCellAi(Enemy* a0);
void BossCellDraw(Enemy* a0, uint16_t d5, uint16_t d1);
void BossCellBlastAi(Enemy* a0);
void BossCellBlastDraw(Enemy* a0, uint16_t d5, uint16_t d1);
void BossCellHit(Enemy* a0);
void BossCellDeath(Enemy* a0);
void BombHit(Enemy* a0, uint16_t d0);
void BombAi(Enemy* a0);
void ExplodeOnPlayer(Enemy* a0);
void ExplodeOnPlayerShielded(Enemy* a0);
void BombDraw(Enemy* a0, uint16_t d5, uint16_t d1);
void MineAi(Enemy* a0);
void MineDraw(Enemy* a0, uint16_t d5, uint16_t d1);
void GrenadeAi(Enemy* a0);
void GrenadeDraw(Enemy* a0, uint16_t d5, uint16_t d1);
void ExplosionAi(Enemy* a0);
void ExplosionDraw(Enemy* a0, uint16_t d5, uint16_t d1);
void FireAi(Enemy* a0);
void FireDraw(Enemy* a0, uint16_t d5, uint16_t d1);
void FireBallAi(Enemy* a0);

// player.cpp
void EnemyOpenDoorA(Cell a0, uint16_t d0, uint16_t d1);
void EnemyOpenDoorB(Cell a0, uint16_t d0, uint16_t d1);
void BossOpenDoorA(Cell a0, uint16_t d0, uint16_t d1);
void BossOpenDoorB(Cell a0, uint16_t d0, uint16_t d1);

// weapon.cpp
uint16_t InventorySprites(uint16_t a2);
void UpdateWeapon();
void FireWeapon();
void BulletHole();
void ShotBreakCell();

// enemy_types.cpp
void Type1Ai(Enemy* a0);
void Type1Draw(Enemy* a0, uint16_t d5, uint16_t d1);
void Type1BodyDraw(Enemy* a0, uint16_t d5, uint16_t d1);
void Type1BodyAi(Enemy* a0);
void Type1Hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4);
void Type2Ai(Enemy* a0);
void Type2Draw(Enemy* a0, uint16_t d5, uint16_t d1);
void body_frame(Enemy* a0, uint16_t anim, uint16_t d5, uint16_t d1);
void Type9Ai(Enemy* a0);
void Type9Draw(Enemy* a0, uint16_t d5, uint16_t d1);
void body_walk(Enemy* a0, uint16_t anim, uint16_t d5, uint16_t d1);
void Type5Ai(Enemy* a0);
void Type6Ai(Enemy* a0);
void Type7Ai(Enemy* a0);
void Type8Ai(Enemy* a0);
void Type4Ai(Enemy* a0);
void Type0Ai(Enemy* a0);
void Type3Start(Enemy* a0);
void Type3Ai(Enemy* a0);
void Type3Draw(Enemy* a0, uint16_t d5, uint16_t d1);
void Type3BodyDraw(Enemy* a0, uint16_t d5, uint16_t d1);
void Type3BodyHit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4);
void Type3Hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4);
void Type0Draw(Enemy* a0, uint16_t d5, uint16_t d1);
void Type0Hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4);
void Type4Draw(Enemy* a0, uint16_t d5, uint16_t d1);
void Type4BodyAi(Enemy* a0);
void Type4Hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4);
void Type8Draw(Enemy* a0, uint16_t d5, uint16_t d1);
void Type8Hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4);
void Type7Draw(Enemy* a0, uint16_t d5, uint16_t d1);
void Type7BodyAi(Enemy* a0);
void Type7Hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4);
void Type6Draw(Enemy* a0, uint16_t d5, uint16_t d1);
void BodyAiWide(Enemy* a0);
void Type6Hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4);
void Type5Draw(Enemy* a0, uint16_t d5, uint16_t d1);
void BodyAiBadClamp(Enemy* a0);
void WalkerHit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4);
void BodyHit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4);

} // namespace game
