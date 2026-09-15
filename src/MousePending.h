#pragma once
#include "MouseReport.h"
#include <stddef.h>
#include <stdint.h>

// Merge movement within each button state, preserving press/release ordering.
// Main-loop owned. Retain reports until the transport accepts them.
class MousePending {
  struct Entry { uint8_t buttons=0; int32_t x=0,y=0,wheel=0,pan=0; uint32_t oldest=0,newest=0; };
  Entry entries[32]{};
  size_t head=0,count=0;
  uint8_t lastButtons=0;
  static int32_t add(int32_t a,int32_t b) {
    const int64_t sum=int64_t(a)+b;
    return sum>1000000?1000000:(sum< -1000000?-1000000:int32_t(sum));
  }
  static int32_t limit(int32_t v,int32_t max) { return v>max?max:(v< -max?-max:v); }
public:
  void clear() {head=count=0;lastButtons=0;}
  bool push(const MouseReport &r,uint32_t received=0) {
    if(!count&&r.buttons==lastButtons&&!r.x&&!r.y&&!r.wheel&&!r.pan)return true;
    size_t tail=(head+count-1)%32;
    if(!count||entries[tail].buttons!=r.buttons) {
      if(count==32)return false;
      tail=(head+count++)%32;entries[tail]={};entries[tail].buttons=r.buttons;entries[tail].oldest=received;
    }
    auto &e=entries[tail];e.newest=received;e.x=add(e.x,r.x);e.y=add(e.y,r.y);
    e.wheel=add(e.wheel,r.wheel);e.pan=add(e.pan,r.pan);return true;
  }
  uint32_t oldest()const{return count?entries[head].oldest:0;}
  uint32_t newest()const{return count?entries[head].newest:0;}
  bool peek(MouseReport &r) const {
    if(!count)return false;
    const auto &e=entries[head];r={e.buttons,int16_t(limit(e.x,32767)),int16_t(limit(e.y,32767)),int8_t(limit(e.wheel,127)),int8_t(limit(e.pan,127))};return true;
  }
  void accepted(const MouseReport &r) {
    if(!count)return;
    auto &e=entries[head];e.x-=r.x;e.y-=r.y;e.wheel-=r.wheel;e.pan-=r.pan;
    lastButtons=r.buttons;
    if(!e.x&&!e.y&&!e.wheel&&!e.pan){head=(head+1)%32;--count;}
  }
};
