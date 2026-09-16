#include "DeviceMenu.h"
#include "ShortcutBindings.h"
#include <cassert>
#include <vector>
#include <array>
struct FakeBackend {
  bool online=true,accept=true;unsigned slot=0,revision=0,releases=0,saves=0;
  ShortcutRecorder recorder;ShortcutConfig shortcuts;
  std::string output,name="HID SWITCHER BLE";
  std::string names[3]={"Air","Work","Third"};
  std::vector<std::array<uint8_t,8>> reports;
  bool connected(){return online;}unsigned selected(){return slot;}uint32_t session(){return revision;}
  uint32_t reportSpacing(){return 15;}void release(){++releases;}
  bool send(const uint8_t *report){
    if(!accept)return false;
    std::array<uint8_t,8> r;memcpy(r.data(),report,8);reports.push_back(r);
    if(report[2]==40)output+='\n';else if(report[2]==42){if(!output.empty())output.pop_back();}
    else if(report[2])output+=inputCharacter(report[2],report[0]);return true;
  }
  void select(unsigned value){slot=value;}
  void captureKeyboard(const uint8_t *keys,uint8_t mods){recorder.keyboard(keys,mods);}
  void captureMouse(uint8_t buttons){recorder.mouse(buttons);}
  std::string deviceName(){return name;}
  std::string computerName(unsigned slot){return names[slot];}
  std::string computers(){return "1 Air\n2 Work\n3 Third\n";}
  std::string binding(unsigned action){
    const char *labels[]={"Cycle","Next","Previous","Slot"};
    return std::string(labels[action])+"\nKeyboard: "+(emptyShortcut(shortcuts.bindings[2*action])?"Not set":"Assigned")+(action==3?"":"\nMouse: Not set");
  }
  std::string diagnostics(){return "USB: ready";}
  bool rename(unsigned i,const std::string &value){names[i]=value;++saves;return true;}
  bool move(unsigned from,unsigned to){if(slot==from)slot=to;else if(slot==to)slot=from;++saves;return true;}
  bool renameDevice(const std::string &value){name=value;++saves;return true;}
  bool clearBinding(unsigned action,unsigned kind){auto &b=shortcuts.bindings[shortcutBindingIndex(action,kind)];b={};b.kind=kind;++saves;return true;}
  bool resetBindings(){shortcuts={};++saves;return true;}
  uint32_t now=0;
  bool beginRecord(unsigned action){recorder.begin(action,now,0);return true;}
  int recordState(){return recorder.state;}std::string recordLabel(){return "Test chord";}
  std::string saveRecord(){
    if(recorder.state!=ShortcutRecorder::Ready)return "Not ready";
    auto next=shortcuts;next.bindings[shortcutBindingIndex(recorder.target,recorder.candidate.kind)]=recorder.candidate;
    if(!validShortcuts(next))return "Conflict";
    shortcuts=next;++saves;return "";
  }
  void cancelRecord(){recorder.cancel();}
};
struct Fixture {
  FakeBackend b;DeviceMenu<FakeBackend> menu{b};
  void tick(){b.now+=16;b.recorder.tick(b.now);menu.tick(b.now);}
  void drain(){unsigned n=0;do{tick();assert(++n<2000);}while(menu.busy());}
  void open(){menu.button(true,b.now);b.now+=3000;menu.button(true,b.now);menu.button(false,b.now);drain();assert(menu.active());}
  void raw(uint8_t key,uint8_t mods=0){uint8_t keys[6]={key};assert(menu.keyboard(keys,mods,b.now));}
  void press(char c){const auto key=textKey(c);raw(key.code,key.modifiers);raw(0);drain();}
  void back(){raw(41);if(menu.active()){raw(0);drain();}}
  void escape(){for(unsigned i=0;menu.active();++i){assert(i<6);raw(41);if(menu.active()){raw(0);drain();}}}
};
int main(){
  // All printable ASCII must round-trip; unsupported modifiers are never echoed.
  for(int c=32;c<127;++c){const auto k=textKey(char(c));assert(inputCharacter(k.code,k.modifiers)==c);}
  assert(inputCharacter(4,1)==0);
  TextConsole emitter;std::vector<std::array<uint8_t,8>> packets;
  assert(emitter.append("aa"));
  auto fail=[](const uint8_t *){return false;};emitter.tick(0,15,fail);assert(emitter.busy());
  auto accept=[&](const uint8_t *r){std::array<uint8_t,8> a;memcpy(a.data(),r,8);packets.push_back(a);return true;};
  for(unsigned t=0;t<=45;t+=15)emitter.tick(t,15,accept);
  assert(packets.size()==4&&packets[0][2]==4&&!packets[1][2]&&packets[2][2]==4&&!packets[3][2]);
  assert(!emitter.busy());assert(!emitter.append(std::string(4097,'x')));
  MenuButton button;assert(!button.update(true,false,0xfffffff0));assert(button.update(true,false,3000));assert(!button.update(true,true,3010));
  button.update(false,true,3011);assert(button.update(true,true,3012));
  Fixture f;f.open();assert(f.b.output.find("1 Computers\n2 Shortcuts\n3 Bluetooth name\n4 Diagnostics\nESC Exit")!=std::string::npos);
  Fixture overview;overview.open();overview.press('2');
  assert(overview.b.output.find("1 Cycle\nKeyboard: Assigned\nMouse: Not set")!=std::string::npos);
  assert(overview.b.output.find("2 Next\nKeyboard: Not set\nMouse: Not set")!=std::string::npos);
  assert(overview.b.output.find("3 Previous\nKeyboard: Not set\nMouse: Not set")!=std::string::npos);
  assert(overview.b.output.find("4 Slot\nKeyboard: Assigned")!=std::string::npos);
  overview.back();overview.press('1');overview.press('1');overview.press('2');
  assert(overview.b.output.find("Current computer name: Work")!=std::string::npos);
  overview.press('X');overview.press('\n');overview.press('N');assert(overview.b.saves==0);
  overview.back();overview.press('3');
  assert(overview.b.output.find("Current Bluetooth name: HID SWITCHER BLE")!=std::string::npos);
  overview.press('M');overview.press('y');overview.press('\n');overview.press('Y');assert(overview.b.name=="My");
  overview.press('3');assert(overview.b.output.find("Current Bluetooth name: My")!=std::string::npos);
  assert(overview.b.output.find("0 Back")==std::string::npos);
  assert(overview.b.output.find("ESC Back")!=std::string::npos);
  overview.escape();
  Fixture reset;reset.open();reset.press('2');reset.press('5');
  reset.raw(textKey('n').code,0x20);reset.raw(0);reset.drain();assert(reset.b.saves==0);
  reset.press('5');reset.raw(textKey('y').code,0x20);reset.raw(0);reset.drain();assert(reset.b.saves==1);
  // Rapid name entry must queue echoes rather than drop characters during output.
  f.press('3');for(char c:std::string("Desk")){auto k=textKey(c);f.raw(k.code,k.modifiers);f.raw(0);}f.drain();f.press('\n');
  assert(f.b.name=="HID SWITCHER BLE");f.press('Y');assert(f.b.name=="Desk"&&f.b.saves==1);
  f.press('2');f.press('1');f.press('1');assert(f.b.recorder.state==ShortcutRecorder::Armed);
  f.raw(0,0x81);f.menu.mouse(8,f.b.now);f.menu.mouse(0,f.b.now);f.raw(0);f.drain();
  assert(f.b.saves==1);f.press('Y');assert(f.b.shortcuts.bindings[1].buttons==8&&f.b.shortcuts.bindings[0].keys[0]==43);
  f.press('2');f.press('Y');assert(emptyShortcut(f.b.shortcuts.bindings[0])&&f.b.shortcuts.bindings[1].buttons==8);
  f.press('2');f.press('4');f.press('1');f.raw(0,0x82);f.raw(0);f.drain();f.press('Y');
  assert(f.b.shortcuts.bindings[6].modifiers==10);
  // Select finishes its text on the original host, then exits before changing slots.
  f.back();f.back();f.press('1');f.press('3');f.press('3');assert(!f.menu.active()&&f.b.slot==2);
  f.open();f.b.online=false;f.tick();assert(!f.menu.active());
  f.b.online=true;f.open();++f.b.revision;f.tick();assert(!f.menu.active());
  f.open();f.b.now+=120001;f.menu.tick(f.b.now);assert(!f.menu.active());
  f.open();f.press('2');f.press('1');f.press('1');f.escape();assert(f.b.recorder.state==ShortcutRecorder::Cancelled);
  Fixture back;back.open();back.press('3');back.press('X');back.press('\n');
  back.raw(41);back.raw(0);back.drain();assert(back.menu.active()&&back.b.saves==0);
  back.press('2');back.press('1');back.press('1');assert(back.b.recorder.active());
  back.raw(41);back.raw(0);back.drain();assert(back.menu.active()&&!back.b.recorder.active());
  back.back(); // Still in the action editor after cancelling capture.
  back.back();back.press('3');
  const auto before=back.b.output.size();back.raw(41);back.raw(41);back.raw(0);back.drain();
  assert(back.menu.active()); // Holding Escape must not unwind multiple levels.
  assert(back.b.output.substr(before).find("1 Computers")!=std::string::npos);
  back.raw(41);assert(!back.menu.active());
  Fixture interrupted;interrupted.open();interrupted.press('2');
  const auto first=textKey('1');interrupted.raw(first.code);interrupted.raw(0);assert(interrupted.menu.busy());
  interrupted.raw(41);interrupted.raw(0);interrupted.drain();assert(interrupted.menu.active());
  interrupted.menu.button(true,interrupted.b.now);assert(!interrupted.menu.active());
  Fixture stalled;stalled.b.accept=false;stalled.menu.button(true,0);stalled.menu.button(true,3000);assert(stalled.menu.active());
  stalled.raw(41);assert(!stalled.menu.active()&&!stalled.menu.busy());
  Fixture offline;offline.b.online=false;offline.menu.button(true,0);offline.menu.button(true,3000);assert(!offline.menu.active());
}
