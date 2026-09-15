#pragma once
#include "TimingStats.h"
struct ControllerTracker {
  struct Packet {uint32_t time=0;uint8_t kind=0;};
  struct Link {uint16_t handle=0xffff;Packet queue[64]{};uint8_t head=0,count=0;uint32_t peak=0;bool desynced=false;ReportSpacing creditSpacing;TimingStats batchSizes;};
  Link links[3]{};
  TimingStats windowMouse,windowBatch,windowSpacing;
  TimingStats completed[3]; // other, keyboard, mouse; controller credit-return age
  uint32_t sent=0,returned=0,unmatched=0,overflow=0,discarded=0;
  Link *find(uint16_t h,bool create=false){for(auto &l:links)if(l.handle==h)return &l;if(create)for(auto &l:links)if(l.handle==0xffff){l.handle=h;return &l;}return nullptr;}
  void submit(uint16_t h,uint8_t kind,uint32_t now){++sent;auto l=find(h,true);if(!l){++overflow;return;}if(l->desynced)return;if(l->count==64){++overflow;l->desynced=true;return;}l->queue[(l->head+l->count)%64]={now,kind};++l->count;if(l->count>l->peak)l->peak=l->count;}
  void complete(uint16_t h,uint16_t n,uint32_t now){returned+=n;auto l=find(h);if(!l||l->desynced){unmatched+=n;return;}if(n){if(l->creditSpacing.started&&uint32_t(now-l->creditSpacing.last)<=100000)windowSpacing.add(now-l->creditSpacing.last);l->creditSpacing.observe(now);l->batchSizes.add(n);windowBatch.add(n);}
    if(n>l->count){unmatched+=n;l->desynced=true;return;}while(n--){const auto p=l->queue[l->head];completed[p.kind<3?p.kind:0].add(now-p.time);if(p.kind==2)windowMouse.add(now-p.time);l->head=(l->head+1)%64;--l->count;}}
  void disconnect(uint16_t h){auto l=find(h);if(l){discarded+=l->count;*l={};}}
};
