// The AI, drawing and hit handlers of the enemy types (templates 0xAB0C,
// second state 0xAC88), in ROM order.
#include "enemy.hpp"
#include "m68k.hpp"
#include "render.hpp"

namespace game {

namespace {

int16_t s16(uint16_t v) { return int16_t(v); }


uint16_t clamp_step(uint16_t target, uint16_t pos, int16_t limit) {
    int16_t d = int16_t(s16(uint16_t(target - pos)) >> 3);
    if (d < -limit) return uint16_t(-limit);
    if (d > limit) return uint16_t(limit);
    return uint16_t(d);
}

} // namespace

// ---- Type 1 (0x182FC-0x18878): AI 0x182FC, draw 0x1856C, hit 0x187C8;
// dead 0x186B8 / 0x18660 / 0x18832.

namespace {

// 0x18334: picks the next spot near the target and how long to walk there.
void t1_decide(Enemy* a0) {
    if (s16(a0->health) < 0) {
        // 0x184E4: killed.
        EnemySound(a0, 0x2D);
        if (a0->stun) return StunEnemy(a0);
        a0->flags = a0->flags & 0xFF2F;
        a0->ai = Act::Type1BodyAi;
        a0->delay = 0;
        a0->hit = Hit::BodyFlip;
        a0->death = Act::DieDropItem47;
        a0->draw = Draw::Type1Body;
        a0->vx = 0;
        a0->vy = 0;
        a0->state = 3;
        CountEnemiesLeft();
        return;
    }
    Enemy* a3 = target_of(a0);
    uint16_t r = uint16_t(Random() >> 16) & 0x1F;
    uint16_t d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
    a0->count = uint8_t((s16(d) >> 6) + r);
    a0->state = 0;
    uint32_t r2 = uint32_t(int32_t(Random()) >> 4);
    a0->dest_x = uint16_t((r2 & 0x1FF) - 0x100 + a3->x + a3->vx);
    a0->dest_y = uint16_t(((r2 >> 16) & 0x1FF) - 0x100 + a3->y + a3->vy);
}

void t1_shot_sound(Enemy* a0) {
    EnemySound(a0, 0x68);
    EnemySound(a0, 0x97);
}

// 0x18452 (and 0x19CD6): fires at the target when it is in sight (else
// decides again); the chance to hit drops when the player crouches or in the
// dark.
void t1_fire(Enemy* a0, void (*decide)(Enemy*)) {
    Enemy* a3 = target_of(a0);
    if (EnemySightBlocked(a0, a3->x, a3->y, a0->x, a0->y)) return decide(a0);
    uint16_t chance = 0x400;
    bool sure = false;
    if (s16(st.view_z) >= 0) {
        if (!st.lighting) sure = true;
        chance = 0x300;
    } else {
        chance = 0x200;
        if (st.lighting) chance = 0x17B;
    }
    if (sure || ((int32_t(Random()) >> 8) & 0x3FF) < chance) {
        uint16_t dx = uint16_t(a0->x - a3->x), dy = uint16_t(a0->y - a3->y);
        uint16_t d = Distance(s16(dx), s16(dy));
        if (d >= 0x400) return;
        HitObject(a3, d, dx, dy);
    }
    t1_shot_sound(a0);
}

} // namespace

// 0x182FC: walks to a spot near the target (state 0), then aims and fires
// (state 1, shot at count 5); state 2 is the knock-back of a hit.
void Type1Ai(Enemy* a0) {
    a0->delay = 0;
    if (a0->state == 2) {
        if (Distance(s16(a0->vx), s16(a0->vy)) >= 10) {
            MoveEnemyNarrow(a0, a0->vx, a0->vy);
            a0->vx = uint16_t(s16(a0->vx) >> 1);
            a0->vy = uint16_t(s16(a0->vy) >> 1);
            return;
        }
        return t1_decide(a0);
    }
    uint16_t vx = clamp_step(a0->dest_x, a0->x, 0x28);
    uint16_t vy = clamp_step(a0->dest_y, a0->y, 0x28);
    a0->vx = vx;
    a0->vy = vy;
    MoveEnemyNarrow(a0, vx, vy);
    if (a0->state != 1) {
        a0->count = uint8_t(a0->count - 1);
        if (a0->count) return TrackTarget(a0);
        a0->state = 1;
        a0->count = 0xA;
    }
    a0->count = uint8_t(a0->count - 1);
    uint8_t t = a0->count;
    if (t == 0) return t1_decide(a0);
    if (t == 5) return t1_fire(a0, t1_decide);
    if (t == 4 || t == 3) t1_shot_sound(a0);
}

// 0x1856C
void Type1Draw(Enemy* a0, uint16_t d5, uint16_t d1) {
    uint8_t state = a0->state, t = a0->count;
    uint16_t anim = 0;
    if (state == 2)
        anim = s16(a0->health) < 0 ? 2 : 1;
    else if (state == 1) {
        uint16_t base = target_of(a0) == player_struct ? 5 : 0xA;
        if (t == 9 || t == 1) anim = base;
        else if (t == 8 || t == 2) anim = uint16_t(base + 1);
        else if (t == 6 || t == 4) anim = uint16_t(base + 3);
        else anim = uint16_t(base + 2);
    }
    DrawEnemySprite(a0, anim, 0xFFFF, s16(d1), d5);
}

// 0x18660: the body, blinking while the template's sprite set is in use.
void Type1BodyDraw(Enemy* a0, uint16_t d5, uint16_t d1) {
    uint16_t d2 = 0;
    if (a0->sprites == SpriteSet::Type1) d2 = kBlinkFrame[uint8_t(uint8_t(st.frame) + a0->id) & 0xF];
    if (a0->count & 1) d2 = uint16_t(d2 + 1);
    DrawEnemySprite(a0, 4, d2, s16(d1), d5);
}

// 0x186B8: the body slides to a stop; the player walking over it picks up
// its gun (weapon 8 with 2-3 clips).
void Type1BodyAi(Enemy* a0) {
    a0->delay = 0;
    slow_down(a0);
    Enemy* a3 = target_of(a0);
    uint16_t d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
    if (d >= 0x60) return TrackNearTarget(a0);
    if (a0->state != 3) return;
    uint16_t clips = uint16_t((((s16(uint16_t(Random())) >> 8) & 1) + 2) << 8);
    if (a3 == player_struct) {
        st.add_ammo = clips;
        uint16_t slot = AddWeapon(8);
        st.add_ammo = 0;
        if (slot == 0xFFFF) return;
        a0->state = 4;
        EnemySound(a0, 0x69);
        return QueueMessage(Msg::Handgun);
    }
    a0->state = 4; // the other player of a link game took it
}

// 0x187C8 (and 0x1999A with state 3): a hit: damage 0x400 - d0 from +0x36,
// knock-back, blood.
void knock_hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4, uint8_t state, bool mark41 = false) {
    a0->state = state;
    a0->count = 0;
    a0->vx = 0;
    a0->vy = 0;
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    if (s16(a0->health) >= 0) {
        a0->health = uint16_t(a0->health - f);
        if (s16(a0->health) >= 0) {
            if (mark41 && a0->timer) a0->timer = 2;
        } else if (uint16_t(f) >= 0x338) {
            SpawnBlood(a0, uint16_t(f), dir.d0, dir.d1);
            a0->stun = st.blast_near;
        }
    }
    set_knock(a0, dir, int16_t(f >> 3));
}

void Type1Hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4) { knock_hit(a0, d0, d3, d4, 2); } // 0x187C8

// ---- Type 2 (0x1887A-0x18BC2): AI 0x1887A, draw 0x18A66, hit 0x18B12 (=
// 0x187C8); dead 0x18AD0 (= 0x1B97A) / 0x18AC8 / 0x18B7C (= 0x1B9BC).

namespace {

// 0x188B2
void t2_decide(Enemy* a0) {
    if (s16(a0->health) < 0) {
        // 0x189E4: killed.
        EnemySound(a0, 0x23);
        if (a0->stun) return StunEnemy(a0);
        a0->flags = a0->flags & 0xFF2F;
        a0->ai = Act::BodyAi;
        a0->delay = 0;
        a0->hit = Hit::BodyFlip;
        a0->death = Act::DieDropItem47;
        a0->draw = Draw::BodyFrame6;
        a0->vx = 0;
        a0->vy = 0;
        CountEnemiesLeft();
        return;
    }
    Enemy* a3 = target_of(a0);
    uint16_t r = uint16_t(Random() >> 16) & 0xF;
    uint16_t d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
    a0->count = uint8_t((s16(d) >> 6) + r);
    a0->state = 0;
    uint32_t r2 = uint32_t(int32_t(Random()) >> 4);
    a0->dest_x = uint16_t((r2 & 0xFF) - 0x80 + a3->x);
    a0->dest_y = uint16_t(((r2 >> 16) & 0xFF) - 0x80 + a3->y);
}

uint16_t clamp_step4(uint16_t target, uint16_t pos, int16_t limit) {
    int16_t d = int16_t(s16(uint16_t(target - pos)) >> 2);
    if (d < -limit) return uint16_t(-limit);
    if (d > limit) return uint16_t(limit);
    return uint16_t(d);
}

// Knock-back state 2 shared by the walking types: slides while fast,
// then decides again.
bool knocked(Enemy* a0) {
    if (a0->state != 2) return false;
    if (Distance(s16(a0->vx), s16(a0->vy)) >= 10) {
        MoveEnemyNarrow(a0, a0->vx, a0->vy);
        a0->vx = uint16_t(s16(a0->vx) >> 1);
        a0->vy = uint16_t(s16(a0->vy) >> 1);
        return true;
    }
    return false;
}

} // namespace

// 0x1887A: walks to a spot near the target, then strikes it within 0x64
// (state 1, at count 5).
void Type2Ai(Enemy* a0) {
    a0->delay = 0;
    if (a0->state == 2) {
        if (knocked(a0)) return;
        return t2_decide(a0);
    }
    uint16_t vx = clamp_step4(a0->dest_x, a0->x, 0x1E);
    uint16_t vy = clamp_step4(a0->dest_y, a0->y, 0x1E);
    a0->vx = vx;
    a0->vy = vy;
    MoveEnemyNarrow(a0, vx, vy);
    if (a0->state != 1) {
        a0->count = uint8_t(a0->count - 1);
        if (a0->count) return TrackTarget(a0);
        a0->state = 1;
        a0->count = 0xA;
    }
    a0->count = uint8_t(a0->count - 1);
    uint8_t t = a0->count;
    if (t == 0) return t2_decide(a0);
    if (t != 5) return;
    Enemy* a3 = target_of(a0);
    uint16_t dx = uint16_t(a0->x - a3->x), dy = uint16_t(a0->y - a3->y);
    if (Distance(s16(dx), s16(dy)) >= 0x64) return;
    HitObject(a3, 0x100, dx, dy);
    EnemySound(a0, 0x94);
}

// 0x18A66
void Type2Draw(Enemy* a0, uint16_t d5, uint16_t d1) {
    uint16_t anim = 0;
    if (a0->state == 2)
        anim = s16(a0->health) < 0 ? 4 : 3;
    else if (a0->state == 1) {
        uint8_t t = a0->count;
        anim = t < 7 && t > 3 ? 2 : 1;
    }
    DrawEnemySprite(a0, anim, 0xFFFF, s16(d1), d5);
}

// 0x1866E with d0 set (0x18AC8 and the other bodies): animation d0, frame
// 1 on odd hit counts.
void body_frame(Enemy* a0, uint16_t anim, uint16_t d5, uint16_t d1) {
    DrawEnemySprite(a0, anim, a0->count & 1, s16(d1), d5);
}

// ---- Types 9 (0x18BC4-0x1905E) and 5 (0x19060-0x1953E): they walk through
// doors, opening them. Type 9 (AI 0x18BC4, draw 0x18EE8, hit 0x18F9E; dead
// 0x18F5C / 0x18F50 / 0x1901C) shoots; type 5 (AI 0x19060, draw 0x193C8,
// hit 0x1947E = 0x18F9E; dead 0x1943C = 0x18F5C / 0x19430 / 0x194FC =
// 0x1901C) throws a shot object (0x15F6E).

namespace {

struct Walker {
    int16_t limit;            // step clamp
    bool state_before_target; // type 9 clears the state before checking the target
    uint8_t fire_lo, fire_hi; // counts that fire
    void (*fire)(Enemy* a0, Enemy* a3);
    uint16_t kill_sound;
    Act dead_ai;
    Hit dead_hit;
    Draw dead_draw;
};

// 0x18C3C / 0x190D8
void walker_new_spot(Enemy* a0, const Walker& w) {
    if (s16(a0->health) < 0) {
        // 0x18E5C / 0x1933C: killed.
        st.exit_timer = 0xF;
        a0->flags = a0->flags & 0xFF2F;
        a0->ai = w.dead_ai;
        a0->delay = 0;
        a0->hit = w.dead_hit;
        a0->death = Act::DieDropItem47Keep;
        a0->draw = w.dead_draw;
        a0->vx = 0;
        a0->vy = 0;
        a0->state = 3;
        CountEnemiesLeft();
        EnemySound(a0, w.kill_sound);
        return;
    }
    if (w.state_before_target) a0->state = 0;
    Enemy* a3 = target_of(a0);
    if (a0->target == kPlayerPos) {
        TrackVisibleTarget(a0);
        return;
    }
    a0->state = 0;
    uint32_t r = uint32_t(int32_t(Random()) >> 4);
    a0->dest_x = uint16_t((r & 0x1FF) - 0x100 + a3->x - a3->vx);
    a0->dest_y = uint16_t(((r >> 16) & 0x1FF) - 0x100 + a3->y - a3->vy);
}

// 0x18C02 / 0x1909E
void walker_decide(Enemy* a0, const Walker& w) {
    Enemy* a3 = target_of(a0);
    if (a0->target == kPlayerPos) {
        TrackVisibleTarget(a0);
        return;
    }
    uint16_t r = uint16_t(Random() >> 16) & 0xF;
    uint16_t d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
    a0->count = uint8_t((s16(d) >> 7) + r);
    walker_new_spot(a0, w);
}

void walker(Enemy* a0, const Walker& w) {
    a0->delay = 0;
    if (a0->state == 2) {
        uint16_t d = Distance(s16(a0->vx), s16(a0->vy));
        a0->count = 1;
        if (d < 10) return walker_new_spot(a0, w);
        MoveEnemyWide(a0, a0->vx, a0->vy);
        a0->vx = uint16_t(s16(a0->vx) >> 1);
        a0->vy = uint16_t(s16(a0->vy) >> 1);
        return;
    }
    uint16_t vx = clamp_step(a0->dest_x, a0->x, w.limit);
    uint16_t vy = clamp_step(a0->dest_y, a0->y, w.limit);
    a0->vx = vx;
    a0->vy = vy;
    uint16_t px = uint16_t((vx << 3) + a0->x), py = uint16_t((vy << 3) + a0->y);
    uint8_t shape = EnemyCollisionShape(a0, px, py);
    if (shape == 6 || shape == 7) {
        a0->x = px;
        a0->y = py;
        uint16_t cx = px & 0xFF00, cy = py & 0xFF00;
        Cell cell = enemy_map(a0) + int16_t(s16(cx) >> 8) + int16_t(s16(cy) >> 3);
        if (shape == 6)
            EnemyOpenDoorA(cell, cx, cy);
        else
            EnemyOpenDoorB(cell, cx, cy);
        EnemySound(a0, 0x67);
    }
    vx = uint16_t(a0->vx - (s16(a0->vx) >> 2));
    vy = uint16_t(a0->vy - (s16(a0->vy) >> 2));
    MoveEnemyWide(a0, vx, vy);
    if (a0->state != 1) {
        a0->count = uint8_t(a0->count - 1);
        if (a0->count) {
            TrackVisibleTarget(a0);
            return;
        }
        a0->state = 1;
        a0->count = 0xA;
    }
    a0->count = uint8_t(a0->count - 1);
    uint8_t t = a0->count;
    if (t == 0) return walker_decide(a0, w);
    if (t < w.fire_lo || t > w.fire_hi) return;
    Enemy* a3 = target_of(a0);
    if (a0->target == kPlayerPos) {
        TrackVisibleTarget(a0);
        return;
    }
    if (EnemySightBlocked(a0, a3->x, a3->y, a0->x, a0->y)) return walker_decide(a0, w);
    w.fire(a0, a3);
}

// 0x18DEC: shoots like type 1 with damage 0x1F4.
void t9_fire(Enemy* a0, Enemy* a3) {
    uint16_t chance = 0x400;
    bool sure = false;
    if (s16(st.view_z) >= 0) {
        if (!st.lighting) sure = true;
        chance = 0x300;
    } else {
        chance = 0x200;
        if (st.lighting) chance = 0x17B;
    }
    if (sure || ((int32_t(Random()) >> 8) & 0x3FF) < chance) {
        uint16_t dx = uint16_t(a0->x - a3->x), dy = uint16_t(a0->y - a3->y);
        if (Distance(s16(dx), s16(dy)) >= 0x400) return;
        HitObject(a3, 0x1F4, dx, dy);
    }
    EnemySound(a0, 0x1E);
}

// 0x19280: throws a 0x15F6E shot at the target, 1/64 of the distance per
// frame.
void t5_fire(Enemy* a2, Enemy* a3) {
    Enemy* a0 = AllocEnemy();
    if (!a0) return;
    uint16_t r = uint16_t(Random() >> 16);
    uint16_t d3 = uint16_t(a3->x - a2->x), d4 = uint16_t(a3->y - a2->y);
    uint16_t d = uint16_t((s16(Distance(s16(d3), s16(d4))) >> 6) + 1);
    if (d) {
        d3 = uint16_t(divs(ext_l(d3), d));
        d4 = uint16_t(divs(ext_l(d4), d));
    }
    InitEnemyShot(a0, uint16_t((s16(d3) >> 1) + a2->x), uint16_t((s16(d4) >> 1) + a2->y), d3, d4, 8,
               uint16_t((r & 7) - 4));
    EnemySound(a0, 0x96);
}

const Walker type9{0x10, true, 4, 6, t9_fire, 0x31, Act::BodyAiBadClamp, Hit::Body, Draw::BodyWalk4};
const Walker type5{0xC, false, 5, 5, t5_fire, 0x29, Act::BodyAiBadClamp, Hit::Body, Draw::BodyWalk6};

} // namespace

void Type9Ai(Enemy* a0) { walker(a0, type9); } // 0x18BC4
void Type5Ai(Enemy* a0) { walker(a0, type5); } // 0x19060

// 0x18EE8 / 0x193C8: animations for knocked back (alive, dead), firing
// (counts 4-6) and walking.
void walker_draw(Enemy* a0, uint16_t d5, uint16_t d1, const uint16_t anim[5]) {
    uint16_t a = anim[4];
    if (a0->state == 2)
        a = s16(a0->health) < 0 ? anim[1] : anim[0];
    else if (a0->state == 1) {
        uint8_t t = a0->count;
        a = t >= 4 && t <= 6 ? anim[2] : anim[3];
    }
    DrawEnemySprite(a0, a, 0xFFFF, s16(d1), d5);
}

// 0x18EE8
void Type9Draw(Enemy* a0, uint16_t d5, uint16_t d1) {
    static const uint16_t anim[5] = {1, 2, 6, 5, 0};
    walker_draw(a0, d5, d1, anim);
}

// 0x193C8
void Type5Draw(Enemy* a0, uint16_t d5, uint16_t d1) {
    static const uint16_t anim[5] = {3, 4, 2, 1, 0};
    walker_draw(a0, d5, d1, anim);
}

// 0x18F50 / 0x19430 and copies: the body, walking frames.
void body_walk(Enemy* a0, uint16_t anim, uint16_t d5, uint16_t d1) { DrawEnemySprite(a0, anim, 0xFFFF, s16(d1), d5); }

// 0x18F5C (and its copies)
void BodyAiBadClamp(Enemy* a0) {
    a0->delay = 0;
    slow_down(a0);
    TrackNearTargetBadClamp(a0);
}

// 0x18F9E (and 0x1947E, 0x1A84A with state 4): hits count only while walking after the first
// steps; a kill sprays two rounds of blood.
void walker_hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4, uint8_t state) {
    if (a0->state != 0 || a0->count < 2) return;
    a0->state = state;
    a0->count = 0;
    a0->vx = 0;
    a0->vy = 0;
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    if (s16(a0->health) >= 0) {
        a0->health = uint16_t(a0->health - f);
        if (s16(a0->health) < 0) {
            SpawnBlood(a0, 0x400, dir.d0, dir.d1);
            SpawnBlood(a0, 0x400, dir.d0, dir.d1);
        }
    }
    set_knock(a0, dir, int16_t(f >> 3));
}

void WalkerHit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4) { walker_hit(a0, d0, d3, d4, 2); } // 0x18F9E

// 0x1901C (and its copies): pushed away by a hit.
void BodyHit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    a0->vx = 0;
    a0->vy = 0;
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    set_knock(a0, dir, int16_t(f >> 4));
}

// ---- Type 6 (0x19540-0x19A46): AI 0x19540, draw 0x198C4, hit 0x1999A;
// dead 0x19958 / 0x1994C / 0x19A04 (= 0x1901C). Wanders; within 0x300 of
// the target it charges (state 1), strikes on arrival and runs back to where
// it started (state 2). State 3 is the knock-back of a hit.

namespace {

uint16_t clamp_shift(uint16_t target, uint16_t pos, int shift, int16_t limit) {
    int16_t d = int16_t(s16(uint16_t(target - pos)) >> shift);
    if (d < -limit) return uint16_t(-limit);
    if (d > limit) return uint16_t(limit);
    return uint16_t(d);
}

uint16_t move_dist(EnemyMove m) { return Distance(s16(m.d0), s16(m.d1)); }

// 0x197C0: a new spot up to 0x200 away, or death.
void t6_wander(Enemy* a0) {
    if (s16(a0->health) >= 0) {
        uint32_t r = uint32_t(int32_t(Random()) >> 4);
        a0->dest_x = uint16_t((r & 0x3FF) - 0x200 + a0->x);
        a0->dest_y = uint16_t(((r >> 16) & 0x3FF) - 0x200 + a0->y);
        a0->state = 0;
        return;
    }
    // 0x197FA: killed.
    EnemySound(a0, 0x2B);
    if (a0->stun) return StunEnemy(a0);
    a0->flags = a0->flags & 0xFF2F;
    a0->ai = Act::BodyAiWide;
    a0->delay = 0;
    a0->hit = Hit::Body;
    a0->death = Act::DieDropItem47;
    a0->draw = Draw::BodyWalk6;
    a0->vx = 0;
    a0->vy = 0;
    a0->state = 4;
    CountEnemiesLeft();
}

// 0x19616: the charge ends: strikes the target within 0x80 (0x100 on
// diagonal cells) and turns back.
void t6_strike(Enemy* a0) {
    a0->state = 2;
    a0->dest_x = a0->home_x;
    a0->dest_y = a0->home_y;
    Enemy* a3 = target_of(a0);
    uint16_t d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
    uint8_t cls = st.cell_class[map_cell(enemy_map(a3) + int16_t(s16(a3->x) >> 8) +
                                       int16_t(s16(uint16_t(a3->y & 0xFF00)) >> 3))];
    if (cls >= 2 && cls <= 5) d = uint16_t(s16(d) >> 1);
    if (d >= 0x80) return;
    uint16_t d3 = uint16_t(s16(uint16_t(a0->home_x - a3->x)) >> 1);
    uint16_t d4 = uint16_t(s16(uint16_t(a0->home_y - a3->y)) >> 1);
    HitObject(a3, 0x28A, d3, d4);
    if (a3 == player_struct) {
        st.push_x = uint16_t(st.push_x << 1);
        st.push_y = uint16_t(st.push_y << 1);
    }
    EnemySound(a0, 0x1B);
}

} // namespace

// 0x19540
void Type6Ai(Enemy* a0) {
    a0->delay = 0;
    uint8_t state = a0->state;
    if (state == 3) {
        a0->count = 0;
        if (Distance(s16(a0->vx), s16(a0->vy)) < 10) return t6_wander(a0);
        MoveEnemyWide(a0, a0->vx, a0->vy);
        a0->vx = uint16_t(s16(a0->vx) >> 1);
        a0->vy = uint16_t(s16(a0->vy) >> 1);
        return;
    }
    if (state == 0) {
        if (!a0->count) {
            Enemy* a3 = target_of(a0);
            uint16_t d = Distance(s16(uint16_t(a3->x - a0->x)), s16(uint16_t(a3->y - a0->y)));
            if (d < 0x300) {
                // 0x19882: charge.
                a0->state = 1;
                d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
                a0->count = uint8_t((s16(d) >> 6) + 6);
                a0->home_x = a0->x;
                a0->home_y = a0->y;
                a0->dest_x = a3->x;
                a0->dest_y = a3->y;
                return;
            }
        } else
            a0->count = uint8_t(a0->count - 1);
        uint16_t vx = clamp_shift(a0->dest_x, a0->x, 3, 0x14);
        uint16_t vy = clamp_shift(a0->dest_y, a0->y, 3, 0x14);
        a0->vx = vx;
        a0->vy = vy;
        if (move_dist(MoveEnemyWide(a0, vx, vy)) <= 10) return t6_wander(a0);
        return TrackTarget(a0);
    }
    if (state == 2) {
        uint16_t vx = clamp_shift(a0->dest_x, a0->x, 2, 0x48);
        uint16_t vy = clamp_shift(a0->dest_y, a0->y, 3, 0x48);
        a0->vx = vx;
        a0->vy = vy;
        uint16_t d = move_dist(MoveEnemyWide(a0, vx, vy));
        a0->count = 0x14;
        if (d <= 10) return t6_wander(a0);
        return TrackTarget(a0);
    }
    a0->count = uint8_t(a0->count - 1);
    if (!a0->count) return t6_strike(a0);
    if (!(a0->count & 3)) {
        Enemy* a3 = target_of(a0);
        a0->dest_x = a3->x;
        a0->dest_y = a3->y;
    }
    uint16_t vx = clamp_shift(a0->dest_x, a0->x, 1, 0x48);
    uint16_t vy = clamp_shift(a0->dest_y, a0->y, 1, 0x48);
    a0->vx = vx;
    a0->vy = vy;
    if (move_dist(MoveEnemyWide(a0, vx, vy)) <= 10) return t6_strike(a0);
    TrackTarget(a0);
}

// 0x198C4
void Type6Draw(Enemy* a0, uint16_t d5, uint16_t d1) {
    uint16_t anim = 0;
    uint8_t state = a0->state;
    if (state == 3)
        anim = s16(a0->health) < 0 ? 4 : 3;
    else if (state == 1 && target_of(a0) == player_struct) {
        Enemy* a3 = player_struct;
        anim = 2;
        if (a0->count >= 6 &&
            Distance(s16(uint16_t(a3->x - a0->x)), s16(uint16_t(a3->y - a0->y))) >= 0x100)
            anim = 1;
    }
    DrawEnemySprite(a0, anim, 0xFFFF, s16(d1), d5);
}

// 0x19958 (and its copies): like 0x1B97A, sliding with the wide margin.
void BodyAiWide(Enemy* a0) {
    a0->delay = 0;
    auto half = [](uint16_t v) { return uint16_t(s16(v) < 0 ? -(-s16(v) >> 1) : s16(v) >> 1); };
    uint16_t vx = half(a0->vx), vy = half(a0->vy);
    a0->vx = vx;
    a0->vy = vy;
    if (vx | vy) MoveEnemyWide(a0, vx, vy);
    TrackNearTarget(a0);
}

void Type6Hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4) { knock_hit(a0, d0, d3, d4, 3); } // 0x1999A

// ---- Types 7 (0x19A48-0x1A212) and 0 (0x1B078-0x1B8D6): soldiers that
// flank the target, open doors, shoot and sometimes throw a bomb (state 5).
// Type 7: AI 0x19A48, draw 0x19ECA, hit 0x1A154; dead 0x19FD2 / 0x19FCA /
// 0x1A1CC (= 0x1B9BC). Type 0: AI 0x1B078, draw 0x1B562, hit 0x1B824; dead
// 0x1B6A2 (= 0x19FD2) / 0x1B68C / 0x1B890 (= 0x1B9BC); now and then it stops
// (state 6) and turns into a type 7.

namespace {

struct Soldier {
    bool far_check;      // type 7 heads straight for a target 0x400 away
    uint16_t spot_mask;  // random part of the count
    int side_shift;      // the flanking offset is the target's speed >> this
    uint16_t throw_mask; // a bomb when Random & this is 0
    uint16_t kill_sound;
    Act dead_ai;
    Hit dead_hit;
    Draw dead_draw;
    void (*decide)(Enemy* a0);
};

// 0x19A80 / 0x1B0B0
void soldier_decide(Enemy* a0, const Soldier& w) {
    if (s16(a0->health) < 0) {
        // 0x19E42 / 0x1B492: killed.
        EnemySound(a0, w.kill_sound);
        if (a0->stun) return StunEnemy(a0);
        a0->flags = a0->flags & 0xFF2F;
        a0->ai = w.dead_ai;
        a0->delay = 0;
        a0->hit = w.dead_hit;
        a0->death = Act::DieDropItem47;
        a0->draw = w.dead_draw;
        a0->vx = 0;
        a0->vy = 0;
        a0->state = 3;
        CountEnemiesLeft();
        return;
    }
    Enemy* a3 = target_of(a0);
    uint16_t d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
    if (w.far_check && d >= 0x400) {
        a0->dest_x = a3->x;
        a0->dest_y = a3->y;
        a0->count = 0xF;
        a0->state = 0;
        return;
    }
    uint16_t r = uint16_t(Random() >> 16) & w.spot_mask;
    a0->count = uint8_t((s16(d) >> 6) + r);
    a0->state = 0;
    uint32_t r2 = Random();
    // A spot beside the target's path, on the side picked by the slot number.
    uint16_t x = uint16_t(-(s16(a3->vy) >> w.side_shift)), y = uint16_t(s16(a3->vx) >> w.side_shift);
    if (a0->id & 1) {
        x = uint16_t(-x);
        y = uint16_t(-y);
    }
    x = uint16_t(x + a3->x);
    y = uint16_t(y + a3->y);
    if (!w.side_shift) {
        x = uint16_t(x + (s16(a3->vx) >> 1));
        y = uint16_t(y + (s16(a3->vy) >> 1));
    }
    r2 = uint32_t(int32_t(r2) >> 4);
    a0->dest_x = uint16_t(x + (r2 & 0x1FF) - 0x100);
    a0->dest_y = uint16_t(y + ((r2 >> 16) & 0x1FF) - 0x100);
}

// 0x19D68 / 0x1B3B8: throws a 0x13ADC bomb at count 4.
void soldier_throw(Enemy* a0, const Soldier& w) {
    a0->count = uint8_t(a0->count - 1);
    if (!a0->count) return w.decide(a0);
    if (a0->count != 4) return;
    EnemySound(a0, 0x1B);
    Enemy* e = AllocEnemy();
    if (!e) return;
    e->delay = 0;
    e->ai = Act::BombAi;
    e->draw = Draw::Bomb;
    e->hit = Hit::Bomb;
    e->flags = e->flags | 0xC8;
    e->state = 0x28;
    Enemy* a2 = target_of(a0);
    uint16_t vx = uint16_t(s16(uint16_t(a2->x - a0->x)) >> 4);
    e->vx = vx;
    e->x = uint16_t((s16(vx) >> 1) + a0->x);
    uint16_t vy = uint16_t(s16(uint16_t(a2->y - a0->y)) >> 4);
    e->vy = vy;
    e->y = uint16_t((s16(vy) >> 1) + a0->y);
    e->z = 0xFFFA;
    e->vz = 8;
}

// 0x19CA0 / 0x1B2F0: aims and fires (shot at count 6).
void soldier_aim(Enemy* a0, const Soldier& w) {
    a0->count = uint8_t(a0->count - 1);
    uint8_t t = a0->count;
    if (t == 0) return w.decide(a0);
    if (t == 6) return t1_fire(a0, w.decide);
    if (t == 5 || t == 4) t1_shot_sound(a0);
}

void t7_decide(Enemy* a0);
void t0_decide(Enemy* a0);
const Soldier type7{true, 0xF, 0, 0xF, 0x21, Act::Type7BodyAi, Hit::BodyFlip, Draw::BodyFrame4, t7_decide};
const Soldier type0{false, 0x1F, 1, 7, 0x1F, Act::Type7BodyAi, Hit::BodyFlip, Draw::BodyFrame11, t0_decide};
void t7_decide(Enemy* a0) { soldier_decide(a0, type7); }
void t0_decide(Enemy* a0) { soldier_decide(a0, type0); }

void soldier(Enemy* a0, const Soldier& w) {
    uint16_t vx = clamp_shift(a0->dest_x, a0->x, 3, 0x2D);
    uint16_t vy = clamp_shift(a0->dest_y, a0->y, 3, 0x2D);
    a0->vx = vx;
    a0->vy = vy;
    uint16_t px = uint16_t(vx + a0->x), py = uint16_t(vy + a0->y);
    uint8_t shape = EnemyCollisionShape(a0, px, py);
    if (shape == 6 || shape == 7) {
        a0->x = px;
        a0->y = py;
        uint16_t cx = px & 0xFF00, cy = py & 0xFF00;
        Cell cell = enemy_map(a0) + int16_t(s16(cx) >> 8) + int16_t(s16(cy) >> 3);
        if (shape == 6)
            EnemyOpenDoorA(cell, cx, cy);
        else
            EnemyOpenDoorB(cell, cx, cy);
        EnemySound(a0, 0x67);
    }
    MoveEnemyNarrow(a0, a0->vx, a0->vy);
    if (a0->state == 1) return soldier_aim(a0, w);
    if (a0->state == 5) return soldier_throw(a0, w);
    a0->count = uint8_t(a0->count - 1);
    if (a0->count) return TrackTarget(a0);
    a0->state = 1;
    a0->count = 0xA;
    if (uint16_t(Random() >> 16) & w.throw_mask) return soldier_aim(a0, w);
    Enemy* a3 = target_of(a0);
    uint16_t d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
    if (d < 0x100 || EnemySightBlocked(a0, a3->x, a3->y, a0->x, a0->y)) return soldier_aim(a0, w);
    a0->state = 5;
    soldier_throw(a0, w);
}

// Knock-back state 2 of the soldiers.
bool soldier_knocked(Enemy* a0, const Soldier& w) {
    if (a0->state != 2) return false;
    if (Distance(s16(a0->vx), s16(a0->vy)) < 10)
        w.decide(a0);
    else {
        MoveEnemyNarrow(a0, a0->vx, a0->vy);
        a0->vx = uint16_t(s16(a0->vx) >> 1);
        a0->vy = uint16_t(s16(a0->vy) >> 1);
    }
    return true;
}

// 0x1B51A: type 0 standing (state 6); at count 9 it becomes a type 7.
void t0_stand(Enemy* a0) {
    a0->count = uint8_t(a0->count + 1);
    if (a0->count != 9) return;
    a0->ai = Act::Type7Ai;
    a0->draw = Draw::Type7;
    a0->sprites = SpriteSet::Type7;
    a0->item_a = 0x69;
    a0->item_b = 0x72;
    a0->timer = 0x1E;
    a0->state = 0;
}

} // namespace

// 0x19A48
void Type7Ai(Enemy* a0) {
    a0->delay = 0;
    if (soldier_knocked(a0, type7)) return;
    soldier(a0, type7);
}

// 0x1B078: +0x41 counts down from 0x32-0x51 to a stop 1/8 of the way to its
// spot.
void Type0Ai(Enemy* a0) {
    a0->delay = 0;
    if (soldier_knocked(a0, type0)) return;
    if (a0->state == 6) return t0_stand(a0);
    if (!a0->timer)
        a0->timer = uint8_t(((s16(uint16_t(Random())) >> 8) & 0x1F) + 0x32);
    else {
        a0->timer = uint8_t(a0->timer - 1);
        if (!a0->timer) {
            a0->state = 6;
            a0->count = 0;
            a0->dest_x = uint16_t((s16(uint16_t(a0->dest_x - a0->x)) >> 3) + a0->x);
            a0->dest_y = uint16_t((s16(uint16_t(a0->dest_y - a0->y)) >> 3) + a0->y);
            return t0_stand(a0);
        }
    }
    soldier(a0, type0);
}

// 0x19ECA: aiming frames face the player (5-7) or another target (8-10);
// throwing 0xB / 0xC.
void Type7Draw(Enemy* a0, uint16_t d5, uint16_t d1) {
    uint16_t anim = 0;
    uint8_t t = a0->count;
    switch (a0->state) {
    case 2: anim = s16(a0->health) < 0 ? 2 : 1; break;
    case 1: {
        uint16_t base = target_of(a0) == player_struct ? 5 : 8;
        anim = t >= 6 || t == 1 ? base : t == 4 || t == 2 ? uint16_t(base + 1) : uint16_t(base + 2);
        break;
    }
    case 5: anim = t < 7 && t > 3 ? 0xC : 0xB; break;
    }
    DrawEnemySprite(a0, anim, 0xFFFF, s16(d1), d5);
}

// 0x19FD2: the body slides to a stop; the player walking over it picks up
// its gun (weapon 10 with 2-3 clips or weapon 7 with 1-2).
void Type7BodyAi(Enemy* a0) {
    a0->delay = 0;
    slow_down(a0);
    Enemy* a3 = target_of(a0);
    uint16_t d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
    if (d >= 0x60) return TrackNearTarget(a0);
    if (a0->state != 3) return;
    uint32_t r = Random();
    bool big = r & 0x10;
    uint16_t weapon = big ? 0xA : 7, base = big ? 2 : 1;
    if (a3 == player_struct) {
        st.add_ammo = uint16_t((((r >> 16) & 1) + base) << 8);
        uint16_t slot = AddWeapon(weapon);
        st.add_ammo = 0;
        if (slot == 0xFFFF) return;
        a0->state = 4;
        EnemySound(a0, 0x69);
        return QueueMessage(big ? Msg::LaserGun : Msg::HandGrenade);
    }
    a0->state = 4; // the other player of a link game took it
}

void Type7Hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4) { knock_hit(a0, d0, d3, d4, 2, true); } // 0x1A154

// ---- Type 8 (0x1A324-0x1A90A): AI 0x1A324, draw 0x1A722, hit 0x1A84A;
// dead 0x1A808 (= 0x18F5C) / 0x1A7FC / 0x1A8C8 (= 0x1901C). Opens doors;
// strikes (states 2, 3) or shoots (state 1). The first time it is killed it
// plays dead (state 6) and gets up again with 0x3E8 health.

namespace {

// 0x1A39C
void t8_new_spot(Enemy* a0) {
    if (s16(a0->health) < 0) {
        if (!a0->timer) {
            a0->state = 6;
            a0->count = 0x19;
            return;
        }
        // 0x1A696: killed.
        st.exit_timer = 0xF;
        a0->flags = a0->flags & 0xFF2F;
        a0->ai = Act::BodyAiBadClamp;
        a0->delay = 0;
        a0->hit = Hit::Body;
        a0->death = Act::DieDropItem47Keep;
        a0->draw = Draw::BodyWalk9;
        a0->vx = 0;
        a0->vy = 0;
        a0->state = 5;
        CountEnemiesLeft();
        EnemySound(a0, 0x2F);
        return;
    }
    Enemy* a3 = target_of(a0);
    if (a0->target == kPlayerPos) {
        TrackVisibleTarget(a0);
        return;
    }
    a0->state = 0;
    uint32_t r = uint32_t(int32_t(Random()) >> 4);
    a0->dest_x = uint16_t((r & 0xFF) - 0x80 + a3->x);
    a0->dest_y = uint16_t(((r >> 16) & 0xFF) - 0x80 + a3->y);
}

// 0x1A362
void t8_decide(Enemy* a0) {
    Enemy* a3 = target_of(a0);
    if (a0->target == kPlayerPos) {
        TrackVisibleTarget(a0);
        return;
    }
    uint16_t r = uint16_t(Random() >> 16) & 0xF;
    uint16_t d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
    a0->count = uint8_t((s16(d) >> 7) + r);
    t8_new_spot(a0);
}

// 0x1A58A: strikes the target within 0x80 at count 5.
void t8_strike(Enemy* a0) {
    a0->count = uint8_t(a0->count - 1);
    if (!a0->count) return t8_decide(a0);
    if (a0->count != 5) return;
    Enemy* a3 = target_of(a0);
    if (a0->target == kPlayerPos) {
        TrackVisibleTarget(a0);
        return;
    }
    uint16_t dx = uint16_t(a0->x - a3->x), dy = uint16_t(a0->y - a3->y);
    if (Distance(s16(dx), s16(dy)) >= 0x80) return;
    HitObject(a3, 0x100, dx, dy);
    EnemySound(a0, 0x1B);
}

// 0x1A5E6: shoots like type 9 at counts 4-6.
void t8_shoot(Enemy* a0) {
    a0->count = uint8_t(a0->count - 1);
    uint8_t t = a0->count;
    if (!t) return t8_decide(a0);
    if (t > 6 || t < 4) return;
    Enemy* a3 = target_of(a0);
    if (a0->target == kPlayerPos) {
        TrackVisibleTarget(a0);
        return;
    }
    if (EnemySightBlocked(a0, a3->x, a3->y, a0->x, a0->y)) return t8_decide(a0);
    t9_fire(a0, a3);
}

} // namespace

// 0x1A324
void Type8Ai(Enemy* a0) {
    a0->delay = 0;
    uint8_t state = a0->state;
    if (state == 4) {
        uint16_t d = Distance(s16(a0->vx), s16(a0->vy));
        a0->count = 1;
        if (d < 10) return t8_new_spot(a0);
        MoveEnemyWide(a0, a0->vx, a0->vy);
        a0->vx = uint16_t(s16(a0->vx) >> 1);
        a0->vy = uint16_t(s16(a0->vy) >> 1);
        return;
    }
    if (state == 6) {
        a0->count = uint8_t(a0->count - 1);
        if (a0->count) return;
        a0->health = 0x3E8;
        a0->timer = 1;
        return t8_new_spot(a0);
    }
    uint16_t vx = clamp_shift(a0->dest_x, a0->x, 2, 0x20);
    uint16_t vy = clamp_shift(a0->dest_y, a0->y, 2, 0x20);
    a0->vx = vx;
    a0->vy = vy;
    uint16_t px = uint16_t((vx << 1) + a0->x), py = uint16_t((vy << 1) + a0->y);
    uint8_t shape = EnemyCollisionShape(a0, px, py);
    if (shape == 6 || shape == 7) {
        a0->x = px;
        a0->y = py;
        uint16_t cx = px & 0xFF00, cy = py & 0xFF00;
        Cell cell = enemy_map(a0) + int16_t(s16(cx) >> 8) + int16_t(s16(cy) >> 3);
        if (shape == 6)
            EnemyOpenDoorA(cell, cx, cy);
        else
            EnemyOpenDoorB(cell, cx, cy);
        EnemySound(a0, 0x67);
    }
    vx = uint16_t(a0->vx - (s16(a0->vx) >> 2));
    vy = uint16_t(a0->vy - (s16(a0->vy) >> 2));
    EnemyMove m = MoveEnemyWide(a0, vx, vy);
    state = a0->state;
    if (state == 1) return t8_shoot(a0);
    if (state == 2 || state == 3) return t8_strike(a0);
    a0->count = uint8_t(a0->count - 1);
    if (a0->count && move_dist(m) >= 10) {
        TrackVisibleTarget(a0);
        return;
    }
    uint32_t r = Random();
    if ((r & 0x80) && s16(a0->health) < 0x7D0) {
        a0->state = 1;
        a0->count = 0xA;
        return t8_shoot(a0);
    }
    a0->state = (r >> 16) & 1 ? 2 : 3;
    a0->count = 0xA;
    t8_strike(a0);
}

// 0x1A722: lying dead flickers between 7 and 8 at random until it gets up.
void Type8Draw(Enemy* a0, uint16_t d5, uint16_t d1) {
    uint16_t anim = 0;
    uint8_t t = a0->count;
    switch (a0->state) {
    case 4: anim = s16(a0->health) < 0 ? 6 : 5; break;
    case 6: anim = t >= 5 ? ((Random() >> 16) & 0xF ? 7 : 8) : 0xA; break;
    case 1: anim = t > 3 && t < 7 ? 4 : 1; break;
    case 2: case 3: anim = t > 3 && t < 7 ? 2 : 1; break;
    }
    DrawEnemySprite(a0, anim, 0xFFFF, s16(d1), d5);
}

void Type8Hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4) { walker_hit(a0, d0, d3, d4, 4); } // 0x1A84A

// ---- Type 4 (0x1A92A-0x1B076): AI 0x1A92A, draw 0x1AD9A, hit 0x1AFC6; dead
// 0x1AEB4 / 0x1AE9E / 0x1B030 (= 0x1B9BC). Patrols (states 0-7 = eight
// directions, turning at blocked cells) until it sees the target, then
// chases (8) and shoots (9), alerting the other patrols within 0x400.
// State 0xA is the knock-back of a hit.

namespace {

// 0x1ABE0: chase a spot near where the target is heading.
void t4_chase(Enemy* a0) {
    Enemy* a3 = target_of(a0);
    uint16_t r = uint16_t(Random() >> 16) & 0xF;
    uint16_t d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
    a0->count = uint8_t((s16(d) >> 7) + r);
    a0->state = 8;
    uint32_t r2 = uint32_t(int32_t(Random()) >> 4);
    a0->dest_x = uint16_t((r2 & 0x1FF) - 0x100 + a3->x + a3->vx);
    a0->dest_y = uint16_t(((r2 >> 16) & 0x1FF) - 0x100 + a3->y + a3->vy);
}

// 0x1AB6C: the other patrolling type 4s within 0x400 start chasing. Returns
// d0, which the hit handler goes on to use.
uint16_t AlertPatrols(Enemy* a0, uint16_t d0) {
    uint16_t n = st.object_count;
    if (!n) return d0;
    for (Enemy* a1 = first_object(); n; n--) {
        Enemy* next = next_object(a1);
        if (a1 != a0 && a1->ai == Act::Type4Ai && a1->state < 8) {
            d0 = Distance(s16(uint16_t(a1->x - a0->x)), s16(uint16_t(a1->y - a0->y)));
            if (d0 <= 0x400) t4_chase(a1);
        }
        a1 = next;
    }
    EnemySound(a0, 0x71);
    return 0x71;
}

// 0x1ABD8
void t4_decide(Enemy* a0) {
    if (s16(a0->health) >= 0) return t4_chase(a0);
    // 0x1AAE4: killed.
    EnemySound(a0, 0x27);
    if (a0->stun) return StunEnemy(a0);
    a0->flags = a0->flags & 0xFF2F;
    a0->ai = Act::Type4BodyAi;
    a0->delay = 0;
    a0->hit = Hit::BodyFlip;
    a0->death = Act::DieDropItem47;
    a0->draw = Draw::BodyFrame7;
    a0->vx = 0;
    a0->vy = 0;
    a0->state = 0xB;
    CountEnemiesLeft();
}

// 0x1AA28: aims, fires at count 6 (the chance and range as type 1, the
// distance counted double).
void t4_aim(Enemy* a0) {
    a0->count = uint8_t(a0->count - 1);
    uint8_t t = a0->count;
    if (!t) return t4_decide(a0);
    if (t != 6) {
        if (t >= 4 && t <= 5) EnemySound(a0, 0x1E);
        return TrackTarget(a0);
    }
    Enemy* a3 = target_of(a0);
    if (EnemySightBlocked(a0, a3->x, a3->y, a0->x, a0->y)) return t4_decide(a0);
    uint16_t chance = 0x400;
    bool sure = false;
    if (s16(st.view_z) >= 0) {
        if (!st.lighting) sure = true;
        chance = 0x300;
    } else {
        chance = 0x200;
        if (st.lighting) chance = 0x17B;
    }
    if (sure || ((int32_t(Random()) >> 8) & 0x3FF) < chance) {
        uint16_t dx = uint16_t(a0->x - a3->x), dy = uint16_t(a0->y - a3->y);
        uint16_t d = uint16_t(Distance(s16(dx), s16(dy)) << 1);
        if (d >= 0x400) return;
        HitObject(a3, d, dx, dy);
    }
    EnemySound(a0, 0x1E);
}

// 0x1AC4E: patrol step (8 per frame) in the direction of the state.
void t4_patrol(Enemy* a0) {
    Enemy* a3 = target_of(a0);
    uint16_t d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
    if (d <= 0x12C) {
        AlertPatrols(a0, d);
        return t4_chase(a0);
    }
    static const int16_t dir[8][2] = {{0, -16}, {-16, -16}, {-16, 0}, {-16, 16}, {0, 16}, {16, 16}, {16, 0}, {16, -16}};
    uint8_t state = a0->state;
    uint8_t k = state < 7 ? state : 7;
    uint16_t dx = uint16_t(dir[k][0]), dy = uint16_t(dir[k][1]);
    if (uint8_t(a0->x) == 0x80 && uint8_t(a0->y) == 0x80) {
        uint16_t px = uint16_t((dx << 4) + a0->x), py = uint16_t((dy << 4) + a0->y);
        uint8_t cls = st.cell_class[map_cell(enemy_map(a0) + int16_t(s16(px) >> 8) + int16_t(s16(uint16_t(py & 0xFF00)) >> 3))];
        if (kEnemyBlocked[cls]) {
            a0->state = uint8_t((k + 1) & 7);
            return;
        }
    }
    a0->x = uint16_t(a0->x + dx);
    a0->y = uint16_t(a0->y + dy);
    uint16_t vx = uint16_t(dx << 1), vy = uint16_t(dy << 1);
    a0->vx = vx;
    a0->vy = vy;
    d = Distance(s16(uint16_t((vx << 4) + a0->x - a3->x)), s16(uint16_t((vy << 4) + a0->y - a3->y)));
    if (d <= 0x180 && !EnemySightBlocked(a0, a3->x, a3->y, a0->x, a0->y)) {
        AlertPatrols(a0, d);
        t4_chase(a0);
    }
    TrackTarget(a0);
}

} // namespace

// 0x1A92A
void Type4Ai(Enemy* a0) {
    a0->delay = 0;
    uint8_t state = a0->state;
    if (state == 0xA) {
        a0->count = uint8_t(a0->count - 1);
        if (!a0->count) return t4_decide(a0);
        MoveEnemyNarrow(a0, a0->vx, a0->vy);
        a0->vx = uint16_t(s16(a0->vx) >> 1);
        a0->vy = uint16_t(s16(a0->vy) >> 1);
        return;
    }
    if (state < 8) return t4_patrol(a0);
    uint16_t vx = clamp_shift(a0->dest_x, a0->x, 1, 0x20);
    uint16_t vy = clamp_shift(a0->dest_y, a0->y, 1, 0x20);
    EnemyMove m = MoveEnemyNarrow(a0, vx, vy);
    a0->vx = uint16_t(m.d0 << 1);
    a0->vy = uint16_t(m.d1 << 1);
    if (state == 9) return t4_aim(a0);
    if (move_dist(m) > 4) {
        a0->count = uint8_t(a0->count - 1);
        if (a0->count) {
            Enemy* a3 = target_of(a0);
            uint16_t dx = uint16_t(a0->x - a3->x), dy = uint16_t(a0->y - a3->y);
            if (Distance(s16(dx), s16(dy)) <= 0x40) {
                HitObject(a3, 0x384, dx, dy);
                EnemySound(a0, 0x1B);
            }
            return TrackTarget(a0);
        }
    }
    a0->state = 9;
    a0->count = 0xA;
    t4_aim(a0);
}

// 0x1AD9A
void Type4Draw(Enemy* a0, uint16_t d5, uint16_t d1) {
    uint16_t anim = 0;
    uint8_t t = a0->count;
    if (a0->state == 0xA) {
        if (s16(a0->health) >= 0)
            anim = t >= 9 ? 3 : t >= 7 ? 0xA : t <= 2 ? 3 : t <= 4 ? 0xA : 9;
        else
            anim = t >= 9 ? 8 : t >= 7 ? 4 : t >= 5 ? 6 : 5;
    } else if (a0->state == 9) {
        bool aim = t >= 4 && t <= 6;
        anim = target_of(a0) == player_struct ? (aim ? 2 : 1) : (aim ? 0xC : 0xB);
    }
    DrawEnemySprite(a0, anim, 0xFFFF, s16(d1), d5);
}

// 0x1AEB4: the body slides to a stop; the player walking over it picks up
// its gun (weapon 10 with 2-3 clips).
void Type4BodyAi(Enemy* a0) {
    a0->delay = 0;
    slow_down(a0);
    Enemy* a3 = target_of(a0);
    uint16_t d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
    if (d >= 0x60) return TrackNearTarget(a0);
    if (a0->state != 0xB) return;
    if (a3 == player_struct) {
        st.add_ammo = uint16_t((((s16(uint16_t(Random())) >> 8) & 1) + 2) << 8);
        uint16_t slot = AddWeapon(0xA);
        st.add_ammo = 0;
        if (slot == 0xFFFF) return;
        a0->state = 0xC;
        EnemySound(a0, 0x69);
        return QueueMessage(Msg::LaserGun);
    }
    a0->state = 0xC; // the other player of a link game took it
    Random(); // the ammo of the link packet
}

// 0x1AFC6: a hit alerts the other patrols; it then knocks back with
// d0 = 0x71 left by the alert sound (only a killing hit uses the damage).
void Type4Hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    a0->state = 0xA;
    a0->count = 0xA;
    a0->vx = 0;
    a0->vy = 0;
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    bool killed = false;
    if (s16(a0->health) >= 0) {
        a0->health = uint16_t(a0->health - f);
        if (s16(a0->health) < 0) {
            a0->hit = Hit::BodyFlip;
            killed = true;
        }
    }
    if (!killed) f = int16_t(AlertPatrols(a0, uint16_t(f)));
    set_knock(a0, dir, int16_t(f >> 3));
}

// 0x1B562
void Type0Draw(Enemy* a0, uint16_t d5, uint16_t d1) {
    uint16_t anim = 0, frame = 0xFFFF;
    uint8_t t = a0->count;
    switch (a0->state) {
    case 2: anim = s16(a0->health) < 0 ? 8 : 7; break;
    case 1: {
        bool player = target_of(a0) == player_struct;
        if (t == 1 || t == 9)
            anim = 1;
        else if (t <= 3 || t == 5 || t >= 7)
            anim = player ? 2 : 0xE;
        else
            anim = player ? 3 : 0xF;
        break;
    }
    case 5: anim = t < 7 && t > 3 ? 5 : 4; break;
    case 6:
        anim = 6;
        frame = uint16_t((t + 1) >> 1);
        if (frame > 4) frame = 4;
        break;
    }
    DrawEnemySprite(a0, anim, frame, s16(d1), d5);
}

// 0x1B824: like 0x187C8, without the check for being dead already; a hit
// that leaves it alive sets +0x41 to 2 (it soon stops).
void Type0Hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    a0->state = 2;
    a0->count = 0;
    a0->vx = 0;
    a0->vy = 0;
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    a0->health = uint16_t(a0->health - f);
    if (s16(a0->health) >= 0)
        a0->timer = 2;
    else if (uint16_t(f) >= 0x338) {
        SpawnBlood(a0, uint16_t(f), dir.d0, dir.d1);
        a0->stun = st.blast_near;
    }
    set_knock(a0, dir, int16_t(f >> 3));
}

// ---- Type 3 (0x14BB4-0x157C8): AI 0x14C2A (then 0x14C40), draw 0x1539E,
// hit 0x1575C; dead 0x156D2 (= 0x1B97A) / 0x15618 / 0x1570C. Crawls on the
// floor (state 0, height -0x18) or the ceiling (1, +0x18), drops or jumps
// (2), and climbs walls (3-10: the wall side and direction). Bites the
// target within 0x64.

namespace {

// 0x1521E: 1 when the cell at (d0, d1) of the enemy's level is a wall.
bool t3_wall(Enemy* a0, uint16_t d0, uint16_t d1) {
    return st.cell_class[map_cell(enemy_map(a0) + int16_t(s16(d0) >> 8) + int16_t(s16(uint16_t(d1 & 0xFF00)) >> 3))] == 1;
}

uint8_t t3_blocks(Cell a1, uint16_t x, uint16_t y) {
    return kEnemyBlocked[st.cell_class[map_cell(a1 + int16_t(s16(x) >> 8) + int16_t(s16(uint16_t(y & 0xFF00)) >> 3))]];
}

// 0x152C8: keeps 0x20 away from blocking neighbours and 0x19 from the
// target.
void KeepCrawlerApart(Enemy* a0) {
    Cell a1 = enemy_map(a0) + int16_t(s16(a0->x) >> 8) + int16_t(s16(uint16_t(a0->y & 0xFF00)) >> 3);
    auto blocks = [&](int16_t o) { return kEnemyBlocked[st.cell_class[map_cell(a1 + o)]] != 0; };
    uint8_t xl = uint8_t(a0->x);
    if (xl < 0x20) {
        if (blocks(-1)) set_low_byte(a0->x, 0x20);
    } else if (xl > 0xDF) {
        if (blocks(1)) set_low_byte(a0->x, 0xDF);
    }
    uint8_t yl = uint8_t(a0->y);
    if (yl < 0x20) {
        if (blocks(-0x20)) set_low_byte(a0->y, 0x20);
    } else if (yl > 0xDF) {
        if (blocks(0x20)) set_low_byte(a0->y, 0xDF);
    }
    Enemy* a3 = target_of(a0);
    uint16_t dx = uint16_t(a0->x - a3->x), dy = uint16_t(a0->y - a3->y);
    uint16_t d = uint16_t(Distance(s16(dx), s16(dy)) + 1);
    if (d == 0 || d >= 0x19) return;
    a0->x = uint16_t(uint16_t(divs(ext_l(dx) << 5, d)) + a3->x);
    a0->y = uint16_t(uint16_t(divs(ext_l(dy) << 5, d)) + a3->y);
}

// 0x15246: sets the velocity and moves, sliding along blocking cells.
void MoveCrawler(Enemy* a0, uint16_t d0, uint16_t d1) {
    a0->vx = d0;
    a0->vy = d1;
    Cell a1 = enemy_map(a0);
    uint16_t x = a0->x, y = a0->y;
    if (!t3_blocks(a1, uint16_t(x + d0), uint16_t(y + d1))) {
        a0->x = uint16_t(x + d0);
        a0->y = uint16_t(y + d1);
    } else if (!t3_blocks(a1, x, uint16_t(y + d1)))
        a0->y = uint16_t(y + d1);
    else if (!t3_blocks(a1, uint16_t(x + d0), y))
        a0->x = uint16_t(x + d0);
    KeepCrawlerApart(a0);
}

// 0x14BB4: a spot near the target, on the floor or the ceiling.
void t3_decide(Enemy* a0) {
    uint16_t r = uint16_t(Random() >> 16) & 0xF;
    Enemy* a3 = target_of(a0);
    uint16_t d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
    a0->count = uint8_t((s16(d) >> 7) + r);
    a0->state = a0->z == 0x18 ? 1 : 0;
    uint32_t r2 = uint32_t(int32_t(Random()) >> 4);
    a0->dest_x = uint16_t((((r2 & 0xFF) - 0x80) << 1) + a3->x);
    a0->dest_y = uint16_t(((((r2 >> 16) & 0xFF) - 0x80) << 1) + a3->y);
}

// 0x1564E: killed.
void t3_kill(Enemy* a0) {
    EnemySound(a0, 0x25);
    if (a0->stun) return StunEnemy(a0);
    a0->flags = a0->flags & 0xFF2F;
    a0->ai = Act::BodyAi;
    a0->delay = 0;
    a0->hit = Hit::Type3Body;
    a0->death = Act::DieDropItem47;
    a0->draw = Draw::Type3Body;
    a0->vx = 0;
    a0->vy = 0;
    a0->state = 0;
    CountEnemiesLeft();
}

// 0x150DE / 0x1512E / 0x1517E / 0x151CE: next to a wall on side (dx, dy)
// it may start climbing it (states lo / hi by where its spot is).
bool t3_climb(Enemy* a0, int16_t dx, int16_t dy, uint8_t lo, uint8_t hi) {
    if (!t3_wall(a0, uint16_t(a0->x + dx), uint16_t(a0->y + dy))) return false;
    uint32_t r = Random();
    if (r & 0xE0) return false;
    bool beyond = dx ? s16(a0->dest_y) > s16(a0->y) : s16(a0->dest_x) > s16(a0->x);
    a0->state = beyond ? hi : lo;
    uint16_t vz = uint16_t(((r >> 16) & 3) + 2);
    if (s16(a0->z) >= 0) vz = uint16_t(-vz);
    a0->vz = vz;
    return true;
}

// 0x14D82: on the floor or ceiling: may climb a wall at a cell edge, else
// walks and bites.
void t3_crawl(Enemy* a0) {
    uint8_t xl = uint8_t(a0->x), yl = uint8_t(a0->y);
    bool mid_y = yl >= 0x40 && yl <= 0xBF, mid_x = xl >= 0x40 && xl <= 0xBF;
    if (xl <= 0x20 && mid_y) {
        if (t3_climb(a0, -0x24, 0, 9, 0xA)) return;
    } else if (xl >= 0xDF && mid_y) {
        if (t3_climb(a0, 0x24, 0, 7, 8)) return;
    } else if (yl <= 0x20 && mid_x) {
        if (t3_climb(a0, 0, -0x24, 4, 3)) return;
    } else if (yl >= 0xDF && mid_x) {
        if (t3_climb(a0, 0, 0x24, 6, 5)) return;
    }
    // 0x14E0A
    MoveCrawler(a0, clamp_shift(a0->dest_x, a0->x, 3, 0x28), clamp_shift(a0->dest_y, a0->y, 3, 0x28));
    Enemy* a3 = target_of(a0);
    uint16_t d = Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y)));
    uint8_t cls = st.cell_class[map_cell(enemy_map(a3) + int16_t(s16(a3->x) >> 8) +
                                       int16_t(s16(uint16_t(a3->y & 0xFF00)) >> 3))];
    if (cls >= 2 && cls <= 5) d = uint16_t(s16(d) >> 1);
    if (d <= 0x64) {
        int16_t jump = s16(st.view_z);
        bool bite = true;
        if (a0->z == 0x18) {
            if (jump < 0) {
                // The target ducks under it: drop.
                a0->state = 2;
                a0->vx = 0;
                a0->vy = 0;
                a0->vz = 0;
                bite = false;
            }
        } else if (a0->z == 0xFFE8 && jump > 0)
            bite = false;
        if (bite && !a0->timer) {
            a0->timer = 0xA;
            EnemySound(a0, 0x93);
            HitObject(a3, 0x2BC, uint16_t(a0->x - a3->x), uint16_t(a0->y - a3->y));
        }
    }
    a0->count = uint8_t(a0->count - 1);
    if (!a0->count) return t3_decide(a0);
    TrackTarget(a0);
}

// States 3-10: climbing along a wall at (dx, dy); without the wall it falls
// off with velocity (fx, fy), else moves with (mx, my) and up or down.
void t3_wall_walk(Enemy* a0, int16_t dx, int16_t dy, int16_t fx, int16_t fy, int16_t mx, int16_t my) {
    if (!t3_wall(a0, uint16_t(a0->x + dx), uint16_t(a0->y + dy))) {
        a0->state = 2;
        a0->vx = uint16_t(fx);
        a0->vy = uint16_t(fy);
        a0->vz = 0;
        return;
    }
    a0->vx = uint16_t(mx);
    a0->vy = uint16_t(my);
    MoveCrawler(a0, uint16_t(mx), uint16_t(my));
    a0->flags = a0->flags | 0xC8;
    int16_t vz = s16(a0->vz);
    a0->z = uint16_t(a0->z + vz);
    int16_t h = s16(a0->z);
    if (vz >= 0 && h >= 0x18) {
        a0->z = 0x18;
        a0->state = 1;
        a0->count = 0;
        return t3_decide(a0);
    }
    if (vz < 0 && h <= -0x18) {
        a0->z = 0xFFE8;
        a0->state = 0;
        a0->count = 0;
        return t3_decide(a0);
    }
    KeepCrawlerApart(a0);
    TrackTarget(a0);
}

} // namespace

// 0x14C2A
void Type3Start(Enemy* a0) {
    a0->ai = Act::Type3Ai;
    a0->state = uint8_t(Random() & 1);
    Type3Ai(a0);
}

// 0x14C40
void Type3Ai(Enemy* a0) {
    a0->delay = 0;
    if (a0->timer) a0->timer = uint8_t(a0->timer - 1);
    Enemy* a3 = target_of(a0);
    auto near = [&]() {
        return Distance(s16(uint16_t(a0->x - a3->x)), s16(uint16_t(a0->y - a3->y))) <= 0x80;
    };
    switch (a0->state) {
    case 0:
        a0->z = 0xFFE8;
        a0->flags = uint16_t((a0->flags & 0xFF7F) | 0x40);
        if (near()) a0->flags = a0->flags & 0xFF3F;
        return t3_crawl(a0);
    case 1:
        a0->z = 0x18;
        a0->flags = uint16_t((a0->flags & 0xFFF7) | 0x40);
        if (near()) a0->flags = a0->flags & 0xFFB7;
        if (!(Random() & 0xFE0)) {
            a0->state = 2;
            a0->vx = 0;
            a0->vy = 0;
            a0->vz = 0;
            return;
        }
        return t3_crawl(a0);
    case 2: {
        a0->flags = a0->flags | 0xC8;
        uint16_t vz = a0->vz;
        a0->z = uint16_t(a0->z + vz);
        a0->vz = uint16_t(vz - 3);
        if (s16(a0->z) <= -0x18) {
            a0->z = 0xFFE8;
            a0->vz = 0;
            a0->state = 0;
            if (s16(a0->health) < 0) return t3_kill(a0);
            return t3_decide(a0);
        }
        MoveCrawler(a0, a0->vx, a0->vy);
        a0->vx = uint16_t(s16(a0->vx) >> 1);
        a0->vy = uint16_t(s16(a0->vy) >> 1);
        return;
    }
    case 3: return t3_wall_walk(a0, 0, -0x24, 0, 0x28, 0x28, 0);
    case 4: return t3_wall_walk(a0, 0, -0x24, 0, 0x28, -0x28, 0);
    case 5: return t3_wall_walk(a0, 0, 0x24, 0, -0x28, 0x28, 0);
    case 6: return t3_wall_walk(a0, 0, 0x24, 0, -0x28, -0x28, 0);
    case 7: return t3_wall_walk(a0, 0x24, 0, -0x28, 0, 0, -0x28);
    case 8: return t3_wall_walk(a0, 0x24, 0, -0x28, 0, 0, 0x28);
    case 9: return t3_wall_walk(a0, -0x24, 0, 0x28, 0, 0, -0x28);
    case 0xA: return t3_wall_walk(a0, -0x24, 0, 0x28, 0, 0, 0x28);
    }
}

// 0x1539E: on a wall the view depends on where the player is.
void Type3Draw(Enemy* a0, uint16_t d5, uint16_t d1) {
    uint8_t state = a0->state;
    if (state < 2) return DrawEnemySprite(a0, state ? 5 : 0, 0xFFFF, s16(d1), d5);
    int16_t x = s16(a0->x), y = s16(a0->y), px = s16(player_x), py = s16(player_y);
    auto ad = [](int16_t v) { return int16_t(v < 0 ? -v : v); };
    int16_t ax = ad(int16_t(x - px)), ay = ad(int16_t(y - py));
    uint16_t anim;
    switch (state) {
    case 3: anim = !(y > py) && int16_t(ax + y) < py ? 4 : x > px ? 2 : 6; break;
    case 4: anim = !(y > py) && int16_t(ax + y) < py ? 3 : x > px ? 1 : 7; break;
    case 5: anim = !(y < py) && int16_t(y - ax) > py ? 3 : x > px ? 7 : 1; break;
    case 6: anim = !(y < py) && int16_t(y - ax) > py ? 4 : x < px ? 2 : 6; break;
    case 7: anim = !(x < px) && int16_t(x - ay) > px ? 3 : y < py ? 7 : 1; break;
    case 8: anim = !(x < px) && int16_t(x - ay) > px ? 4 : y > py ? 2 : 6; break;
    case 9: anim = !(x > px) && int16_t(x + ay) < px ? 4 : y < py ? 2 : 6; break;
    case 0xA: anim = !(x > px) && int16_t(x + ay) < px ? 3 : y > py ? 7 : 1; break;
    default:
        if (s16(a0->vz) > 0) return DrawEnemySprite(a0, 8, 1, s16(d1), d5);
        if (s16(a0->health) >= 0) return DrawEnemySprite(a0, 1, 0, s16(d1), d5);
        return DrawEnemySprite(a0, 0xA, s16(a0->z) <= 0x10 ? 1 : 0, s16(d1), d5);
    }
    DrawEnemySprite(a0, anim, 0xFFFF, s16(d1), d5);
}

// 0x15618: the body twitches now and then.
void Type3BodyDraw(Enemy* a0, uint16_t d5, uint16_t d1) {
    uint16_t d2 = (Random() & 0xF00) ? 1 : 0;
    if (a0->state == 2) d2 = uint16_t(d2 + 2);
    if (a0->count & 1) d2 = uint16_t(d2 + 4);
    DrawEnemySprite(a0, 9, d2, s16(d1), d5);
}

// 0x1570C: the body is knocked about.
void Type3BodyHit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    a0->vx = 0;
    a0->vy = 0;
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    a0->health = uint16_t(a0->health - f);
    set_knock(a0, dir, int16_t(f >> 4));
    a0->state = 2;
    a0->count = uint8_t(a0->count + 1);
}

// 0x1575C: knocked off the floor (jumping) or the wall or ceiling.
void Type3Hit(Enemy* a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    a0->count = 0;
    a0->vx = 0;
    a0->vy = 0;
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    if (s16(a0->health) >= 0) {
        a0->health = uint16_t(a0->health - f);
        a0->stun = st.blast_near;
    }
    set_knock(a0, dir, int16_t(f >> 3));
    a0->state = 2;
    a0->vz = 0;
    if (a0->z == 0xFFE8) a0->vz = 8;
}

} // namespace game
