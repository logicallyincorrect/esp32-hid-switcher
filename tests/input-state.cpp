#include "InputState.h"
#include <cassert>
int main() {
  InputState input;
  uint8_t down[8] = {1, 0, 4}, up[8] = {};
  input.keyboard(down); input.buttons = 3;
  input.barrier();
  assert(input.consumeKeyboard());
  assert(input.forwardedButtons() == 0);
  input.keyboard(up); assert(input.consumeKeyboard());
  input.keyboard(down); assert(!input.consumeKeyboard());
  input.buttons = 0; assert(input.forwardedButtons() == 0);
  input.buttons = 2; assert(input.forwardedButtons() == 2);
  // Lost queue contents can hide physical presses. Require release on both devices.
  input.keyboard(up); input.buttons = 0; input.barrier(true);
  input.keyboard(down); input.buttons = 1;
  assert(input.consumeKeyboard() && input.forwardedButtons() == 0);
  input.keyboard(up); input.buttons = 0;
  assert(input.consumeKeyboard() && input.forwardedButtons() == 0);
  input.keyboard(down); input.buttons = 1;
  assert(!input.consumeKeyboard() && input.forwardedButtons() == 1);
  // A mouse barrier must not suppress keyboard typing after a clean transition.
  input.keyboard(up); input.barrier();
  input.keyboard(down); assert(!input.consumeKeyboard());
}
