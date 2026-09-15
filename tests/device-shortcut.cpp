#include "DeviceShortcut.h"
#include <cassert>
#include <initializer_list>
int main() {
  for (int slot=0; slot<3; ++slot) {
    uint8_t keys[6] = {uint8_t(0x1e + slot),0,0,0,0,0};
    for (int control : {0x01,0x10,0x11})
      for (int command : {0x08,0x80,0x88}) {
        const auto modifiers = control | command;
        assert(deviceSwitchSlot(keys,modifiers)==slot);
        assert(deviceSwitchSlot(keys,modifiers|0x02)==-1);
        assert(deviceSwitchSlot(keys,modifiers|0x04)==-1);
        assert(deviceSwitchSlot(keys,modifiers|0x20)==-1);
        assert(deviceSwitchSlot(keys,modifiers|0x40)==-1);
      }
    for (int modifiers : {0,0x01,0x10,0x11,0x08,0x80,0x88})
      assert(deviceSwitchSlot(keys,modifiers)==-1);
    keys[1]=4; assert(deviceSwitchSlot(keys,0x81)==-1);
    keys[1]=0x1f; assert(deviceSwitchSlot(keys,0x81)==-1);
  }
  uint8_t keys[6]={0}; assert(deviceSwitchSlot(keys,0x81)==-1);
  keys[0]=0x3a; assert(deviceSwitchSlot(keys,0x81)==-1);
  keys[0]=0x21; assert(deviceSwitchSlot(keys,0x81)==-1);
  keys[0]=1; assert(deviceSwitchSlot(keys,0x81)==-1);
  keys[0]=0; keys[5]=0x20; assert(deviceSwitchSlot(keys,0x81)==2);
}
