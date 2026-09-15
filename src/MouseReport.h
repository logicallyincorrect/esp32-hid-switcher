#pragma once
#include <stdint.h>
struct MouseReport { uint8_t buttons = 0; int16_t x = 0, y = 0; int8_t wheel = 0, pan = 0; };
