// 3D view walls: DrawWallSpan (0xCCB6) and DrawPortalSpan (0xD5CE), which
// record the spans render.cpp projected for the screen (hires).
#include "game.hpp"
#include "hires.hpp"
#include "render.hpp"

namespace game {

namespace {

// Columns [lit_a, lit_b) unshaded: the light (FF106A) without night vision.
void lit_columns(hires::Cmd& hc) {
    if (int16_t(st.nv_slot) < 0 && int16_t(st.light_slot) >= 0) {
        hc.lit_a = st.lit_start;
        hc.lit_b = st.lit_end;
    }
}

} // namespace

// The wall face view.strip with the door offsets view.door.
void DrawWallSpan() {
    hires::Cmd hc{};
    hc.strip = view.strip;
    hc.shaded = st.walls_shaded != 0;
    hc.tilt = int16_t(uint16_t(st.view_z));
    hc.door = view.door[0] || view.door[1];
    hc.gradient = !hc.door;
    hc.door_a = int8_t(view.door[0]);
    hc.door_b = int8_t(view.door[1]);
    lit_columns(hc);
    hires::wall(hc);
    WideWallDepth();
}

// A portal: the background shows through above and below the opening,
// which is as high as a wall at that distance.
void DrawPortalSpan() {
    hires::Cmd hc{};
    hc.gradient = view.portal_gradient != 0;
    hc.tilt = int16_t(uint16_t(st.view_z));
    hires::portal(hc);
}

} // namespace game
