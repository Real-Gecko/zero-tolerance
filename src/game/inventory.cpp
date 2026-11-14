// Weapons inventory and its HUD slots (0x109E6-0x11320), level passwords
// (0x57E0C-0x58054).
#include "display.hpp"
#include "game.hpp"
#include "m68k.hpp"

#include "../host.hpp"

namespace game {

namespace {

void wait_dma() {
    while (fb_dma_pending) host::frame();
}

// HUD tile bytes of slot n.
uint16_t slot_dest(int n) { return uint16_t((n << 9) + 0x9600); }

void clear_slot_tiles(uint16_t dest) {
    for (int i = 0; i < 128; i++) display::tile_long(uint16_t(dest + i * 4), 0);
}

} // namespace

// 0x1DF54: two-digit number (0-127) in the HUD digit font, as two tile
// columns of 6 rows at tile bytes dest.
void DrawAmmoDigits(uint16_t n, uint16_t dest) {
    n &= 0x7F;
    uint16_t tens = 0;
    while (n >= 10) {
        n = uint16_t(n - 10);
        tens++;
    }
    auto glyph = [](uint16_t d) {
        return assets::tiles("screens/tiles/hud_ammo_digits.png").data() + 24 * (d >> 1) + 2 * (d & 1);
    };
    const uint8_t *ones = glyph(n), *t = glyph(tens);
    for (int row = 0; row < 6; row++)
        display::tile_long(uint16_t(dest + row * 4),
                           uint32_t((tens ? be16(t + row * 4) : 0) << 16 | be16(ones + row * 4)));
}

// 0x10A7A: the 16 tiles of one inventory slot at tile bytes dest (16 tiles
// of hud_inventory_icons per weapon, table 0x10AC8).
void DrawInventorySlot(const WeaponSlot& slot, uint16_t dest) {
    uint16_t w = slot.weapon;
    if (!w) return clear_slot_tiles(dest);
    const uint8_t* a1 = assets::tiles("screens/tiles/hud_inventory_icons.png", {{4, 4}}).data() + (w - 1) * 0x200;
    for (int i = 0; i < 0x1A; i++, a1 += 4) display::tile_long(uint16_t(dest + i * 4), be32(a1));
    DrawAmmoDigits(uint16_t(((slot.ammo - 1) >> 8) + 1), uint16_t(dest + 104));
    a1 += 0x18;
    for (int i = 0; i < 0x60; i++, a1 += 4) display::tile_long(uint16_t(dest + 128 + i * 4), be32(a1));
}

// 0x109E6: empty inventory.
void ClearInventory() {
    st.add_ammo = 0;
    st.nv_slot = st.light_slot = st.scanner_slot = st.armour_slot = st.shield_slot = 0xFFFF;
    st.lit_start = 0x48; // the original stores 0x38 first, then overwrites it
    st.weapon = 0xFF;
    st.next_weapon = 0;
    st.lower = 0x20;
    st.fire_step = 0;
    st.slot_count = 0;
    for (int i = 0; i < 5; i++) st.slot_order[i] = uint8_t(i);
    for (WeaponSlot& s : st.slots) s = {};
    wait_dma();
    for (int i = 0; i < 5; i++) DrawInventorySlot(st.slots[i], slot_dest(i));
}

// 0x11C02: firing animation type of the selected weapon.
void SetFireAnim() {
    uint16_t w = st.slots[st.slot_order[0]].weapon;
    st.next_weapon = kFireAnim[w];
}

// 0x1125E: cycles to the next weapon.
void NextWeapon() {
    PlaySoundLocal(0x6B);
    uint8_t first = st.slot_order[0];
    for (int i = 0; i < 4; i++) st.slot_order[i] = st.slot_order[i + 1];
    st.slot_order[4] = first;
    SetFireAnim();
}

// 0x11286: night vision on. Returns d0.
uint16_t NightVisionOn() {
    WriteCram(game_palette(true, st.indoors), 0x3F);
    st.walls_shaded = 0;
    st.sprite_shades = 3;
    st.visibility = 0x10;
    st.backdrop_image = st.lighting == 3 ? 3 : 0;
    return BuildBackdrop();
}

// 0x112F2: night vision off. Returns d0.
uint16_t NightVisionOff() {
    WriteCram(game_palette(false, st.indoors), 0x3F);
    uint16_t mode = st.lighting;
    st.lighting = 0xFFFF;
    return SetLighting(mode);
}

// 0x11084: adds weapon d0 (with add_ammo ammo, or its default) to the
// inventory. Returns the slot, or 0xFFFF if it could not be taken.
uint16_t AddWeapon(uint16_t weapon) {
    uint16_t d7 = 0;
    for (; d7 < 5; d7++)
        if (st.slots[d7].weapon == weapon) break;
    uint16_t slot;
    if (d7 < 5) {
        WeaponSlot& s = st.slots[d7];
        wait_dma();
        uint16_t d2 = st.add_ammo;
        if (!d2) d2 = kStartAmmo[weapon];
        uint16_t d1 = kMaxAmmo[weapon];
        if (s.ammo == d1) return 0xFFFF;
        d2 = uint16_t(d2 + s.ammo);
        if (d2 > d1) d2 = d1;
        s.ammo = d2;
        DrawInventorySlot(s, slot_dest(d7));
        slot = d7;
    } else {
        if (st.slot_count == 5) return 0xFFFF;
        st.slot_count += 1;
        wait_dma();
        for (d7 = 0; st.slots[d7].weapon; d7++) {
            if (d7 == 4) {
                st.slot_count = 5;
                return 0xFFFF;
            }
        }
        WeaponSlot& s = st.slots[d7];
        s.weapon = weapon;
        s.ammo = st.add_ammo ? st.add_ammo : kStartAmmo[weapon];
        DrawInventorySlot(s, slot_dest(d7));
        SetFireAnim();
        slot = d7;
    }
    st.slot_blink[slot] = 3;
    if (weapon == 9 && int16_t(st.nv_slot) < 0) {
        st.nv_slot = slot;
        NightVisionOn();
        return 0;
    }
    if (weapon == 6 && int16_t(st.light_slot) < 0) {
        st.light_slot = slot;
        st.visibility = 0x10;
    }
    if (weapon == 1 && int16_t(st.scanner_slot) < 0) st.scanner_slot = slot;
    if (weapon == 3 && int16_t(st.armour_slot) < 0) st.armour_slot = slot;
    if (weapon == 5 && int16_t(st.shield_slot) < 0) st.shield_slot = slot;
    return 0;
}

// 0x10B8C: the selected item is used up: its slot is cleared.
void UseUpItem() {
    uint16_t sel = st.slot_order[0];
    st.pickup_check = 1;
    st.slots[sel] = {};
    st.slot_count -= 1;
    st.next_weapon = 0;
    clear_slot_tiles(slot_dest(sel));
}

// 0x10B00: uses one unit of the selected item's charge (night vision,
// goggles...). Called once per frame while it is active.
void UseItemCharge() {
    wait_dma();
    uint16_t sel = st.slot_order[0];
    WeaponSlot& s = st.slots[sel];
    if (s.ammo <= 0x100) return UseUpItem();
    s.ammo = uint16_t(s.ammo - 0x100);
    uint16_t left = s.ammo;
    if (left < 0x300 && left >= 0x200) QueueMessage(Msg::AmmoLow);
    DrawAmmoDigits(uint16_t(((left - 1) >> 8) + 1), uint16_t(slot_dest(sel) + 0x68));
}

namespace {

// 0x10BD8, 0x10CC6, 0x10DC0, 0x10EAC: uses one unit of the charge of the
// active timed item whose slot index is in var. When it runs out, the slot is
// cleared and var moves to another slot holding the same item, or becomes -1
// (gone = true). Returns d0.
uint16_t item_timer(uint16_t& var, uint16_t item, bool& gone) {
    wait_dma();
    uint16_t d5 = uint16_t((var << 9) + 0x9600);
    WeaponSlot& s = st.slots[var];
    gone = false;
    if (s.ammo > 0x100) {
        s.ammo = uint16_t(s.ammo - 0x100);
        uint16_t n = uint16_t((uint16_t(s.ammo - 1) >> 8) + 1);
        DrawAmmoDigits(n, uint16_t(d5 + 0x68));
        return n;
    }
    st.pickup_check = 1;
    s = {};
    st.slot_count -= 1;
    clear_slot_tiles(d5);
    for (uint16_t i = 0; i < 5; i++) {
        if (st.slots[i].weapon == item) {
            var = i;
            return uint16_t(d5 >> 14);
        }
    }
    var = 0xFFFF;
    gone = true;
    return uint16_t(d5 >> 14);
}

} // namespace

// 0x10BD8: night vision charge. Returns d0.
uint16_t NightVisionCharge() {
    bool gone;
    uint16_t d0 = item_timer(st.nv_slot, 9, gone);
    return gone ? NightVisionOff() : d0;
}

// 0x10CC6: charge of item 6; when gone the lighting mode is reapplied. Returns d0.
uint16_t LightCharge() {
    bool gone;
    uint16_t d0 = item_timer(st.light_slot, 6, gone);
    if (!gone) return d0;
    uint16_t mode = st.lighting;
    st.lighting = 0xFFFF;
    return SetLighting(mode);
}

// 0x10DC0: charge of item 1.
void ScannerCharge() {
    bool gone;
    item_timer(st.scanner_slot, 1, gone);
}

// 0x10EAC: charge of item 3 (armour).
void ArmourCharge() {
    bool gone;
    item_timer(st.armour_slot, 3, gone);
}

// 0x10F98: charge of item 5 (shield).
void ShieldCharge() {
    bool gone;
    item_timer(st.shield_slot, 5, gone);
}

// 0x57E0C: counts the enemies left on the current level; when none are
// left, starts the "level cleared" countdown.
void CountEnemiesLeft() {
    if (st.cleared_timer) return;
    uint16_t level = st.pw_level;
    uint16_t d6 = 0;
    uint16_t n = st.object_count;
    Enemy* a0 = first_object();
    for (int i = 0; i < n; i++) {
        if (a0->level == uint8_t(level) && (a0->flags & 0x14) == 0x14) d6++;
        a0 = next_object(a0);
        if (!a0) break;
    }
    Cell map = level_map(level);
    for (int i = 0; i < 0x400; i++) {
        uint8_t c = st.cell_class[map_cell(map + i)];
        if (c >= 0x29 && c <= 0x6B && (c <= 0x2B || c >= 0x65)) d6++;
    }
    if (d6 > 0x63) d6 = 0x63;
    if (d6 != st.counter_shown) DrawEnemyCounter(d6);
    st.enemies_left = d6;
    if (d6) return;
    st.cleared_timer = 0xE1;
    QueueMessage(Msg::ZeroEnemies);
}

namespace {

// Ammo as eighths of each weapon's maximum into carry_ammo, health into
// carry_health.
void save_inventory() {
    for (int i = 0; i < 5; i++) st.carry_weapon[i] = uint8_t(st.slots[i].weapon);
    int n = 0;
    for (uint16_t w = 1; w < 0xF && n != 5; w++) {
        for (const WeaponSlot& s : st.slots) {
            if (s.weapon != w) continue;
            uint32_t d0 = ext_l(s.ammo) << 3;
            if (d0) d0--;
            st.carry_ammo[n++] = uint8_t(divu(d0, kMaxAmmo[w]));
            break;
        }
    }
    while (n != 5) st.carry_ammo[n++] = 0;
    uint32_t h = ext_l(st.health) << 6;
    if (h) h--;
    st.carry_health = uint8_t(divu(h, 0x64));
}

} // namespace

// 0x57EC4: after the countdown, stores the password for the cleared level.
void StoreLevelPassword() {
    if (!st.cleared_timer) return;
    save_inventory();
    level_id = uint8_t((uint8_t(cur_area) << 4) + uint8_t(st.pw_level));
    EncodePassword(st.level_passwords + st.pw_level * 9);
    st.pw_level += 1;
    st.cleared_timer = 0;
    CountEnemiesLeft();
}

// 0x57F98: password for the next area.
void StoreAreaPassword() {
    save_inventory();
    level_id = uint8_t((uint8_t(cur_area) << 4) + 0xF);
    EncodePassword(st.area_password);
    st.pw_level += 1;
    st.cleared_timer = 0;
    CountEnemiesLeft();
}

} // namespace game
