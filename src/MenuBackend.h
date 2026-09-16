#pragma once
#include "BleHid.h"
#include "UsbHost.h"
#include <esp_app_desc.h>
#include <string>

class MenuBackend {
  BleHid &_ble;
  UsbHost &_usb;
  String _token;
  bool command(const char *op,unsigned action=0,unsigned kind=0){
    JsonDocument request,result;String error;
    request["op"]=op;request["action"]=action;request["kind"]=kind;
    return _ble.shortcutCommand(request.as<JsonVariantConst>(),result.to<JsonObject>(),error);
  }
public:
  MenuBackend(BleHid &ble, UsbHost &usb):_ble(ble),_usb(usb){}
  bool connected(){return _ble.isConnected();}
  unsigned selected(){return _ble.selected();}
  uint32_t session(){return _ble.menuSession();}
  uint32_t reportSpacing(){return _ble.menuReportSpacing();}
  void release(){_ble.releaseAll();}
  bool send(const uint8_t *report){return _ble.sendMenuReport(report);}
  void select(unsigned slot){_ble.selectSlot(slot);}
  void captureKeyboard(const uint8_t *keys,uint8_t modifiers){_ble.captureKeyboard(keys,modifiers);}
  void captureMouse(uint8_t buttons){_ble.captureMouse(buttons);}
  std::string deviceName(){return _ble.deviceName().c_str();}
  std::string computerName(unsigned slot){return _ble.config().slots[slot].name;}
  std::string computers(){
    std::string text;
    for(unsigned i=0;i<3;++i){
      text+=std::to_string(i+1)+" "+_ble.config().slots[i].name;
      text+=_ble.connected(i)?" [connected]":(_ble.config().slots[i].assigned==2?" [removal pending]":(_ble.config().slots[i].assigned?" [disconnected]":" [unpaired]"));
      if(i==selected())text+=" *";
      text+="\n";
    }
    return text;
  }
  std::string binding(unsigned action){
    const char *names[]={"Cycle","Next","Previous","Slot"};
    std::string text=names[action];text+="\nKeyboard: ";text+=_ble.bindingLabel(action,1).c_str();
    if(action!=3){text+="\nMouse: ";text+=_ble.bindingLabel(action,2).c_str();}
    return text;
  }
  bool rename(unsigned slot,const std::string &name){
    uint8_t order[]={0,1,2};char names[3][33];
    for(unsigned i=0;i<3;++i)memcpy(names[i],_ble.config().slots[i].name,33);
    snprintf(names[slot],33,"%s",name.c_str());
    return _ble.configure(order,names,_ble.config().generation);
  }
  bool move(unsigned from,unsigned to){
    uint8_t order[]={0,1,2};const auto item=order[from];
    if(from<to)for(unsigned i=from;i<to;++i)order[i]=order[i+1];
    else for(unsigned i=from;i>to;--i)order[i]=order[i-1];
    order[to]=item;char names[3][33];
    for(unsigned i=0;i<3;++i)memcpy(names[i],_ble.config().slots[order[i]].name,33);
    return _ble.configure(order,names,_ble.config().generation);
  }
  bool paired(unsigned slot){return _ble.config().slots[slot].assigned!=0;}
  std::string forget(unsigned slot){return _ble.forgetComputer(slot).c_str();}
  bool renameDevice(const std::string &name){return _ble.setDeviceName(name.c_str());}
  bool clearBinding(unsigned action,unsigned kind){return command("shortcut-clear",action,kind);}
  bool resetBindings(){return command("shortcuts-reset");}
  bool beginRecord(unsigned action){
    JsonDocument request,result;String error;request["op"]="shortcut-record";request["action"]=action;
    if(!_ble.shortcutCommand(request.as<JsonVariantConst>(),result.to<JsonObject>(),error))return false;
    _token=result["token"].as<String>();return true;
  }
  int recordState(){return _ble.recordState();}
  std::string recordLabel(){return _ble.recordedLabel().c_str();}
  std::string saveRecord(){
    JsonDocument request,result;String error;request["op"]="shortcut-save";request["token"]=_token;
    const bool ok=_ble.shortcutCommand(request.as<JsonVariantConst>(),result.to<JsonObject>(),error);
    return ok?std::string():std::string(error.c_str());
  }
  void cancelRecord(){
    if(!_token.length())return;
    JsonDocument request,result;String error;request["op"]="shortcut-cancel";request["token"]=_token;
    _ble.shortcutCommand(request.as<JsonVariantConst>(),result.to<JsonObject>(),error);_token="";
  }
  std::string diagnostics(){
    JsonDocument doc;_ble.appendStatus(doc.to<JsonObject>());
    char text[384];snprintf(text,sizeof(text),
      "Firmware: %.32s\nUSB: %s\nBLE errors: %lu  Recoveries: %lu\nMouse interval: %.2f ms\nUSB spacing: %.2f ms  BLE spacing: %.2f ms\nQueue wait: %.2f ms\nThese times are not mouse-to-screen latency.",
      esp_app_get_description()->version,_usb.healthy()?"ready":"not ready",
      (unsigned long)(doc["send_errors"]|0UL),(unsigned long)(doc["recoveries"]|0UL),
      doc["traffic"]["merge_threshold_ms"].as<double>(),doc["mouse_timing"]["arrival_spacing"]["mean_ms"].as<double>(),
      doc["mouse_timing"]["submission_spacing"]["mean_ms"].as<double>(),doc["mouse_timing"]["bridge_queue_wait"]["mean_ms"].as<double>());
    return text;
  }
};
