#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Coordinates and extents in the user's desktop pixel space. Slot numbers
// remain stable; dimensions describe edge mapping, not mouse calibration.
struct MonitorLayout {
  struct Screen { int32_t x=0,y=0;uint32_t width=1920,height=1080,enabled=1,estimated=1,sensitivity=100; };
  uint32_t version=2,generation=0,active=0;
  Screen screens[3]={{0,0,1920,1080,1},{1920,0,1920,1080,1},{3840,0,1920,1080,1}};
  bool valid() const {
    if(version!=2||active>1)return false;
    unsigned count=0;
    for(unsigned i=0;i<3;++i){const auto &a=screens[i];
      if(a.sensitivity<25||a.sensitivity>400||a.estimated>1||a.enabled>1||a.width<64||a.width>16384||a.height<64||a.height>16384||a.x<-65536||a.x>65536||a.y<-65536||a.y>65536)return false;
      if(!a.enabled)continue;
      ++count;
      for(unsigned j=0;j<i;++j){const auto &b=screens[j];if(!b.enabled)continue;
        if(a.x<int64_t(b.x)+b.width&&b.x<int64_t(a.x)+a.width&&a.y<int64_t(b.y)+b.height&&b.y<int64_t(a.y)+a.height)return false;
      }
    }
    return !active||count>0;
  }
  struct LegacyScreen {int32_t x,y;uint32_t width,height,enabled,estimated;};
  struct Legacy {uint32_t version,generation,active;LegacyScreen screens[3];};
  bool load(const void *bytes,size_t size){
    MonitorLayout next;
    if(size==sizeof(MonitorLayout))memcpy(&next,bytes,size);
    else if(size==sizeof(Legacy)){
      Legacy old;memcpy(&old,bytes,size);if(old.version!=1)return false;
      next.generation=old.generation;next.active=old.active;
      for(unsigned i=0;i<3;++i){const auto &s=old.screens[i];next.screens[i]={s.x,s.y,s.width,s.height,s.enabled,s.estimated,100};}
    }else return false;
    if(!next.valid())return false;
    *this=next;return true;
  }
  // side: -1 left, +1 right, -2 top, +2 bottom. Only touching edges
  // with a nonzero shared segment connect; corners and gaps do not.
  int crossing(unsigned from,int side,int nx,int ny,uint8_t ready,int16_t &outX,int16_t &outY) const {
    if(from>=3||(!screens[from].enabled&&!(ready&(1u<<from))))return -1;
    const bool horizontal=side==1||side==-1;
    auto projected=[&](unsigned index){
      Screen result=screens[index];
      for(unsigned j=0;j<3;++j){
        if(j==index||!screens[j].enabled||(ready&(1u<<j)))continue;
        const auto &offline=screens[j];
        const bool verticalOverlap=result.y<offline.y+int32_t(offline.height)&&offline.y<result.y+int32_t(result.height);
        const bool horizontalOverlap=result.x<offline.x+int32_t(offline.width)&&offline.x<result.x+int32_t(result.width);
        if(result.x>=offline.x&&verticalOverlap)result.x-=int32_t(offline.width);
        if(result.y>=offline.y&&horizontalOverlap)result.y-=int32_t(offline.height);
      }
      return result;
    };
    const Screen initial=projected(from);
    unsigned source=from;int64_t point=int64_t(horizontal?initial.y:initial.x)*32767+int64_t(horizontal?ny:nx)*(horizontal?initial.height:initial.width);
    for(unsigned hop=0;hop<3;++hop){
      const Screen a=projected(source);
      for(unsigned i=0;i<3;++i){const auto &b=screens[i];if(i==source||(!b.enabled&&!(ready&(1u<<i)))||!(ready&(1u<<i)))continue;
      const Screen candidate=projected(i);
      const bool touching=side==1?int64_t(a.x)+a.width==candidate.x:side==-1?int64_t(candidate.x)+candidate.width==a.x:side==2?int64_t(a.y)+a.height==candidate.y:int64_t(candidate.y)+candidate.height==a.y;
      if(!touching)continue;
      const int64_t start=horizontal?candidate.y:candidate.x,end=start+(horizontal?candidate.height:candidate.width);
      const int64_t aStart=horizontal?a.y:a.x,aEnd=aStart+(horizontal?a.height:a.width);
      if(aStart>=end||start>=aEnd||point<start*32767||point>end*32767)continue;
      const int16_t mapped=int16_t((point-start*32767)/(horizontal?candidate.height:candidate.width));
      outX=horizontal?(side>0?0:32767):mapped;
      outY=horizontal?mapped:(side>0?0:32767);return int(i);
      }
    }
    return -1;
  }
};
