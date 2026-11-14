// Player movement, collision and cell interaction.
#include "game.hpp"

#include <algorithm>
#include <initializer_list>

#include "../host.hpp"
#include "m68k.hpp"
#include "render.hpp"

namespace game {

namespace {

auto& height = st.stair_height; // view height offset on stairs
auto& lift = st.lift;   // 1 going down, -1 going up, 2 / -2 arrived

// 0xE634
void ExitCell() { st.level_exit = 1; }

// 0xE662: hazard cell: pushes the player towards the cell centre and hurts.
void HazardCell() {
    uint16_t d0 = uint16_t((player_x & 0xFF00) | 0x80) - player_x;
    uint16_t d1 = uint16_t((player_y & 0xFF00) | 0x80) - player_y;
    uint16_t d3 = d0, d4 = d1;
    d0 = Distance(int16_t(d0), int16_t(d1));
    bool sound = int16_t(uint16_t(st.shield_slot)) < 0;
    HitPlayerIgnoreArmour(d0, d3, d4);
    if (sound) PlaySoundLocal(0x1B);
}

// Lift cells: the first ride on the level shows message 0x1E67A.
void lift_message() {
    if (!st.cleared_timer && st.pw_level == cur_level) QueueMessage(Msg::FloorNotSecured);
}

void lift_music() {
    if (last_sound == 0x88) return;
    StopMusic();
    PlaySound(0x88);
}

// 0xE69E: lift down.
void LiftDown() {
    lift_message();
    if (height) return;
    lift_music();
    lift = 1;
    PlaySoundLocal(0x6F);
}

// 0xE6FE: lift up.
void LiftUp() {
    lift_message();
    if (height) return;
    lift_music();
    lift = 0xFFFF;
    PlaySoundLocal(0x6F);
}

// 0xE75E: lift, keeps the direction of a ride going up.
void LiftContinue() {
    lift_message();
    if (height) return;
    lift_music();
    lift = int16_t(uint16_t(lift)) < 0 ? 0xFFFF : 1;
    PlaySoundLocal(0x6F);
}

// 0xE7D6: lift stop.
void LiftStop() {
    if (last_sound == 0x88) {
        StopMusic();
        PlaySound(cur_area);
    }
    if (lift) lift = int16_t(uint16_t(lift)) < 0 ? 0xFFFE : 2;
    height = 0;
}

// 0x11B2A: item d0 (weapon or ammo) goes into the inventory if there is room.
void take_item(uint16_t d0, Cell a0) {
    if (AddWeapon(d0)) return;
    put_cell(a0, 0);
    PlaySoundLocal(0x69);
    // Table 0x1EF14
    static const Msg msgs[15] = {Msg::Blank, Msg::BioScanner, Msg::Mine, Msg::Vest, Msg::Extinguisher,
                                 Msg::FireSuit, Msg::FlashLight, Msg::HandGrenade, Msg::Handgun,
                                 Msg::NightVision, Msg::LaserGun, Msg::RocketLauncher, Msg::Shotgun,
                                 Msg::FlameThrower, Msg::PulseLaser};
    QueueMessage(msgs[d0]);
}

// 0x11B7C: medikit.
void take_medikit(Cell a0) {
    if (st.health == 0x64) return;
    put_cell(a0, 0);
    PlaySoundLocal(0x6A);
    QueueMessage(Msg::Medipack);
    st.health += st.character == 2 ? 0x28 : 0x14;
    DrawHealthBar();
}

// 0xA33A / 0xA38C: pulls the player towards the cell centre when closer than
// range.
void pull_to_centre(uint16_t range, uint16_t mul) {
    uint16_t cx = uint16_t((player_x & 0xFF00) | 0x80), cy = uint16_t((player_y & 0xFF00) | 0x80);
    uint16_t dx = uint16_t(player_x - cx), dy = uint16_t(player_y - cy);
    uint16_t d = Distance(int16_t(dx), int16_t(dy));
    if (d >= range) return;
    d++;
    player_x = uint16_t(lo(divs(ext_l(uint16_t(dx * mul)), d)) + cx);
    player_y = uint16_t(lo(divs(ext_l(uint16_t(dy * mul)), d)) + cy);
}

// 0xA3DE
void StopJump() {
    st.view_z = uint16_t(int16_t(uint16_t(st.view_z)) >> 1);
    st.view_z_target = 0;
    st.jump_speed = 0;
}

// 0xA3EC: keeps the player away from the cell edges while the view is high.
void KeepFromCellEdges() {
    int16_t h = int16_t(uint16_t(height));
    if ((h < 0 ? uint16_t(-h) : uint16_t(h)) < 0x18) return;
    bool moved = false;
    uint8_t x = uint8_t(player_x);
    if (x < 0x20) set_low_byte(player_x, 0x20), moved = true;
    else if (x > 0xDF) set_low_byte(player_x, 0xDF), moved = true;
    uint8_t y = uint8_t(player_y);
    if (y < 0x20) set_low_byte(player_y, 0x20), moved = true;
    else if (y > 0xDF) set_low_byte(player_y, 0xDF), moved = true;
    if (!moved) return;
    st.speed = 0;
    st.strafe = 0;
    st.push_x = 0;
    st.push_y = 0;
}

// 0xA456 / 0xA472 / 0xA48E / 0xA4AA: on a stair cell, keeps the player on
// one half of the cell along the stairs (pos = player_y or _x).
void stair_half(uint16_t& pos, bool high_half) {
    StopJump();
    if (bool(pos & 0x80) != high_half) set_low_byte(pos, 0x80);
}

uint8_t cls_at(Cell a0) { return st.cell_class[map_cell(a0)]; }

// 0xA4C6 / 0xA4EC
void StairCornerA(Cell a0) {
    if (cls_at(a0 + 1) == 0x17) stair_half(player_y, true);
    else if (cls_at(a0 - 1) == 0x3F) stair_half(player_y, false);
    else if (cls_at(a0 + 0x20) == 0x47) stair_half(player_x, false);
    else stair_half(player_x, true);
}

// 0xA4EC
void StairCornerB(Cell a0) {
    if (cls_at(a0 - 1) == 0x17) stair_half(player_y, true);
    else if (cls_at(a0 + 1) == 0x3F) stair_half(player_y, false);
    else if (cls_at(a0 - 0x20) == 0x47) stair_half(player_x, false);
    else stair_half(player_x, true);
}

// 0xB25E / 0xB29A: door cells; the cell becomes the open door, the first
// cell of class 0x2D / 0x2E (FF62F3 / FF62F4).
void open_door(Cell a0, uint8_t open_class) {
    if (st.door_count == 16) return;
    Door& d = st.doors[st.door_count++];
    d.cell = a0;
    d.opening = 0x20;
    d.restore = map_cell(a0);
    put_cell(a0, st.class_cell[open_class]);
    st.radar_redraw = 1;
    d.x = player_x & 0xFF00;
    d.y = player_y & 0xFF00;
    d.mode = 0;
    d.level = uint8_t(st.cur_level);
    PlaySoundLocal(0x67);
}

void OpenDoorA(Cell a0) { open_door(a0, 0x2D); } // 0xB25E
void OpenDoorB(Cell a0) { open_door(a0, 0x2E); } // 0xB29A

// 0xB1C4 / 0xB202 (enemies) and 0xB130 / 0xB168 (the boss cell): door cell
// a0 opens; the entry gets the cell position (d0, d1), the player's level
// and, for the boss cell, kind 2 and no old cell byte.
// With the list full, 0xB202 writes the second half of an entry past the
// list's end and moves the end pointer off the entry grid, so the original
// then runs through all memory looking for the end; here it does nothing.
void other_open_door(Cell a0, uint8_t open_class, uint16_t d0, uint16_t d1, uint8_t kind) {
    if (st.door_count == 16) return;
    Door& d = st.doors[st.door_count++];
    d.cell = a0;
    d.opening = 0x20;
    d.restore = kind ? 0 : map_cell(a0);
    put_cell(a0, st.class_cell[open_class]);
    st.radar_redraw = 1;
    d.x = d0;
    d.y = d1;
    d.mode = kind;
    d.level = uint8_t(st.cur_level);
}

} // namespace

void EnemyOpenDoorA(Cell a0, uint16_t d0, uint16_t d1) { other_open_door(a0, 0x2D, d0, d1, 0); } // 0xB1C4
void EnemyOpenDoorB(Cell a0, uint16_t d0, uint16_t d1) { other_open_door(a0, 0x2E, d0, d1, 0); } // 0xB202
void BossOpenDoorA(Cell a0, uint16_t d0, uint16_t d1) { other_open_door(a0, 0x2D, d0, d1, 2); } // 0xB130
void BossOpenDoorB(Cell a0, uint16_t d0, uint16_t d1) { other_open_door(a0, 0x2E, d0, d1, 2); } // 0xB168

// 0xA1D2: special cells under the player (handlers of table 0xA30A).
void FloorCellUnderPlayer() {
    Cell a0 = cur_map + (int16_t(player_x) >> 8) + (int16_t(player_y & 0xFF00) >> 3);
    switch (kFloorKind[cls_at(a0)]) {
    case 1: StopJump(); break;
    case 2: pull_to_centre(0x28, 48); break;
    case 3: KeepFromCellEdges(); break;
    case 4: pull_to_centre(0x58, 96); break;
    case 5: stair_half(player_y, true); break;
    case 6: stair_half(player_y, false); break;
    case 7: stair_half(player_x, true); break;
    case 8: stair_half(player_x, false); break;
    case 9: StairCornerA(a0); break;
    case 10: StairCornerB(a0); break;
    case 11: OpenDoorA(a0); break;
    case 12: OpenDoorB(a0); break;
    }
}

// 0xA9CE: level n's variants of the map cells.
void ApplyLevelCellVariants(uint16_t level) {
    for (uint8_t& c : st.maps[level]) {
        uint8_t m = kLevelVariant[st.cell_class[c]];
        if (m != 0xFF) c = st.class_cell[m];
    }
}

// 0xB33A: opening of the door at map cell a0, 0 when it is not open.
uint16_t DoorOpening(Cell a0) {
    for (int i = 0; i < st.door_count; i++)
        if (st.doors[i].cell == a0) return st.doors[i].opening;
    return 0;
}

namespace {

// Puts door i's cell back and removes it from the list.
void close_door(int i) {
    put_cell(st.doors[i].cell, st.doors[i].restore);
    std::copy(st.doors + i + 1, st.doors + st.door_count, st.doors + i);
    st.door_count--;
}

} // namespace

// 0xB35C: opens and closes the doors in the door list.
void UpdateDoors() {
    for (int i = 0; i < st.door_count;) {
        Door& d = st.doors[i];
        bool remove = false;
        if (d.mode == 1) {
        } else if (d.mode == 2) {
            if (d.opening == 0x80) remove = true;
            else d.opening += 0x20;
        } else {
            uint16_t x0 = d.x, y0 = d.y;
            uint16_t x1 = uint16_t(x0 + 0xFF), y1 = uint16_t(y0 + 0xFF);
            auto inside = [&](uint16_t x, uint16_t y) { return x0 <= x && x <= x1 && y0 <= y && y <= y1; };
            bool blocked = inside(player_x, player_y);
            if (!blocked && st.object_count) {
                Enemy* e = first_object();
                for (uint16_t n = st.object_count; n; n--, e = next_object(e))
                    if ((e->flags & 0x10) && inside(e->x, e->y)) {
                        blocked = true;
                        break;
                    }
            }
            if (blocked) {
                if (d.opening != 0x80) d.opening += 0x20;
            } else {
                d.opening -= 0x20;
                if (!d.opening) {
                    PlaySoundLocal(0x67);
                    remove = true;
                }
            }
        }
        if (!remove) {
            i++;
            continue;
        }
        st.radar_redraw = 1;
        close_door(i);
    }
}

// 0xB48A: puts every door that is not mode 1 back into the map and removes
// it from the list.
void ResetDoors() {
    for (int i = 0; i < st.door_count;) {
        if (st.doors[i].mode == 1) i++;
        else close_door(i);
    }
}

// 0xD940: health warnings when the health (FF0DE4) drops below 0x33 or 0x10.
void HealthWarnings() {
    if (st.prev_health > 0x32) {
        if (st.health <= 0x32) QueueMessage(Msg::HealthLow);
    } else if (st.prev_health > 0xF) {
        if (st.health <= 0xF) QueueMessage(Msg::HealthCritical);
    }
}

// 0xD800: the player is hit from distance d0 by a shot from direction
// (d3, d4): damage (unless the armour FF1064 is active), the camera shake
// (FF0E20), the push (FF0DF0) and the health warnings.
void HitPlayer(uint16_t d0, uint16_t d3, uint16_t d4) {
    if (st.dying) return;
    uint16_t d1 = d0;
    int16_t jump_h = int16_t(st.view_z);
    if (jump_h > 0)
        d1 = uint16_t(d1 - (int16_t(d1) >> 2));
    else if (jump_h < 0)
        d1 = uint16_t(d1 + (int16_t(d1) >> 1));
    if (d1 >= 0x400) return;
    d1 = uint16_t(int16_t(d1) >> 6);
    bool armour = int16_t(st.armour_slot) >= 0;
    if (!armour) {
        d1 = uint16_t(0x10 - d1);
        if (st.character == 3) d1 = uint16_t(d1 - (int16_t(d1) >> 2));
        if (st.character == 4) d1 = uint16_t(int16_t(d1) >> 1);
        st.prev_health = st.health;
        st.health = uint16_t(st.health - d1);
        d1 = uint16_t(0x10 - d1);
    }
    d1 = uint16_t((d1 << 2) & 0x3C);
    const uint16_t* jump = kJump[d1 >> 2];
    if (!armour) {
        d1 = uint16_t(0xF - (d1 >> 3));
        if (d1 > st.hit_flash) st.hit_flash = d1;
    }
    if (!st.jump_speed && !st.view_z) st.jump_speed = jump[0];
    int16_t k = int16_t(jump[1]);
    uint16_t d = uint16_t(Distance(int16_t(d3), int16_t(d4)) + 1);
    d3 = uint16_t(divs(uint32_t(int32_t(int16_t(d3)) * k), d));
    d4 = uint16_t(divs(uint32_t(int32_t(int16_t(d4)) * k), d));
    while (Distance(int16_t(d3), int16_t(d4)) > 0x100) {
        d3 = uint16_t(int16_t(d3) >> 1);
        d4 = uint16_t(int16_t(d4) >> 1);
    }
    st.push_x = d3;
    st.push_y = d4;
    if (jump[0] >= 7) {
        st.view_z_target = 0xFFF0;
        st.crouch_time = 0x14;
    }
    HealthWarnings();
    if (!armour)
        DrawHealthBar();
    else
        ArmourCharge();
}

// 0xD7DC: a hit on the player; the shield (FF1066) only loses charge.
void HitPlayerIgnoreArmour(uint16_t d0, uint16_t d3, uint16_t d4) {
    if (int16_t(st.shield_slot) >= 0) return ShieldCharge();
    uint16_t armour = st.armour_slot;
    st.armour_slot = 0xFFFF;
    HitPlayer(d0, d3, d4);
    st.armour_slot = armour;
}

namespace {

auto& dirx = st.dir_x;
auto& diry = st.dir_y;
auto& push_x = st.push_x; // knock-back, decays by 1/4 per frame
auto& push_y = st.push_y;
auto& jump = st.view_z;
auto& jump_target = st.view_z_target;
auto& jump_speed = st.jump_speed;
auto& crouch_time = st.crouch_time;
auto& turn = st.turn;
auto& turn_target = st.turn_target;
auto& speed = st.speed;
auto& speed_target = st.speed_target;
auto& strafe = st.strafe;
auto& strafe_target = st.strafe_target;

int16_t s16(uint16_t v) { return int16_t(v); }
uint16_t asr(uint16_t v, int n) { return uint16_t(s16(v) >> n); }

// Moves cur towards target by diff >> shift.
void approach(uint16_t& cur, uint16_t target, int shift) {
    uint16_t d = uint16_t(target - cur);
    if (!d) return;
    if (s16(d) < 0) cur = uint16_t(cur + asr(d, shift));
    else cur = uint16_t(cur - asr(uint16_t(-d), shift));
}

uint16_t mul8(uint16_t a, uint16_t b) { return uint16_t((int32_t(s16(a)) * s16(b)) >> 8); }

// 0xE1B2: collision shape blocking point (x, y) of map a1, 0 when free.
uint8_t PointCollision(Cell a1, uint16_t x, uint16_t y) {
    uint8_t cls = st.cell_class[map_cell(a1 + int16_t(uint16_t((s16(x) >> 8) + (s16(uint16_t(y & 0xFF00)) >> 3))))];
    if (!cls) return 0;
    uint8_t shape = CollisionShape(cls);
    uint8_t px = uint8_t(x), py = uint8_t(y);
    switch (shape) {
    case 1: return 1;
    case 2: return px < py ? 0 : 2;
    case 3: return px < uint8_t(~py) ? 0 : 3;
    case 4: return px > py ? 0 : 4;
    case 5: return px > uint8_t(~py) ? 0 : 5;
    default: return 0;
    }
}

uint8_t shape_at(Cell a) { return CollisionShape(st.cell_class[map_cell(a)]); }

bool any(uint8_t v, std::initializer_list<uint8_t> l) {
    for (uint8_t x : l)
        if (v == x) return true;
    return false;
}

// 0xDE3E: keeps the player off the walls and diagonal walls around the cell.
void keep_off_walls(Cell a1) {
    uint8_t x = uint8_t(player_x), y = uint8_t(player_y);
    a1 += int16_t(uint16_t((s16(player_x) >> 8) + (s16(uint16_t(player_y & 0xFF00)) >> 3)));
    bool right = x & 0x80, down = y & 0x80;
    uint8_t own = shape_at(a1);
    uint8_t side = shape_at(a1 + (right ? 1 : -1));
    uint8_t vert = shape_at(a1 + (down ? 0x20 : -0x20));
    uint8_t diag = shape_at(a1 + (down ? 0x20 : -0x20) + (right ? 1 : -1));
    uint8_t t;
    if (right && down) {
        if (x >= 0xE0 && any(side, {1, 4, 5})) x = 0xDF;
        if (y >= 0xE0 && any(vert, {1, 2, 5})) y = 0xDF;
        if (x >= 0xE0 && y >= 0xE0 && any(diag, {1, 2, 4, 5})) x = y = 0xDF;
        if (own == 2) { t = uint8_t(y - 0x2D); if (x > t) x = t; }
        else if (own == 4) { t = uint8_t(x - 0x2D); if (y > t) y = t; }
        else if (own == 5) { t = uint8_t(0x2D - y); if (x < t) x = t; }
    } else if (right) {
        if (x >= 0xE0 && any(side, {1, 4, 5})) x = 0xDF;
        if (y <= 0x1F && any(vert, {1, 3, 4})) y = 0x20;
        if (x >= 0xE0 && y <= 0x1F && any(diag, {1, 3, 4, 5})) x = 0xDF, y = 0x20;
        if (own == 3) { t = uint8_t(0xD3 - y); if (x > t) x = t; }
        else if (own == 5) { t = uint8_t(0x2D - x); if (y < t) y = t; }
        else if (own == 4) { t = uint8_t(0x2D + y); if (x < t) x = t; }
    } else if (down) {
        if (x <= 0x1F && any(side, {1, 2, 3})) x = 0x20;
        if (y >= 0xE0 && any(vert, {1, 2, 5})) y = 0xDF;
        if (x <= 0x1F && y >= 0xE0 && any(diag, {1, 2, 3, 5})) x = 0x20, y = 0xDF;
        if (own == 3) { t = uint8_t(0xD3 - x); if (y > t) y = t; }
        else if (own == 5) { t = uint8_t(0x2D - y); if (x < t) x = t; }
        else if (own == 2) { t = uint8_t(y - 0x2D); if (x > t) x = t; }
    } else {
        if (x <= 0x1F && any(side, {1, 2, 3})) x = 0x20;
        if (y <= 0x1F && any(vert, {1, 3, 4})) y = 0x20;
        if (x <= 0x1F && y <= 0x1F && any(diag, {1, 2, 3, 4})) x = y = 0x20;
        if (own == 2) { t = uint8_t(x + 0x2D); if (y < t) y = t; }
        else if (own == 4) { t = uint8_t(y + 0x2D); if (x < t) x = t; }
        else if (own == 3) { t = uint8_t(0xD3 - x); if (y > t) y = t; }
    }
    set_low_byte(player_x, x);
    set_low_byte(player_y, y);
}

// Moves the player by (dx, dy), sliding along walls. False when blocked.
bool move(Cell a1, uint16_t dx, uint16_t dy) {
    uint8_t r = PointCollision(a1, uint16_t(player_x + dx), uint16_t(player_y + dy));
    if (r == 1) {
        if (!PointCollision(a1, uint16_t(player_x + dx), player_y)) player_x = uint16_t(player_x + dx);
        else if (!PointCollision(a1, player_x, uint16_t(player_y + dy))) player_y = uint16_t(player_y + dy);
        else return false;
        return true;
    }
    if (r) {
        // Diagonal wall: first along the wall, then the rest.
        uint16_t d6 = asr(uint16_t(dy - dx), 1), d5 = uint16_t(-d6);
        if (!PointCollision(a1, uint16_t(player_x + d5), uint16_t(player_y + d6))) {
            dx = d5;
            dy = d6;
        } else {
            dx = uint16_t(dx - d5);
            dy = uint16_t(dy - d6);
            if (PointCollision(a1, uint16_t(player_x + dx), uint16_t(player_y + dy))) return false;
        }
    }
    player_x = uint16_t(player_x + dx);
    player_y = uint16_t(player_y + dy);
    return true;
}

} // namespace

// 0xDAB8: player controls, turning, jumping, movement and collision.
void PlayerControls() {
    // PC controls: the strafe keys strafe instead of turning, the mouse turns.
    uint8_t strafe_keys = host::strafe_keys();
    uint8_t pad = uint8_t(pad_buttons & ~strafe_keys), prev = st.pad_prev;
    if (st.dying) {
        speed_target = 0;
        turn_target = 0;
        strafe_target = 0;
        jump_target = 0xFFE4;
        crouch_time = 0x63;
    } else {
        if ((pad & 0x20) && !(prev & 0x20)) NextWeapon();
        turn_target = 0;
        speed_target = 0;
        strafe_target = 0;
        if (pad & 0x40) {
            if (s16(jump) <= 0) {
                if (pad & 4) strafe_target = 0xFFD8;
                if (pad & 8) strafe_target = 0x28;
            }
            if ((pad & 1) && s16(jump) <= 0) {
                if (s16(jump) < 0) jump_target = 0;
                else if (!((prev & 0x40) && (prev & 1)) && !(pad & 4) && !(pad & 8)) {
                    jump_speed = 9;
                    jump = 0;
                }
            }
            if ((pad & 2) && !(pad & 4) && !(pad & 8)) {
                jump_target = 0xFFF0;
                crouch_time = 0x1E;
            }
        } else {
            if (pad & 4) turn_target = 0xFFF0;
            if (pad & 8) turn_target = 0x10;
            if (pad & 1) speed_target = st.character == 3 ? 0x2D : 0x28;
            if (pad & 2) speed_target = st.character == 3 ? 0xFFDD : 0xFFEC;
        }
        if (s16(jump) <= 0) {
            if (strafe_keys & 4) strafe_target = 0xFFD8;
            if (strafe_keys & 8) strafe_target = 0x28;
        }
    }
    if (jump_target == 0xFFF0 && !(crouch_time = uint16_t(crouch_time - 1))) jump_target = 0;
    uint16_t d0 = jump_speed;
    if (d0 || s16(jump) > 0) {
        jump_speed = uint16_t(jump_speed - 2);
        jump = uint16_t(jump + d0);
        if (s16(jump) <= 0) {
            jump = 0;
            jump_speed = 0;
        }
    } else {
        d0 = jump;
        if (s16(d0) < 0) {
            strafe_target = asr(strafe_target, 1);
            speed_target = asr(speed_target, 1);
        }
        if (s16(d0) < s16(jump_target)) jump = uint16_t(jump + 4);
        else if (d0 != jump_target) jump = uint16_t(jump - 4);
    }
    if (s16(jump) <= 0) approach(turn, turn_target, turn_target ? 3 : 1);
    int mouse = host::mouse_turn();
    if (st.dying) mouse = 0;
    if (turn || mouse) {
        player_angle = uint16_t((player_angle + turn + mouse) & 0x1FF);
        dirx = kDirVectors[player_angle][0];
        diry = kDirVectors[player_angle][1];
    }
    if (s16(jump) <= 0) {
        approach(speed, speed_target, speed_target ? 4 : 1);
        approach(strafe, strafe_target, strafe_target ? 4 : 1);
    }
    Cell a1 = cur_map;
    uint16_t dx, dy;
    d0 = speed;
    if (!d0) d0 = push_x | push_y;
    if (!d0) {
        if (!strafe) return FloorCellUnderPlayer();
        dx = mul8(uint16_t(-diry), strafe);
        dy = mul8(dirx, strafe);
    } else {
        push_x = uint16_t(push_x - (s16(push_x) < 0 ? asr(push_x, 2) : uint16_t(-asr(uint16_t(-push_x), 2))));
        push_y = uint16_t(push_y - (s16(push_y) < 0 ? asr(push_y, 2) : uint16_t(-asr(uint16_t(-push_y), 2))));
        dx = mul8(dirx, d0);
        dy = mul8(diry, d0);
        if (strafe) {
            dx = uint16_t(dx + mul8(uint16_t(-diry), strafe));
            dy = uint16_t(dy + mul8(dirx, strafe));
        }
    }
    dx = uint16_t(dx + push_x);
    dy = uint16_t(dy + push_y);
    if (move(a1, dx, dy)) keep_off_walls(a1);
    FloorCellUnderPlayer();
}

// 0xEC1C: head bob and footstep sounds while walking.
void HeadBob() {
    uint16_t v = uint16_t((s16(speed) < 0 ? -speed : speed) + (s16(strafe) < 0 ? -strafe : strafe));
    auto& bob = st.bob;
    bob = uint16_t(bob + v);
    // 0xEBE0
    uint8_t step = kFootstep[(s16(bob) >> 5) & 0xF];
    if (step != st.footstep) {
        st.footstep = step;
        PlaySoundLocal(step);
    }
    if (v >= 0x28) {
        uint16_t i = uint16_t((s16(bob) >> 6) & 7);
        uint16_t dh = kBob[i][0], da = kBob[i][1];
        if (da || dh) {
            st.bob_lift = dh;
            st.bob_turn = da;
            st.bob_angle = player_angle;
            st.bob_dir_x = dirx;
            st.bob_dir_y = diry;
            st.bob_z = jump;
            jump = uint16_t(jump + dh);
            player_angle = uint16_t((player_angle + da) & 0x1FF);
            dirx = kDirVectors[player_angle][0];
            diry = kDirVectors[player_angle][1];
            return;
        }
    }
    st.bob_turn = st.bob_lift = 0;
}

// 0x11AB4: pickup cells.
void PickupItem(Cell a0, uint8_t d3) {
    if (st.dying) return;
    if (d3 == 0x82) return take_item(0xD, a0);
    if (d3 == 0x36) return take_item(0xE, a0);
    uint16_t d0 = uint16_t(d3 - 0x18);
    if (d0 > 0xE) return;
    if (d0 >= 1 && d0 <= 0xC) take_item(d0, a0);
    else if (d0 == 0xD) take_medikit(a0);
}

// 0xE238: cell class -> collision shape (table 0xE23E). The 68000 code only
// replaces the low byte of d3.
uint8_t CollisionShape(uint16_t d3) { return kCollisionShape[uint8_t(d3)]; }

// What using the cell a0 of class d3 does (called by 0xE3C4 / 0xE3F8).
void CellUseHandler(CellUse use, Cell a0, uint16_t d3) {
    switch (use) {
    case CellUse::DoorA: OpenDoorA(a0); break;
    case CellUse::DoorB: OpenDoorB(a0); break;
    case CellUse::Light0: SetLighting(0); break;
    case CellUse::Light1: SetLighting(1); break;
    case CellUse::Light2: SetLighting(2); break;
    case CellUse::Light3: SetLighting(3); break;
    case CellUse::Hazard: HazardCell(); break;
    case CellUse::Pickup: PickupItem(a0, uint8_t(d3)); break;
    case CellUse::Flash: StartFlash(); break;
    case CellUse::LiftStop: LiftStop(); break;
    case CellUse::LiftDown: LiftDown(); break;
    case CellUse::LiftUp: LiftUp(); break;
    case CellUse::LiftContinue: LiftContinue(); break;
    case CellUse::Exit: ExitCell(); break;
    case CellUse::None: break;
    }
}

} // namespace game
