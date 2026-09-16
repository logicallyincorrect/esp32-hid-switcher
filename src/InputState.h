#pragma once
#include <cstdint>
#include <cstring>

// Physical input and release barriers belong to the application loop only.
struct InputState {
  uint8_t modifiers = 0, keys[6] = {}, buttons = 0;
  bool keyboardHeld = false, keyboardBlocked = false, mouseBlocked = false;
  void keyboard(const uint8_t report[8]) {
    modifiers = report[0];
    memcpy(keys, report + 2, sizeof(keys));
    keyboardHeld = modifiers != 0;
    for (auto key : keys) keyboardHeld |= key != 0;
  }
  void barrier(bool uncertain = false) {
    keyboardBlocked = uncertain || keyboardHeld;
    mouseBlocked = uncertain || buttons != 0;
  }
  bool consumeKeyboard() {
    if (!keyboardBlocked) return false;
    if (!keyboardHeld) keyboardBlocked = false;
    return true;
  }
  uint8_t forwardedButtons() {
    if (!buttons) mouseBlocked = false;
    return mouseBlocked ? 0 : buttons;
  }
};
