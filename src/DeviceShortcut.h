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

// Tab is consumed even when there is no other connected computer.
inline bool deviceCycleShortcut(const uint8_t *keys, uint8_t modifiers) {
  if (!(modifiers & 0x11) || !(modifiers & 0x88) || (modifiers & ~0x99)) return false;
  bool tab = false;
  for (int i = 0; i < 6; ++i) {
    if (!keys[i]) continue;
    if (keys[i] != 0x2b || tab) return false;
    tab = true;
  }
  return tab;
}
inline unsigned nextConnectedSlot(unsigned current, uint8_t connectedMask) {
  for (unsigned step = 1; step < 3; ++step) {
    const unsigned candidate = (current + step) % 3;
    if (connectedMask & (1u << candidate)) return candidate;
  }
  return current;
}
