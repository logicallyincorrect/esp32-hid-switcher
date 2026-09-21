#pragma once
#include <stdint.h>
#include "MouseReport.h"

// Two human-marked corners measured using RELATIVE output, never the current
// absolute transform. Caller drains transport before accepting each mark.
class PointerCalibration {
public:
  enum Stage { Idle,TopLeft,BottomRight,Ready };
  static constexpr uint32_t timeout=120000,settleTime=200;
  Stage stage=Idle;
  unsigned slot=0,origin=0;
  uint8_t remaining=0,buttons=0;
  int64_t x=0,y=0;
  bool pending=false,armed=false;
  uint32_t activity=0,markAt=0;
  bool active()const{return stage!=Idle;}
  static int first(uint8_t mask){for(int i=0;i<3;++i)if(mask&(1u<<i))return i;return -1;}
  bool begin(uint8_t mask,unsigned original,uint32_t now){
    mask&=7;if(!mask||original>=3)return false;
    origin=original;remaining=mask;start(unsigned(first(mask)),now);return true;
  }
  void start(unsigned next,uint32_t now){
    slot=next;stage=TopLeft;x=y=0;pending=armed=false;buttons=0;activity=now;
  }
  void cancel(){stage=Idle;remaining=0;pending=false;}
  bool expired(uint32_t now)const{return active()&&uint32_t(now-activity)>timeout;}
  // Returns true only for movement that should be forwarded to the host.
  bool motion(const MouseReport &r,uint32_t now){
    if(!active())return false;
    const bool click=(r.buttons&1)&&!(buttons&1)&&armed;
    if(r.x||r.y||buttons!=r.buttons)activity=now;
    buttons=r.buttons;if(!buttons)armed=true;
    if(pending||stage==Ready)return false;
    if(stage==BottomRight){x+=r.x;y+=r.y;}
    if(click){pending=true;markAt=now;armed=false;}
    return true;
  }
  void settle(bool drained,uint32_t now){
    if(!pending||!drained||buttons||uint32_t(now-markAt)<settleTime)return;
    pending=false;
    if(stage==TopLeft){x=y=0;stage=BottomRight;}
    else if(stage==BottomRight)stage=Ready;
  }
  bool valid()const{return stage==Ready&&x>=64&&y>=64&&x<=1000000&&y<=1000000;}
  // Remove completed slot; newly connected hosts never join an ongoing sweep.
  int next(uint8_t ready) {remaining&=~(1u<<slot);remaining&=ready;return first(remaining);}
};
