#pragma once
#include <cstdint>
#include <cstddef>

namespace EdgeProtocol {
inline constexpr char service[] = "e963a000-8f4d-4a7b-9b65-62485c81a320";
inline constexpr char sample[] = "e963a001-8f4d-4a7b-9b65-62485c81a320";
inline constexpr char status[] = "e963a002-8f4d-4a7b-9b65-62485c81a320";
constexpr size_t size = 12;
inline uint32_t read32(const uint8_t *p){return uint32_t(p[0])|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
inline void write32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));}
struct Sample {uint8_t edge=0;bool dragging=false,enabled=false;uint16_t y=0;uint32_t epoch=0;};
inline bool decode(const uint8_t *p,size_t n,Sample &out){
  if(n!=size||p[0]!=1||p[1]>2||(p[2]&~3)||p[3]||p[10]||p[11])return false;
  out={p[1],bool(p[2]&1),bool(p[2]&2),uint16_t(p[4]|uint16_t(p[5])<<8),read32(p+6)};return true;
}
}

// Main-loop state only. USB motion is authoritative after the pointer stops at an edge.
class EdgeSwitch {
public:
  static constexpr uint32_t freshness=600, cooldown=800;
  static constexpr unsigned pushDistance=100;
  struct Host {EdgeProtocol::Sample sample;uint32_t received=0,edgeSince=0;bool seen=false,armed=false;};
  uint32_t epoch=1;
  unsigned selected=0;
  int target=-1;
private:
  Host _hosts[3];
  bool _allowed=false;
  uint8_t _ready=0;
  uint32_t _changed=0;
  unsigned _distance=0;
  bool fresh(unsigned slot,uint32_t now)const{return slot<3&&(_ready&(1u<<slot))&&_hosts[slot].seen&&_hosts[slot].sample.enabled&&!_hosts[slot].sample.dragging&&uint32_t(now-_hosts[slot].received)<=freshness;}
public:
  void reset(uint32_t now){
    ++epoch;if(!epoch)++epoch;
    for(auto &host:_hosts)host={};
    target=-1;_changed=now;_distance=0;
  }
  void sync(unsigned slot,uint8_t ready,bool allowed,uint32_t now){
    if(slot!=selected||ready!=_ready||allowed!=_allowed){reset(now);selected=slot;_ready=ready;_allowed=allowed;}
  }
  void sample(unsigned slot,const EdgeProtocol::Sample &sample,uint32_t received,uint32_t now){
    if(slot>=3||sample.epoch!=epoch||uint32_t(now-received)>freshness)return;
    auto &host=_hosts[slot];
    if(slot==selected&&(!host.seen||sample.edge!=host.sample.edge||!sample.edge||
       sample.dragging||!sample.enabled||uint32_t(received-host.received)>freshness))_distance=0;
    if(!host.seen||sample.edge!=host.sample.edge)host.edgeSince=received;
    host.sample=sample;host.received=received;host.seen=true;
    if(!sample.edge&&sample.enabled&&!sample.dragging)host.armed=true;
    if(target>=0&&slot==selected&&(!sample.enabled||sample.dragging||!sample.edge))reset(now);
  }
  bool motion(int16_t dx,uint8_t buttons,uint32_t received,uint32_t now){
    if(buttons){if(target>=0)reset(now);_distance=0;return false;}
    if(target>=0)return true;
    auto &host=_hosts[selected];
    if(!_allowed||!fresh(selected,now)||!host.armed||!host.sample.edge||uint32_t(now-_changed)<cooldown||uint32_t(now-received)>100){_distance=0;return false;}
    // Queued reports from before the edge observation do not count as a push.
    if(int32_t(received-host.edgeSince)<0)return false;
    int outward=host.sample.edge==1?-int(dx):int(dx);
    if(outward<=0){if(outward<0)_distance=0;return false;}
    _distance+=unsigned(outward);
    if(_distance<pushDistance)return false;
    int destination=-1;
    const int direction=host.sample.edge==1?-1:1;
    for(int slot=int(selected)+direction;slot>=0&&slot<3;slot+=direction){
      if(_ready&(1u<<slot)){destination=slot;break;}
    }
    if(destination<0){_distance=0;return false;}
    target=destination;
    return true;
  }
  int finish(uint32_t now){
    if(target<0||!_allowed)return -1;
    if(!fresh(selected,now)||!_hosts[selected].sample.edge||!(_ready&(1u<<target))){reset(now);return -1;}
    const int result=target;reset(now);return result;
  }
  void packet(unsigned slot,uint8_t out[EdgeProtocol::size])const{
    for(unsigned i=0;i<EdgeProtocol::size;++i)out[i]=0;
    out[0]=1;out[1]=uint8_t(slot);out[2]=uint8_t(selected);
    EdgeProtocol::write32(out+6,epoch);
  }
};
