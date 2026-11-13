// Mega Drive VDP: ports, DMA and a scanline renderer to 32-bit pixels.
#pragma once

#include <cstdint>

namespace md::vdp {

extern uint8_t vram[0x10000];
extern uint16_t cram[64];
extern uint16_t vsram[40];
extern uint8_t reg[32];

void reset();
void write_ctrl(uint16_t v);
void write_data(uint16_t v);
uint16_t read_ctrl();
uint16_t read_data();
uint16_t read_hv();
void set_vblank(bool on);

int width();  // 256 or 320
int height(); // 224 or 240
bool vint_enabled();

// Renders the current state into out (pitch in pixels), ARGB8888.
void render(uint32_t* out, int pitch);

} // namespace md::vdp
