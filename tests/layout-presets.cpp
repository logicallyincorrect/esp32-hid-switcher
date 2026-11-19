#include "LayoutPresets.h"
#include <cassert>
#include <cstring>
int main(){
  // Migration: paired slot 1 stored disabled and overlapping slot 0 -> on screen, moved right.
  MonitorLayout old;old.active=1;
  old.screens[0]={0,917,1920,1243,1,0,100};old.screens[1]={0,0,7680,2160,0,0,100};old.screens[2]={9600,0,7680,2160,1,0,100};
  auto p=migratePresets(old,0b011);
  assert(p.valid()&&p.presets[0].used&&!strcmp(p.presets[0].name,"Default")&&p.arrangement==1);
  assert(p.presets[0].screens[1].enabled&&p.presets[0].screens[1].x==1920&&p.presets[0].screens[1].estimated==1);
  assert(!p.presets[0].screens[2].enabled&&p.onScreen()==0b011);
  // Runtime layout follows the active preset.
  assert(p.runtime().version==2&&p.runtime().active==1&&p.runtime().screens[1].x==1920);
  p.arrangement=0;assert(p.onScreen()==7);p.arrangement=1;
  // Names.
  assert(validPresetName("Split")&&!validPresetName("")&&!validPresetName("   ")&&!validPresetName("0123456789012345678901234"));
  // Duplicate names (case-insensitive) and empty presets are invalid.
  p.presets[1]=p.presets[0];strcpy(p.presets[1].name,"default");assert(!p.valid());
  strcpy(p.presets[1].name,"Home PC");p.presets[1].screens[1].enabled=0;p.presets[1].screens[2]={1920,0,7680,2160,1,0,100};assert(p.valid());
  auto none=p;for(auto &s:none.presets[1].screens)s.enabled=0;assert(!none.valid());
  // Next preset wraps; delete active moves to lowest used; last cannot be deleted.
  assert(nextPreset(p)==1);p.activeIndex=1;assert(nextPreset(p)==0);
  assert(deletePreset(p,1)&&p.activeIndex==0&&!p.presets[1].used);assert(!deletePreset(p,0)&&!deletePreset(p,3));
  // Reorder follows the computer; forget hides everywhere.
  const uint8_t order[3]={2,0,1};auto r=p;permutePresets(r,order);assert(r.presets[0].screens[0].x==p.presets[0].screens[2].x);
  hideSlot(r,0);assert(!r.presets[0].screens[0].enabled);
  // Replacement: Work Mac (1) hidden by "Home PC" preset; Home PC (2) took its rectangle.
  LayoutPreset work{},home{};work.used=home.used=1;
  work.screens[0]={0,917,1920,1243,1,0,100};work.screens[1]={1920,0,7680,2160,1,0,100};work.screens[2]={9600,0,7680,2160,0,0,100};
  home.screens[0]={0,917,1920,1243,1,0,100};home.screens[1]={1920,0,7680,2160,0,0,100};home.screens[2]={1920,0,7680,2160,1,0,100};
  assert(replacementSlot(work,home,1,0b111)==2);
  // Shifted preset: aligned on slot 0, still picks slot 2.
  auto shifted=home;for(auto &s:shifted.screens)s.x+=5000;assert(replacementSlot(work,shifted,1,0b111)==2);
  // Selected still on screen -> unchanged; nothing connected on screen -> unchanged; no overlap -> lowest slot.
  assert(replacementSlot(work,home,0,0b111)==0);assert(replacementSlot(work,home,1,0b010)==1);
  auto far=home;far.screens[2].x=40000;assert(replacementSlot(work,far,1,0b111)==0);
  // Storage round trip.
  LayoutPresets q;assert(q.load(&p,sizeof(p))&&q.activeIndex==p.activeIndex);
  assert(!q.load(&p,sizeof(p)-1));
}
