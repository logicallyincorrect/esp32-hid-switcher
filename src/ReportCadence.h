#pragma once
#include <stdint.h>
// Preserve the cadence across small loop delays; never burst to catch up.
class ReportCadence {
  uint32_t deadline=0;
  bool started=false;
public:
  bool due(uint32_t now,uint32_t period) {
    if(!started){deadline=now;started=true;}
    if(int32_t(now-deadline)<0)return false;
    const uint32_t late=now-deadline;
    deadline+=((late/period)+1)*period;
    return true;
  }
};
