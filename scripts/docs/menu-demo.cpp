#include <fstream>
#include <filesystem>
#include "DeviceMenu.h"
#include "ShortcutBindings.h"
#include <cassert>
#include <vector>
#include <array>
struct DemoBackend {
  bool online=true,accept=true;unsigned slot=0,revision=0,releases=0,saves=0;
  ShortcutRecorder recorder;ShortcutConfig shortcuts;
  std::string output,name="HID SWITCHER BLE";
  std::string names[3]={"Personal Mac","Work Mac","Computer 3"};
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
  std::string computers(){return "1 "+names[0]+" [connected] *\n2 "+names[1]+" [connected]\n3 "+names[2]+" [unpaired]\n";}
  std::string binding(unsigned action){
    const char *labels[]={"Cycle","Next","Previous","Slot"};
    auto keyboard=emptyShortcut(shortcuts.bindings[2*action])?"Not set":action==3?"Ctrl + Cmd + 1/2/3":"Ctrl + Cmd + Tab";
    auto mouse=action==3?"":emptyShortcut(shortcuts.bindings[2*action+1])?"\nMouse: Not set":"\nMouse: Button 4";
    return std::string(labels[action])+"\nKeyboard: "+keyboard+mouse;
  }
  std::string diagnostics(){return "USB: ready";}
  bool rename(unsigned i,const std::string &value){names[i]=value;++saves;return true;}
  bool move(unsigned from,unsigned to){if(slot==from)slot=to;else if(slot==to)slot=from;++saves;return true;}
  bool renameDevice(const std::string &value){name=value;++saves;return true;}
  bool clearBinding(unsigned action,unsigned kind){auto &b=shortcuts.bindings[shortcutBindingIndex(action,kind)];b={};b.kind=kind;++saves;return true;}
  bool resetBindings(){shortcuts={};++saves;return true;}
  bool pairs[3]={true,true,false};unsigned forgotten=99;
  bool paired(unsigned i){return pairs[i];}
  std::string forget(unsigned i){forgotten=i;pairs[i]=false;if(i==slot){online=false;++revision;}return "";}
  uint32_t now=0;
  bool beginRecord(unsigned action){recorder.begin(action,now,0);return true;}
  int recordState(){return recorder.state;}std::string recordLabel(){return "Button 4";}
  std::string saveRecord(){
    if(recorder.state!=ShortcutRecorder::Ready)return "Not ready";
    auto next=shortcuts;next.bindings[shortcutBindingIndex(recorder.target,recorder.candidate.kind)]=recorder.candidate;
    if(!validShortcuts(next))return "Conflict";
    shortcuts=next;++saves;return "";
  }
  void cancelRecord(){recorder.cancel();}
};
struct Fixture {
  DemoBackend b;DeviceMenu<DemoBackend> menu{b};
  void tick(){b.now+=16;b.recorder.tick(b.now);menu.tick(b.now);}
  void drain(){unsigned n=0;do{tick();assert(++n<2000);}while(menu.busy());}
  void open(){menu.button(true,b.now);b.now+=3000;menu.button(true,b.now);menu.button(false,b.now);drain();assert(menu.active());}
  void raw(uint8_t key,uint8_t mods=0){uint8_t keys[6]={key};assert(menu.keyboard(keys,mods,b.now));}
  void press(char c){const auto key=textKey(c);raw(key.code,key.modifiers);raw(0);drain();}
  void back(){raw(41);if(menu.active()){raw(0);drain();}}
  void escape(){for(unsigned i=0;menu.active();++i){assert(i<6);raw(41);if(menu.active()){raw(0);drain();}}}
};

// Export actual DeviceMenu output. The backend simulates settings and connections.
int main(int argc,char **argv){
  assert(argc==2);std::filesystem::path dir(argv[1]);std::filesystem::create_directories(dir);
  auto save=[&](const char *file,const Fixture &f){std::ofstream(dir/file)<<f.b.output;};
  Fixture name;name.open();save("name-01.txt",name);
  name.press('1');save("name-02.txt",name);
  name.press('1');name.press('2');save("name-03.txt",name);
  for(char c:std::string("Studio Mac"))name.press(c);
  save("name-04.txt",name);name.raw(40);name.raw(0);name.drain();save("name-05.txt",name);
  name.press('Y');name.press('1');save("name-06.txt",name);
  assert(name.b.names[1]=="Studio Mac");
  name.back();save("name-07.txt",name);
  name.back();assert(!name.menu.active());
}
