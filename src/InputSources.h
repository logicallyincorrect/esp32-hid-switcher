#pragma once
#include <stdint.h>
#include <string.h>
#include "MouseReport.h"

// Application-loop owned. A release from one source must not release another.
template<unsigned Count>
class InputSources {
  uint8_t keyboards[Count][8]={},buttons[Count]={};
public:
  void clear(unsigned source){if(source<Count){memset(keyboards[source],0,8);buttons[source]=0;}}
  void keyboard(unsigned source,const uint8_t *report){if(source<Count)memcpy(keyboards[source],report,8);}
  void combined(uint8_t out[8])const{
    memset(out,0,8);bool keys[256]={},rollover=false;
    for(const auto &r:keyboards){out[0]|=r[0];for(unsigned i=2;i<8;++i){if(r[i]&&r[i]<=3)rollover=true;else if(r[i])keys[r[i]]=true;}}
    unsigned n=0;for(unsigned k=4;k<256;++k)if(keys[k]){if(n<6)out[2+n]=k;++n;}
    if(rollover||n>6)memset(out+2,1,6);
  }
  uint8_t mouse(unsigned source,uint8_t held){if(source<Count)buttons[source]=held;return mouseButtons();}
  uint8_t mouseButtons()const{uint8_t b=0;for(auto held:buttons)b|=held;return b;}
};
