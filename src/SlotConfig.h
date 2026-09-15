#pragma once
#include <stdint.h>
#include <string.h>
struct SlotConfig {
  uint32_t version=1, generation=0;
  uint8_t selected=0;
  struct Slot { uint8_t assigned=0,type=0,address[6]={}; char name[33]={}; } slots[3];
};
inline bool validOrder(const uint8_t order[3]) {
  unsigned seen=0;for(unsigned i=0;i<3;++i){if(order[i]>=3)return false;seen|=1u<<order[i];}return seen==7;
}
inline bool validName(const char *name) {
  const size_t length=strnlen(name,33);if(!length||length>32)return false;
  for(size_t i=0;i<length;++i)if(uint8_t(name[i])<32||uint8_t(name[i])==127)return false;
  return true;
}
inline bool reorderedConfig(const SlotConfig &old,const uint8_t order[3],const char names[3][33],SlotConfig &next) {
  if(!validOrder(order))return false;
  next=old;++next.generation;
  for(unsigned i=0;i<3;++i){
    if(!validName(names[i]))return false;
    next.slots[i]=old.slots[order[i]];memcpy(next.slots[i].name,names[i],33);
    if(order[i]==old.selected)next.selected=i;
  }
  return true;
}
