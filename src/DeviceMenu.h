#pragma once
#include "TextConsole.h"
#include "EdgeSettings.h"
#include "PointerTuning.h"
#include <string.h>

// Platform-independent state machine. Backend owns settings and BLE transport.
template<class Backend> class DeviceMenu {
  enum Page { Main,Computers,RenameSlot,RenameText,RenameConfirm,MoveFrom,MoveTo,MoveConfirm,
              SelectSlot,SelectDrain,ForgetSlot,ForgetConfirm,ForgetDrain,Actions,Binding,RecordArming,Recording,RecordConfirm,
              ClearConfirm,ResetConfirm,NameText,NameConfirm,EdgeText,EdgeConfirm,PointerSlot,PointerField,PointerText,PointerConfirm,CalibrationSlots,CalibrationConfirm,CalibrationDrain,Seamless,BleInputs,BleDevices,BleWait,BleForget };
  Backend &_backend;
  TextConsole _output;
  MenuButton _button;
  Page _page=Main;
  bool _enableAfterCalibration=false,_bleScan=false;
  uint32_t _bleRevision=0;
  uint32_t _bleConfirmId=0,_bleConfirmCode=0;
  unsigned _blePage=0;
  bool _active=false,_released=false,_textReady=false;
  uint8_t _previous[6]={},_modifiers=0,_buttons=0;
  unsigned _host=0,_slot=0,_destination=0,_action=0,_kind=0,_edgeDistance=0;
  uint32_t _session=0,_activity=0;
  std::string _name;
  void print(const std::string &text){if(!_output.append(text))close();}
  void prompt(const std::string &text){_textReady=false;print("\n"+text+(_page==Main?"":"\nESC Back")+"\n> ");}
  void main(){_page=Main;prompt(std::string("1 Computers\n2 Shortcuts\n3 Bluetooth name\n4 Diagnostics\n5 Edge switching")+"\n6 Seamless switching"+std::string(_backend.absoluteMode()?"\n7 Pointer tuning":"")+std::string(_backend.bleInputAvailable()?"\n8 BLE input devices":"")+"\nESC Exit");}
  void bleInputs(){_page=BleInputs;prompt("BLE input devices\n"+_backend.bleInputStatus()+"\n1 Scan for keyboards\n2 Reconnect saved device\n3 Disconnect input\n4 Forget input pairing\n5 Scan all BLE devices");}
  void bleDevices(){
    _page=BleDevices;const auto devices=_backend.bleInputDevices();std::string list;
    size_t start=0;unsigned index=0;
    while(start<devices.size()){
      const auto end=devices.find('\n',start);const auto line=devices.substr(start,end==std::string::npos?end:end-start);
      if(index>=_blePage*9&&index<(_blePage+1)*9){const auto space=line.find(' ');if(space!=std::string::npos)list+=std::to_string(index%9+1)+line.substr(space)+"\n";}
      ++index;if(end==std::string::npos)break;start=end+1;
    }
    if((_blePage+1)*9<_backend.bleInputCount())list+="n Next page\n";
    if(_blePage)list+="p Previous page\n";
    prompt(_backend.bleInputStatus()+"\nChoose your device:\n"+list);
  }
  void bleRequest(const std::string &command,bool scan=false){
    if(!_backend.bleInputCommand(command)){prompt("Input device operation is busy or unavailable.");return;}
    _bleScan=scan;_blePage=0;_bleConfirmCode=0;_bleRevision=_backend.bleInputRevision();_page=BleWait;
    prompt(scan?"Scanning nearby Bluetooth keyboards and pointers. No device is added until you choose it.":"Working. Keep this document focused for pairing instructions. Use a USB keyboard until pairing finishes.");
  }
  const char *pointerLabel()const{return "Shared sensitivity";}
  void pointerFields(){_page=PointerText;_kind=0;_name.clear();prompt("Shared sensitivity percentage (1-1600). Current: "+std::to_string(_backend.sharedSpeed())+"%. 100% = 1 desktop pixel per mouse count. Enter to review.");}
  void seamless(){_page=Seamless;prompt(std::string("Seamless switching: ")+(_backend.seamlessEnabled()?"ON":"OFF")+"\n1 Enable\n2 Disable"+(_backend.absoluteMode()?"\n3 Optional sensitivity calibration":""));}
  void calibrationInstructions(){_page=CalibrationConfirm;prompt("For each selected computer:\nMove to TOP LEFT, left-click and release.\nWait for TWO cyan LED flashes.\nMove smoothly to BOTTOM RIGHT, left-click and release.\nAvoid pushing beyond the edges.\nOne cyan flash = top left. Two = bottom right.\nAfter each slot, results are typed here before selecting the next slot.\nGreen = saved; amber = cancelled; red = failed. ESC or BOOT cancels.\nClicks stay local. Wheel is disabled during calibration.\nStart? y Yes / n No");}
  void calibrationSlots(){_enableAfterCalibration=false;_page=CalibrationSlots;std::string text="Calibrate pointer:\n";const unsigned ready=_backend.calibrationReady();
    for(unsigned i=0;i<3;++i)text+=std::to_string(i+1)+" "+_backend.computerName(i)+((ready&(1u<<i))?" [ready]\n":" [unavailable]\n");
    prompt(text+"4 All ready computers\nUnavailable: connect first. If connected, re-pair to enable calibration.");}
  void computers(){_page=Computers;prompt(_backend.computers()+"1 Rename\n2 Move\n3 Select\n4 Forget pairing");}
  void actions(){
    _page=Actions;std::string text;
    for(unsigned i=0;i<4;++i)text+=std::to_string(i+1)+" "+_backend.binding(i)+"\n\n";
    prompt(text+"5 Restore defaults");
  }
  void binding(){_page=Binding;prompt(_backend.binding(_action)+"\n1 Record\n2 Clear keyboard"+(_action==3?std::string():"\n3 Clear mouse")+"");}
  void done(bool ok){prompt(ok?"Saved.":"Could not save. Settings unchanged.");main();}
  void close(){if(_page==BleWait)_backend.cancelBleInput();_backend.cancelRecord();_backend.release();_output.clear();_active=false;}
  void back(){
    if(_page==BleWait)_backend.cancelBleInput();
    _backend.cancelRecord();_backend.release();_output.clear();_name.clear();_textReady=false;
    switch(_page){
    case Main:close();break;
    case BleInputs:main();break;
    case BleDevices:case BleWait:case BleForget:bleInputs();break;
    case Computers:case Actions:case NameText:case NameConfirm:case EdgeText:case EdgeConfirm:case PointerSlot:case Seamless:main();break;
    case CalibrationSlots:seamless();break;
    case CalibrationConfirm:case CalibrationDrain:calibrationSlots();break;
    case PointerField:_page=PointerSlot;prompt("Tune which paired computer?\n"+_backend.computers());break;
    case PointerText:case PointerConfirm:main();break;
    case RenameSlot:computers();break;
    case RenameText:case RenameConfirm:_page=RenameSlot;prompt("Rename slot:\n"+_backend.computers());break;
    case MoveFrom:case SelectSlot:case SelectDrain:case ForgetSlot:computers();break;
    case ForgetConfirm:case ForgetDrain:_page=ForgetSlot;prompt("Forget pairing for slot:\n"+_backend.computers());break;
    case MoveTo:case MoveConfirm:_page=MoveFrom;prompt("Move from slot:\n"+_backend.computers());break;
    case Binding:case ResetConfirm:actions();break;
    case RecordArming:case Recording:case RecordConfirm:case ClearConfirm:binding();break;
    }
  }
  static unsigned number(char c){return unsigned(c-'1');}
  void key(uint8_t code,uint8_t mods){
    char c=inputCharacter(code,mods);
    if(_page==RenameText||_page==NameText||_page==EdgeText||_page==PointerText){
      if(code==42&&mods==0){if(!_name.empty()){_name.pop_back();print("\b");}return;}
      if(code==40&&mods==0){
        if(_page==PointerText){
          if(!PointerTuning::parse(_name,_kind,_edgeDistance)){_name.clear();prompt("Enter a whole percentage from 1 to 1600.");return;}
          _page=PointerConfirm;prompt(std::string("Save ")+pointerLabel()+": "+std::to_string(_edgeDistance)+"%?\ny Yes\nn No");return;
        }
        if(_page==EdgeText){
          if(!EdgeSettings::parse(_name,_edgeDistance)){_name.clear();prompt("Enter a whole number from 1 to 10000.");return;}
          _page=EdgeConfirm;prompt("Save edge distance: "+std::to_string(_edgeDistance)+" counts?\ny Yes\nn No");return;
        }
        if(_name.empty()){prompt("Enter a name.");return;}
        _page=_page==RenameText?RenameConfirm:NameConfirm;prompt("Save name: "+_name+"?\ny Yes\nn No");return;
      }
      const size_t limit=_page==EdgeText?32:(_page==NameText?29:32);
      if(c&&_name.size()<limit){_name+=c;print(std::string(1,c));}
      return;
    }
    if(c>='A'&&c<='Z')c=char(c-'A'+'a');
    switch(_page){
    case Main:
      if(c=='1')computers();else if(c=='2')actions();
      else if(c=='3'){_name.clear();_page=NameText;prompt("Current Bluetooth name: "+_backend.deviceName()+"\nNew name (ASCII, 29 characters). Enter to review.");}
      else if(c=='4'){prompt(_backend.diagnostics());main();}
      else if(c=='5'){_name.clear();_page=EdgeText;prompt("Current edge distance: "+std::to_string(_backend.edgeThreshold())+" counts\nNew distance (1-10000). Higher needs more mouse movement.\nEnter to review.");}
      else if(c=='6')seamless();
      else if(c=='7'&&_backend.absoluteMode())pointerFields();
      else if(c=='8'&&_backend.bleInputAvailable())bleInputs();break;
    case BleInputs:
      if(c=='1')bleRequest("scan",true);
      else if(c=='2')bleRequest("reconnect");
      else if(c=='3')bleRequest("disconnect");
      else if(c=='4'){_page=BleForget;prompt("Forget the saved wireless input pairing? Computer pairings stay unchanged.\ny Yes / n No");}
      else if(c=='5')bleRequest("scan all",true);break;
    case BleDevices:
      if(c>='1'&&c<='9'&&_blePage*9+unsigned(c-'0')<=_backend.bleInputCount())bleRequest("connect "+std::to_string(_blePage*9+unsigned(c-'0')));
      else if(c=='n'&&(_blePage+1)*9<_backend.bleInputCount()){++_blePage;bleDevices();}
      else if(c=='p'&&_blePage){--_blePage;bleDevices();}break;
    case BleForget:
      if(c=='y')bleRequest("forget");else if(c=='n')bleInputs();break;
    case BleWait:
      if(_bleConfirmCode&&c=='y'){
        const bool ok=_backend.bleConfirm(_bleConfirmId,_bleConfirmCode-1);_bleConfirmCode=0;
        prompt(ok?"Confirmed. Waiting for the input device...":"Pairing prompt expired or changed. Wait for a new prompt or ESC to cancel.");
      }else if(_bleConfirmCode&&c=='n')back();
      break;
    case Seamless:
      if(c=='1')done(_backend.setSeamlessEnabled(true));
      else if(c=='2')done(_backend.setSeamlessEnabled(false));
      else if(c=='3'&&_backend.absoluteMode())calibrationSlots();break;
    case CalibrationSlots:
      if(c>='1'&&c<='4'){
        const unsigned ready=_backend.calibrationReady();_destination=c=='4'?ready:(ready&(1u<<number(c)));
        if(!_destination){prompt("No calibration-ready computer selected.");calibrationSlots();break;}
        calibrationInstructions();
      }break;
    case CalibrationConfirm:
      if(c=='y'){_page=CalibrationDrain;prompt("Starting when this message finishes. Keep this document focused. Results return here after each slot and at completion.");}
      else if(c=='n')calibrationSlots();break;
    case PointerSlot:
      if(c>='1'&&c<='3'){_slot=number(c);if(!_backend.paired(_slot)){prompt("Pair that computer first.");break;}pointerFields();}break;
    case PointerField:
      if(c=='1'||c=='2'){_kind=number(c);_name.clear();_page=PointerText;prompt(std::string(pointerLabel())+" percentage (1-1600). Current: "+std::to_string(_backend.pointerValue(_slot,_kind))+"%. Enter to review.");}break;
    case PointerConfirm:
      if(c=='y')done(_backend.setSharedSpeed(_edgeDistance));else if(c=='n')pointerFields();break;
    case Computers:
      if(c=='1'){_page=RenameSlot;prompt("Rename slot:\n"+_backend.computers());}
      else if(c=='2'){_page=MoveFrom;prompt("Move from slot:\n"+_backend.computers());}
      else if(c=='3'){_page=SelectSlot;prompt("Select a slot. This closes the menu.\n"+_backend.computers());}
      else if(c=='4'){_page=ForgetSlot;prompt("Forget pairing for slot:\n"+_backend.computers());}
      break;
    case ForgetSlot:
      if(c>='1'&&c<='3'){
        _slot=number(c);
        if(!_backend.paired(_slot)){prompt("This slot is already unpaired.");computers();break;}
        _page=ForgetConfirm;prompt("Forget slot "+std::to_string(_slot+1)+": "+_backend.computerName(_slot)+"?\nThis disconnects that computer. Its name and shortcuts stay saved.\ny Yes\nn No");
      }break;
    case ForgetConfirm:
      if(c=='y'){_page=ForgetDrain;prompt("Removing pairing. Also forget this device in that computer's Bluetooth settings before pairing again.");}
      else if(c=='n')computers();break;
    case RenameSlot:
      if(c>='1'&&c<='3'){_slot=number(c);_name.clear();_page=RenameText;prompt("Current computer name: "+_backend.computerName(_slot)+"\nNew name (ASCII, 32 characters). Enter to review.");}break;
    case MoveFrom:
      if(c>='1'&&c<='3'){_slot=number(c);_page=MoveTo;prompt("Move to slot:\n"+_backend.computers());}break;
    case MoveTo:
      if(c>='1'&&c<='3'){_destination=number(c);_page=MoveConfirm;prompt("Move slot "+std::to_string(_slot+1)+" to "+std::to_string(_destination+1)+"?\ny Yes\nn No");}break;
    case MoveConfirm:
      if(c=='y'){const bool ok=_backend.move(_slot,_destination);_host=_backend.selected();done(ok);}
      else if(c=='n')computers();break;
    case RenameConfirm:
      if(c=='y')done(_backend.rename(_slot,_name));else if(c=='n')computers();break;
    case EdgeConfirm:
      if(c=='y')done(_backend.setEdgeThreshold(_edgeDistance));else if(c=='n')main();break;
    case NameConfirm:
      if(c=='y')done(_backend.renameDevice(_name));else if(c=='n')main();break;
    case SelectSlot:
      if(c>='1'&&c<='3'){_destination=number(c);_page=SelectDrain;prompt("Menu closed. Selecting slot "+std::to_string(_destination+1)+".");}break;
    case Actions:
      if(c>='1'&&c<='4'){_action=number(c);binding();}
      else if(c=='5'){_page=ResetConfirm;prompt("Restore all default shortcuts?\ny Yes\nn No");}
      break;
    case Binding:
      if(c=='1'){_page=RecordArming;prompt(_action==3?"Release all keys. Then hold and release only the modifier keys.":"Release all keys/buttons. Then press and release the keyboard or mouse shortcut.");}
      else if(c=='2'||(c=='3'&&_action!=3)){_kind=c=='2'?1:2;_page=ClearConfirm;prompt(std::string("Clear ")+(_kind==1?"keyboard":"mouse")+" shortcut?\ny Yes\nn No");}
      break;
    case ClearConfirm:
      if(c=='y'){done(_backend.clearBinding(_action,_kind));}else if(c=='n')binding();break;
    case ResetConfirm:
      if(c=='y')done(_backend.resetBindings());else if(c=='n')actions();break;
    case RecordConfirm:
      if(c=='y'){const std::string error=_backend.saveRecord();_backend.cancelRecord();prompt(error.empty()?"Shortcut saved.":error);binding();}
      else if(c=='n'){_backend.cancelRecord();binding();}break;
    default:break;
    }
  }
public:
  explicit DeviceMenu(Backend &backend):_backend(backend){}
  bool active()const{return _active;}
  bool busy()const{return _output.busy();}
  void cancel(){if(_active)close();}
  // Calibration bypasses menu input tracking. Adopt current physical state so
  // held keys/buttons cannot select an option when the menu reappears.
  bool resume(uint32_t now,const uint8_t *keys,uint8_t modifiers,uint8_t buttons){
    if(_active||!_backend.connected())return false;
    _backend.release();_output.clear();_name.clear();
    _host=_backend.selected();_session=_backend.session();_activity=now;
    memcpy(_previous,keys,6);_modifiers=modifiers;_buttons=buttons;
    _released=!modifiers&&!buttons;for(auto key:_previous)if(key)_released=false;
    _active=true;main();return true;
  }
  void button(bool down,uint32_t now){
    if(!_button.update(down,_active,now))return;
    if(_active){close();return;}
    if(!_backend.connected())return;
    _backend.release();_host=_backend.selected();_session=_backend.session();_activity=now;_active=true;
    _released=!_modifiers&&!_buttons;for(auto k:_previous)if(k)_released=false;
    print("\nHID SWITCHER\nUse a blank text document, US layout, Caps Lock off.\nWait for each prompt. ESC goes back. BOOT exits.\n");main();
  }
  bool keyboard(const uint8_t *keys,uint8_t modifiers,uint32_t now){
    _backend.captureKeyboard(keys,modifiers);
    uint8_t previous[6];memcpy(previous,_previous,6);memcpy(_previous,keys,6);_modifiers=modifiers;
    if(!_active)return false;
    _activity=now;
    bool empty=!modifiers;for(unsigned i=0;i<6;++i)if(keys[i])empty=false;
    if(empty&&!_buttons)_released=true;
    // Escape cancels pending output and returns to the parent page.
    for(unsigned i=0;i<6;++i)if(keys[i]==41&&!modifiers){
      bool held=false;for(auto key:previous)if(key==41)held=true;
      if(!held)back();return true;
    }
    if(!_released||(_output.busy()&&!_textReady)||_page==Recording||_page==RecordArming||_page==SelectDrain||_page==ForgetDrain||_page==CalibrationDrain)return true;
    for(unsigned i=0;i<6;++i)if(keys[i]){
      bool held=false;for(auto k:previous)if(k==keys[i])held=true;
      if(!held){
        const bool editing=_textReady&&(_page==NameText||_page==RenameText||_page==EdgeText||_page==PointerText);
        key(keys[i],modifiers);
        if(!editing||(_page!=NameText&&_page!=RenameText&&_page!=EdgeText&&_page!=PointerText))break;
      }
    }
    return true;
  }
  bool mouse(uint8_t buttons,uint32_t now){
    _backend.captureMouse(buttons);
    if(_buttons!=buttons)_activity=now;
    _buttons=buttons;
    if(!_buttons&&!_modifiers){bool empty=true;for(auto k:_previous)if(k)empty=false;if(empty)_released=true;}
    return _active;
  }
  void tick(uint32_t now){
    if(!_active)return;
    if(!_backend.connected()||_backend.selected()!=_host||_backend.session()!=_session||uint32_t(now-_activity)>120000){close();return;}
    _output.tick(now,_backend.reportSpacing(),[this](const uint8_t *report){return _backend.send(report);});
    if(_output.busy())return;
    if(_page==BleWait){
      if(!_backend.bleInputBusy()){if(_bleScan)bleDevices();else bleInputs();return;}
      const auto revision=_backend.bleInputRevision();
      if(revision!=_bleRevision){
        _bleRevision=revision;_bleConfirmId=_backend.blePairingId();_bleConfirmCode=_backend.bleComparison();
        prompt(_backend.bleInputStatus()+(_bleConfirmCode?"\nDo BOTH codes match? y Yes / n Cancel":""));
      }
      return;
    }
    if(_page==NameText||_page==RenameText||_page==EdgeText||_page==PointerText)_textReady=true;
    if(_page==CalibrationDrain){
      // Finish instructions on the original host before selecting any other host.
      const unsigned mask=_destination;
      _backend.release();
      if(_backend.beginCalibration(mask,_enableAfterCalibration)){_output.clear();_active=false;}
      else {prompt("Could not start calibration. Check connections and try again.");calibrationSlots();}
      return;
    }
    if(_page==SelectDrain){const auto slot=_destination;close();_backend.select(slot);return;}
    if(_page==ForgetDrain){
      const auto error=_backend.forget(_slot);
      if(!_backend.connected()||_backend.session()!=_session){close();return;}
      prompt(error.empty()?"Pairing removed. Slot available.":error);computers();return;
    }
    if(_page==RecordArming){
      if(_backend.beginRecord(_action)){_page=Recording;_activity=now;}
      else {prompt("Could not start recording.");binding();}
    }else if(_page==Recording){
      const int state=_backend.recordState();
      if(state==3){_page=RecordConfirm;prompt("Captured: "+_backend.recordLabel()+"\nSave shortcut?\ny Yes\nn No");}
      else if(state==4||state==5){_backend.cancelRecord();prompt("Recording cancelled or expired.");binding();}
    }else if(_page==RecordConfirm&&_backend.recordState()==5){_backend.cancelRecord();prompt("Recording expired.");binding();}
  }
};
