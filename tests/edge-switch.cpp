#include "EdgeSwitch.h"
#include <cassert>
struct Fixture {
  EdgeSwitch e;
  Fixture(){e.sync(0,7,true,0);sample(0,0,1000);sample(1,0,1000);sample(2,0,1000);sample(0,2,1000);}
  void sample(unsigned slot,uint8_t edge,uint32_t now,bool drag=false,bool enabled=true){e.sample(slot,{edge,drag,enabled,32000,e.epoch},now,now);}
  bool push(uint32_t now){return e.motion(12,0,now,now);}
};
int main(){
  // The destination has no companion samples: a ready HID link is sufficient.
  EdgeSwitch alone;alone.sync(0,3,true,0);
  alone.sample(0,{0,false,true,0,alone.epoch},1000,1000);
  alone.sample(0,{2,false,true,0,alone.epoch},1000,1000);
  assert(!alone.motion(12,0,1000,1000));assert(alone.motion(12,0,1120,1120));
  assert(alone.finish(1120)==1);
  Fixture f;assert(!f.push(1000));assert(f.push(1120));assert(f.e.finish(1120)==1);
  Fixture left;left.sample(0,1,1000);assert(!left.e.motion(-12,0,1000,1000));assert(left.e.motion(-12,0,1120,1120));assert(left.e.finish(1120)==2);
  Fixture stale;assert(!stale.push(1700));assert(!stale.push(1820));
  Fixture drag;drag.sample(0,2,1000,true);assert(!drag.push(1000));assert(!drag.push(1120));
  Fixture timeout;timeout.push(1000);timeout.push(1120);assert(timeout.e.finish(2000)==-1&&timeout.e.target==-1);
  Fixture blocked;blocked.e.sync(0,7,false,1000);blocked.sample(0,0,1000);blocked.sample(0,2,1000);assert(!blocked.push(2000));
  Fixture disabled;disabled.sample(1,0,1000,false,false);disabled.push(1000);assert(disabled.push(1120)&&disabled.e.finish(1120)==1);
  Fixture skip;skip.e.sync(0,5,true,0);skip.sample(0,0,1000);skip.sample(0,2,1000);skip.push(1000);assert(skip.push(1120)&&skip.e.finish(1120)==2);
  Fixture noSource;noSource.e.reset(0);assert(!noSource.push(1000)&&!noSource.push(1120));
  Fixture reverse;reverse.push(1000);reverse.e.motion(-2,0,1100,1100);assert(!reverse.push(1120));
  Fixture buttons;buttons.push(1000);buttons.push(1120);assert(!buttons.e.motion(0,1,1130,1130)&&buttons.e.target==-1);
  Fixture noRearm;noRearm.e.reset(1000);noRearm.sample(0,2,2000);noRearm.sample(1,0,2000);assert(!noRearm.push(2000)&&!noRearm.push(2120));
  Fixture queued;queued.sample(0,2,1000);assert(!queued.e.motion(100,0,1000,1200));
  Fixture inactive;inactive.sample(2,2,1000);inactive.sample(0,0,1000);assert(!inactive.push(1000)&&!inactive.push(1120));
  uint8_t p[12]={1,2,2,0,0,128,4,3,2,1,0,0};EdgeProtocol::Sample s;
  assert(EdgeProtocol::decode(p,12,s)&&s.epoch==0x01020304&&s.y==32768&&s.enabled);
  assert(!EdgeProtocol::decode(p,11,s));p[3]=1;assert(!EdgeProtocol::decode(p,12,s));
  Fixture revoked;revoked.push(1000);revoked.push(1120);revoked.sample(0,0,1130);assert(revoked.e.finish(1140)==-1);
  Fixture disconnected;disconnected.push(1000);disconnected.push(1120);disconnected.sample(1,0,1130);disconnected.e.sync(0,5,true,1140);assert(disconnected.e.finish(1140)==-1);
  Fixture packet;packet.push(1000);packet.push(1120);uint8_t status[12];packet.e.packet(1,status);assert(status[3]==0&&status[10]==0&&EdgeProtocol::read32(status+6)==packet.e.epoch);packet.e.packet(2,status);assert(status[3]==0&&status[10]==0);
  Fixture placement;placement.push(1000);placement.push(1120);assert(placement.e.finish(1120)==1);
  const auto side=placement.e.entry;const auto y=placement.e.height;
  placement.e.sync(1,7,true,1120);placement.e.place(1,side,y,1120);
  placement.e.packet(1,status);assert(status[10]==1&&status[3]==1);
  placement.e.packet(0,status);assert(status[10]==0);
  placement.e.sync(1,7,true,1870);placement.e.packet(1,status);assert(status[10]==0);
  Fixture moved;moved.push(1000);moved.push(1120);moved.e.sync(2,7,true,1130);assert(moved.e.target==-1);
}
