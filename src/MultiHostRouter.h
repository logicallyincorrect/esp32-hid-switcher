#pragma once
#include <stdint.h>
#include <string.h>

// Main-loop owned. Every report goes to one explicit connection, never broadcast.
class MultiHostRouter {
public:
  static constexpr uint16_t NONE = 0xffff;
  using Sender = bool (*)(void *, uint16_t, const uint8_t *, uint8_t, size_t);
  struct Link { uint16_t handle = NONE; bool ready = false; uint8_t release = 3; uint8_t subscriptions = 0; };
  MultiHostRouter(Sender sender, void *context) : _sender(sender), _context(context) {}
  void connect(unsigned slot, uint16_t handle) {
    if (slot < 3) _links[slot] = {handle, false, 3, 0};
  }
  void disconnect(uint16_t handle) {
    for (auto &link : _links) if (link.handle == handle) link = {};
  }
  void ready(unsigned slot, bool value, uint8_t subscriptions = 1) {
    if (slot < 3) {
      if (_links[slot].ready != value || _links[slot].subscriptions != subscriptions) _links[slot].release = 3;
      _links[slot].ready = value;
      _links[slot].subscriptions = subscriptions;
    }
  }
  void select(unsigned slot) {
    if (slot >= 3 || slot == _selected) return;
    _links[_selected].release = 3;
    _links[slot].release = 3;
    _selected = slot;
    service();
  }
  void releaseAll() {
    for (auto &link : _links) link.release = 3;
    service();
  }
  void service() {
    const uint8_t empty[8] = {};
    for (auto &link : _links) if (link.handle != NONE && link.ready) {
      for (uint8_t id=1;id<=2;++id) {
        const uint8_t bit=1u<<(id-1);
        if ((link.release & bit) && (link.subscriptions & bit))
          if (_sender(_context,link.handle,empty,id,id==1?8:7)) link.release &= ~bit;
      }
    }
  }
  void send(const uint8_t *report, uint8_t id=1, size_t length=8) {
    if(id<1||id>2)return;
    service();
    auto &link = _links[_selected];
    const uint8_t bit=1u<<(id-1);
    if(link.handle!=NONE && link.ready && (link.subscriptions & bit) && !(link.release & bit))
      if(!_sender(_context,link.handle,report,id,length))link.release |= bit;
  }
  bool tryKeyboard(const uint8_t *report) {
    auto &link=_links[_selected];
    return link.handle!=NONE && link.ready && (link.subscriptions&1) && !(link.release&1)
      && _sender(_context,link.handle,report,1,8);
  }
  bool tryMouse(const uint8_t *report) {
    auto &link=_links[_selected];
    return link.handle!=NONE && link.ready && (link.subscriptions&2) && !(link.release&2)
      && _sender(_context,link.handle,report,2,7);
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
};
