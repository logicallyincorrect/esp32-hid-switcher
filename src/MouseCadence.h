#pragma once
#include <stdint.h>
// Keep scheduled deadlines across ordinary loop jitter. A stall or long idle
// restarts the cadence instead of sending a burst to repay missed deadlines.
class MouseCadence {
  uint32_t last=0,deadline=0,period=0;
  bool sent=false;
public:
  bool due(uint32_t now,uint32_t interval){
    if(interval!=period){period=interval;if(sent)deadline=last+period;}
    return !sent || uint32_t(now-last)>=period || int32_t(now-deadline)>=0;
  }
  void accepted(uint32_t now){
    if(!sent || uint32_t(now-deadline)>=period/2)deadline=now+period;
    else deadline+=period;
    last=now;sent=true;
  }
  void clear(){sent=false;}
};
