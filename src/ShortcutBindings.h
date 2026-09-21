#pragma once
#include <stdint.h>
#include <string.h>

// Modifiers are normalized so either side of Ctrl/Shift/Alt/GUI works.
inline uint8_t shortcutModifiers(uint8_t value) { return (value | (value >> 4)) & 15; }
struct ShortcutBinding {
  uint8_t kind=1, modifiers=0, keys[6]={}, buttons=0; // 1 keyboard, 2 mouse
};
inline ShortcutBinding keyboardBinding(const uint8_t *keys,uint8_t modifiers) {
  ShortcutBinding b;b.modifiers=shortcutModifiers(modifiers);
  unsigned n=0;
  for(unsigned i=0;i<6;++i)if(keys[i])b.keys[n++]=keys[i];
  for(unsigned i=1;i<n;++i)for(unsigned j=i;j>0&&b.keys[j]<b.keys[j-1];--j){auto v=b.keys[j];b.keys[j]=b.keys[j-1];b.keys[j-1]=v;}
  return b;
}
inline bool validShortcut(const ShortcutBinding &b) {
  if(b.modifiers>15)return false;
  if(b.kind==2){for(auto key:b.keys)if(key)return false;return b.buttons!=0;}
  if(b.kind!=1||b.buttons||!b.keys[0])return false;
  uint8_t previous=0;bool ended=false;
  for(auto key:b.keys){if(!key){ended=true;continue;}if(ended||key<4||key>=0xe0||key<=previous)return false;previous=key;}
  return true;
}
inline bool sameShortcut(const ShortcutBinding &a,const ShortcutBinding &b) {
  return a.kind==b.kind&&a.modifiers==b.modifiers&&a.buttons==b.buttons&&!memcmp(a.keys,b.keys,6);
}
inline bool conflictingShortcut(const ShortcutBinding &a,const ShortcutBinding &b) {
  if(a.kind!=b.kind||a.modifiers!=b.modifiers)return false;
  if(a.kind==2)return (a.buttons&b.buttons)==a.buttons||(a.buttons&b.buttons)==b.buttons;
  auto subset=[](const ShortcutBinding &x,const ShortcutBinding &y){for(auto key:x.keys){if(!key)break;bool found=false;for(auto other:y.keys)found|=other==key;if(!found)return false;}return true;};
  return subset(a,b)||subset(b,a);
}
enum ShortcutAction { Cycle=0, Next=1, Previous=2, Slot=3 };
constexpr unsigned ShortcutBindingCount=7;
inline unsigned shortcutBindingIndex(unsigned action,unsigned kind){return 2*action+(kind==2);}
inline bool emptyShortcut(const ShortcutBinding &b){
  if(b.modifiers||b.buttons)return false;
  for(auto key:b.keys)if(key)return false;
  return true;
}
struct ShortcutConfig {
  uint32_t version=1,generation=0;
  // Keyboard/mouse pairs for Cycle, Next, Previous; keyboard modifiers for Slot.
  ShortcutBinding bindings[ShortcutBindingCount];
  ShortcutConfig(){
    for(unsigned i=0;i<ShortcutBindingCount;++i)bindings[i].kind=i%2?2:1;
    bindings[0].modifiers=9;bindings[0].keys[0]=0x2b;
    bindings[6].modifiers=9;
  }
};
inline bool validShortcuts(const ShortcutConfig &config) {
  if(config.version!=1)return false;
  for(unsigned i=0;i<ShortcutBindingCount;++i){const auto &b=config.bindings[i];
    if(b.kind!=(i%2?2:1))return false;
    if(emptyShortcut(b))continue;
    if(i==6){
      if(!b.modifiers||b.modifiers>15||b.buttons)return false;
      for(auto key:b.keys)if(key)return false;
      // A Slot modifier chord reserves each of its three numbered variants.
      for(uint8_t digit=0x1e;digit<=0x20;++digit){
        auto numbered=b;numbered.keys[0]=digit;
        for(unsigned j=0;j<6;++j)if(validShortcut(config.bindings[j])&&conflictingShortcut(numbered,config.bindings[j]))return false;
      }
    }else{
      if(!validShortcut(b))return false;
      for(unsigned j=0;j<i;++j)if(validShortcut(config.bindings[j])&&conflictingShortcut(b,config.bindings[j]))return false;
    }
  }
  return true;
}
// 0..2 mean Cycle/Next/Previous; 3..5 mean direct destination slots 0..2.
inline int matchingShortcut(const ShortcutConfig &config,const uint8_t *keys,uint8_t modifiers,uint8_t buttons,uint8_t kind=0) {
  const auto keyboard=keyboardBinding(keys,modifiers);
  for(unsigned i=0;i<6;++i){const auto &b=config.bindings[i];if(emptyShortcut(b)||(kind&&b.kind!=kind))continue;
    if(b.kind==1&&sameShortcut(b,keyboard))return int(i/2);
    if(b.kind==2&&b.buttons&&b.modifiers==shortcutModifiers(modifiers)&&b.buttons==buttons&&!keyboard.keys[0])return int(i/2);
  }
  const auto &slot=config.bindings[6];
  if(kind!=2&&slot.modifiers&&slot.modifiers==keyboard.modifiers&&keyboard.keys[0]>=0x1e&&keyboard.keys[0]<=0x20&&!keyboard.keys[1])return 3+keyboard.keys[0]-0x1e;
  return -1;
}
inline unsigned shortcutDestination(int action,unsigned current,uint8_t connectedMask){
  if(action>=3&&action<=5)return unsigned(action-3);
  if(action==Cycle||action==Next||action==Previous){
    for(unsigned step=1;step<3;++step){
      const unsigned candidate=(current+(action==Previous?3-step:step))%3;
      if(connectedMask&(1u<<candidate))return candidate;
    }
  }
  return current;
}
class ShortcutRecorder {
public:
  enum State { Idle, Armed, Capturing, Ready, Cancelled, Expired };
  State state=Idle;ShortcutBinding candidate;unsigned target=0,kind=0;uint32_t started=0,generation=0;
  uint8_t keys[6]={},modifiers=0,buttons=0;
  bool active() const {return state==Armed||state==Capturing||state==Ready;}
  bool consuming() const {return state==Armed||state==Capturing;}
  void begin(unsigned action,uint32_t now,uint32_t revision,unsigned inputKind=0){kind=inputKind;target=action;started=now;generation=revision;candidate={};state=Armed;_released=!modifiers&&!buttons;for(auto key:keys)if(key)_released=false;}
  void cancel(){state=Cancelled;}
  void tick(uint32_t now){if(active()&&uint32_t(now-started)>60000)state=Expired;}
  bool keyboard(const uint8_t *input,uint8_t mods){memcpy(keys,input,6);modifiers=mods;return observe();}
  bool mouse(uint8_t value){buttons=value;return observe();}
private:
  bool _released=false;
  static unsigned keyCount(const ShortcutBinding &b){unsigned n=0;for(auto k:b.keys)if(k)++n;return n;}
  bool observe(){
    if(!consuming())return false;
    auto keyboard=keyboardBinding(keys,modifiers);
    if(!modifiers&&!buttons&&!keyboard.keys[0])_released=true;
    if(!_released)return true;
    if(!modifiers&&!buttons&&keyboard.keys[0]==0x29&&!keyboard.keys[1]){state=Cancelled;return true;}
    if(target==Slot){
      // Record only modifiers; actual slot numbers are supplied when switching.
      // Ordinary keys and mouse buttons do not create a Slot binding.
      if(buttons||keyboard.keys[0])return true;
      if(state==Armed&&modifiers){candidate={};candidate.modifiers=shortcutModifiers(modifiers);state=Capturing;}
      else if(state==Capturing){
        if(!modifiers)state=Ready;
        else if((keyboard.modifiers|candidate.modifiers)==keyboard.modifiers)candidate.modifiers=keyboard.modifiers;
      }
      return true;
    }
    if(state==Armed&&!keyboard.keys[0]&&!buttons)return true;
    if(state==Capturing){
      if(candidate.kind==1&&!keyboard.keys[0]&&!modifiers){state=Ready;return true;}
      if(candidate.kind==2&&!buttons){state=Ready;return true;}
    }
    if(kind!=1&&buttons&&!keyboard.keys[0]){
      ShortcutBinding mouse;mouse.kind=2;mouse.modifiers=shortcutModifiers(modifiers);mouse.buttons=buttons;
      if(state==Armed||(candidate.kind==2&&(buttons|candidate.buttons)==buttons&&(mouse.modifiers|candidate.modifiers)==mouse.modifiers)){candidate=mouse;state=Capturing;}
    }else if(kind!=2&&!buttons&&validShortcut(keyboard)){
      if(state==Armed||(candidate.kind==1&&(keyCount(keyboard)>keyCount(candidate)||(keyCount(keyboard)==keyCount(candidate)&&!memcmp(keyboard.keys,candidate.keys,6)&&(keyboard.modifiers|candidate.modifiers)==keyboard.modifiers)))){candidate=keyboard;state=Capturing;}
    }
    return true;
  }
};
