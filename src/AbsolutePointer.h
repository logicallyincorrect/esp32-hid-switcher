#pragma once
#include "MouseReport.h"
#include "PointerTuning.h"
#include <stdint.h>

// Firmware-owned normalized position. No claim to observe the OS cursor.
class AbsolutePointer {
public:
  static constexpr int maximum=32767, center=16384;
  static constexpr uint32_t cooldown=800;
  struct Position { int16_t x=center,y=center; };
  Position positions[3];
  int target=-1;
  void reset(uint32_t now) {target=-1;distance=0;changed=now;for(auto &f:fractionsX)f.clear();for(auto &f:fractionsY)f.clear();}
  void select(unsigned slot,uint32_t now) {if(slot!=selected){selected=slot;reset(now);}}
  void sync(unsigned slot,uint8_t ready,bool allowed,uint32_t now) {
    if(slot!=selected||ready!=readyMask||allowed!=enabled)reset(now);
    selected=slot;readyMask=ready;enabled=allowed;
  }
  // Mutates only X/Y into absolute coordinates; scroll and buttons stay intact.
  // Once switching is pending, consume remaining reports until finish/recheck.
  bool motion(MouseReport &r,bool blocked,uint32_t ageMs,uint32_t now,
              unsigned threshold,int gainX,int gainY,int denominator=1,int denominatorY=0) {
    if(!denominatorY)denominatorY=denominator;
    if(blocked||r.buttons||ageMs>100) {target=-1;distance=0;}
    if(target>=0)return true;
    auto &p=positions[selected];
    const int dx=r.x,previousX=p.x;
    const int nextX=int(p.x)+fractionsX[selected].apply(dx,gainX,denominator);
    const int nextY=int(p.y)+fractionsY[selected].apply(r.y,gainY,denominatorY);
    p.x=clamp(nextX);p.y=clamp(nextY);
    if((nextX<=0&&dx<0)||(nextX>=maximum&&dx>0))fractionsX[selected].clear();
    if((nextY<=0&&r.y<0)||(nextY>=maximum&&r.y>0))fractionsY[selected].clear();
    r.x=p.x;r.y=p.y;
    if(blocked||r.buttons||ageMs>100||!enabled||!(readyMask&(1u<<selected))||now-changed<cooldown){distance=0;return false;}
    const int64_t projected=int64_t(previousX)*denominator+int64_t(dx)*gainX;
    const int direction=projected<0?-1:(projected>int64_t(maximum)*denominator?1:0);
    if(!direction){if(dx)distance=0;return false;}
    // Count only motion beyond the edge, not the whole screen-crossing report.
    const unsigned toEdge=unsigned(direction<0?previousX:maximum-previousX);
    const unsigned countsToEdge=(uint64_t(toEdge)*unsigned(denominator)+unsigned(gainX)-1)/unsigned(gainX);
    const unsigned outward=unsigned(direction<0?-dx:dx);
    if(direction!=pushDirection)distance=0;
    pushDirection=direction;
    distance+=outward>countsToEdge?outward-countsToEdge:0;
    if(distance<threshold)return false;
    for(int slot=int(selected)+direction;slot>=0&&slot<3;slot+=direction)
      if(readyMask&(1u<<slot)){target=slot;entry={int16_t(direction>0?0:maximum),p.y};return true;}
    distance=0;return false; // no wrap
  }
  void reorder(const uint8_t order[3],uint32_t now) {
    Position copy[3]={positions[0],positions[1],positions[2]};
    for(unsigned i=0;i<3;++i)positions[i]=copy[order[i]];
    reset(now);
  }
  int destination()const{return target;}
  MouseReport enter(unsigned slot,uint32_t now) {
    positions[slot]=entry;selected=slot;reset(now);
    return {0,positions[slot].x,positions[slot].y,0,0};
  }
private:
  static int16_t clamp(int value){return int16_t(value<0?0:(value>maximum?maximum:value));}
  unsigned selected=0,distance=0;
  int pushDirection=0;
  uint8_t readyMask=0;
  bool enabled=false;
  uint32_t changed=0;
  Position entry;
  FractionalScale fractionsX[3],fractionsY[3];
};
