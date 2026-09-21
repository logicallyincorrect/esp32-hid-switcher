#include "ShortcutBindings.h"
#include <cassert>
int main(){
  ShortcutConfig config;assert(validShortcuts(config));
  uint8_t tab[6]={0x2b},empty[6]={},one[6]={0x1e};
  assert(matchingShortcut(config,tab,0x81,0)==Cycle);
  assert(matchingShortcut(config,tab,0x18,0)==Cycle);
  assert(matchingShortcut(config,tab,0x83,0)==-1);
  assert(matchingShortcut(config,tab,0x81,0,2)==-1);
  assert(matchingShortcut(config,empty,0,0)==-1); // Unset bindings never fire.
  for(unsigned i=0;i<3;++i){uint8_t key[6]={uint8_t(0x1e + i)};assert(matchingShortcut(config,key,0x81,0)==int(3+i));}
  config.bindings[1].modifiers=9;config.bindings[1].buttons=8;
  assert(validShortcuts(config));assert(matchingShortcut(config,empty,0x81,8)==Cycle);
  assert(matchingShortcut(config,empty,0x81,24)==-1);
  assert(matchingShortcut(config,one,0x81,8)==3);
  assert(matchingShortcut(config,tab,0x81,0)==Cycle); // Mouse preserves keyboard.
  config.bindings[2]=config.bindings[0];assert(!validShortcuts(config));
  config.bindings[2].keys[1]=0x30;assert(!validShortcuts(config)); // Subset conflict.
  config.bindings[2]=keyboardBinding(one,0x81);assert(!validShortcuts(config)); // Slot reserves all three digits.
  config.bindings[2]={};assert(validShortcuts(config));
  config.bindings[6].kind=2;assert(!validShortcuts(config)); // Slot cannot use mouse.
  config=ShortcutConfig{};config.bindings[6].keys[0]=4;assert(!validShortcuts(config));
  for(unsigned current=0;current<3;++current)for(uint8_t mask=0;mask<8;++mask){
    unsigned expected=current;
    if(mask&(1u<<((current+1)%3)))expected=(current+1)%3;
    else if(mask&(1u<<((current+2)%3)))expected=(current+2)%3;
    assert(shortcutDestination(Cycle,current,mask)==expected);
    assert(shortcutDestination(Next,current,mask)==expected);
    unsigned previous=current;
    if(mask&(1u<<((current+2)%3)))previous=(current+2)%3;
    else if(mask&(1u<<((current+1)%3)))previous=(current+1)%3;
    assert(shortcutDestination(Previous,current,mask)==previous);
    for(int slot=0;slot<3;++slot)assert(shortcutDestination(3+slot,current,mask)==unsigned(slot));
  }
  ShortcutRecorder r;r.begin(Cycle,0,0);
  assert(r.keyboard(empty,0x81));assert(r.state==ShortcutRecorder::Armed);
  assert(r.keyboard(tab,0x81));assert(r.state==ShortcutRecorder::Capturing);
  r.keyboard(tab,0); // Releasing modifiers first retains the captured chord.
  r.keyboard(empty,0);assert(r.state==ShortcutRecorder::Ready);assert(r.candidate.modifiers==9&&r.candidate.keys[0]==0x2b);
  assert(!r.keyboard(one,0));
  r.begin(Next,10,0);r.keyboard(empty,0);r.mouse(8);r.mouse(24);r.mouse(16);r.mouse(0);
  assert(r.state==ShortcutRecorder::Ready&&r.candidate.kind==2&&r.candidate.buttons==24);
  r.mouse(1);r.begin(Cycle,20,0);r.mouse(1);assert(r.state==ShortcutRecorder::Armed);r.mouse(0);r.mouse(4);r.mouse(0);
  assert(r.state==ShortcutRecorder::Ready&&r.candidate.buttons==4);
  r.begin(Cycle,30,0);uint8_t esc[6]={0x29};r.keyboard(esc,0);assert(r.state==ShortcutRecorder::Cancelled);
  r.keyboard(empty,0);r.begin(Cycle,40,0);r.tick(60041);assert(r.state==ShortcutRecorder::Expired);
  r.begin(Cycle,0xfffffff0,0);r.tick(60000);assert(r.state==ShortcutRecorder::Expired);
  r.begin(Cycle,0,0);uint8_t invalid[6]={1};r.keyboard(invalid,0);assert(r.state==ShortcutRecorder::Armed);
  r.cancel();assert(!r.active());r.keyboard(empty,0);
  r.begin(Slot,0,0);r.mouse(8);r.mouse(0);assert(r.state==ShortcutRecorder::Armed);
  r.keyboard(empty,0x01);r.keyboard(empty,0x81);r.keyboard(empty,0x80);r.keyboard(empty,0);
  assert(r.state==ShortcutRecorder::Ready&&r.candidate.kind==1&&r.candidate.modifiers==9&&!r.candidate.keys[0]);
  r.tick(60001);assert(r.state==ShortcutRecorder::Expired); // Confirmation also expires.
  r.keyboard(empty,0);r.mouse(0);r.begin(Cycle,0,0,1);
  r.mouse(8);r.mouse(0);assert(r.state==ShortcutRecorder::Armed);
  r.keyboard(tab,1);r.keyboard(empty,1);assert(r.state==ShortcutRecorder::Capturing);
  r.keyboard(empty,0);assert(r.state==ShortcutRecorder::Ready&&!r.consuming());
  r.begin(Next,0,0,2);r.keyboard(tab,0);r.keyboard(empty,0);assert(r.state==ShortcutRecorder::Armed);
  r.mouse(8);r.mouse(0);assert(r.state==ShortcutRecorder::Ready&&!r.consuming());
}
