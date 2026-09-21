#include "Application.h"

void Application::serialRequest(const char *line){
  JsonDocument request,response;auto out=response.to<JsonObject>();out["id"]=nullptr;
  String error;bool ok=false;
  if(strncmp(line,"HID1 ",5)||deserializeJson(request,line+5))error="Expected @HID1 JSON request";
  else if(!request["id"].is<uint32_t>()||!request["op"].is<const char *>())error="id must be an unsigned integer; op must be a string";
  else {
    out["id"]=request["id"];auto result=out["result"].to<JsonObject>();
    const String op=request["op"].as<String>();
    const bool read=op=="status"||op=="input-status"||op=="shortcuts"||op=="shortcut-status";
    const bool recording=_ble.recordState()>=1&&_ble.recordState()<=3;
    if(!read&&(_menu.active()||(_ble.calibrationActive()&&op!="calibration-cancel")||(recording&&!op.startsWith("shortcut-"))))error="Finish the active configuration or calibration session first";
    else if(op=="status"){
      result["protocol"]=1;result["name"]=_ble.deviceName();result["selected"]=_ble.selected();result["generation"]=_ble.config().generation;
      const auto app=esp_app_get_description();auto firmware=result["firmware"].to<JsonObject>();
      firmware["version"]=app->version;firmware["date"]=app->date;firmware["time"]=app->time;
      char buildId[65];for(unsigned i=0;i<32;++i)snprintf(buildId+i*2,3,"%02x",app->app_elf_sha256[i]);firmware["build_id"]=buildId;
      _usb.appendStatus(result["usb"].to<JsonObject>());
      result["ble_input"]=bool(HID_BLE_INPUT);result["absolute"]=_ble.absoluteMode();result["seamless"]=_ble.seamlessEnabled();
      result["edge_distance"]=_ble.edgeThreshold();result["pointer_speed"]=_ble.sharedSpeed();
      _ble.appendPointerPosition(result["pointer_position"].to<JsonObject>());result["calibration_needed"]=_ble.calibrationNeeded();result["calibration_ready"]=_ble.calibrationReady();
      result["calibration_active"]=_ble.calibrationActive();result["calibration_stage"]=_ble.calibrationStage();result["calibration_result"]=_ble.calibrationResult();
      auto slots=result["slots"].to<JsonArray>();for(unsigned i=0;i<3;++i){auto slot=slots.add<JsonObject>();slot["name"]=_ble.config().slots[i].name;slot["paired"]=_ble.config().slots[i].assigned;slot["connected"]=_ble.connected(i);}
      _ble.appendLayout(result["monitor_layout"].to<JsonObject>());
      _ble.appendShortcuts(result["shortcuts"].to<JsonObject>());ok=true;
    }else if(op=="input-status"){
      result["available"]=_backend.bleInputAvailable();result["busy"]=_backend.bleInputBusy();result["status"]=_backend.bleInputStatus();result["devices"]=_backend.bleInputDevices();result["count"]=_backend.bleInputCount();result["revision"]=_backend.bleInputRevision();ok=true;
#if HID_BLE_INPUT
      _bleInput.appendStatus(result);
#endif
    }else if(op=="input-confirm"){
#if HID_BLE_INPUT
      if(request["attempt"].is<uint32_t>()&&request["code"].is<uint32_t>())ok=_bleInput.confirmPairing(request["attempt"],request["code"]);
#endif
      if(!ok)error="Pairing prompt expired or changed. Check the current code and retry.";
    }else if(op=="input"){
      if(!request["command"].is<const char *>())error="command must be a string";
      else {ok=_backend.bleInputCommand(request["command"]);result["accepted"]=ok;if(!ok)error="Unsupported input command, busy, or BLE input unavailable";}
    }else if(op=="input-cancel"){_backend.cancelBleInput();ok=true;}
    else if(op=="name"){
      if(request["name"].is<const char *>())ok=_ble.setDeviceName(request["name"].as<String>());
    }else if(op=="slots"){
      auto order=request["order"].as<JsonArrayConst>();auto names=request["names"].as<JsonArrayConst>();
      uint8_t indices[3];char labels[3][33]={};bool valid=order.size()==3&&names.size()==3&&request["generation"].is<uint32_t>();
      for(unsigned i=0;valid&&i<3;++i){valid=order[i].is<unsigned>()&&order[i].as<unsigned>()<3&&names[i].is<const char *>();if(valid){const auto name=names[i].as<JsonString>();valid=name.size()>0&&name.size()<=32&&!memchr(name.c_str(),0,name.size());indices[i]=order[i];if(valid)memcpy(labels[i],name.c_str(),name.size());}}
      if(valid)ok=_ble.configure(indices,labels,request["generation"]);
      if(!ok)error="Invalid slots, stale generation, pairing in progress, or save failed";
    }else if(op=="select"||op=="forget"){
      if(request["slot"].is<unsigned>()&&request["slot"].as<unsigned>()<3){
        const unsigned slot=request["slot"];
        if(op=="select"){_input.barrier();select(slot);ok=_ble.selected()==slot;}
        else if(request["confirm"].is<bool>()&&request["confirm"].as<bool>()){error=_ble.forgetComputer(slot);ok=error.isEmpty();}
      }
    }else if(op=="edge"){
      if(request["value"].is<unsigned>())ok=_ble.setEdgeThreshold(request["value"]);
    }else if(op=="monitor-layout"){
      ok=_ble.setLayout(request.as<JsonVariantConst>(),error);
    }else if(op=="pointer-speed"){
      if(request["value"].is<unsigned>())ok=_ble.setSharedSpeed(request["value"]);
    }else if(op=="pointer-probe"){
      if(!_input.keyboardHeld&&!_input.buttons&&request["x"].is<int>()&&request["y"].is<int>())ok=_ble.probePointer(request["x"],request["y"]);
      if(!ok)error="Probe requires active absolute mode, a connected computer, released input, and coordinates 0–32767";
    }else if(op=="pointer"){
      if(request["slot"].is<unsigned>()&&request["field"].is<unsigned>()&&request["value"].is<unsigned>())ok=_ble.setPointerValue(request["slot"],request["field"],request["value"]);
    }else if(op=="seamless"){
      if(request["enabled"].is<bool>())ok=_ble.setSeamlessEnabled(request["enabled"]);
      if(!ok)error="Calibration active or save failed";
    }else if(op=="calibration-start"){
      if(request["mask"].is<unsigned>()&&request["mask"].as<unsigned>()>0&&request["mask"].as<unsigned>()<=7&&request["enable"].is<bool>())ok=_ble.beginCalibration(request["mask"],request["enable"],false);
    }else if(op=="calibration-cancel"){_ble.cancelCalibration();ok=true;}
    else if(op=="shortcuts"||op.startsWith("shortcut-")||op=="shortcuts-reset")ok=_ble.shortcutCommand(request.as<JsonVariantConst>(),result,error);
    else error="Unknown operation";
    if(ok&&!read&&op!="pointer-probe"&&!_ble.calibrationActive()){_ble.releaseAll();_input.barrier();}
  }
  out["ok"]=ok;if(!ok)out["error"]=error.isEmpty()?"Invalid arguments or operation failed":error;
  String wire;serializeJson(response,wire);Serial.printf("\n@HID1 %s\n",wire.c_str());
}
