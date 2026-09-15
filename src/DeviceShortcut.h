#pragma once
#include <stdint.h>

// Control + Command + normal number-row 1/2/3. Either side is accepted.
// HID Control bits: 0/4; GUI (Command) bits: 3/7.
inline int deviceSwitchSlot(const uint8_t *keys, uint8_t modifiers) {
  if (!(modifiers & 0x11) || !(modifiers & 0x88) || (modifiers & ~0x99)) return -1;
  int slot = -1;
  for (int i = 0; i < 6; ++i) {
    if (!keys[i]) continue;
    if (keys[i] < 0x1e || keys[i] > 0x20 || slot != -1) return -1;
    slot = keys[i] - 0x1e;
  }
  return slot;
}
