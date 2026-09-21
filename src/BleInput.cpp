#include "BleInput.h"
#if HID_BLE_INPUT
#include <esp_random.h>
#include <NimBLEUtils.h>
static NimBLEUUID uuid(uint16_t value){return NimBLEUUID(value);}
void BleInput::begin(Sink sink){
  static_assert(CONFIG_BT_NIMBLE_MAX_CONNECTIONS>=4,"BLE input needs a fourth connection");
  _sink=sink;_client=NimBLEDevice::createClient();assert(_client);
  assert(NimBLEDevice::setCustomGapHandler(securityEvent,this));
  _commands=xQueueCreate(4,sizeof(Request));assert(_commands);
  assert(xTaskCreate(task,"ble-input",10240,this,1,nullptr)==pdPASS);
  Serial.println("[BLE input] Experimental host ready. UART: b scan, b connect N, b reconnect, b disconnect, b forget, b status (newline).");
}
bool BleInput::command(const char *line){
  if(!strncmp(line,"confirm ",8)){
    if(strlen(line)!=14)return false;
    uint32_t code=0;for(unsigned i=8;i<14;++i){if(line[i]<'0'||line[i]>'9')return false;code=code*10+line[i]-'0';}
    uint32_t expected=code+1;
    if(!_busy.load()||_cancelled.load()||!_client->isConnected()||!_comparison.compare_exchange_strong(expected,0))return false;
    const bool accepted=NimBLEDevice::injectConfirmPasskey(_client->getConnInfo(),true);
    if(!accepted){_pairingIssue=3;_client->disconnect();}
    return accepted;
  }
  Request r{};unsigned n=0;char extra=0;
  if(!strcmp(line,"scan"))r.type=Request::Scan;
  else if(!strcmp(line,"scan all")){r.type=Request::Scan;r.all=true;}
  else if(sscanf(line,"connect %u %c",&n,&extra)==1&&n>=1&&n<=ScanCapacity){r.type=Request::Connect;r.index=n-1;}
  else if(!strcmp(line,"reconnect"))r.type=Request::Reconnect;
  else if(!strcmp(line,"disconnect"))r.type=Request::Disconnect;
  else if(!strcmp(line,"forget"))r.type=Request::Forget;
  else if(!strcmp(line,"status"))r.type=Request::Status;
  else {Serial.println("[BLE input] Commands: b scan | b connect N | b reconnect | b disconnect | b forget | b status | b confirm XXXXXX");return false;}
  if(!_commands||_busy.exchange(true))return false;
  _cancelled=false;
  _operation=r.type==Request::Scan?1:(r.type==Request::Connect||r.type==Request::Reconnect)?2:5;
  if(xQueueSend(_commands,&r,0)!=pdTRUE){_busy=false;return false;}
  return true;
}
void BleInput::task(void *context){static_cast<BleInput *>(context)->run();}
void BleInput::run(){
  _prefs.begin("ble-input",false);
  Target target;
  if(_prefs.isKey("target")&&_prefs.getBytesLength("target")==sizeof(target)&&_prefs.getBytes("target",&target,sizeof(target))==sizeof(target)&&target.version==1&&target.type<=3){_saved=NimBLEAddress(target.address,target.type);_hasSaved=!_saved.isNull();}
  const auto savedName=_prefs.getString("name","Saved BLE input");
  portENTER_CRITICAL(&_viewLock);snprintf(_savedName,sizeof(_savedName),"%s",savedName.c_str());portEXIT_CRITICAL(&_viewLock);
  _client->setClientCallbacks(this,false);_client->setConnectTimeout(5000);
  Request r;
  while(true){
    if(xQueueReceive(_commands,&r,portMAX_DELAY)!=pdTRUE)continue;
    if(_cancelled){_busy=false;continue;}
    switch(r.type){
      case Request::Scan:scan(r.all);break;
      case Request::Connect:
        if(r.index>=_targetCount)message("Scan first and choose a listed number.");
        else connect(_targets[r.index]);break;
      case Request::Reconnect:
        if(_hasSaved)connect(_saved);else message("No saved peripheral; scan first.");break;
      case Request::Disconnect:release();_client->disconnect();message("Input disconnected. Computer pairings are unchanged.");break;
      case Request::Forget:
        release();_client->disconnect();
        if(_hasSaved){
          if(NimBLEDevice::isBonded(_saved)&&!NimBLEDevice::deleteBond(_saved)){message("Could not remove input bond; settings retained.");break;}
          if(!_prefs.remove("target")){message("Could not clear saved target; retry forget.");break;}_hasSaved=false;
          _prefs.remove("name");
        }
        message("Input pairing cleared; computer pairings retained.");break;
      case Request::Status:
        Serial.printf("[BLE input] connected=%u ready=%u saved=%u reports=%lu unsupported=%lu\n",_client->isConnected(),_ready.load(),_hasSaved.load(),(unsigned long)_reports.load(),(unsigned long)_unsupported.load());break;
    }
    if(_cancelled){release();_client->disconnect();message("Cancelled.");}
    _operation=0;_busy=false;
  }
}
void BleInput::scan(bool all){
  if(_client->isConnected()){message("Disconnect input before scanning.");return;}
  auto scanner=NimBLEDevice::getScan();scanner->setActiveScan(true);scanner->setInterval(80);scanner->setWindow(60);scanner->setDuplicateFilter(false);scanner->setMaxResults(96);
  scanner->clearResults();_targetCount=0;portENTER_CRITICAL(&_viewLock);_visibleCount=0;portEXIT_CRITICAL(&_viewLock);message("Looking for nearby devices for 12 seconds. Keep your device in pairing mode.");
  portENTER_CRITICAL(&_scanLock);_scanCache.clear();_collectScan=true;portEXIT_CRITICAL(&_scanLock);
  scanner->getResults(12000);
  portENTER_CRITICAL(&_scanLock);_collectScan=false;portEXIT_CRITICAL(&_scanLock);
  unsigned connectable=0,matches=0;
  const unsigned limit=all?ScanCapacity:9;
  for(unsigned i=0;i<_scanCache.count;++i){
    const auto &d=_scanCache.entries[i];if(!d.connectable)continue;
    ++connectable;
    const bool hid=d.hid;
    if(!all&&!hid)continue;
    ++matches;if(_targetCount==limit)continue;
    _targets[_targetCount++]=NimBLEAddress(d.address,d.type);std::string name=d.name;
    if(name.size()>40)name.resize(40);for(auto &c:name)if(c<32||c>126)c='?';
    const auto address=NimBLEAddress(d.address,d.type).toString();
    char label[56]={};snprintf(label,sizeof(label),"%s [%s]",name.empty()?(hid?"Unnamed HID":"Unnamed BLE"):name.c_str(),address.substr(12).c_str());
    portENTER_CRITICAL(&_viewLock);memcpy(_names[_targetCount-1],label,sizeof(label));_visibleCount=_targetCount;portEXIT_CRITICAL(&_viewLock);
    Serial.printf("[BLE input] %u: %s (%s)\n",_targetCount,name.empty()?"unnamed HID":name.c_str(),address.c_str());
  }
  const unsigned seen=_scanCache.count;scanner->clearResults();
  char summary[192];
  snprintf(summary,sizeof(summary),"Scan: %u advertisements, %u connectable; showing %u %s devices.%s%s",seen,connectable,_targetCount,all?"BLE":"HID",matches>limit||_scanCache.limited?" List limited; rescan closer to your device.":"",!all?" Missing device? Try Scan all BLE devices.":" Select only your input device; HID support is checked after pairing.");
  message(summary);
}
void BleInput::release(){_comparison=0;_accept=false;_ready=false;if(_sink.reset)_sink.reset(_sink.context);}
void BleInput::connect(const NimBLEAddress &address){
  if(_client->isConnected()){message("Disconnect the current input first.");return;}
  release();_endpointCount=0;_pairingIssue=0;_disconnectReason=0;_pairingAction=NoCallback;_securityStatus=-1;
  _attempt=esp_random();_pairingCode=0;_securityStarted=0;_operation=2;
  char selected[56];snprintf(selected,sizeof(selected),"BLE input [%s]",address.toString().c_str());
  portENTER_CRITICAL(&_viewLock);
  if(_hasSaved.load()&&address==_saved)snprintf(selected,sizeof(selected),"%s",_savedName);
  for(unsigned i=0;i<_targetCount;++i)if(address==_targets[i]){snprintf(selected,sizeof(selected),"%s",_names[i]);break;}
  memcpy(_currentName,selected,sizeof(selected));portEXIT_CRITICAL(&_viewLock);
  message("Connecting selected input. Keep this document focused for pairing instructions.");
  if(_cancelled)return;
  // Pair source first, then destination computers: the stack's IO capability
  // is global. Restore the normal host-pairing policy on every exit path.
  struct PairingDisplay {
    PairingDisplay(){NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);}
    ~PairingDisplay(){NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);}
  } pairingDisplay;
  if(!_client->connect(address)){message("Connection failed. Wake the keyboard or rescan if its address changed.");return;}
  const auto identity=_client->getConnInfo().getIdAddress();
  if(!NimBLEDevice::isBonded(identity)&&NimBLEDevice::getNumBonds()>=CONFIG_BT_NIMBLE_MAX_BONDS){
    message("Bond store full; no existing computer bond will be evicted. Forget an old input first.");_client->disconnect();return;
  }
  // NimBLE 2.5.1's blocking secureConnection shares a task waiter with MTU
  // and identity events. Those can return success before encryption finishes.
  // Start security without that waiter and observe only this link's ENC_CHANGE.
  int startError=0;
  _operation=3;_securityStarted=millis();
  const bool started=_client->getConnInfo().isEncrypted()||NimBLEDevice::startSecurity(_client->getConnHandle(),&startError);
  const uint32_t securityStart=millis();
  while(started&&!_cancelled.load()&&_client->isConnected()&&
        !_client->getConnInfo().isEncrypted()&&_securityStatus.load()<=0&&
        uint32_t(millis()-securityStart)<30000){vTaskDelay(pdMS_TO_TICKS(20));}
  if(_cancelled.load()){_comparison=0;_client->disconnect();return;}
  if(!started||!_client->isConnected()||!_client->getConnInfo().isEncrypted()){
    const int status=_securityStatus.load();
    const int error=!started?startError:status>0?status:!_client->isConnected()?BLE_HS_ENOTCONN:BLE_HS_ETIMEOUT;
    char detail[192];
    const char *cause=_pairingIssue==1?"Peer requested passkey entry; unsupported":
                      _pairingIssue==3?"Numeric confirmation submission failed":
                      error==BLE_HS_ETIMEOUT?"Timed out waiting for encryption":
                      "Encryption failed";
    // Keep the action in the final result: a sub-second failure can overwrite
    // the passkey prompt before the web UI's next status poll.
    const char *actions[]={"no callback","display passkey","enter passkey","compare passkey"};
    snprintf(detail,sizeof(detail),"%s (security=%d / 0x%x, disconnect=%d, pairing=%s). Not forwarding.",cause,error,unsigned(error),_disconnectReason.load(),actions[_pairingAction.load()]);
    message(detail);
    _comparison=0;
    Serial.printf("[BLE input] Security error: %d (%s)\n",error,NimBLEUtils::returnCodeToString(error));
    _client->disconnect();return;
  }
  _comparison=0;
  if(_cancelled){_client->disconnect();return;}
  _operation=4;message("Paired. Checking keyboard and pointer support...");
  if(!discover()){message("No supported keyboard/relative-pointer input; not forwarding.");release();_client->disconnect();return;}
  if(_cancelled||!_client->isConnected()){release();_client->disconnect();return;}
  const auto next=_client->getConnInfo().getIdAddress();Target target;target.type=next.getType();memcpy(target.address,next.getVal(),6);
  const bool saved=_prefs.putBytes("target",&target,sizeof(target))==sizeof(target);
  if(saved){_saved=next;_hasSaved=true;_prefs.putString("name",selected);portENTER_CRITICAL(&_viewLock);memcpy(_savedName,selected,sizeof(selected));portEXIT_CRITICAL(&_viewLock);}else Serial.println("[BLE input] Could not persist reconnect target; previous target retained.");
  _accept=true;_ready=true;
  if(!_client->isConnected()||!_client->getConnInfo().isEncrypted()){release();return;}
  message(saved?"Connected. Input follows the selected computer.":"Connected for this session. Saving failed; reconnect after reboot is unavailable for this device.");
}
int BleInput::securityEvent(ble_gap_event *event,void *context){
  auto &self=*static_cast<BleInput *>(context);
#if CONFIG_BT_NIMBLE_EXT_ADV
  if(event->type==BLE_GAP_EVENT_EXT_DISC){
    const auto &d=event->ext_disc;
    const bool connectable=(d.props&(BLE_HCI_ADV_CONN_MASK|BLE_HCI_ADV_DIRECT_MASK))!=0;
#else
  if(event->type==BLE_GAP_EVENT_DISC){
    const auto &d=event->disc;
    const bool connectable=d.event_type==BLE_HCI_ADV_RPT_EVTYPE_ADV_IND||d.event_type==BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
#endif
    portENTER_CRITICAL(&self._scanLock);
    if(self._collectScan.load())self._scanCache.observe(d.addr.val,d.addr.type,connectable,d.data,d.length_data);
    portEXIT_CRITICAL(&self._scanLock);
  }

  if(event->type==BLE_GAP_EVENT_ENC_CHANGE&&self._client&&
     event->enc_change.conn_handle==self._client->getConnHandle()){
    self._securityStatus=event->enc_change.status;
  }
  return 0;
}
bool BleInput::subscribe(NimBLERemoteCharacteristic *value,unsigned map,uint8_t id,uint8_t boot){
  if(!value||!value->canNotify()||_endpointCount==8)return false;
  const unsigned index=_endpointCount;_endpoints[index]={value,uint8_t(map),id,boot};
  if(!value->subscribe(true,[this,index](NimBLERemoteCharacteristic *,uint8_t *data,size_t length,bool){receive(index,data,length);})){Serial.println("[BLE input] Notification subscription failed.");return false;}
  ++_endpointCount;return true;
}
bool BleInput::discover(){
  unsigned mapIndex=0;
  // NimBLE 2.x getters return cached vectors unless refresh is requested.
  // A fresh connection has no service cache; discover it before inspecting HID.
  for(auto service:_client->getServices(true)){
    if(_cancelled)return false;
    if(service->getUUID()!=uuid(0x1812))continue;
    if(mapIndex==2){Serial.println("[BLE input] More than two HID services are unsupported.");return false;}
    // Discover every report characteristic, not just those fetched by UUID
    // below. Refresh before obtaining pointers to avoid invalidating them.
    service->getCharacteristics(true);
    auto &map=_maps[mapIndex];map.keys=map.pointer=false;
    const auto descriptor=service->getCharacteristic(uuid(0x2a4b));
    const auto protocol=service->getCharacteristic(uuid(0x2a4e));
    if(descriptor){
      const auto bytes=descriptor->readValue();
      if(bytes.length()>0&&bytes.length()<=2048){map.keys=map.keyboard.parse(bytes.data(),bytes.length());map.pointer=map.mouse.parse(bytes.data(),bytes.length());}
    }
    if(map.keys||map.pointer){
      const uint8_t reportMode=1;
      if(protocol&&!protocol->writeValue(&reportMode,1,protocol->canWrite()))return false;
      Serial.printf("[BLE input] HID service: keyboard=%u relative-pointer=%u\n",map.keys,map.pointer);
      for(auto value:service->getCharacteristics()){
        if(value->getUUID()!=uuid(0x2a4d)||!value->canNotify())continue;
        auto reference=value->getDescriptor(uuid(0x2908));if(!reference)continue;
        const auto ref=reference->readValue();if(ref.length()!=2||ref[1]!=1)continue;
        if(!(map.keys&&map.keyboard.supports(ref[0]))&&!(map.pointer&&map.mouse.supports(ref[0])))continue;
        if(!subscribe(value,mapIndex,ref[0]))return false;
      }
    }else{
      // Explicit boot protocol is the only safe fallback for an unknown map.
      auto keyboard=service->getCharacteristic(uuid(0x2a22));
      if(protocol&&keyboard){
        const uint8_t bootMode=0;if(!protocol->writeValue(&bootMode,1,protocol->canWrite()))return false;
        if(!subscribe(keyboard,mapIndex,0,1))return false;
        auto mouse=service->getCharacteristic(uuid(0x2a33));if(mouse&&!subscribe(mouse,mapIndex,0,2))return false;
        Serial.println("[BLE input] Using boot keyboard/mouse reports.");
      }else Serial.println("[BLE input] Unsupported HID layout (digitizer/consumer/vendor reports are not forwarded).");
    }
    ++mapIndex;
  }
  return _endpointCount!=0&&_client->isConnected();
}
void BleInput::receive(unsigned index,const uint8_t *data,size_t length){
  if(!_accept.load()||index>=_endpointCount)return;
  auto &e=_endpoints[index];uint8_t keys[8]={};MouseReport mouse{};bool gotKeys=false,gotMouse=false,hasButtons=false;
  if(e.boot==1&&length==8){memcpy(keys,data,8);gotKeys=true;}
  else if(e.boot==2&&(length==3||length==4)){mouse={uint8_t(data[0]&7),int8_t(data[1]),int8_t(data[2]),int8_t(length==4?data[3]:0),0};gotMouse=true;}
  else if(!e.boot){
    const auto &map=_maps[e.map];
    if(map.keys)gotKeys=map.keyboard.decode(e.id,data,length,keys);
    if(map.pointer&&length<=64){uint8_t packet[65];size_t prefix=e.id?1:0;packet[0]=e.id;memcpy(packet+prefix,data,length);gotMouse=map.mouse.decode(packet,length+prefix,mouse,hasButtons);}
  }
  if(gotKeys&&_sink.keyboard)_sink.keyboard(_sink.context,index+1,keys,8);
  if(gotMouse&&_sink.mouse){if(e.boot||hasButtons)e.buttons=mouse.buttons;mouse.buttons=e.buttons;_sink.mouse(_sink.context,index+1,mouse);}
  if(gotKeys||gotMouse)++_reports;else ++_unsupported;
}
void BleInput::onDisconnect(NimBLEClient *,int reason){_disconnectReason=reason;const bool wasReady=_ready.load();release();if(wasReady)message("Input disconnected. Use Reconnect saved device to reconnect.");Serial.printf("[BLE input] Disconnected (%d); held input released. Use b reconnect.\n",reason);}
uint32_t BleInput::onPassKeyDisplay(NimBLEConnInfo &){_pairingAction=DisplayPasskey;const uint32_t key=esp_random()%1000000;_pairingCode=key;char text[120];snprintf(text,sizeof(text),"Type %06lu on the WIRELESS keyboard, then press Enter. These keys are for Bluetooth pairing.",(unsigned long)key);message(text);return key;}
void BleInput::onPassKeyEntry(NimBLEConnInfo &info){_pairingAction=EnterPasskey;_pairingIssue=1;Serial.println("[BLE input] Peer-display passkey entry is unsupported; disconnecting.");auto client=NimBLEDevice::getClientByHandle(info.getConnHandle());if(client)client->disconnect();}
void BleInput::onConfirmPasskey(NimBLEConnInfo &info,uint32_t code){
  _pairingAction=ComparePasskey;
  if(_cancelled.load()||!_busy.load()||code>999999){NimBLEDevice::injectConfirmPasskey(info,false);return;}
  _pairingCode=code;_comparison=code+1;
  char text[192];snprintf(text,sizeof(text),"Compare %06lu on both devices. Confirm only if they match. Web: Codes match. Board menu: Y. UART: b confirm %06lu. ESC or Cancel to reject.",(unsigned long)code,(unsigned long)code);message(text);
}
void BleInput::message(const char *text){
  char copy[192]={};snprintf(copy,sizeof(copy),"%s",text);
  portENTER_CRITICAL(&_viewLock);memcpy(_status,copy,sizeof(copy));++_revision;portEXIT_CRITICAL(&_viewLock);
  Serial.printf("[BLE input] %s\n",copy);
}
std::string BleInput::status(){char copy[192];portENTER_CRITICAL(&_viewLock);memcpy(copy,_status,sizeof(copy));portEXIT_CRITICAL(&_viewLock);return copy;}
bool BleInput::confirmPairing(uint32_t attempt,uint32_t code){
  if(attempt!=_attempt.load()||code>999999)return false;
  char commandText[24];snprintf(commandText,sizeof(commandText),"confirm %06lu",(unsigned long)code);
  return command(commandText);
}
void BleInput::appendStatus(JsonObject out){
  char current[56],saved[56];
  portENTER_CRITICAL(&_viewLock);memcpy(current,_currentName,sizeof(current));memcpy(saved,_savedName,sizeof(saved));portEXIT_CRITICAL(&_viewLock);
  out["ready"]=_ready.load();out["saved"]=_hasSaved.load();out["device"]=current;out["saved_name"]=_hasSaved.load()?saved:"";
  const char *operations[]={"idle","scanning","connecting","pairing","checking support","updating"};
  out["state"]=_ready.load()?"connected":_busy.load()?operations[_operation.load()]:_hasSaved.load()?"disconnected":"not paired";
  auto pairing=out["pairing"].to<JsonObject>();
  const auto action=_pairingAction.load();
  pairing["kind"]=action==DisplayPasskey?"display":action==ComparePasskey?"compare":"none";
  pairing["active"]=_busy.load()&&!_cancelled.load()&&_operation.load()==3&&(action==DisplayPasskey||(action==ComparePasskey&&_comparison.load()!=0));
  pairing["id"]=_attempt.load();pairing["code"]=_pairingCode.load();
  const auto elapsed=uint32_t(millis()-_securityStarted.load());
  pairing["seconds_left"]=_operation.load()==3&&elapsed<30000?(30000-elapsed+999)/1000:0;
}
uint32_t BleInput::revision(){portENTER_CRITICAL(&_viewLock);const auto r=_revision;portEXIT_CRITICAL(&_viewLock);return r;}
unsigned BleInput::deviceCount(){portENTER_CRITICAL(&_viewLock);const auto count=_visibleCount;portEXIT_CRITICAL(&_viewLock);return count;}
std::string BleInput::devices(){
  char names[ScanCapacity][56];unsigned count;
  portENTER_CRITICAL(&_viewLock);memcpy(names,_names,sizeof(names));count=_visibleCount;portEXIT_CRITICAL(&_viewLock);
  std::string result;for(unsigned i=0;i<count;++i)result+=std::to_string(i+1)+" "+names[i]+"\n";
  return result.empty()?"No devices found. Put the keyboard in Bluetooth pairing mode and scan again.\n":result;
}
void BleInput::cancel(){
  if(!_busy.load())return;
  _cancelled=true;
  _comparison=0;
  NimBLEDevice::getScan()->stop();
  if(_client)_client->disconnect();
}
#endif
