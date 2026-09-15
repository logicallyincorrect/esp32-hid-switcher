#pragma once
#include <cstdint>
// Main-loop state for one connection. Healthy links never trigger recovery.
struct ReconnectGuard {
  enum class Action { None, Secure, Refresh, Disconnect };
  uint32_t connectedAt=0,lastAttempt=0,encryptedAt=0;
  bool sawEncrypted=false,attempted=false,securityAccepted=false,refreshed=false,closing=false;
  void reset(uint32_t now){*this={};connectedAt=now;}
  Action poll(uint32_t now,bool encrypted,bool keyboardSubscribed){
    if(closing)return Action::None;
    if(encrypted&&!sawEncrypted){sawEncrypted=true;encryptedAt=now;}
    if(encrypted&&keyboardSubscribed)return Action::None;
    const bool expired=encrypted?(uint32_t(now-encryptedAt)>=15000):(uint32_t(now-connectedAt)>=45000);
    if(expired){closing=true;return Action::Disconnect;}
    if(encrypted&&!refreshed&&uint32_t(now-encryptedAt)>=5000){refreshed=true;return Action::Refresh;}
    if(!encrypted&&!securityAccepted&&uint32_t(now-connectedAt)>=1000&&(!attempted||uint32_t(now-lastAttempt)>=1000)){
      attempted=true;lastAttempt=now;return Action::Secure;
    }
    return Action::None;
  }
};
