// The AI, drawing and hit handlers of the enemy types (templates 0xAB0C,
// second state 0xAC88), in ROM order.
#include "enemy.hpp"
#include "m68k.hpp"
#include "render.hpp"

namespace game {

namespace {

int16_t s16(uint16_t v) { return int16_t(v); }

constexpr uint32_t player_struct = 0xFF119C;

uint16_t clamp_step(uint16_t target, uint16_t pos, int16_t limit) {
    int16_t d = int16_t(s16(uint16_t(target - pos)) >> 3);
    if (d < -limit) return uint16_t(-limit);
    if (d > limit) return uint16_t(limit);
    return uint16_t(d);
}

void link_state_packet(uint32_t a0, uint32_t handler) {
    if (!link_mode) return;
    wr32(a0 + 0x1A, handler);
    wr8(0xFF0FFA, 0x12);
    wr8(0xFF0FFB, rd8(a0 + 0x3E));
    wr8(0xFF0FFC, uint8_t(rd8(a0 + 5) | 0x20));
    wr8(0xFF0FFD, rd8(a0 + 0x32));
    sub_01C03A(0xFF0FFA);
}

} // namespace

// ---- Type 1 (0x182FC-0x18878): AI 0x182FC, draw 0x1856C, hit 0x187C8;
// dead 0x186B8 / 0x18660 / 0x18832.

namespace {

// 0x18334: picks the next spot near the target and how long to walk there.
void t1_decide(uint32_t a0) {
    if (s16(rd16(a0 + 0x36)) < 0) {
        // 0x184E4: killed.
        sub_00D740(a0, 0x2D);
        if (rd8(a0 + 0x30)) return loc_01B8D8(a0);
        wr16(a0 + 4, rd16(a0 + 4) & 0xFF2F);
        wr32(a0 + 0x12, 0x186B8);
        wr8(a0 + 0x1F, 0);
        wr32(a0 + 0xA, 0x18832);
        wr32(a0 + 0x16, 0x14454);
        wr32(a0 + 0xE, 0x18660);
        wr16(a0 + 0x2A, 0);
        wr16(a0 + 0x2C, 0);
        wr8(a0 + 0x34, 3);
        sub_057E0C();
        return link_state_packet(a0, 0x16F7C);
    }
    uint32_t a3 = rd32(a0 + 6);
    uint16_t r = uint16_t(Random() >> 16) & 0x1F;
    uint16_t d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    wr8(a0 + 0x35, uint8_t((s16(d) >> 6) + r));
    wr8(a0 + 0x34, 0);
    uint32_t r2 = uint32_t(int32_t(Random()) >> 4);
    wr16(a0 + 0x38, uint16_t((r2 & 0x1FF) - 0x100 + rd16(a3 + 0x20) + rd16(a3 + 0x2A)));
    wr16(a0 + 0x3A, uint16_t(((r2 >> 16) & 0x1FF) - 0x100 + rd16(a3 + 0x22) + rd16(a3 + 0x2C)));
}

void t1_shot_sound(uint32_t a0) {
    sub_00D740(a0, 0x68);
    sub_00D740(a0, 0x97);
}

// 0x18452 (and 0x19CD6): fires at the target when it is in sight (else
// decides again); the chance to hit drops when the player crouches or in the
// dark.
void t1_fire(uint32_t a0, void (*decide)(uint32_t)) {
    uint32_t a3 = rd32(a0 + 6);
    if (sub_0166A0(a0, rd16(a3 + 0x20), rd16(a3 + 0x22), rd16(a0 + 0x20), rd16(a0 + 0x22))) return decide(a0);
    uint16_t chance = 0x400;
    bool sure = false;
    if (s16(U16{0xFF0E1A}) >= 0) {
        if (!U16{0xFF0EB2}) sure = true;
        chance = 0x300;
    } else {
        chance = 0x200;
        if (U16{0xFF0EB2}) chance = 0x17B;
    }
    if (sure || ((int32_t(Random()) >> 8) & 0x3FF) < chance) {
        uint16_t dx = uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20)), dy = uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22));
        uint16_t d = Distance(s16(dx), s16(dy));
        if (d >= 0x400) return;
        CallEnemyHit(rd32(a3 + 0xA), a3, d, dx, dy);
    }
    t1_shot_sound(a0);
}

} // namespace

// 0x182FC: walks to a spot near the target (state 0), then aims and fires
// (state 1, shot at count 5); state 2 is the knock-back of a hit.
void h_0182FC(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    if (rd8(a0 + 0x34) == 2) {
        if (Distance(s16(rd16(a0 + 0x2A)), s16(rd16(a0 + 0x2C))) >= 10) {
            sub_0145AA(a0, rd16(a0 + 0x2A), rd16(a0 + 0x2C));
            wr16(a0 + 0x2A, uint16_t(s16(rd16(a0 + 0x2A)) >> 1));
            wr16(a0 + 0x2C, uint16_t(s16(rd16(a0 + 0x2C)) >> 1));
            return;
        }
        return t1_decide(a0);
    }
    uint16_t vx = clamp_step(rd16(a0 + 0x38), rd16(a0 + 0x20), 0x28);
    uint16_t vy = clamp_step(rd16(a0 + 0x3A), rd16(a0 + 0x22), 0x28);
    wr16(a0 + 0x2A, vx);
    wr16(a0 + 0x2C, vy);
    sub_0145AA(a0, vx, vy);
    if (rd8(a0 + 0x34) != 1) {
        wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
        if (rd8(a0 + 0x35)) return loc_016084(a0);
        wr8(a0 + 0x34, 1);
        wr8(a0 + 0x35, 0xA);
    }
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
    uint8_t t = rd8(a0 + 0x35);
    if (t == 0) return t1_decide(a0);
    if (t == 5) return t1_fire(a0, t1_decide);
    if (t == 4 || t == 3) t1_shot_sound(a0);
}

// 0x1856C
void h_01856C(uint32_t a0, uint16_t d5, uint16_t d1) {
    uint8_t state = rd8(a0 + 0x34), t = rd8(a0 + 0x35);
    uint16_t anim = 0;
    if (state == 2)
        anim = s16(rd16(a0 + 0x36)) < 0 ? 2 : 1;
    else if (state == 1) {
        uint16_t base = rd32(a0 + 6) == player_struct ? 5 : 0xA;
        if (t == 9 || t == 1) anim = base;
        else if (t == 8 || t == 2) anim = uint16_t(base + 1);
        else if (t == 6 || t == 4) anim = uint16_t(base + 3);
        else anim = uint16_t(base + 2);
    }
    sub_01BA04(a0, anim, 0xFFFF, s16(d1), d5);
}

// 0x18660: the body, blinking while the template's sprite set is in use.
void h_018660(uint32_t a0, uint16_t d5, uint16_t d1) {
    uint16_t d2 = 0;
    if (rd32(a0 + 0x42) == 0x16EC58) d2 = rd8(0x186A8 + (uint8_t(U8{0xFF0EB7} + rd8(a0 + 0x3E)) & 0xF));
    if (rd8(a0 + 0x35) & 1) d2 = uint16_t(d2 + 1);
    sub_01BA04(a0, 4, d2, s16(d1), d5);
}

// 0x186B8: the body slides to a stop; the player walking over it picks up
// its gun (weapon 8 with 2-3 clips).
void h_0186B8(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    slow_down(a0);
    uint32_t a3 = rd32(a0 + 6);
    uint16_t d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    if (d >= 0x60) return loc_0160E8(a0);
    if (rd8(a0 + 0x34) != 3) return;
    uint16_t clips = uint16_t((((s16(uint16_t(Random())) >> 8) & 1) + 2) << 8);
    if (a3 == player_struct) {
        U16{0xFF1034} = clips;
        uint16_t slot = sub_011084(8);
        U16{0xFF1034} = 0;
        if (slot == 0xFFFF) return;
        wr8(a0 + 0x34, 4);
        sub_00D740(a0, 0x69);
        return sub_01E0C2(0x1E43E);
    }
    wr8(a0 + 0x34, 4);
    wr8(0xFF0FFA, 0x13);
    wr8(0xFF0FFB, 8);
    wr8(0xFF0FFC, uint8_t(clips >> 8));
    sub_01C03A(0xFF0FFA);
}

// 0x187C8 (and 0x1999A with state 3): a hit: damage 0x400 - d0 from +0x36,
// knock-back, blood.
void knock_hit(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4, uint8_t state, bool mark41 = false) {
    wr8(a0 + 0x34, state);
    wr8(a0 + 0x35, 0);
    wr16(a0 + 0x2A, 0);
    wr16(a0 + 0x2C, 0);
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    if (s16(rd16(a0 + 0x36)) >= 0) {
        wr16(a0 + 0x36, uint16_t(rd16(a0 + 0x36) - f));
        if (s16(rd16(a0 + 0x36)) >= 0) {
            if (mark41 && rd8(a0 + 0x41)) wr8(a0 + 0x41, 2);
        } else if (uint16_t(f) >= 0x338) {
            sub_0157CA(a0, uint16_t(f), dir.d0, dir.d1);
            wr8(a0 + 0x30, U8{0xFF26F0});
        }
    }
    set_knock(a0, dir, int16_t(f >> 3));
}

void h_0187C8(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4) { knock_hit(a0, d0, d3, d4, 2); }

// ---- Type 2 (0x1887A-0x18BC2): AI 0x1887A, draw 0x18A66, hit 0x18B12 (=
// 0x187C8); dead 0x18AD0 (= 0x1B97A) / 0x18AC8 / 0x18B7C (= 0x1B9BC).

namespace {

// 0x188B2
void t2_decide(uint32_t a0) {
    if (s16(rd16(a0 + 0x36)) < 0) {
        // 0x189E4: killed.
        sub_00D740(a0, 0x23);
        if (rd8(a0 + 0x30)) return loc_01B8D8(a0);
        wr16(a0 + 4, rd16(a0 + 4) & 0xFF2F);
        wr32(a0 + 0x12, 0x18AD0);
        wr8(a0 + 0x1F, 0);
        wr32(a0 + 0xA, 0x18B7C);
        wr32(a0 + 0x16, 0x14454);
        wr32(a0 + 0xE, 0x18AC8);
        wr16(a0 + 0x2A, 0);
        wr16(a0 + 0x2C, 0);
        sub_057E0C();
        return link_state_packet(a0, 0x16F82);
    }
    uint32_t a3 = rd32(a0 + 6);
    uint16_t r = uint16_t(Random() >> 16) & 0xF;
    uint16_t d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    wr8(a0 + 0x35, uint8_t((s16(d) >> 6) + r));
    wr8(a0 + 0x34, 0);
    uint32_t r2 = uint32_t(int32_t(Random()) >> 4);
    wr16(a0 + 0x38, uint16_t((r2 & 0xFF) - 0x80 + rd16(a3 + 0x20)));
    wr16(a0 + 0x3A, uint16_t(((r2 >> 16) & 0xFF) - 0x80 + rd16(a3 + 0x22)));
}

uint16_t clamp_step4(uint16_t target, uint16_t pos, int16_t limit) {
    int16_t d = int16_t(s16(uint16_t(target - pos)) >> 2);
    if (d < -limit) return uint16_t(-limit);
    if (d > limit) return uint16_t(limit);
    return uint16_t(d);
}

// Knock-back state 2 shared by the walking types: slides while fast,
// then decides again.
bool knocked(uint32_t a0) {
    if (rd8(a0 + 0x34) != 2) return false;
    if (Distance(s16(rd16(a0 + 0x2A)), s16(rd16(a0 + 0x2C))) >= 10) {
        sub_0145AA(a0, rd16(a0 + 0x2A), rd16(a0 + 0x2C));
        wr16(a0 + 0x2A, uint16_t(s16(rd16(a0 + 0x2A)) >> 1));
        wr16(a0 + 0x2C, uint16_t(s16(rd16(a0 + 0x2C)) >> 1));
        return true;
    }
    return false;
}

} // namespace

// 0x1887A: walks to a spot near the target, then strikes it within 0x64
// (state 1, at count 5).
void h_01887A(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    if (rd8(a0 + 0x34) == 2) {
        if (knocked(a0)) return;
        return t2_decide(a0);
    }
    uint16_t vx = clamp_step4(rd16(a0 + 0x38), rd16(a0 + 0x20), 0x1E);
    uint16_t vy = clamp_step4(rd16(a0 + 0x3A), rd16(a0 + 0x22), 0x1E);
    wr16(a0 + 0x2A, vx);
    wr16(a0 + 0x2C, vy);
    sub_0145AA(a0, vx, vy);
    if (rd8(a0 + 0x34) != 1) {
        wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
        if (rd8(a0 + 0x35)) return loc_016084(a0);
        wr8(a0 + 0x34, 1);
        wr8(a0 + 0x35, 0xA);
    }
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
    uint8_t t = rd8(a0 + 0x35);
    if (t == 0) return t2_decide(a0);
    if (t != 5) return;
    uint32_t a3 = rd32(a0 + 6);
    uint16_t dx = uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20)), dy = uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22));
    if (Distance(s16(dx), s16(dy)) >= 0x64) return;
    CallEnemyHit(rd32(a3 + 0xA), a3, 0x100, dx, dy);
    sub_00D740(a0, 0x94);
}

// 0x18A66
void h_018A66(uint32_t a0, uint16_t d5, uint16_t d1) {
    uint16_t anim = 0;
    if (rd8(a0 + 0x34) == 2)
        anim = s16(rd16(a0 + 0x36)) < 0 ? 4 : 3;
    else if (rd8(a0 + 0x34) == 1) {
        uint8_t t = rd8(a0 + 0x35);
        anim = t < 7 && t > 3 ? 2 : 1;
    }
    sub_01BA04(a0, anim, 0xFFFF, s16(d1), d5);
}

// 0x1866E with d0 set (0x18AC8 and the other bodies): animation d0, frame
// 1 on odd hit counts.
void body_frame(uint32_t a0, uint16_t anim, uint16_t d5, uint16_t d1) {
    sub_01BA04(a0, anim, rd8(a0 + 0x35) & 1, s16(d1), d5);
}

// ---- Types 9 (0x18BC4-0x1905E) and 5 (0x19060-0x1953E): they walk through
// doors, opening them. Type 9 (AI 0x18BC4, draw 0x18EE8, hit 0x18F9E; dead
// 0x18F5C / 0x18F50 / 0x1901C) shoots; type 5 (AI 0x19060, draw 0x193C8,
// hit 0x1947E = 0x18F9E; dead 0x1943C = 0x18F5C / 0x19430 / 0x194FC =
// 0x1901C) throws a shot object (0x15F6E).

namespace {

// 0x1A90C
void sub_01A90C() {
    if (!link_mode) return;
    wr8(0xFF0FFA, 0x18);
    sub_01C03A(0xFF0FFA);
}

struct Walker {
    int16_t limit;            // step clamp
    bool state_before_target; // type 9 clears the state before checking the target
    uint8_t fire_lo, fire_hi; // counts that fire
    void (*fire)(uint32_t a0, uint32_t a3);
    uint16_t kill_sound;
    uint32_t dead_ai, dead_hit, dead_draw, link;
};

// 0x18C3C / 0x190D8
void walker_new_spot(uint32_t a0, const Walker& w) {
    if (s16(rd16(a0 + 0x36)) < 0) {
        // 0x18E5C / 0x1933C: killed.
        U16{0xFF2724} = 0xF;
        sub_01A90C();
        wr16(a0 + 4, rd16(a0 + 4) & 0xFF2F);
        wr32(a0 + 0x12, w.dead_ai);
        wr8(a0 + 0x1F, 0);
        wr32(a0 + 0xA, w.dead_hit);
        wr32(a0 + 0x16, 0x144FE);
        wr32(a0 + 0xE, w.dead_draw);
        wr16(a0 + 0x2A, 0);
        wr16(a0 + 0x2C, 0);
        wr8(a0 + 0x34, 3);
        sub_057E0C();
        sub_00D740(a0, w.kill_sound);
        return link_state_packet(a0, w.link);
    }
    if (w.state_before_target) wr8(a0 + 0x34, 0);
    uint32_t a3 = rd32(a0 + 6);
    if (!a3) {
        loc_0160C4(a0);
        return;
    }
    wr8(a0 + 0x34, 0);
    uint32_t r = uint32_t(int32_t(Random()) >> 4);
    wr16(a0 + 0x38, uint16_t((r & 0x1FF) - 0x100 + rd16(a3 + 0x20) - rd16(a3 + 0x2A)));
    wr16(a0 + 0x3A, uint16_t(((r >> 16) & 0x1FF) - 0x100 + rd16(a3 + 0x22) - rd16(a3 + 0x2C)));
}

// 0x18C02 / 0x1909E
void walker_decide(uint32_t a0, const Walker& w) {
    uint32_t a3 = rd32(a0 + 6);
    if (!a3) {
        loc_0160C4(a0);
        return;
    }
    uint16_t r = uint16_t(Random() >> 16) & 0xF;
    uint16_t d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    wr8(a0 + 0x35, uint8_t((s16(d) >> 7) + r));
    walker_new_spot(a0, w);
}

void walker(uint32_t a0, const Walker& w) {
    wr8(a0 + 0x1F, 0);
    if (rd8(a0 + 0x34) == 2) {
        uint16_t d = Distance(s16(rd16(a0 + 0x2A)), s16(rd16(a0 + 0x2C)));
        wr8(a0 + 0x35, 1);
        if (d < 10) return walker_new_spot(a0, w);
        sub_0146FE(a0, rd16(a0 + 0x2A), rd16(a0 + 0x2C));
        wr16(a0 + 0x2A, uint16_t(s16(rd16(a0 + 0x2A)) >> 1));
        wr16(a0 + 0x2C, uint16_t(s16(rd16(a0 + 0x2C)) >> 1));
        return;
    }
    uint16_t vx = clamp_step(rd16(a0 + 0x38), rd16(a0 + 0x20), w.limit);
    uint16_t vy = clamp_step(rd16(a0 + 0x3A), rd16(a0 + 0x22), w.limit);
    wr16(a0 + 0x2A, vx);
    wr16(a0 + 0x2C, vy);
    uint16_t px = uint16_t((vx << 3) + rd16(a0 + 0x20)), py = uint16_t((vy << 3) + rd16(a0 + 0x22));
    uint8_t shape = sub_013D42(a0, px, py);
    if (shape == 6 || shape == 7) {
        wr16(a0 + 0x20, px);
        wr16(a0 + 0x22, py);
        uint16_t cx = px & 0xFF00, cy = py & 0xFF00;
        uint32_t cell = enemy_map(a0) + int16_t(s16(cx) >> 8) + int16_t(s16(cy) >> 3);
        if (shape == 6)
            sub_00B1C4(cell, cx, cy);
        else
            sub_00B202(cell, cx, cy);
        sub_00D740(a0, 0x67);
    }
    vx = uint16_t(rd16(a0 + 0x2A) - (s16(rd16(a0 + 0x2A)) >> 2));
    vy = uint16_t(rd16(a0 + 0x2C) - (s16(rd16(a0 + 0x2C)) >> 2));
    sub_0146FE(a0, vx, vy);
    if (rd8(a0 + 0x34) != 1) {
        wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
        if (rd8(a0 + 0x35)) {
            loc_0160C4(a0);
            return;
        }
        wr8(a0 + 0x34, 1);
        wr8(a0 + 0x35, 0xA);
    }
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
    uint8_t t = rd8(a0 + 0x35);
    if (t == 0) return walker_decide(a0, w);
    if (t < w.fire_lo || t > w.fire_hi) return;
    uint32_t a3 = rd32(a0 + 6);
    if (!a3) {
        loc_0160C4(a0);
        return;
    }
    if (sub_0166A0(a0, rd16(a3 + 0x20), rd16(a3 + 0x22), rd16(a0 + 0x20), rd16(a0 + 0x22))) return walker_decide(a0, w);
    w.fire(a0, a3);
}

// 0x18DEC: shoots like type 1 with damage 0x1F4.
void t9_fire(uint32_t a0, uint32_t a3) {
    uint16_t chance = 0x400;
    bool sure = false;
    if (s16(U16{0xFF0E1A}) >= 0) {
        if (!U16{0xFF0EB2}) sure = true;
        chance = 0x300;
    } else {
        chance = 0x200;
        if (U16{0xFF0EB2}) chance = 0x17B;
    }
    if (sure || ((int32_t(Random()) >> 8) & 0x3FF) < chance) {
        uint16_t dx = uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20)), dy = uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22));
        if (Distance(s16(dx), s16(dy)) >= 0x400) return;
        CallEnemyHit(rd32(a3 + 0xA), a3, 0x1F4, dx, dy);
    }
    sub_00D740(a0, 0x1E);
}

// 0x19280: throws a 0x15F6E shot at the target, 1/64 of the distance per
// frame.
void t5_fire(uint32_t a2, uint32_t a3) {
    uint32_t a0 = sub_013466();
    if (!a0) return;
    uint16_t r = uint16_t(Random() >> 16);
    uint16_t d3 = uint16_t(rd16(a3 + 0x20) - rd16(a2 + 0x20)), d4 = uint16_t(rd16(a3 + 0x22) - rd16(a2 + 0x22));
    uint16_t d = uint16_t((s16(Distance(s16(d3), s16(d4))) >> 6) + 1);
    if (d) {
        d3 = uint16_t(divs(ext_l(d3), d));
        d4 = uint16_t(divs(ext_l(d4), d));
    }
    sub_015F38(a0, uint16_t((s16(d3) >> 1) + rd16(a2 + 0x20)), uint16_t((s16(d4) >> 1) + rd16(a2 + 0x22)), d3, d4, 8,
               uint16_t((r & 7) - 4));
    if (link_mode) {
        wr32(a0 + 0x16, 0x13554);
        wr32(a0 + 0x1A, 0x16F56);
        wr8(0xFF0FFA, 4);
        wr8(0xFF0FFB, rd8(a0 + 0x3E));
        wr16(0xFF0FFC, rd16(a0 + 0x20));
        wr16(0xFF0FFE, rd16(a0 + 0x22));
        wr8(0xFF1000, rd8(a0 + 0x25));
        wr8(0xFF1001, uint8_t(rd8(a0 + 5) | 0x20));
        wr8(0xFF1002, rd8(a0 + 0x32));
        wr8(0xFF1003, 6);
        wr16(0xFF1004, rd16(a0 + 0x2A));
        wr16(0xFF1006, rd16(a0 + 0x2C));
        sub_01C03A(0xFF0FFA);
    }
    sub_00D740(a0, 0x96);
}

const Walker type9{0x10, true, 4, 6, t9_fire, 0x31, 0x18F5C, 0x1901C, 0x18F50, 0x16F88};
const Walker type5{0xC, false, 5, 5, t5_fire, 0x29, 0x1943C, 0x194FC, 0x19430, 0x16F8E};

} // namespace

void h_018BC4(uint32_t a0) { walker(a0, type9); }
void h_019060(uint32_t a0) { walker(a0, type5); }

// 0x18EE8 / 0x193C8: animations for knocked back (alive, dead), firing
// (counts 4-6) and walking.
void walker_draw(uint32_t a0, uint16_t d5, uint16_t d1, const uint16_t anim[5]) {
    uint16_t a = anim[4];
    if (rd8(a0 + 0x34) == 2)
        a = s16(rd16(a0 + 0x36)) < 0 ? anim[1] : anim[0];
    else if (rd8(a0 + 0x34) == 1) {
        uint8_t t = rd8(a0 + 0x35);
        a = t >= 4 && t <= 6 ? anim[2] : anim[3];
    }
    sub_01BA04(a0, a, 0xFFFF, s16(d1), d5);
}

void h_018EE8(uint32_t a0, uint16_t d5, uint16_t d1) {
    static const uint16_t anim[5] = {1, 2, 6, 5, 0};
    walker_draw(a0, d5, d1, anim);
}

void h_0193C8(uint32_t a0, uint16_t d5, uint16_t d1) {
    static const uint16_t anim[5] = {3, 4, 2, 1, 0};
    walker_draw(a0, d5, d1, anim);
}

// 0x18F50 / 0x19430 and copies: the body, walking frames.
void body_walk(uint32_t a0, uint16_t anim, uint16_t d5, uint16_t d1) { sub_01BA04(a0, anim, 0xFFFF, s16(d1), d5); }

// 0x18F5C (and its copies)
void h_018F5C(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    slow_down(a0);
    loc_0161B2(a0);
}

// 0x18F9E (and 0x1947E, 0x1A84A with state 4): hits count only while walking after the first
// steps; a kill sprays two rounds of blood.
void walker_hit(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4, uint8_t state) {
    if (rd8(a0 + 0x34) != 0 || rd8(a0 + 0x35) < 2) return;
    wr8(a0 + 0x34, state);
    wr8(a0 + 0x35, 0);
    wr16(a0 + 0x2A, 0);
    wr16(a0 + 0x2C, 0);
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    if (s16(rd16(a0 + 0x36)) >= 0) {
        wr16(a0 + 0x36, uint16_t(rd16(a0 + 0x36) - f));
        if (s16(rd16(a0 + 0x36)) < 0) {
            sub_0157CA(a0, 0x400, dir.d0, dir.d1);
            sub_0157CA(a0, 0x400, dir.d0, dir.d1);
        }
    }
    set_knock(a0, dir, int16_t(f >> 3));
}

void h_018F9E(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4) { walker_hit(a0, d0, d3, d4, 2); }

// 0x1901C (and its copies): pushed away by a hit.
void h_01901C(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    wr16(a0 + 0x2A, 0);
    wr16(a0 + 0x2C, 0);
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
void t6_wander(uint32_t a0) {
    if (s16(rd16(a0 + 0x36)) >= 0) {
        uint32_t r = uint32_t(int32_t(Random()) >> 4);
        wr16(a0 + 0x38, uint16_t((r & 0x3FF) - 0x200 + rd16(a0 + 0x20)));
        wr16(a0 + 0x3A, uint16_t(((r >> 16) & 0x3FF) - 0x200 + rd16(a0 + 0x22)));
        wr8(a0 + 0x34, 0);
        return;
    }
    // 0x197FA: killed.
    sub_00D740(a0, 0x2B);
    if (rd8(a0 + 0x30)) return loc_01B8D8(a0);
    wr16(a0 + 4, rd16(a0 + 4) & 0xFF2F);
    wr32(a0 + 0x12, 0x19958);
    wr8(a0 + 0x1F, 0);
    wr32(a0 + 0xA, 0x19A04);
    wr32(a0 + 0x16, 0x14454);
    wr32(a0 + 0xE, 0x1994C);
    wr16(a0 + 0x2A, 0);
    wr16(a0 + 0x2C, 0);
    wr8(a0 + 0x34, 4);
    sub_057E0C();
    link_state_packet(a0, 0x16F94);
}

// 0x19616: the charge ends: strikes the target within 0x80 (0x100 on
// diagonal cells) and turns back.
void t6_strike(uint32_t a0) {
    wr8(a0 + 0x34, 2);
    wr16(a0 + 0x38, rd16(a0 + 0x48));
    wr16(a0 + 0x3A, rd16(a0 + 0x4A));
    uint32_t a3 = rd32(a0 + 6);
    uint16_t d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    uint8_t cls = rd8(cell_class + rd8(enemy_map(a3) + int16_t(s16(rd16(a3 + 0x20)) >> 8) +
                                       int16_t(s16(uint16_t(rd16(a3 + 0x22) & 0xFF00)) >> 3)));
    if (cls >= 2 && cls <= 5) d = uint16_t(s16(d) >> 1);
    if (d >= 0x80) return;
    uint16_t d3 = uint16_t(s16(uint16_t(rd16(a0 + 0x48) - rd16(a3 + 0x20))) >> 1);
    uint16_t d4 = uint16_t(s16(uint16_t(rd16(a0 + 0x4A) - rd16(a3 + 0x22))) >> 1);
    CallEnemyHit(rd32(a3 + 0xA), a3, 0x28A, d3, d4);
    if (a3 == player_struct) {
        U16{0xFF0DF0} = uint16_t(U16{0xFF0DF0} << 1);
        U16{0xFF0DF2} = uint16_t(U16{0xFF0DF2} << 1);
    }
    sub_00D740(a0, 0x1B);
}

} // namespace

void h_019540(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    uint8_t state = rd8(a0 + 0x34);
    if (state == 3) {
        wr8(a0 + 0x35, 0);
        if (Distance(s16(rd16(a0 + 0x2A)), s16(rd16(a0 + 0x2C))) < 10) return t6_wander(a0);
        sub_0146FE(a0, rd16(a0 + 0x2A), rd16(a0 + 0x2C));
        wr16(a0 + 0x2A, uint16_t(s16(rd16(a0 + 0x2A)) >> 1));
        wr16(a0 + 0x2C, uint16_t(s16(rd16(a0 + 0x2C)) >> 1));
        return;
    }
    if (state == 0) {
        if (!rd8(a0 + 0x35)) {
            uint32_t a3 = rd32(a0 + 6);
            uint16_t d = Distance(s16(uint16_t(rd16(a3 + 0x20) - rd16(a0 + 0x20))), s16(uint16_t(rd16(a3 + 0x22) - rd16(a0 + 0x22))));
            if (d < 0x300) {
                // 0x19882: charge.
                wr8(a0 + 0x34, 1);
                d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
                wr8(a0 + 0x35, uint8_t((s16(d) >> 6) + 6));
                wr16(a0 + 0x48, rd16(a0 + 0x20));
                wr16(a0 + 0x4A, rd16(a0 + 0x22));
                wr16(a0 + 0x38, rd16(a3 + 0x20));
                wr16(a0 + 0x3A, rd16(a3 + 0x22));
                return;
            }
        } else
            wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
        uint16_t vx = clamp_shift(rd16(a0 + 0x38), rd16(a0 + 0x20), 3, 0x14);
        uint16_t vy = clamp_shift(rd16(a0 + 0x3A), rd16(a0 + 0x22), 3, 0x14);
        wr16(a0 + 0x2A, vx);
        wr16(a0 + 0x2C, vy);
        if (move_dist(sub_0146FE(a0, vx, vy)) <= 10) return t6_wander(a0);
        return loc_016084(a0);
    }
    if (state == 2) {
        uint16_t vx = clamp_shift(rd16(a0 + 0x38), rd16(a0 + 0x20), 2, 0x48);
        uint16_t vy = clamp_shift(rd16(a0 + 0x3A), rd16(a0 + 0x22), 3, 0x48);
        wr16(a0 + 0x2A, vx);
        wr16(a0 + 0x2C, vy);
        uint16_t d = move_dist(sub_0146FE(a0, vx, vy));
        wr8(a0 + 0x35, 0x14);
        if (d <= 10) return t6_wander(a0);
        return loc_016084(a0);
    }
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
    if (!rd8(a0 + 0x35)) return t6_strike(a0);
    if (!(rd8(a0 + 0x35) & 3)) {
        uint32_t a3 = rd32(a0 + 6);
        wr16(a0 + 0x38, rd16(a3 + 0x20));
        wr16(a0 + 0x3A, rd16(a3 + 0x22));
    }
    uint16_t vx = clamp_shift(rd16(a0 + 0x38), rd16(a0 + 0x20), 1, 0x48);
    uint16_t vy = clamp_shift(rd16(a0 + 0x3A), rd16(a0 + 0x22), 1, 0x48);
    wr16(a0 + 0x2A, vx);
    wr16(a0 + 0x2C, vy);
    if (move_dist(sub_0146FE(a0, vx, vy)) <= 10) return t6_strike(a0);
    loc_016084(a0);
}

// 0x198C4
void h_0198C4(uint32_t a0, uint16_t d5, uint16_t d1) {
    uint16_t anim = 0;
    uint8_t state = rd8(a0 + 0x34);
    if (state == 3)
        anim = s16(rd16(a0 + 0x36)) < 0 ? 4 : 3;
    else if (state == 1 && rd32(a0 + 6) == player_struct) {
        uint32_t a3 = player_struct;
        anim = 2;
        if (rd8(a0 + 0x35) >= 6 &&
            Distance(s16(uint16_t(rd16(a3 + 0x20) - rd16(a0 + 0x20))), s16(uint16_t(rd16(a3 + 0x22) - rd16(a0 + 0x22)))) >= 0x100)
            anim = 1;
    }
    sub_01BA04(a0, anim, 0xFFFF, s16(d1), d5);
}

// 0x19958 (and its copies): like 0x1B97A, sliding with the wide margin.
void h_019958(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    auto half = [](uint16_t v) { return uint16_t(s16(v) < 0 ? -(-s16(v) >> 1) : s16(v) >> 1); };
    uint16_t vx = half(rd16(a0 + 0x2A)), vy = half(rd16(a0 + 0x2C));
    wr16(a0 + 0x2A, vx);
    wr16(a0 + 0x2C, vy);
    if (vx | vy) sub_0146FE(a0, vx, vy);
    loc_0160E8(a0);
}

void h_01999A(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4) { knock_hit(a0, d0, d3, d4, 3); }

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
    uint32_t dead_ai, dead_hit, dead_draw;
    void (*decide)(uint32_t a0);
};

// 0x19A80 / 0x1B0B0
void soldier_decide(uint32_t a0, const Soldier& w) {
    if (s16(rd16(a0 + 0x36)) < 0) {
        // 0x19E42 / 0x1B492: killed.
        sub_00D740(a0, w.kill_sound);
        if (rd8(a0 + 0x30)) return loc_01B8D8(a0);
        wr16(a0 + 4, rd16(a0 + 4) & 0xFF2F);
        wr32(a0 + 0x12, w.dead_ai);
        wr8(a0 + 0x1F, 0);
        wr32(a0 + 0xA, w.dead_hit);
        wr32(a0 + 0x16, 0x14454);
        wr32(a0 + 0xE, w.dead_draw);
        wr16(a0 + 0x2A, 0);
        wr16(a0 + 0x2C, 0);
        wr8(a0 + 0x34, 3);
        sub_057E0C();
        return link_state_packet(a0, 0x16F9A);
    }
    uint32_t a3 = rd32(a0 + 6);
    uint16_t d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    if (w.far_check && d >= 0x400) {
        wr16(a0 + 0x38, rd16(a3 + 0x20));
        wr16(a0 + 0x3A, rd16(a3 + 0x22));
        wr8(a0 + 0x35, 0xF);
        wr8(a0 + 0x34, 0);
        return;
    }
    uint16_t r = uint16_t(Random() >> 16) & w.spot_mask;
    wr8(a0 + 0x35, uint8_t((s16(d) >> 6) + r));
    wr8(a0 + 0x34, 0);
    uint32_t r2 = Random();
    // A spot beside the target's path, on the side picked by the slot number.
    uint16_t x = uint16_t(-(s16(rd16(a3 + 0x2C)) >> w.side_shift)), y = uint16_t(s16(rd16(a3 + 0x2A)) >> w.side_shift);
    if (rd8(a0 + 0x3E) & 1) {
        x = uint16_t(-x);
        y = uint16_t(-y);
    }
    x = uint16_t(x + rd16(a3 + 0x20));
    y = uint16_t(y + rd16(a3 + 0x22));
    if (!w.side_shift) {
        x = uint16_t(x + (s16(rd16(a3 + 0x2A)) >> 1));
        y = uint16_t(y + (s16(rd16(a3 + 0x2C)) >> 1));
    }
    r2 = uint32_t(int32_t(r2) >> 4);
    wr16(a0 + 0x38, uint16_t(x + (r2 & 0x1FF) - 0x100));
    wr16(a0 + 0x3A, uint16_t(y + ((r2 >> 16) & 0x1FF) - 0x100));
}

// 0x19D68 / 0x1B3B8: throws a 0x13ADC bomb at count 4.
void soldier_throw(uint32_t a0, const Soldier& w) {
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
    if (!rd8(a0 + 0x35)) return w.decide(a0);
    if (rd8(a0 + 0x35) != 4) return;
    sub_00D740(a0, 0x1B);
    uint32_t e = sub_013466();
    if (!e) return;
    wr8(e + 0x1F, 0);
    wr32(e + 0x12, 0x13ADC);
    wr32(e + 0xE, 0x13F3E);
    wr32(e + 0xA, 0x13AD2);
    wr16(e + 4, rd16(e + 4) | 0xC8);
    wr8(e + 0x34, 0x28);
    uint32_t a2 = rd32(a0 + 6);
    uint16_t vx = uint16_t(s16(uint16_t(rd16(a2 + 0x20) - rd16(a0 + 0x20))) >> 4);
    wr16(e + 0x2A, vx);
    wr16(e + 0x20, uint16_t((s16(vx) >> 1) + rd16(a0 + 0x20)));
    uint16_t vy = uint16_t(s16(uint16_t(rd16(a2 + 0x22) - rd16(a0 + 0x22))) >> 4);
    wr16(e + 0x2C, vy);
    wr16(e + 0x22, uint16_t((s16(vy) >> 1) + rd16(a0 + 0x22)));
    wr16(e + 0x24, 0xFFFA);
    wr16(e + 0x2E, 8);
    if (!link_mode) return;
    wr32(e + 0x1A, 0x16DC4);
    wr8(0xFF0FFA, 4);
    wr8(0xFF0FFB, rd8(e + 0x3E));
    wr16(0xFF0FFC, rd16(e + 0x20));
    wr16(0xFF0FFE, rd16(e + 0x22));
    wr8(0xFF1000, rd8(e + 0x25));
    wr8(0xFF1001, uint8_t(rd8(e + 5) | 0x20));
    wr8(0xFF1002, rd8(e + 0x32));
    wr8(0xFF1003, 0);
    wr16(0xFF1004, rd16(e + 0x2A));
    wr16(0xFF1006, rd16(e + 0x2C));
    sub_01C03A(0xFF0FFA);
}

// 0x19CA0 / 0x1B2F0: aims and fires (shot at count 6).
void soldier_aim(uint32_t a0, const Soldier& w) {
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
    uint8_t t = rd8(a0 + 0x35);
    if (t == 0) return w.decide(a0);
    if (t == 6) return t1_fire(a0, w.decide);
    if (t == 5 || t == 4) t1_shot_sound(a0);
}

void t7_decide(uint32_t a0);
void t0_decide(uint32_t a0);
const Soldier type7{true, 0xF, 0, 0xF, 0x21, 0x19FD2, 0x1A1CC, 0x19FCA, t7_decide};
const Soldier type0{false, 0x1F, 1, 7, 0x1F, 0x1B6A2, 0x1B890, 0x1B68C, t0_decide};
void t7_decide(uint32_t a0) { soldier_decide(a0, type7); }
void t0_decide(uint32_t a0) { soldier_decide(a0, type0); }

void soldier(uint32_t a0, const Soldier& w) {
    uint16_t vx = clamp_shift(rd16(a0 + 0x38), rd16(a0 + 0x20), 3, 0x2D);
    uint16_t vy = clamp_shift(rd16(a0 + 0x3A), rd16(a0 + 0x22), 3, 0x2D);
    wr16(a0 + 0x2A, vx);
    wr16(a0 + 0x2C, vy);
    uint16_t px = uint16_t(vx + rd16(a0 + 0x20)), py = uint16_t(vy + rd16(a0 + 0x22));
    uint8_t shape = sub_013D42(a0, px, py);
    if (shape == 6 || shape == 7) {
        wr16(a0 + 0x20, px);
        wr16(a0 + 0x22, py);
        uint16_t cx = px & 0xFF00, cy = py & 0xFF00;
        uint32_t cell = enemy_map(a0) + int16_t(s16(cx) >> 8) + int16_t(s16(cy) >> 3);
        if (shape == 6)
            sub_00B1C4(cell, cx, cy);
        else
            sub_00B202(cell, cx, cy);
        sub_00D740(a0, 0x67);
    }
    sub_0145AA(a0, rd16(a0 + 0x2A), rd16(a0 + 0x2C));
    if (rd8(a0 + 0x34) == 1) return soldier_aim(a0, w);
    if (rd8(a0 + 0x34) == 5) return soldier_throw(a0, w);
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
    if (rd8(a0 + 0x35)) return loc_016084(a0);
    wr8(a0 + 0x34, 1);
    wr8(a0 + 0x35, 0xA);
    if (uint16_t(Random() >> 16) & w.throw_mask) return soldier_aim(a0, w);
    uint32_t a3 = rd32(a0 + 6);
    uint16_t d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    if (d < 0x100 || sub_0166A0(a0, rd16(a3 + 0x20), rd16(a3 + 0x22), rd16(a0 + 0x20), rd16(a0 + 0x22))) return soldier_aim(a0, w);
    wr8(a0 + 0x34, 5);
    soldier_throw(a0, w);
}

// Knock-back state 2 of the soldiers.
bool soldier_knocked(uint32_t a0, const Soldier& w) {
    if (rd8(a0 + 0x34) != 2) return false;
    if (Distance(s16(rd16(a0 + 0x2A)), s16(rd16(a0 + 0x2C))) < 10)
        w.decide(a0);
    else {
        sub_0145AA(a0, rd16(a0 + 0x2A), rd16(a0 + 0x2C));
        wr16(a0 + 0x2A, uint16_t(s16(rd16(a0 + 0x2A)) >> 1));
        wr16(a0 + 0x2C, uint16_t(s16(rd16(a0 + 0x2C)) >> 1));
    }
    return true;
}

// 0x1B51A: type 0 standing (state 6); at count 9 it becomes a type 7.
void t0_stand(uint32_t a0) {
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) + 1));
    if (rd8(a0 + 0x35) != 9) return;
    wr32(a0 + 0x12, 0x19A48);
    wr32(a0 + 0xE, 0x19ECA);
    wr32(a0 + 0x42, 0x17BA80);
    wr8(a0 + 0x46, 0x69);
    wr8(a0 + 0x47, 0x72);
    wr32(a0 + 0x1A, 0x17A8A);
    wr8(a0 + 0x41, 0x1E);
    wr8(a0 + 0x34, 0);
}

} // namespace

void h_019A48(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    if (soldier_knocked(a0, type7)) return;
    soldier(a0, type7);
}

// 0x1B078: +0x41 counts down from 0x32-0x51 to a stop 1/8 of the way to its
// spot.
void h_01B078(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    if (soldier_knocked(a0, type0)) return;
    if (rd8(a0 + 0x34) == 6) return t0_stand(a0);
    if (!rd8(a0 + 0x41))
        wr8(a0 + 0x41, uint8_t(((s16(uint16_t(Random())) >> 8) & 0x1F) + 0x32));
    else {
        wr8(a0 + 0x41, uint8_t(rd8(a0 + 0x41) - 1));
        if (!rd8(a0 + 0x41)) {
            wr8(a0 + 0x34, 6);
            wr8(a0 + 0x35, 0);
            wr16(a0 + 0x38, uint16_t((s16(uint16_t(rd16(a0 + 0x38) - rd16(a0 + 0x20))) >> 3) + rd16(a0 + 0x20)));
            wr16(a0 + 0x3A, uint16_t((s16(uint16_t(rd16(a0 + 0x3A) - rd16(a0 + 0x22))) >> 3) + rd16(a0 + 0x22)));
            return t0_stand(a0);
        }
    }
    soldier(a0, type0);
}

// 0x19ECA: aiming frames face the player (5-7) or another target (8-10);
// throwing 0xB / 0xC.
void h_019ECA(uint32_t a0, uint16_t d5, uint16_t d1) {
    uint16_t anim = 0;
    uint8_t t = rd8(a0 + 0x35);
    switch (rd8(a0 + 0x34)) {
    case 2: anim = s16(rd16(a0 + 0x36)) < 0 ? 2 : 1; break;
    case 1: {
        uint16_t base = rd32(a0 + 6) == player_struct ? 5 : 8;
        anim = t >= 6 || t == 1 ? base : t == 4 || t == 2 ? uint16_t(base + 1) : uint16_t(base + 2);
        break;
    }
    case 5: anim = t < 7 && t > 3 ? 0xC : 0xB; break;
    }
    sub_01BA04(a0, anim, 0xFFFF, s16(d1), d5);
}

// 0x19FD2: the body slides to a stop; the player walking over it picks up
// its gun (weapon 10 with 2-3 clips or weapon 7 with 1-2).
void h_019FD2(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    slow_down(a0);
    uint32_t a3 = rd32(a0 + 6);
    uint16_t d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    if (d >= 0x60) return loc_0160E8(a0);
    if (rd8(a0 + 0x34) != 3) return;
    uint32_t r = Random();
    bool big = r & 0x10;
    uint16_t weapon = big ? 0xA : 7, base = big ? 2 : 1;
    if (a3 == player_struct) {
        U16{0xFF1034} = uint16_t((((r >> 16) & 1) + base) << 8);
        uint16_t slot = sub_011084(weapon);
        U16{0xFF1034} = 0;
        if (slot == 0xFFFF) return;
        wr8(a0 + 0x34, 4);
        sub_00D740(a0, 0x69);
        return sub_01E0C2(big ? 0x1E49E : 0x1E40C);
    }
    wr8(a0 + 0x34, 4);
    wr8(0xFF0FFA, 0x13);
    wr8(0xFF0FFB, uint8_t(weapon));
    wr8(0xFF0FFC, uint8_t(((r >> 8) & 1) + base));
    sub_01C03A(0xFF0FFA);
}

void h_01A154(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4) { knock_hit(a0, d0, d3, d4, 2, true); }

// ---- Type 8 (0x1A324-0x1A90A): AI 0x1A324, draw 0x1A722, hit 0x1A84A;
// dead 0x1A808 (= 0x18F5C) / 0x1A7FC / 0x1A8C8 (= 0x1901C). Opens doors;
// strikes (states 2, 3) or shoots (state 1). The first time it is killed it
// plays dead (state 6) and gets up again with 0x3E8 health.

namespace {

// 0x1A39C
void t8_new_spot(uint32_t a0) {
    if (s16(rd16(a0 + 0x36)) < 0) {
        if (!rd8(a0 + 0x41)) {
            wr8(a0 + 0x34, 6);
            wr8(a0 + 0x35, 0x19);
            return;
        }
        // 0x1A696: killed.
        U16{0xFF2724} = 0xF;
        sub_01A90C();
        wr16(a0 + 4, rd16(a0 + 4) & 0xFF2F);
        wr32(a0 + 0x12, 0x1A808);
        wr8(a0 + 0x1F, 0);
        wr32(a0 + 0xA, 0x1A8C8);
        wr32(a0 + 0x16, 0x144FE);
        wr32(a0 + 0xE, 0x1A7FC);
        wr16(a0 + 0x2A, 0);
        wr16(a0 + 0x2C, 0);
        wr8(a0 + 0x34, 5);
        sub_057E0C();
        sub_00D740(a0, 0x2F);
        return link_state_packet(a0, 0x16FA0);
    }
    uint32_t a3 = rd32(a0 + 6);
    if (!a3) {
        loc_0160C4(a0);
        return;
    }
    wr8(a0 + 0x34, 0);
    uint32_t r = uint32_t(int32_t(Random()) >> 4);
    wr16(a0 + 0x38, uint16_t((r & 0xFF) - 0x80 + rd16(a3 + 0x20)));
    wr16(a0 + 0x3A, uint16_t(((r >> 16) & 0xFF) - 0x80 + rd16(a3 + 0x22)));
}

// 0x1A362
void t8_decide(uint32_t a0) {
    uint32_t a3 = rd32(a0 + 6);
    if (!a3) {
        loc_0160C4(a0);
        return;
    }
    uint16_t r = uint16_t(Random() >> 16) & 0xF;
    uint16_t d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    wr8(a0 + 0x35, uint8_t((s16(d) >> 7) + r));
    t8_new_spot(a0);
}

// 0x1A58A: strikes the target within 0x80 at count 5.
void t8_strike(uint32_t a0) {
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
    if (!rd8(a0 + 0x35)) return t8_decide(a0);
    if (rd8(a0 + 0x35) != 5) return;
    uint32_t a3 = rd32(a0 + 6);
    if (!a3) {
        loc_0160C4(a0);
        return;
    }
    uint16_t dx = uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20)), dy = uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22));
    if (Distance(s16(dx), s16(dy)) >= 0x80) return;
    CallEnemyHit(rd32(a3 + 0xA), a3, 0x100, dx, dy);
    sub_00D740(a0, 0x1B);
}

// 0x1A5E6: shoots like type 9 at counts 4-6.
void t8_shoot(uint32_t a0) {
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
    uint8_t t = rd8(a0 + 0x35);
    if (!t) return t8_decide(a0);
    if (t > 6 || t < 4) return;
    uint32_t a3 = rd32(a0 + 6);
    if (!a3) {
        loc_0160C4(a0);
        return;
    }
    if (sub_0166A0(a0, rd16(a3 + 0x20), rd16(a3 + 0x22), rd16(a0 + 0x20), rd16(a0 + 0x22))) return t8_decide(a0);
    t9_fire(a0, a3);
}

} // namespace

void h_01A324(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    uint8_t state = rd8(a0 + 0x34);
    if (state == 4) {
        uint16_t d = Distance(s16(rd16(a0 + 0x2A)), s16(rd16(a0 + 0x2C)));
        wr8(a0 + 0x35, 1);
        if (d < 10) return t8_new_spot(a0);
        sub_0146FE(a0, rd16(a0 + 0x2A), rd16(a0 + 0x2C));
        wr16(a0 + 0x2A, uint16_t(s16(rd16(a0 + 0x2A)) >> 1));
        wr16(a0 + 0x2C, uint16_t(s16(rd16(a0 + 0x2C)) >> 1));
        return;
    }
    if (state == 6) {
        wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
        if (rd8(a0 + 0x35)) return;
        wr16(a0 + 0x36, 0x3E8);
        wr8(a0 + 0x41, 1);
        return t8_new_spot(a0);
    }
    uint16_t vx = clamp_shift(rd16(a0 + 0x38), rd16(a0 + 0x20), 2, 0x20);
    uint16_t vy = clamp_shift(rd16(a0 + 0x3A), rd16(a0 + 0x22), 2, 0x20);
    wr16(a0 + 0x2A, vx);
    wr16(a0 + 0x2C, vy);
    uint16_t px = uint16_t((vx << 1) + rd16(a0 + 0x20)), py = uint16_t((vy << 1) + rd16(a0 + 0x22));
    uint8_t shape = sub_013D42(a0, px, py);
    if (shape == 6 || shape == 7) {
        wr16(a0 + 0x20, px);
        wr16(a0 + 0x22, py);
        uint16_t cx = px & 0xFF00, cy = py & 0xFF00;
        uint32_t cell = enemy_map(a0) + int16_t(s16(cx) >> 8) + int16_t(s16(cy) >> 3);
        if (shape == 6)
            sub_00B1C4(cell, cx, cy);
        else
            sub_00B202(cell, cx, cy);
        sub_00D740(a0, 0x67);
    }
    vx = uint16_t(rd16(a0 + 0x2A) - (s16(rd16(a0 + 0x2A)) >> 2));
    vy = uint16_t(rd16(a0 + 0x2C) - (s16(rd16(a0 + 0x2C)) >> 2));
    EnemyMove m = sub_0146FE(a0, vx, vy);
    state = rd8(a0 + 0x34);
    if (state == 1) return t8_shoot(a0);
    if (state == 2 || state == 3) return t8_strike(a0);
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
    if (rd8(a0 + 0x35) && move_dist(m) >= 10) {
        loc_0160C4(a0);
        return;
    }
    uint32_t r = Random();
    if ((r & 0x80) && s16(rd16(a0 + 0x36)) < 0x7D0) {
        wr8(a0 + 0x34, 1);
        wr8(a0 + 0x35, 0xA);
        return t8_shoot(a0);
    }
    wr8(a0 + 0x34, (r >> 16) & 1 ? 2 : 3);
    wr8(a0 + 0x35, 0xA);
    t8_strike(a0);
}

// 0x1A722: lying dead flickers between 7 and 8 at random until it gets up.
void h_01A722(uint32_t a0, uint16_t d5, uint16_t d1) {
    uint16_t anim = 0;
    uint8_t t = rd8(a0 + 0x35);
    switch (rd8(a0 + 0x34)) {
    case 4: anim = s16(rd16(a0 + 0x36)) < 0 ? 6 : 5; break;
    case 6: anim = t >= 5 ? ((Random() >> 16) & 0xF ? 7 : 8) : 0xA; break;
    case 1: anim = t > 3 && t < 7 ? 4 : 1; break;
    case 2: case 3: anim = t > 3 && t < 7 ? 2 : 1; break;
    }
    sub_01BA04(a0, anim, 0xFFFF, s16(d1), d5);
}

void h_01A84A(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4) { walker_hit(a0, d0, d3, d4, 4); }

// ---- Type 4 (0x1A92A-0x1B076): AI 0x1A92A, draw 0x1AD9A, hit 0x1AFC6; dead
// 0x1AEB4 / 0x1AE9E / 0x1B030 (= 0x1B9BC). Patrols (states 0-7 = eight
// directions, turning at blocked cells) until it sees the target, then
// chases (8) and shoots (9), alerting the other patrols within 0x400.
// State 0xA is the knock-back of a hit.

namespace {

// 0x1ABE0: chase a spot near where the target is heading.
void t4_chase(uint32_t a0) {
    uint32_t a3 = rd32(a0 + 6);
    uint16_t r = uint16_t(Random() >> 16) & 0xF;
    uint16_t d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    wr8(a0 + 0x35, uint8_t((s16(d) >> 7) + r));
    wr8(a0 + 0x34, 8);
    uint32_t r2 = uint32_t(int32_t(Random()) >> 4);
    wr16(a0 + 0x38, uint16_t((r2 & 0x1FF) - 0x100 + rd16(a3 + 0x20) + rd16(a3 + 0x2A)));
    wr16(a0 + 0x3A, uint16_t(((r2 >> 16) & 0x1FF) - 0x100 + rd16(a3 + 0x22) + rd16(a3 + 0x2C)));
}

// 0x1AB6C: the other patrolling type 4s within 0x400 start chasing. Returns
// d0, which the hit handler goes on to use.
uint16_t sub_01AB6C(uint32_t a0, uint16_t d0) {
    uint16_t n = U16{0xFF24E8};
    if (!n) return d0;
    for (uint32_t a1 = U32{0xFF24EA}; n; n--) {
        uint32_t next = rd32(a1);
        if (a1 != a0 && rd32(a1 + 0x12) == 0x1A92A && rd8(a1 + 0x34) < 8) {
            d0 = Distance(s16(uint16_t(rd16(a1 + 0x20) - rd16(a0 + 0x20))), s16(uint16_t(rd16(a1 + 0x22) - rd16(a0 + 0x22))));
            if (d0 <= 0x400) t4_chase(a1);
        }
        a1 = next;
    }
    sub_00D740(a0, 0x71);
    return 0x71;
}

// 0x1ABD8
void t4_decide(uint32_t a0) {
    if (s16(rd16(a0 + 0x36)) >= 0) return t4_chase(a0);
    // 0x1AAE4: killed.
    sub_00D740(a0, 0x27);
    if (rd8(a0 + 0x30)) return loc_01B8D8(a0);
    wr16(a0 + 4, rd16(a0 + 4) & 0xFF2F);
    wr32(a0 + 0x12, 0x1AEB4);
    wr8(a0 + 0x1F, 0);
    wr32(a0 + 0xA, 0x1B030);
    wr32(a0 + 0x16, 0x14454);
    wr32(a0 + 0xE, 0x1AE9E);
    wr16(a0 + 0x2A, 0);
    wr16(a0 + 0x2C, 0);
    wr8(a0 + 0x34, 0xB);
    sub_057E0C();
    link_state_packet(a0, 0x16FA6);
}

// 0x1AA28: aims, fires at count 6 (the chance and range as type 1, the
// distance counted double).
void t4_aim(uint32_t a0) {
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
    uint8_t t = rd8(a0 + 0x35);
    if (!t) return t4_decide(a0);
    if (t != 6) {
        if (t >= 4 && t <= 5) sub_00D740(a0, 0x1E);
        return loc_016084(a0);
    }
    uint32_t a3 = rd32(a0 + 6);
    if (sub_0166A0(a0, rd16(a3 + 0x20), rd16(a3 + 0x22), rd16(a0 + 0x20), rd16(a0 + 0x22))) return t4_decide(a0);
    uint16_t chance = 0x400;
    bool sure = false;
    if (s16(U16{0xFF0E1A}) >= 0) {
        if (!U16{0xFF0EB2}) sure = true;
        chance = 0x300;
    } else {
        chance = 0x200;
        if (U16{0xFF0EB2}) chance = 0x17B;
    }
    if (sure || ((int32_t(Random()) >> 8) & 0x3FF) < chance) {
        uint16_t dx = uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20)), dy = uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22));
        uint16_t d = uint16_t(Distance(s16(dx), s16(dy)) << 1);
        if (d >= 0x400) return;
        CallEnemyHit(rd32(a3 + 0xA), a3, d, dx, dy);
    }
    sub_00D740(a0, 0x1E);
}

// 0x1AC4E: patrol step (8 per frame) in the direction of the state.
void t4_patrol(uint32_t a0) {
    uint32_t a3 = rd32(a0 + 6);
    uint16_t d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    if (d <= 0x12C) {
        sub_01AB6C(a0, d);
        return t4_chase(a0);
    }
    static const int16_t dir[8][2] = {{0, -16}, {-16, -16}, {-16, 0}, {-16, 16}, {0, 16}, {16, 16}, {16, 0}, {16, -16}};
    uint8_t state = rd8(a0 + 0x34);
    uint8_t k = state < 7 ? state : 7;
    uint16_t dx = uint16_t(dir[k][0]), dy = uint16_t(dir[k][1]);
    if (rd8(a0 + 0x21) == 0x80 && rd8(a0 + 0x23) == 0x80) {
        uint16_t px = uint16_t((dx << 4) + rd16(a0 + 0x20)), py = uint16_t((dy << 4) + rd16(a0 + 0x22));
        uint8_t cls = rd8(cell_class + rd8(enemy_map(a0) + int16_t(s16(px) >> 8) + int16_t(s16(uint16_t(py & 0xFF00)) >> 3)));
        if (rd8(0x14858 + cls)) {
            wr8(a0 + 0x34, uint8_t((k + 1) & 7));
            return;
        }
    }
    wr16(a0 + 0x20, uint16_t(rd16(a0 + 0x20) + dx));
    wr16(a0 + 0x22, uint16_t(rd16(a0 + 0x22) + dy));
    uint16_t vx = uint16_t(dx << 1), vy = uint16_t(dy << 1);
    wr16(a0 + 0x2A, vx);
    wr16(a0 + 0x2C, vy);
    d = Distance(s16(uint16_t((vx << 4) + rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t((vy << 4) + rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    if (d <= 0x180 && !sub_0166A0(a0, rd16(a3 + 0x20), rd16(a3 + 0x22), rd16(a0 + 0x20), rd16(a0 + 0x22))) {
        sub_01AB6C(a0, d);
        t4_chase(a0);
    }
    loc_016084(a0);
}

} // namespace

void h_01A92A(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    uint8_t state = rd8(a0 + 0x34);
    if (state == 0xA) {
        wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
        if (!rd8(a0 + 0x35)) return t4_decide(a0);
        sub_0145AA(a0, rd16(a0 + 0x2A), rd16(a0 + 0x2C));
        wr16(a0 + 0x2A, uint16_t(s16(rd16(a0 + 0x2A)) >> 1));
        wr16(a0 + 0x2C, uint16_t(s16(rd16(a0 + 0x2C)) >> 1));
        return;
    }
    if (state < 8) return t4_patrol(a0);
    uint16_t vx = clamp_shift(rd16(a0 + 0x38), rd16(a0 + 0x20), 1, 0x20);
    uint16_t vy = clamp_shift(rd16(a0 + 0x3A), rd16(a0 + 0x22), 1, 0x20);
    EnemyMove m = sub_0145AA(a0, vx, vy);
    wr16(a0 + 0x2A, uint16_t(m.d0 << 1));
    wr16(a0 + 0x2C, uint16_t(m.d1 << 1));
    if (state == 9) return t4_aim(a0);
    if (move_dist(m) > 4) {
        wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
        if (rd8(a0 + 0x35)) {
            uint32_t a3 = rd32(a0 + 6);
            uint16_t dx = uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20)), dy = uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22));
            if (Distance(s16(dx), s16(dy)) <= 0x40) {
                CallEnemyHit(rd32(a3 + 0xA), a3, 0x384, dx, dy);
                sub_00D740(a0, 0x1B);
            }
            return loc_016084(a0);
        }
    }
    wr8(a0 + 0x34, 9);
    wr8(a0 + 0x35, 0xA);
    t4_aim(a0);
}

// 0x1AD9A
void h_01AD9A(uint32_t a0, uint16_t d5, uint16_t d1) {
    uint16_t anim = 0;
    uint8_t t = rd8(a0 + 0x35);
    if (rd8(a0 + 0x34) == 0xA) {
        if (s16(rd16(a0 + 0x36)) >= 0)
            anim = t >= 9 ? 3 : t >= 7 ? 0xA : t <= 2 ? 3 : t <= 4 ? 0xA : 9;
        else
            anim = t >= 9 ? 8 : t >= 7 ? 4 : t >= 5 ? 6 : 5;
    } else if (rd8(a0 + 0x34) == 9) {
        bool aim = t >= 4 && t <= 6;
        anim = rd32(a0 + 6) == player_struct ? (aim ? 2 : 1) : (aim ? 0xC : 0xB);
    }
    sub_01BA04(a0, anim, 0xFFFF, s16(d1), d5);
}

// 0x1AEB4: the body slides to a stop; the player walking over it picks up
// its gun (weapon 10 with 2-3 clips).
void h_01AEB4(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    slow_down(a0);
    uint32_t a3 = rd32(a0 + 6);
    uint16_t d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    if (d >= 0x60) return loc_0160E8(a0);
    if (rd8(a0 + 0x34) != 0xB) return;
    if (a3 == player_struct) {
        U16{0xFF1034} = uint16_t((((s16(uint16_t(Random())) >> 8) & 1) + 2) << 8);
        uint16_t slot = sub_011084(0xA);
        U16{0xFF1034} = 0;
        if (slot == 0xFFFF) return;
        wr8(a0 + 0x34, 0xC);
        sub_00D740(a0, 0x69);
        return sub_01E0C2(0x1E49E);
    }
    wr8(a0 + 0x34, 0xC);
    wr8(0xFF0FFA, 0x13);
    wr8(0xFF0FFB, 0xA);
    wr8(0xFF0FFC, uint8_t(((s16(uint16_t(Random())) >> 8) & 1) + 2));
    sub_01C03A(0xFF0FFA);
}

// 0x1AFC6: a hit alerts the other patrols; it then knocks back with
// d0 = 0x71 left by the alert sound (only a killing hit uses the damage).
void h_01AFC6(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    wr8(a0 + 0x34, 0xA);
    wr8(a0 + 0x35, 0xA);
    wr16(a0 + 0x2A, 0);
    wr16(a0 + 0x2C, 0);
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    bool killed = false;
    if (s16(rd16(a0 + 0x36)) >= 0) {
        wr16(a0 + 0x36, uint16_t(rd16(a0 + 0x36) - f));
        if (s16(rd16(a0 + 0x36)) < 0) {
            wr32(a0 + 0xA, 0x1B030);
            killed = true;
        }
    }
    if (!killed) f = int16_t(sub_01AB6C(a0, uint16_t(f)));
    set_knock(a0, dir, int16_t(f >> 3));
}

// 0x1B562
void h_01B562(uint32_t a0, uint16_t d5, uint16_t d1) {
    uint16_t anim = 0, frame = 0xFFFF;
    uint8_t t = rd8(a0 + 0x35);
    switch (rd8(a0 + 0x34)) {
    case 2: anim = s16(rd16(a0 + 0x36)) < 0 ? 8 : 7; break;
    case 1: {
        bool player = rd32(a0 + 6) == player_struct;
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
    sub_01BA04(a0, anim, frame, s16(d1), d5);
}

// 0x1B824: like 0x187C8, without the check for being dead already; a hit
// that leaves it alive sets +0x41 to 2 (it soon stops).
void h_01B824(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    wr8(a0 + 0x34, 2);
    wr8(a0 + 0x35, 0);
    wr16(a0 + 0x2A, 0);
    wr16(a0 + 0x2C, 0);
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    wr16(a0 + 0x36, uint16_t(rd16(a0 + 0x36) - f));
    if (s16(rd16(a0 + 0x36)) >= 0)
        wr8(a0 + 0x41, 2);
    else if (uint16_t(f) >= 0x338) {
        sub_0157CA(a0, uint16_t(f), dir.d0, dir.d1);
        wr8(a0 + 0x30, U8{0xFF26F0});
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
bool t3_wall(uint32_t a0, uint16_t d0, uint16_t d1) {
    return rd8(cell_class + rd8(enemy_map(a0) + int16_t(s16(d0) >> 8) + int16_t(s16(uint16_t(d1 & 0xFF00)) >> 3))) == 1;
}

uint8_t t3_blocks(uint32_t a1, uint16_t x, uint16_t y) {
    return rd8(0x14858 + rd8(cell_class + rd8(a1 + int16_t(s16(x) >> 8) + int16_t(s16(uint16_t(y & 0xFF00)) >> 3))));
}

// 0x152C8: keeps 0x20 away from blocking neighbours and 0x19 from the
// target.
void sub_0152C8(uint32_t a0) {
    uint32_t a1 = enemy_map(a0) + int16_t(s16(rd16(a0 + 0x20)) >> 8) + int16_t(s16(uint16_t(rd16(a0 + 0x22) & 0xFF00)) >> 3);
    auto blocks = [&](int16_t o) { return rd8(0x14858 + rd8(cell_class + rd8(a1 + o))) != 0; };
    uint8_t xl = rd8(a0 + 0x21);
    if (xl < 0x20) {
        if (blocks(-1)) wr8(a0 + 0x21, 0x20);
    } else if (xl > 0xDF) {
        if (blocks(1)) wr8(a0 + 0x21, 0xDF);
    }
    uint8_t yl = rd8(a0 + 0x23);
    if (yl < 0x20) {
        if (blocks(-0x20)) wr8(a0 + 0x23, 0x20);
    } else if (yl > 0xDF) {
        if (blocks(0x20)) wr8(a0 + 0x23, 0xDF);
    }
    uint32_t a3 = rd32(a0 + 6);
    uint16_t dx = uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20)), dy = uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22));
    uint16_t d = uint16_t(Distance(s16(dx), s16(dy)) + 1);
    if (d == 0 || d >= 0x19) return;
    wr16(a0 + 0x20, uint16_t(uint16_t(divs(ext_l(dx) << 5, d)) + rd16(a3 + 0x20)));
    wr16(a0 + 0x22, uint16_t(uint16_t(divs(ext_l(dy) << 5, d)) + rd16(a3 + 0x22)));
}

// 0x15246: sets the velocity and moves, sliding along blocking cells.
void sub_015246(uint32_t a0, uint16_t d0, uint16_t d1) {
    wr16(a0 + 0x2A, d0);
    wr16(a0 + 0x2C, d1);
    uint32_t a1 = enemy_map(a0);
    uint16_t x = rd16(a0 + 0x20), y = rd16(a0 + 0x22);
    if (!t3_blocks(a1, uint16_t(x + d0), uint16_t(y + d1))) {
        wr16(a0 + 0x20, uint16_t(x + d0));
        wr16(a0 + 0x22, uint16_t(y + d1));
    } else if (!t3_blocks(a1, x, uint16_t(y + d1)))
        wr16(a0 + 0x22, uint16_t(y + d1));
    else if (!t3_blocks(a1, uint16_t(x + d0), y))
        wr16(a0 + 0x20, uint16_t(x + d0));
    sub_0152C8(a0);
}

// 0x14BB4: a spot near the target, on the floor or the ceiling.
void t3_decide(uint32_t a0) {
    uint16_t r = uint16_t(Random() >> 16) & 0xF;
    uint32_t a3 = rd32(a0 + 6);
    uint16_t d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    wr8(a0 + 0x35, uint8_t((s16(d) >> 7) + r));
    wr8(a0 + 0x34, rd16(a0 + 0x24) == 0x18 ? 1 : 0);
    uint32_t r2 = uint32_t(int32_t(Random()) >> 4);
    wr16(a0 + 0x38, uint16_t((((r2 & 0xFF) - 0x80) << 1) + rd16(a3 + 0x20)));
    wr16(a0 + 0x3A, uint16_t(((((r2 >> 16) & 0xFF) - 0x80) << 1) + rd16(a3 + 0x22)));
}

// 0x1564E: killed.
void t3_kill(uint32_t a0) {
    sub_00D740(a0, 0x25);
    if (rd8(a0 + 0x30)) return loc_01B8D8(a0);
    wr16(a0 + 4, rd16(a0 + 4) & 0xFF2F);
    wr32(a0 + 0x12, 0x156D2);
    wr8(a0 + 0x1F, 0);
    wr32(a0 + 0xA, 0x1570C);
    wr32(a0 + 0x16, 0x14454);
    wr32(a0 + 0xE, 0x15618);
    wr16(a0 + 0x2A, 0);
    wr16(a0 + 0x2C, 0);
    wr8(a0 + 0x34, 0);
    sub_057E0C();
    link_state_packet(a0, 0x175E4);
}

// 0x150DE / 0x1512E / 0x1517E / 0x151CE: next to a wall on side (dx, dy)
// it may start climbing it (states lo / hi by where its spot is).
bool t3_climb(uint32_t a0, int16_t dx, int16_t dy, uint8_t lo, uint8_t hi) {
    if (!t3_wall(a0, uint16_t(rd16(a0 + 0x20) + dx), uint16_t(rd16(a0 + 0x22) + dy))) return false;
    uint32_t r = Random();
    if (r & 0xE0) return false;
    bool beyond = dx ? s16(rd16(a0 + 0x3A)) > s16(rd16(a0 + 0x22)) : s16(rd16(a0 + 0x38)) > s16(rd16(a0 + 0x20));
    wr8(a0 + 0x34, beyond ? hi : lo);
    uint16_t vz = uint16_t(((r >> 16) & 3) + 2);
    if (s16(rd16(a0 + 0x24)) >= 0) vz = uint16_t(-vz);
    wr16(a0 + 0x2E, vz);
    return true;
}

// 0x14D82: on the floor or ceiling: may climb a wall at a cell edge, else
// walks and bites.
void t3_crawl(uint32_t a0) {
    uint8_t xl = rd8(a0 + 0x21), yl = rd8(a0 + 0x23);
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
    sub_015246(a0, clamp_shift(rd16(a0 + 0x38), rd16(a0 + 0x20), 3, 0x28), clamp_shift(rd16(a0 + 0x3A), rd16(a0 + 0x22), 3, 0x28));
    uint32_t a3 = rd32(a0 + 6);
    uint16_t d = Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22))));
    uint8_t cls = rd8(cell_class + rd8(enemy_map(a3) + int16_t(s16(rd16(a3 + 0x20)) >> 8) +
                                       int16_t(s16(uint16_t(rd16(a3 + 0x22) & 0xFF00)) >> 3)));
    if (cls >= 2 && cls <= 5) d = uint16_t(s16(d) >> 1);
    if (d <= 0x64) {
        int16_t jump = s16(U16{0xFF0E1A});
        bool bite = true;
        if (rd16(a0 + 0x24) == 0x18) {
            if (jump < 0) {
                // The target ducks under it: drop.
                wr8(a0 + 0x34, 2);
                wr16(a0 + 0x2A, 0);
                wr16(a0 + 0x2C, 0);
                wr16(a0 + 0x2E, 0);
                bite = false;
            }
        } else if (rd16(a0 + 0x24) == 0xFFE8 && jump > 0)
            bite = false;
        if (bite && !rd8(a0 + 0x41)) {
            wr8(a0 + 0x41, 0xA);
            sub_00D740(a0, 0x93);
            CallEnemyHit(rd32(a3 + 0xA), a3, 0x2BC, uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20)), uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22)));
        }
    }
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) - 1));
    if (!rd8(a0 + 0x35)) return t3_decide(a0);
    loc_016084(a0);
}

// States 3-10: climbing along a wall at (dx, dy); without the wall it falls
// off with velocity (fx, fy), else moves with (mx, my) and up or down.
void t3_wall_walk(uint32_t a0, int16_t dx, int16_t dy, int16_t fx, int16_t fy, int16_t mx, int16_t my) {
    if (!t3_wall(a0, uint16_t(rd16(a0 + 0x20) + dx), uint16_t(rd16(a0 + 0x22) + dy))) {
        wr8(a0 + 0x34, 2);
        wr16(a0 + 0x2A, uint16_t(fx));
        wr16(a0 + 0x2C, uint16_t(fy));
        wr16(a0 + 0x2E, 0);
        return;
    }
    wr16(a0 + 0x2A, uint16_t(mx));
    wr16(a0 + 0x2C, uint16_t(my));
    sub_015246(a0, uint16_t(mx), uint16_t(my));
    wr16(a0 + 4, rd16(a0 + 4) | 0xC8);
    int16_t vz = s16(rd16(a0 + 0x2E));
    wr16(a0 + 0x24, uint16_t(rd16(a0 + 0x24) + vz));
    int16_t h = s16(rd16(a0 + 0x24));
    if (vz >= 0 && h >= 0x18) {
        wr16(a0 + 0x24, 0x18);
        wr8(a0 + 0x34, 1);
        wr8(a0 + 0x35, 0);
        return t3_decide(a0);
    }
    if (vz < 0 && h <= -0x18) {
        wr16(a0 + 0x24, 0xFFE8);
        wr8(a0 + 0x34, 0);
        wr8(a0 + 0x35, 0);
        return t3_decide(a0);
    }
    sub_0152C8(a0);
    loc_016084(a0);
}

} // namespace

void h_014C2A(uint32_t a0) {
    wr32(a0 + 0x12, 0x14C40);
    wr8(a0 + 0x34, uint8_t(Random() & 1));
    h_014C40(a0);
}

void h_014C40(uint32_t a0) {
    wr8(a0 + 0x1F, 0);
    if (rd8(a0 + 0x41)) wr8(a0 + 0x41, uint8_t(rd8(a0 + 0x41) - 1));
    uint32_t a3 = rd32(a0 + 6);
    auto near = [&]() {
        return Distance(s16(uint16_t(rd16(a0 + 0x20) - rd16(a3 + 0x20))), s16(uint16_t(rd16(a0 + 0x22) - rd16(a3 + 0x22)))) <= 0x80;
    };
    switch (rd8(a0 + 0x34)) {
    case 0:
        wr16(a0 + 0x24, 0xFFE8);
        wr16(a0 + 4, uint16_t((rd16(a0 + 4) & 0xFF7F) | 0x40));
        if (near()) wr16(a0 + 4, rd16(a0 + 4) & 0xFF3F);
        return t3_crawl(a0);
    case 1:
        wr16(a0 + 0x24, 0x18);
        wr16(a0 + 4, uint16_t((rd16(a0 + 4) & 0xFFF7) | 0x40));
        if (near()) wr16(a0 + 4, rd16(a0 + 4) & 0xFFB7);
        if (!(Random() & 0xFE0)) {
            wr8(a0 + 0x34, 2);
            wr16(a0 + 0x2A, 0);
            wr16(a0 + 0x2C, 0);
            wr16(a0 + 0x2E, 0);
            return;
        }
        return t3_crawl(a0);
    case 2: {
        wr16(a0 + 4, rd16(a0 + 4) | 0xC8);
        uint16_t vz = rd16(a0 + 0x2E);
        wr16(a0 + 0x24, uint16_t(rd16(a0 + 0x24) + vz));
        wr16(a0 + 0x2E, uint16_t(vz - 3));
        if (s16(rd16(a0 + 0x24)) <= -0x18) {
            wr16(a0 + 0x24, 0xFFE8);
            wr16(a0 + 0x2E, 0);
            wr8(a0 + 0x34, 0);
            if (s16(rd16(a0 + 0x36)) < 0) return t3_kill(a0);
            return t3_decide(a0);
        }
        sub_015246(a0, rd16(a0 + 0x2A), rd16(a0 + 0x2C));
        wr16(a0 + 0x2A, uint16_t(s16(rd16(a0 + 0x2A)) >> 1));
        wr16(a0 + 0x2C, uint16_t(s16(rd16(a0 + 0x2C)) >> 1));
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
void h_01539E(uint32_t a0, uint16_t d5, uint16_t d1) {
    uint8_t state = rd8(a0 + 0x34);
    if (state < 2) return sub_01BA04(a0, state ? 5 : 0, 0xFFFF, s16(d1), d5);
    int16_t x = s16(rd16(a0 + 0x20)), y = s16(rd16(a0 + 0x22)), px = s16(player_x), py = s16(player_y);
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
        if (s16(rd16(a0 + 0x2E)) > 0) return sub_01BA04(a0, 8, 1, s16(d1), d5);
        if (s16(rd16(a0 + 0x36)) >= 0) return sub_01BA04(a0, 1, 0, s16(d1), d5);
        return sub_01BA04(a0, 0xA, s16(rd16(a0 + 0x24)) <= 0x10 ? 1 : 0, s16(d1), d5);
    }
    sub_01BA04(a0, anim, 0xFFFF, s16(d1), d5);
}

// 0x15618: the body twitches now and then.
void h_015618(uint32_t a0, uint16_t d5, uint16_t d1) {
    uint16_t d2 = (Random() & 0xF00) ? 1 : 0;
    if (rd8(a0 + 0x34) == 2) d2 = uint16_t(d2 + 2);
    if (rd8(a0 + 0x35) & 1) d2 = uint16_t(d2 + 4);
    sub_01BA04(a0, 9, d2, s16(d1), d5);
}

// 0x1570C: the body is knocked about.
void h_01570C(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    wr16(a0 + 0x2A, 0);
    wr16(a0 + 0x2C, 0);
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    wr16(a0 + 0x36, uint16_t(rd16(a0 + 0x36) - f));
    set_knock(a0, dir, int16_t(f >> 4));
    wr8(a0 + 0x34, 2);
    wr8(a0 + 0x35, uint8_t(rd8(a0 + 0x35) + 1));
}

// 0x1575C: knocked off the floor (jumping) or the wall or ceiling.
void h_01575C(uint32_t a0, uint16_t d0, uint16_t d3, uint16_t d4) {
    wr8(a0 + 0x35, 0);
    wr16(a0 + 0x2A, 0);
    wr16(a0 + 0x2C, 0);
    EnemyMove dir = knock_direction(d3, d4);
    int16_t f = int16_t(0x400 - d0);
    if (f < 0) return;
    if (s16(rd16(a0 + 0x36)) >= 0) {
        wr16(a0 + 0x36, uint16_t(rd16(a0 + 0x36) - f));
        wr8(a0 + 0x30, U8{0xFF26F0});
    }
    set_knock(a0, dir, int16_t(f >> 3));
    wr8(a0 + 0x34, 2);
    wr16(a0 + 0x2E, 0);
    if (rd16(a0 + 0x24) == 0xFFE8) wr16(a0 + 0x2E, 8);
}

} // namespace game
