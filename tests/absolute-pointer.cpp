#include "AbsolutePointer.h"
#include "MousePending.h"
#include "MultiHostRouter.h"
#include <cassert>
#include <array>
#include <vector>
struct Transport {
  bool fail=false;
  std::vector<std::array<uint8_t,7>> mice;
  static bool send(void *ctx,uint16_t,const uint8_t *data,uint8_t id,size_t){
    auto &t=*static_cast<Transport*>(ctx);if(t.fail)return false;
    if(id==2){std::array<uint8_t,7> value;memcpy(value.data(),data,7);t.mice.push_back(value);}return true;
  }
};
int main(){
  AbsolutePointer p;p.sync(0,5,true,0);
  MouseReport r{0,1024,100,1,-1};
  assert(!p.motion(r,false,0,1000,100,16,16));
  assert(r.x==32767&&r.y==17984&&r.wheel==1&&r.pan==-1);
  r={0,99,0,0,0};assert(!p.motion(r,false,0,1001,100,16,16));
  r={0,1,0,0,0};assert(p.motion(r,false,0,1002,100,16,16));
  assert(p.destination()==2); // skip disconnected middle slot
  auto entry=p.enter(2,1002);assert(entry.x==0&&entry.y==17984);
  p.sync(2,5,true,1002);
  r={0,-200,0,0,0};assert(!p.motion(r,false,0,1003,100,16,16)); // immediate return guard
  r={1,-200,0,0,0};assert(!p.motion(r,false,0,2000,100,16,16)); // drag
  r={0,-200,0,0,0};assert(!p.motion(r,true,0,2001,100,16,16)); // keyboard/barrier
  r={0,-200,0,0,0};assert(!p.motion(r,false,101,2002,100,16,16)); // stale input
  r={0,-100,0,0,0};assert(p.motion(r,false,0,2003,100,16,16));
  p.sync(2,4,true,2004);assert(p.destination()==-1); // lost destination before commit
  r={0,-32768,-32768,0,0};assert(!p.motion(r,false,0,3000,100,256,256));assert(r.x==0&&r.y==0);
  r={0,32767,32767,0,0};assert(!p.motion(r,false,0,3001,100,256,256));assert(r.x==32767&&r.y==32767); // no wrap
  p.sync(2,5,false,3002);r={0,-32768,0,0,0};assert(!p.motion(r,false,0,4002,1,16,16));

  // Manual selection must take effect even before the next service tick.
  p.positions[1]={123,456};p.select(1,5000);
  r={0,1,1,0,0};assert(!p.motion(r,false,0,5001,100,16,16));assert(r.x==139&&r.y==472);
  uint8_t order[3]={1,0,2};p.reorder(order,5002);assert(p.positions[0].x==139);

  AbsolutePointer fractional;fractional.sync(0,3,true,0);fractional.positions[0]={0,0};
  for(int i=0;i<10;++i){r={0,1,1,0,0};fractional.motion(r,false,0,1000,100,10,10,100);}
  assert(r.x==1&&r.y==1); // low sensitivity cannot stick at the entry edge
  fractional.positions[0].x=32767;
  r={0,100,0,0,0};assert(fractional.motion(r,false,0,1001,100,1,1,100)); // threshold stays in raw counts
  FractionalScale scale;assert(scale.apply(1,50)==0);assert(scale.apply(1,50)==1);
  assert(scale.apply(-1,50)==0);assert(scale.apply(1,50)==0);assert(scale.apply(1,50)==1);
  PointerTuning tuning;unsigned value=0;assert(tuning.valid());
  assert(!PointerTuning::parse("0",0,value)&&!PointerTuning::parse("1601",0,value));
  assert(PointerTuning::parse("42",0,value)&&value==42);

  // Minimum resistance crosses on the report that reaches the boundary,
  // including fractional movement, with no delay after enabling/manual selection.
  AbsolutePointer smooth;smooth.sync(0,7,true,0);smooth.positions[0]={32766,1234};
  r={0,1,0,0,0};assert(smooth.motion(r,false,0,1,1,1,1));
  assert(smooth.destination()==1);smooth.enter(1,1);
  r={0,-1,0,0,0};assert(!smooth.motion(r,false,0,2,1,1,1));
  // Continued motion across even a small/fast middle display is not blocked.
  r={0,32767,0,0,0};assert(smooth.motion(r,false,0,3,1,1,1));
  assert(smooth.destination()==2);smooth.enter(2,3);
  r={0,-1,0,0,0};assert(smooth.motion(r,false,0,83,1,1,1));
  assert(smooth.destination()==1);
  AbsolutePointer fine;fine.sync(0,3,true,0);fine.positions[0]={32766,1000};
  r={0,1,0,0,0};assert(!fine.motion(r,false,0,1,1,1,1,2));
  r={0,1,0,0,0};assert(fine.motion(r,false,0,2,1,1,1,2));
  AbsolutePointer left;left.sync(1,3,true,0);left.positions[1]={1,1000};
  r={0,-1,0,0,0};assert(left.motion(r,false,0,1,1,1,1));assert(left.destination()==0);
  // Holding a button still prevents minimum-resistance switching.
  AbsolutePointer held;held.sync(0,3,true,0);held.positions[0]={32766,1000};
  r={1,1,0,0,0};assert(!held.motion(r,false,0,1,1,1,1));
  r={0,1,0,0,0};assert(!held.motion(r,true,0,2,1,1,1));

  MousePending q(true);assert(q.push({0,0,0,0,0}));assert(q.peek(r)&&r.x==0);q.accepted(r);
  assert(q.push({0,100,200,127,0}));assert(q.push({0,300,400,100,0}));
  assert(q.push({1,500,600,0,0}));assert(q.push({0,700,800,0,0}));
  assert(q.peek(r)&&r.x==300&&r.y==400&&r.wheel==127); // replace positions, sum scroll
  MouseReport retry;assert(q.peek(retry)&&retry.x==300); // failed send unchanged
  q.accepted(r);assert(q.peek(r)&&r.x==300&&r.wheel==100);q.accepted(r);
  assert(q.peek(r)&&r.buttons==1&&r.x==500);q.accepted(r);
  assert(q.peek(r)&&r.buttons==0&&r.x==700);q.accepted(r);assert(!q.peek(r));

  Transport t;MultiHostRouter router(Transport::send,&t,true);
  router.connect(0,10);router.ready(0,true,3);router.service();
  assert(t.mice.back()[2]==64&&t.mice.back()[4]==64); // first neutral center
  uint8_t mouse[7]={1,5,6,7,8,9,10};
  assert(router.tryMouse(mouse));t.fail=true;router.releaseAll();
  assert(!router.tryMouse(mouse));t.fail=false;router.service();
  auto neutral=t.mice.back();assert(neutral[0]==0&&neutral[1]==5&&neutral[2]==6&&neutral[3]==7&&neutral[4]==8&&neutral[5]==0&&neutral[6]==0);
  // Failed movement does not change the position used by release barriers.
  t.fail=true;mouse[1]=99;assert(!router.tryMouse(mouse));t.fail=false;router.releaseAll();assert(t.mice.back()[1]==5);
}
