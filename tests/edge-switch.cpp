#include "EdgeSwitch.h"
#include <cassert>
struct Fixture {
  EdgeSwitch e;
  Fixture(uint8_t ready=7){e.sync(0,ready,true,0);sample(0,0,1000);sample(0,2,1000);}
  void sample(unsigned slot,uint8_t edge,uint32_t now,bool drag=false,bool enabled=true){e.sample(slot,{edge,drag,enabled,0,e.epoch},now,now);}
  bool push(uint32_t now){return e.motion(1,0,now,now);}
};
int main(){
  // One outward count switches on the first report, without destination samples.
  Fixture right;assert(right.push(1000));assert(right.e.finish(1000)==1);
  Fixture left;left.sample(0,1,1000);assert(left.e.motion(-1,0,1000,1000));assert(left.e.finish(1000)==2);
  Fixture skip(5);assert(skip.push(1000));assert(skip.e.finish(1000)==2);
  Fixture alone(1);assert(!alone.push(1000));
  Fixture disabled;disabled.sample(1,0,1000,false,false);assert(disabled.push(1000));assert(disabled.e.finish(1000)==1);
  Fixture stale;assert(!stale.push(1700));
  Fixture dragging;dragging.sample(0,2,1000,true);assert(!dragging.push(1000));
  Fixture buttons;assert(!buttons.e.motion(1,1,1000,1000));
  Fixture reverse;assert(!reverse.e.motion(-1,0,1000,1000));assert(reverse.push(1000));
  Fixture vertical;assert(!vertical.e.motion(0,0,1000,1000));
  Fixture queued;assert(!queued.e.motion(1,0,800,1000));
  Fixture blocked;blocked.e.sync(0,7,false,1000);assert(!blocked.push(1000));
  Fixture cancel;assert(cancel.push(1000));cancel.e.sync(0,7,false,1000);assert(cancel.e.finish(1000)==-1);
  Fixture disconnect;assert(disconnect.push(1000));disconnect.e.sync(0,5,true,1000);assert(disconnect.e.finish(1000)==-1);
  Fixture revoked;assert(revoked.push(1000));revoked.sample(0,0,1000);assert(revoked.e.finish(1000)==-1);
  Fixture noRearm;noRearm.e.reset(0);noRearm.sample(0,2,1000);assert(!noRearm.push(1000));
  Fixture cooldown;assert(cooldown.push(1000));assert(cooldown.e.finish(1000)==1);cooldown.e.sync(1,7,true,1000);
  cooldown.sample(1,0,1500);cooldown.sample(1,2,1500);assert(!cooldown.push(1799));assert(cooldown.push(1800));
  uint8_t packet[12];cooldown.e.packet(2,packet);assert(packet[3]==0&&packet[4]==0&&packet[5]==0&&packet[10]==0);
  uint8_t p[12]={1,2,2,0,0,128,4,3,2,1,0,0};EdgeProtocol::Sample s;
  assert(EdgeProtocol::decode(p,12,s)&&s.epoch==0x01020304&&s.enabled);
  assert(!EdgeProtocol::decode(p,11,s));p[3]=1;assert(!EdgeProtocol::decode(p,12,s));
}
