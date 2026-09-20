#pragma once
#include <stdint.h>
#include <string.h>

// Main-loop owned. Every report goes to one explicit connection, never broadcast.
class MultiHostRouter {
public:
  static constexpr uint16_t NONE = 0xffff;
  using Sender = bool (*)(void *, uint16_t, const uint8_t *, uint8_t, size_t);
  struct Link { uint16_t handle = NONE; bool ready = false; uint8_t release = 7; uint8_t subscriptions = 0; uint8_t position[4]={0,64,0,64}; uint8_t absoluteButtons=0; };
  MultiHostRouter(Sender sender, void *context, bool absolute=false) : _sender(sender), _context(context), _absolute(absolute), _absoluteOutput(absolute) {}
  void absoluteOutput(bool enabled){_absoluteOutput=enabled;}
  void connect(unsigned slot, uint16_t handle) {
    if (slot < 3) _links[slot] = {handle, false, 7, 0};
  }
  void disconnect(uint16_t handle) {
    for (auto &link : _links) if (link.handle == handle) link = {};
  }
  void ready(unsigned slot, bool value, uint8_t subscriptions = 1) {
    if (slot < 3) {
      if (_links[slot].ready != value || _links[slot].subscriptions != subscriptions) _links[slot].release = 7;
      _links[slot].ready = value;
      _links[slot].subscriptions = subscriptions;
    }
  }
  void select(unsigned slot) {
    if (slot >= 3 || slot == _selected) return;
    _links[_selected].release = 7;
    _links[slot].release = 7;
    _selected = slot;
    service();
  }
  void releaseAll() {
    for (auto &link : _links) link.release = 7;
    service();
  }
  void service() {
    for (auto &link : _links) if (link.handle != NONE && link.ready) {
      for (uint8_t id=1;id<=3;++id) {
        const uint8_t bit=1u<<(id-1);
        if ((link.release & bit) && (link.subscriptions & bit)) {
          if(id==2&&_absolute&&!_absoluteOutput&&!link.absoluteButtons){link.release&=~bit;continue;}
          uint8_t neutral[8]={};
          if(_absolute&&id==2)memcpy(neutral+1,link.position,4);
          if (_sender(_context,link.handle,neutral,id,id==1?8:7)){link.release &= ~bit;if(id==2)link.absoluteButtons=0;}
        }
      }
    }
  }
  void send(const uint8_t *report, uint8_t id=1, size_t length=8) {
    if(id<1||id>2)return;
    service();
    auto &link = _links[_selected];
    const uint8_t bit=1u<<(id-1);
    if(link.handle!=NONE && link.ready && (link.subscriptions & bit) && !(link.release & bit)) {
        if(!_sender(_context,link.handle,report,id,length))link.release |= bit;
        else if(_absolute&&id==2&&length==7){memcpy(link.position,report+1,4);link.absoluteButtons=report[0];}
      }
  }
  bool tryKeyboard(const uint8_t *report) {
    auto &link=_links[_selected];
    return link.handle!=NONE && link.ready && (link.subscriptions&1) && !(link.release&1)
      && _sender(_context,link.handle,report,1,8);
  }
  bool tryMouse(const uint8_t *report) {
    auto &link=_links[_selected];
    const bool sent=link.handle!=NONE && link.ready && (link.subscriptions&2) && !(link.release&2)
      && _sender(_context,link.handle,report,2,7);
    if(sent&&_absolute){memcpy(link.position,report+1,4);link.absoluteButtons=report[0];}
    return sent;
  }
  bool tryRelativeMouse(const uint8_t *report) {
    auto &link=_links[_selected];
    return link.handle!=NONE&&link.ready&&(link.subscriptions&4)&&!(link.release&4)
      &&_sender(_context,link.handle,report,3,7);
  }
  void reorder(const uint8_t order[3]) {
    Link copy[3]; memcpy(copy,_links,sizeof(copy));
    const unsigned selected=_selected;
    releaseAll();
    // Copy after releases so already-sent releases are not repeated unnecessarily.
    memcpy(copy,_links,sizeof(copy));
    for(unsigned i=0;i<3;++i) { _links[i]=copy[order[i]]; if(order[i]==selected)_selected=i; }
  }
  bool connected(unsigned slot) const {
    return slot < 3 && _links[slot].handle != NONE && _links[slot].ready && (_links[slot].subscriptions & 1);
  }
  unsigned selected() const { return _selected; }
private:
  Link _links[3];
  unsigned _selected = 0;
  Sender _sender;
  void *_context;
  bool _absolute,_absoluteOutput;
};
