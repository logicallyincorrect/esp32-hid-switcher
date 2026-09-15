#include "SetupServer.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <esp_random.h>
#include "SetupPage.h"
#include "WiFiCredentials.h"
#include "USBManager.h"
#include "RuntimeHealth.h"

static bool readNetwork(String &ssid,String &password){
  ssid="";password="";Preferences prefs;WiFiCredentials credentials;
  if(!prefs.begin("setup-wifi",true))return false;
  bool ok=prefs.isKey("network")&&prefs.getBytesLength("network")==sizeof(credentials)&&prefs.getBytes("network",&credentials,sizeof(credentials))==sizeof(credentials)&&validWiFiCredentials(credentials);
  prefs.end();if(ok){ssid=credentials.ssid;password=credentials.password;}else{ssid="";password="";}return ok;
}
static bool saveNetwork(const String &ssid,const String &password){
  WiFiCredentials credentials;ssid.toCharArray(credentials.ssid,sizeof(credentials.ssid));password.toCharArray(credentials.password,sizeof(credentials.password));
  if(!validWiFiCredentials(credentials))return false;
  Preferences prefs;if(!prefs.begin("setup-wifi",false))return false;
  const bool ok=prefs.putBytes("network",&credentials,sizeof(credentials))==sizeof(credentials);prefs.end();return ok;
}

void SetupServer::error(int status,const char *message){JsonDocument doc;doc["error"]=message;String body;serializeJson(doc,body);_server.send(status,"application/json",body);}
bool SetupServer::authorize(){if(!_active||_server.header("X-Setup-Token")!=_token){error(403,"Reopen the setup page and try again.");return false;}return true;}
void SetupServer::state(){
  JsonDocument doc;doc["device_name"]=_ble.deviceName();doc["selected"]=_ble.selected();doc["generation"]=_ble.config().generation;
  auto network=doc["network"].to<JsonObject>();network["connected"]=_online;network["connecting"]=_joining||_pending;network["ssid"]=_online?WiFi.SSID():(_pending?_pendingSsid:_ssid);network["ip"]=_online?WiFi.localIP().toString():String("");network["url"]="http://moonlander.local";network["portal"]=_ap;network["error"]=_networkError;
  USBManager::appendStatus(doc["usb"].to<JsonObject>());
  _ble.appendStatus(doc["bluetooth"].to<JsonObject>());
  _firmware.status(doc["firmware"].to<JsonObject>());
  auto array=doc["slots"].to<JsonArray>();for(unsigned i=0;i<3;++i){auto slot=array.add<JsonObject>();slot["name"]=_ble.config().slots[i].name;slot["assigned"]=bool(_ble.config().slots[i].assigned);slot["connected"]=_ble.connected(i);}
  String body;serializeJson(doc,body);_server.sendHeader("Cache-Control","no-store");_server.send(200,"application/json",body);
}
void SetupServer::begin(){
  _firmware.begin();
  pinMode(0,INPUT_PULLUP);
  const char *headers[]={"X-Setup-Token"};_server.collectHeaders(headers,1);
  _server.on("/",HTTP_GET,[this](){String page=SETUP_PAGE;page.replace("__SETUP_TOKEN__",_token);_server.sendHeader("Cache-Control","no-store");_server.send(200,"text/html; charset=utf-8",page);});
  _server.on("/api/state",HTTP_GET,[this](){state();});
  _server.on("/api/select",HTTP_POST,[this](){
    if(!authorize())return;
    if(_firmware.busy()||_firmware.rebootPending()){error(409,"Wait for the firmware update to finish.");return;}
    JsonDocument doc;if(_server.arg("plain").length()>128||deserializeJson(doc,_server.arg("plain"))||!doc["slot"].is<unsigned>()||doc["slot"].as<unsigned>()>2){error(400,"Choose a slot from 1 to 3.");return;}
    unsigned selected=doc["slot"];_ble.releaseAll();_ble.selectSlot(selected);
    if(_ble.selected()!=selected){error(500,"Could not save selection.");return;}state();
  });
  _server.on("/api/device",HTTP_POST,[this](){
    if(!authorize())return;
    if(_firmware.busy()||_firmware.rebootPending()){error(409,"Wait for the firmware update to finish.");return;}
    JsonDocument doc;
    if(_server.arg("plain").length()>256||deserializeJson(doc,_server.arg("plain"))||!doc["name"].is<const char *>()){error(400,"Provide a Bluetooth name.");return;}
    const String name=doc["name"].as<String>();
    if(!validDeviceName(name.c_str(),name.length())){error(400,"Use 1-29 UTF-8 bytes without control characters.");return;}
    if(!_ble.setDeviceName(name)){error(500,"Could not save the Bluetooth name. Try again.");return;}
    state();
  });
  _server.on("/api/config",HTTP_POST,[this](){
    if(!authorize())return;
    if(_firmware.busy()||_firmware.rebootPending()){error(409,"Wait for the firmware update to finish.");return;}
    JsonDocument doc;if(_server.arg("plain").length()>1024||deserializeJson(doc,_server.arg("plain"))||!doc["generation"].is<uint32_t>()||!doc["order"].is<JsonArray>()||!doc["names"].is<JsonArray>()||doc["order"].size()!=3||doc["names"].size()!=3){error(400,"Invalid computer settings.");return;}
    uint8_t order[3];char names[3][33]={};
    for(unsigned i=0;i<3;++i){if(!doc["order"][i].is<unsigned>()||doc["order"][i].as<unsigned>()>=3||!doc["names"][i].is<const char *>()){error(400,"Invalid slot or name.");return;}order[i]=doc["order"][i];const char *name=doc["names"][i];if(!validName(name)){error(400,"Names must contain 1–32 bytes and no control characters.");return;}strcpy(names[i],name);}
    if(!validOrder(order)){error(400,"Each computer must occupy a different slot.");return;}
    if(!_ble.configure(order,names,doc["generation"])){error(409,"Settings changed or pairing is in progress. Reload and try again.");return;}state();
  });
  _server.on("/api/wifi",HTTP_POST,[this](){
    if(!authorize())return;
    if(_firmware.busy()||_firmware.rebootPending()){error(409,"Wait for the firmware update to finish.");return;}
    JsonDocument doc;
    if(_server.arg("plain").length()>512||deserializeJson(doc,_server.arg("plain"))||!doc["ssid"].is<const char *>()||!doc["password"].is<const char *>()){error(400,"Enter your Wi-Fi name and password.");return;}
    String ssid=doc["ssid"].as<String>(),password=doc["password"].as<String>();
    if(ssid.length()==0||ssid.length()>32||password.length()>63||(password.length()!=0&&password.length()<8)||ssid.length()!=strlen(ssid.c_str())||password.length()!=strlen(password.c_str())){error(400,"Use a Wi-Fi name of 1–32 bytes and a password of 8–63 characters, or leave it empty for an open network.");return;}
    if(_joining||_pending){error(409,"A Wi-Fi connection is already in progress.");return;}
    // Reply before changing networks; retain the last working credentials until success.
    _pendingSsid=ssid;_pendingPassword=password;_pending=true;_joinAt=millis();_networkError="";
    _server.send(202,"application/json","{\"connecting\":true}");
  });
  _server.on("/api/firmware",HTTP_POST,[this](){
    if(!_uploadAuthorized){error(403,"Reopen the setup page and try again.");}
    else if(_uploadFiles!=1||!_firmware.commit()){error(400,_firmware.error().length()?_firmware.error().c_str():"Choose one firmware.bin file.");}
    else _server.send(202,"application/json","{\"restarting\":true}");
    _uploadFiles=0;_uploadAuthorized=false;
    if(!_firmware.rebootPending())_ble.setMaintenance(false);
  },[this](){
    feedRuntimeWatchdog();auto &upload=_server.upload();_uploadActivity=millis();
    if(upload.status==UPLOAD_FILE_START){
      _uploadAuthorized=_active&&_server.header("X-Setup-Token")==_token;
      if(!_uploadAuthorized)return;
      if(++_uploadFiles!=1){_firmware.abort("Upload only one firmware file");return;}
      if(_firmware.start())_ble.setMaintenance(true);
    }else if(_uploadAuthorized&&upload.status==UPLOAD_FILE_WRITE){_firmware.write(upload.buf,upload.currentSize);}
    else if(_uploadAuthorized&&upload.status==UPLOAD_FILE_END){_firmware.finish();}
    else if(upload.status==UPLOAD_FILE_ABORTED){_firmware.abort("Upload interrupted; current firmware is unchanged");_uploadFiles=0;_ble.setMaintenance(false);}
  });
  _server.on("/api/firmware/rollback",HTTP_POST,[this](){
    if(!authorize())return;
    if(!_firmware.rollback()){error(409,_firmware.error().c_str());return;}
    _ble.setMaintenance(true);_server.send(202,"application/json","{\"restarting\":true}");
  });
  _server.onNotFound([this](){
    if(_ap&&_server.method()==HTTP_GET&&!_server.uri().startsWith("/api/")){
      _server.sendHeader("Location","http://192.168.4.1/");_server.send(302,"text/plain","");
    }else error(404,"Not found");
  });
  char token[33];snprintf(token,sizeof(token),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());_token=token;
  readNetwork(_ssid,_password);
  WiFi.persistent(false);WiFi.setHostname("moonlander");WiFi.setAutoReconnect(true);
  WiFi.setAutoReconnect(false);WiFi.mode(WIFI_OFF);
  _ble.setWiFiControl([](void *context,bool enabled){return static_cast<SetupServer *>(context)->setRadio(enabled);},this);
  _active=true;
  Serial.println("[Setup] Wi-Fi off. Use BLE CLI wifi on, or hold BOOT3s for setup.");
}
void SetupServer::startPortal(){
  if(_ap||_firmware.busy())return;
  _radioEnabled=true;
  // Simpler password is deferred; preserve the deployed credential in this build.
  Preferences prefs;prefs.begin("setup-wifi",false);String stored=prefs.getString("password","");
  if(stored.length()!=16){char text[17];snprintf(text,sizeof(text),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());stored=text;prefs.putString("password",stored);}prefs.end();
  const char *password=stored.c_str();
  WiFi.mode(WIFI_AP_STA);
  if(!WiFi.softAP("Moonlander Setup",password,1,false,4)){Serial.println("[Setup] Unable to open setup Wi-Fi");return;}
  _server.begin();_ap=true;_closeApAt=0;_dns.start(53,"*",WiFi.softAPIP());
  Serial.printf("[Setup] Wi-Fi: Moonlander Setup | password: %s | http://192.168.4.1\n",password);
}
void SetupServer::stopPortal(){
  if(!_ap)return;
  _dns.stop();WiFi.softAPdisconnect(true);WiFi.mode(WIFI_STA);_ap=false;_closeApAt=0;
  Serial.println("[Setup] Setup hotspot closed; web UI remains available on home Wi-Fi");
}
bool SetupServer::setRadio(bool enabled){
  if(_firmware.busy())return false;
  if(enabled==_radioEnabled&&!_radioPaused)return true;
  _radioPaused=false;
  if(enabled){
    _radioEnabled=true;readNetwork(_ssid,_password);WiFi.setAutoReconnect(true);
    if(_ssid.length()){WiFi.mode(WIFI_STA);_server.begin();joinNetwork();}else startPortal();
  }else{
    _dns.stop();_server.stop();if(_mdns){MDNS.end();_mdns=false;}
    WiFi.setAutoReconnect(false);WiFi.mode(WIFI_OFF);
    _radioEnabled=_online=_joining=_ap=_pending=false;_pendingSsid="";_pendingPassword="";_lostAt=_closeApAt=0;
    Serial.println("[Setup] Wi-Fi off; BLE CLI remains available");
  }
  _ble.setWiFiState(_radioEnabled,_online);return true;
}
void SetupServer::toggle(){
  if(_firmware.busy())return;
  if(!_radioEnabled)setRadio(true);
  startPortal();
}
void SetupServer::joinNetwork(){
  _online=false;_joining=true;_joinAt=millis();_lostAt=0;
  WiFi.begin(_ssid.c_str(),_password.c_str());
  Serial.println("[Setup] Connecting to saved Wi-Fi");
}
void SetupServer::radioComparison(){
  if(_radioPaused||!_online||_ap||_pending||_joining||_firmware.busy()){Serial.println("[Radio test] Needs idle setup server on normal Wi-Fi");return;}
  // Volatile diagnostic only. Always reconnect after 45s; no saved settings change.
  if(!WiFi.mode(WIFI_OFF)){Serial.println("[Radio test] Could not stop Wi-Fi");return;}
  _radioPaused=true;_radioPauseAt=millis();_online=false;
  Serial.println("[Radio test] WIFI_OFF for45s; automatic restore follows");
}
void SetupServer::loop(){
  _firmware.loop(USBManager::healthy());
  _ble.setWiFiState(_radioEnabled,_online);
  if(digitalRead(0)==LOW){if(!_pressed)_pressed=millis();if(!_handled&&millis()-_pressed>=3000){_handled=true;toggle();}}else{_pressed=0;_handled=false;}
  if(!_radioEnabled)return;
  if(_radioPaused){
    if(millis()-_radioPauseAt>=45000){_radioPaused=false;WiFi.mode(WIFI_STA);joinNetwork();Serial.println("[Radio test] WIFI_RESTORE");}
    return;
  }
  if(_firmware.busy()&&millis()-_uploadActivity>15000){_firmware.abort("Upload timed out; current firmware is unchanged");_uploadFiles=0;_ble.setMaintenance(false);}

  if(_ap)_dns.processNextRequest();
  _server.handleClient();
  if(_pending&&!_joining&&millis()-_joinAt>=500){
    startPortal();WiFi.disconnect(false,false);_ssid=_pendingSsid;_password=_pendingPassword;joinNetwork();
  }
  if(_joining&&WiFi.status()==WL_CONNECTED){
    _joining=false;_online=true;_lostAt=0;_networkError="";
    if(_pending){
      if(!saveNetwork(_ssid,_password))_networkError="Connected, but could not save Wi-Fi settings. Try again.";
      _pending=false;_pendingSsid="";_pendingPassword="";
    }
    if(!_mdns){_mdns=MDNS.begin("moonlander");if(_mdns)MDNS.addService("http","tcp",80);}
    if(_ap&&_networkError.isEmpty())_closeApAt=millis();
    Serial.printf("[Setup] Online: http://moonlander.local | http://%s\n",WiFi.localIP().toString().c_str());
  }
  if(_joining&&millis()-_joinAt>=30000){
    _joining=false;_networkError="Could not connect. Check the Wi-Fi name and password; use a 2.4 GHz network.";
    if(_pending){
      _pending=false;_pendingSsid="";_pendingPassword="";
      readNetwork(_ssid,_password);
      WiFi.disconnect(false,false);
    }
    startPortal();Serial.println("[Setup] Wi-Fi connection timed out; setup portal available");
  }
  if(_online&&WiFi.status()!=WL_CONNECTED){_online=false;_lostAt=millis();_closeApAt=0;}
  if(!_online&&!_joining&&!_pending&&_ssid.length()){
    if(WiFi.status()==WL_CONNECTED){_joining=true;}
    else if(!_lostAt)_lostAt=millis();
    else if(millis()-_lostAt>=30000){startPortal();joinNetwork();}
  }
  if(_online&&_ap&&_closeApAt&&millis()-_closeApAt>=10000)stopPortal();
}
