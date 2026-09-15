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
  for (int control : {0x01,0x10,0x11})
    for (int command : {0x08,0x80,0x88}) {
      uint8_t tab[6]={0,0,0,0,0,0x2b};
      assert(deviceCycleShortcut(tab,control|command));
      for (int extra : {0x02,0x04,0x20,0x40})
        assert(!deviceCycleShortcut(tab,control|command|extra));
      assert(deviceSwitchSlot(tab,control|command)==-1);
      tab[0]=4;assert(!deviceCycleShortcut(tab,control|command));
    }
  uint8_t tab[6]={0x2b};
  for (int modifiers : {0,0x01,0x10,0x11,0x08,0x80,0x88})
    assert(!deviceCycleShortcut(tab,modifiers));
  tab[1]=0x2b;assert(!deviceCycleShortcut(tab,0x81));
  tab[0]=tab[1]=0;assert(!deviceCycleShortcut(tab,0x81));
  for (unsigned current=0;current<3;++current)
    for (uint8_t mask=0;mask<8;++mask) {
      unsigned expected=current;
      const unsigned first=(current+1)%3,second=(current+2)%3;
      if(mask&(1u<<first))expected=first;
      else if(mask&(1u<<second))expected=second;
      assert(nextConnectedSlot(current,mask)==expected);
    }
}
