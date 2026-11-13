// Six-button pads on the controller ports, driven by the host input.
#pragma once

#include <cstdint>

namespace md::pad {

// Active-high button bits for set_buttons().
enum : uint16_t {
    UP = 1, DOWN = 2, LEFT = 4, RIGHT = 8, B = 16, C = 32, A = 64, START = 128,
    Z = 256, Y = 512, X = 1024, MODE = 2048,
};

void set_buttons(int port, uint16_t held);
void new_frame(); // resets the six-button TH step counter
uint8_t read(int port);
void write(int port, uint8_t v);

} // namespace md::pad
