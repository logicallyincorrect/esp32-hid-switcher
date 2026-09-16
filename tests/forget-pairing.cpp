#include "ForgetPairing.h"
#include <cassert>
#include <cstdio>
int main(){
  SlotConfig initial;
  for(unsigned i=0;i<3;++i){auto &s=initial.slots[i];s.assigned=1;s.type=i%2;memset(s.address,i+1,6);snprintf(s.name,33,"Computer %u",i+1);}
  for(unsigned target=0;target<3;++target){
    auto live=initial,disk=initial;unsigned saves=0,unpairs=0;
    auto save=[&](const SlotConfig &next){++saves;disk=next;return true;};
    auto unpair=[&](const SlotConfig::Slot &s){assert(s.assigned==2);assert(!memcmp(s.address,initial.slots[target].address,6));++unpairs;return true;};
    assert(forgetPairing(live,target,save,unpair)==ForgetResult::Done);
    assert(saves==2&&unpairs==1&&!live.slots[target].assigned);
    assert(!memcmp(&live,&disk,sizeof(live))&&live.selected==initial.selected);
    assert(!strcmp(live.slots[target].name,initial.slots[target].name));
    for(unsigned i=0;i<3;++i)if(i!=target)assert(!memcmp(&live.slots[i],&initial.slots[i],sizeof(SlotConfig::Slot)));
    assert(forgetPairing(live,target,save,unpair)==ForgetResult::Done&&unpairs==1&&saves==2);
  }
  auto live=initial,disk=initial;unsigned calls=0;
  auto failedSave=[](const SlotConfig &){return false;};auto countUnpair=[&](const SlotConfig::Slot &){++calls;return true;};
  assert(forgetPairing(live,0,failedSave,countUnpair)==ForgetResult::SaveFailed&&calls==0);
  assert(!memcmp(&live,&initial,sizeof(live)));
  auto save=[&](const SlotConfig &next){disk=next;return true;};
  assert(forgetPairing(live,0,save,[](const SlotConfig::Slot &){return false;})==ForgetResult::UnpairFailed);
  assert(disk.slots[0].assigned==2);live=disk; // Restart after bond-store failure.
  assert(forgetPairing(live,0,save,countUnpair)==ForgetResult::Done&&!disk.slots[0].assigned);
  live=disk=initial;unsigned saves=0;
  auto failFinal=[&](const SlotConfig &next){if(++saves==2)return false;disk=next;return true;};
  assert(forgetPairing(live,1,failFinal,countUnpair)==ForgetResult::SaveFailed);
  assert(disk.slots[1].assigned==2);live=disk; // Restart after bond deletion but before slot cleanup.
  assert(forgetPairing(live,1,save,countUnpair)==ForgetResult::Done&&!disk.slots[1].assigned);
  const unsigned previous=calls;
  assert(forgetPairing(live,3,save,countUnpair)==ForgetResult::SaveFailed&&calls==previous);
}
