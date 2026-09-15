#pragma once
#include <stdint.h>
// Actual errors trigger bounded recovery; idle input never does.
struct RecoveryPolicy {
  bool pending=false,attempted=false;
  uint32_t lastAttempt=0,attempts=0;
  void fault(){pending=true;}
  bool poll(uint32_t now){
    if(!pending||(attempted&&uint32_t(now-lastAttempt)<30000))return false;
    pending=false;attempted=true;lastAttempt=now;++attempts;return true;
  }
};
