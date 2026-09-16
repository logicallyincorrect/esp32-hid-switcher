#pragma once
#include "SlotConfig.h"
// assigned=2 is a durable deletion marker. Keep the identity until both stores
// are updated so an interrupted deletion can be completed after restart.
enum class ForgetResult { Done, SaveFailed, UnpairFailed };
template<class Save,class Unpair>
ForgetResult forgetPairing(SlotConfig &config,unsigned slot,Save save,Unpair unpair){
  if(slot>=3)return ForgetResult::SaveFailed;
  if(!config.slots[slot].assigned)return ForgetResult::Done;
  if(config.slots[slot].assigned!=2){
    auto pending=config;pending.slots[slot].assigned=2;++pending.generation;
    if(!save(pending))return ForgetResult::SaveFailed;
    config=pending;
  }
  if(!unpair(config.slots[slot]))return ForgetResult::UnpairFailed;
  auto next=config;auto &cleared=next.slots[slot];
  cleared.assigned=0;cleared.type=0;memset(cleared.address,0,6);++next.generation;
  if(!save(next))return ForgetResult::SaveFailed;
  config=next;return ForgetResult::Done;
}
