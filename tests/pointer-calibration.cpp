#include "PointerCalibration.h"
#include "PointerTuning.h"
#include "AbsolutePointer.h"
#include <cassert>
int main(){
  PointerCalibration c;assert(!c.begin(0,0,0));assert(c.begin(5,1,100));assert(c.slot==0&&c.origin==1);
  assert(c.motion({1,0,0,0,0},101));assert(!c.pending); // held button on entry is not a mark
  c.motion({},102);c.motion({0,-20,-20,0,0},103);c.motion({1,0,0,0,0},104);
  assert(c.pending);c.settle(true,500);assert(c.stage==PointerCalibration::TopLeft); // needs release
  c.motion({},501);c.settle(false,502);assert(c.stage==PointerCalibration::TopLeft); // transport must drain
  c.settle(true,503);assert(c.stage==PointerCalibration::BottomRight&&c.x==0&&c.y==0);
  c.motion({0,3000,1000,0,0},504);c.motion({0,-100,-50,0,0},505);c.motion({1,100,50,0,0},506);
  assert(c.x==3000&&c.y==1000);assert(!c.motion({1,1000,1000,0,0},507)); // frozen at click
  c.motion({},508);c.settle(true,600);assert(c.stage==PointerCalibration::BottomRight);
  c.settle(true,800);assert(c.valid());
  assert(c.next(7)==2); // only slots in initial snapshot, never new slot 1
  c.start(2,801);assert(c.x==0&&!c.pending&&c.stage==PointerCalibration::TopLeft);
  c.motion({},802);c.motion({1,0,0,0,0},803);c.motion({},804);c.settle(true,1100);
  c.motion({1,-100,200,0,0},1101);c.motion({},1102);c.settle(true,1400);assert(!c.valid());
  assert(c.next(1)==-1);c.cancel();assert(!c.active());
  assert(c.begin(7,0,0xfffffff0));assert(c.expired(120001));c.cancel();
  // Calibrated spans reproduce full-screen traversal without percentages rounding it.
  AbsolutePointer pointer;pointer.sync(0,3,false,0);pointer.positions[0]={0,0};MouseReport r{0,3000,1000,0,0};
  pointer.motion(r,false,0,1,100,3276700,3276700,300000,100000);assert(r.x==32767&&r.y==32767);
  PointerTuning t;t.spanX=3000;t.spanY=1000;assert(t.valid());t.spanY=0;assert(!t.valid());
}
