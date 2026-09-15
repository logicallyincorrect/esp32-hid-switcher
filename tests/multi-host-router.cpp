#include "MultiHostRouter.h"
#include <cassert>
#include <vector>
#include <array>
struct Sent { uint16_t handle; std::array<uint8_t,8> report; uint8_t id; };
struct Transport {
  std::vector<Sent> sent;
  uint16_t fail = 0xffff;
  static bool send(void *context, uint16_t handle, const uint8_t *data, uint8_t id, size_t length) {
    auto &t = *static_cast<Transport *>(context);
    if (handle == t.fail) return false;
    Sent s{handle,{},id}; memcpy(s.report.data(),data,length); t.sent.push_back(s); return true;
  }
};
int main() {
  Transport t; MultiHostRouter r(Transport::send,&t);
  uint8_t key[8]={0x80,0,4};
  r.send(key); assert(t.sent.empty());
  for (int i=0;i<3;++i) { r.connect(i,10+i); r.ready(i,true); }
  r.service(); assert(t.sent.size()==3);
  for (const auto &s:t.sent) for (auto b:s.report) assert(b==0);
  t.sent.clear(); r.send(key); assert(t.sent.size()==1 && t.sent.back().handle==10);
  t.sent.clear(); r.select(1);
  assert(t.sent.size()==2 && t.sent[0].handle==10 && t.sent[1].handle==11);
  for (const auto &s:t.sent) for (auto b:s.report) assert(b==0);
  t.sent.clear(); r.send(key); assert(t.sent.size()==1 && t.sent.back().handle==11);
  t.sent.clear(); r.select(1); assert(t.sent.empty());
  r.disconnect(11); r.send(key); assert(t.sent.empty());
  assert(r.connected(0) && r.connected(2) && !r.connected(1));
  r.connect(1,99); r.send(key); assert(t.sent.empty()); // not authenticated/subscribed
  r.ready(1,true); r.service(); assert(t.sent.size()==1 && t.sent[0].handle==99 && t.sent[0].report[2]==0);
  t.sent.clear(); r.send(key); assert(t.sent.size()==1 && t.sent[0].handle==99);
  // Notification failure blocks further input until an empty report is accepted.
  t.fail=99; t.sent.clear(); r.send(key); r.send(key); assert(t.sent.empty());
  t.fail=0xffff; r.service(); assert(t.sent.size()==1 && t.sent[0].report[2]==0);
  // Release to the old host is retried even after switching to another host.
  t.fail=99; r.select(2); t.sent.clear(); r.send(key);
  assert(t.sent.size()==1 && t.sent[0].handle==12);
  t.fail=0xffff; t.sent.clear(); r.service(); assert(t.sent.size()==1 && t.sent[0].handle==99 && t.sent[0].report[2]==0);
  // Loss of notification subscription prevents sending until ready again.
  r.ready(2,false); t.sent.clear(); r.send(key); assert(t.sent.empty());
  r.ready(2,true); r.service(); assert(t.sent.size()==1 && t.sent[0].report[2]==0);
  t.sent.clear(); r.select(3); assert(r.selected()==2 && t.sent.empty());
  r.releaseAll(); assert(t.sent.size()==3);
  // Mouse backpressure retries without injecting a false button release.
  Transport mouseTransport;MultiHostRouter mouseRouter(Transport::send,&mouseTransport);
  mouseRouter.connect(0,42);mouseRouter.ready(0,true,3);mouseRouter.service();
  mouseTransport.sent.clear();uint8_t mouse[7]={1,5,0,0,0,0,0};
  mouseTransport.fail=42;assert(!mouseRouter.tryMouse(mouse));
  mouseTransport.fail=0xffff;assert(mouseRouter.tryMouse(mouse));
  assert(mouseTransport.sent.size()==1 && mouseTransport.sent[0].report[0]==1);
  mouseRouter.select(1);assert(!mouseRouter.tryMouse(mouse));
  Transport keyTransport;MultiHostRouter keyRouter(Transport::send,&keyTransport);
  keyRouter.connect(0,70);keyRouter.ready(0,true,3);keyRouter.service();keyTransport.sent.clear();
  keyTransport.fail=70;assert(!keyRouter.tryKeyboard(key));
  keyTransport.fail=0xffff;assert(keyRouter.tryKeyboard(key));
  assert(keyTransport.sent.size()==1&&keyTransport.sent[0].handle==70&&keyTransport.sent[0].report[2]==4);
  keyRouter.select(1);assert(!keyRouter.tryKeyboard(key));
}
