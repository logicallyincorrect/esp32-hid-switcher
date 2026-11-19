#pragma once
#include "MonitorLayout.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Named screen arrangements ("presets"). Slots stay stable; a preset decides
// which slots are on screen and where. arrangement 0 = free (all on screen).
struct LayoutPreset {
  uint32_t used=0;
  char name[25]={};
  MonitorLayout::Screen screens[3];
};

// 1-24 printable ASCII characters, not all spaces.
inline bool validPresetName(const char *name){
  if(!name)return false;
  size_t length=0;bool visible=false;
  for(;name[length];++length){
    if(length>=24)return false;
    const unsigned char c=name[length];
    if(c<0x20||c>0x7e)return false;
    if(c!=' ')visible=true;
  }
  return length>0&&visible;
}

inline bool presetNamesEqual(const char *a,const char *b){
  for(;*a&&*b;++a,++b){
    char x=*a,y=*b;
    if(x>='A'&&x<='Z')x+=32;
    if(y>='A'&&y<='Z')y+=32;
    if(x!=y)return false;
  }
  return *a==*b;
}

struct LayoutPresets {
  static constexpr unsigned Count=4;
  uint32_t version=1,generation=0,activeIndex=0,arrangement=0;
  LayoutPreset presets[Count];

  bool valid() const {
    if(version!=1||activeIndex>=Count||!presets[activeIndex].used||arrangement>1)return false;
    for(unsigned i=0;i<Count;++i){
      const auto &p=presets[i];
      if(!p.used)continue;
      if(p.used!=1||p.name[24]!=0||!validPresetName(p.name))return false;
      for(unsigned j=0;j<i;++j)if(presets[j].used&&presetNamesEqual(presets[j].name,p.name))return false;
      MonitorLayout layout;layout.active=1;
      unsigned enabled=0;
      for(unsigned s=0;s<3;++s){layout.screens[s]=p.screens[s];if(p.screens[s].enabled==1)++enabled;}
      if(!enabled||!layout.valid())return false;
    }
    return true;
  }
  bool load(const void *bytes,size_t size){
    if(size!=sizeof(LayoutPresets))return false;
    LayoutPresets next;memcpy(&next,bytes,size);
    if(!next.valid())return false;
    *this=next;return true;
  }
  MonitorLayout runtime() const {
    MonitorLayout layout;
    layout.version=2;layout.generation=generation;layout.active=arrangement;
    for(unsigned i=0;i<3;++i)layout.screens[i]=presets[activeIndex<Count?activeIndex:0].screens[i];
    return layout;
  }
  uint8_t onScreen() const {
    if(arrangement==0)return 7;
    uint8_t mask=0;
    const auto &p=presets[activeIndex<Count?activeIndex:0];
    for(unsigned i=0;i<3;++i)if(p.screens[i].enabled)mask|=uint8_t(1u<<i);
    return mask;
  }
};

inline bool screensOverlap(const MonitorLayout::Screen &a,const MonitorLayout::Screen &b){
  return a.x<int64_t(b.x)+b.width&&b.x<int64_t(a.x)+a.width&&a.y<int64_t(b.y)+b.height&&b.y<int64_t(a.y)+a.height;
}

// Enable a slot; if it collides with another enabled screen, park it to the
// right of the rightmost one.
inline void placeOnScreen(MonitorLayout::Screen screens[3],unsigned slot){
  if(slot>=3)return;
  screens[slot].enabled=1;
  bool overlap=false;
  for(unsigned i=0;i<3;++i)if(i!=slot&&screens[i].enabled&&screensOverlap(screens[slot],screens[i]))overlap=true;
  if(!overlap)return;
  bool found=false;int64_t right=0;int32_t rightY=0;
  for(unsigned i=0;i<3;++i){
    if(i==slot||!screens[i].enabled)continue;
    const int64_t edge=int64_t(screens[i].x)+screens[i].width;
    if(!found||edge>right){right=edge;rightY=screens[i].y;found=true;}
  }
  screens[slot].x=int32_t(right);screens[slot].y=rightY;screens[slot].estimated=1;
}

inline LayoutPresets migratePresets(const MonitorLayout &old,uint8_t paired){
  LayoutPresets result;
  auto &p=result.presets[0];
  p.used=1;strcpy(p.name,"Default");
  for(unsigned i=0;i<3;++i){p.screens[i]=old.screens[i];if(paired&&!(paired&(1u<<i)))p.screens[i].enabled=0;}
  for(unsigned i=0;i<3;++i)if(paired&(1u<<i))placeOnScreen(p.screens,i);
  // Nothing paired (fresh device): keep the stored flags, but never migrate to
  // an empty preset.
  if(!p.screens[0].enabled&&!p.screens[1].enabled&&!p.screens[2].enabled)placeOnScreen(p.screens,0);
  result.activeIndex=0;result.arrangement=old.active;result.generation=old.generation;
  return result;
}

// A preset's only on-screen slot stays on: an unpaired slot is never ready, so
// this is harmless and keeps every preset valid.
inline void hideSlot(LayoutPresets &presets,unsigned slot){
  if(slot>=3)return;
  for(auto &p:presets.presets){
    bool other=false;
    for(unsigned i=0;i<3;++i)if(i!=slot&&p.screens[i].enabled)other=true;
    if(other)p.screens[slot].enabled=0;
  }
}

inline void permutePresets(LayoutPresets &presets,const uint8_t order[3]){
  for(auto &p:presets.presets){
    MonitorLayout::Screen old[3];
    for(unsigned i=0;i<3;++i)old[i]=p.screens[i];
    for(unsigned i=0;i<3;++i)p.screens[i]=old[order[i]];
  }
}

inline bool deletePreset(LayoutPresets &presets,unsigned index){
  if(index>=LayoutPresets::Count||!presets.presets[index].used)return false;
  unsigned used=0;
  for(const auto &p:presets.presets)if(p.used)++used;
  if(used<=1)return false;
  presets.presets[index]=LayoutPreset{};
  if(presets.activeIndex==index)
    for(unsigned i=0;i<LayoutPresets::Count;++i)if(presets.presets[i].used){presets.activeIndex=i;break;}
  return true;
}

// Next used preset after the active one, wrapping; -1 when none.
inline int nextPreset(const LayoutPresets &presets){
  for(unsigned step=1;step<=LayoutPresets::Count;++step){
    const unsigned i=(presets.activeIndex+step)%LayoutPresets::Count;
    if(presets.presets[i].used)return int(i);
  }
  return -1;
}

// When switching presets hides the selected slot, pick the connected on-screen
// slot that took over its rectangle (compared after aligning shared slots).
inline int replacementSlot(const LayoutPreset &from,const LayoutPreset &to,unsigned selected,uint8_t connected){
  if(selected>=3||to.screens[selected].enabled)return int(selected);
  int64_t dx=0,dy=0;
  for(unsigned i=0;i<3;++i)if(from.screens[i].enabled&&to.screens[i].enabled){
    dx=int64_t(to.screens[i].x)-from.screens[i].x;dy=int64_t(to.screens[i].y)-from.screens[i].y;break;
  }
  const auto &s=from.screens[selected];
  const int64_t sx=int64_t(s.x)+dx,sy=int64_t(s.y)+dy;
  int best=-1;int64_t bestScore=-1;
  for(unsigned i=0;i<3;++i){
    if(!to.screens[i].enabled||!(connected&(1u<<i)))continue;
    const auto &c=to.screens[i];
    const int64_t w=int64_t(sx+s.width<int64_t(c.x)+c.width?sx+s.width:int64_t(c.x)+c.width)-(sx>c.x?sx:c.x);
    const int64_t h=int64_t(sy+s.height<int64_t(c.y)+c.height?sy+s.height:int64_t(c.y)+c.height)-(sy>c.y?sy:c.y);
    const int64_t score=w>0&&h>0?w*h:0;
    if(score>bestScore){bestScore=score;best=int(i);}
  }
  return best<0?int(selected):best;
}

// Selection after activating preset `to`. With the arrangement flag off,
// presets have no effect and every connected computer counts as on screen.
inline int activationSlot(const LayoutPresets &presets,unsigned to,unsigned selected,uint8_t connected){
  if(!presets.arrangement||to>=LayoutPresets::Count||presets.activeIndex>=LayoutPresets::Count)return int(selected);
  return replacementSlot(presets.presets[presets.activeIndex],presets.presets[to],selected,connected);
}
