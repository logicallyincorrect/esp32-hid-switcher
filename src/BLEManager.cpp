#include "BLEManager.h"
#include "Config.h"
#include "ControllerDiagnostics.h"
#include <nimble/nimble/host/include/host/ble_gatt.h>
#include <nimble/nimble/host/include/host/ble_l2cap.h>
#include <nimble/nimble/host/include/host/ble_hs_mbuf.h>
#include <nimble/nimble/host/src/ble_att_priv.h>

static_assert(MYNEWT_VAL(BLE_SM_MAX_PROCS)>=3,"Need one security procedure per host");
static_assert(MYNEWT_VAL(BLE_STORE_MAX_CCCDS)>=12,"Need persistent subscriptions for all hosts");

// One keyboard report ID, six keys plus modifiers. No shared live report value.
static uint8_t reportMap[] = {
  0x05,0x01, 0x09,0x06, 0xa1,0x01, 0x85,0x01,
  0x05,0x07, 0x19,0xe0, 0x29,0xe7, 0x15,0x00, 0x25,0x01,
  0x75,0x01, 0x95,0x08, 0x81,0x02,
  0x95,0x01, 0x75,0x08, 0x81,0x01,
  0x95,0x05, 0x75,0x01, 0x05,0x08, 0x19,0x01, 0x29,0x05, 0x91,0x02,
  0x95,0x01, 0x75,0x03, 0x91,0x01,
  0x95,0x06, 0x75,0x08, 0x15,0x00, 0x26,0xff,0x00,
  0x05,0x07, 0x19,0x00, 0x2a,0xff,0x00, 0x81,0x00, 0xc0,
  // Mouse: 8 buttons, signed 16-bit relative X/Y, wheel and horizontal pan.
  0x05,0x01,0x09,0x02,0xa1,0x01,0x85,0x02,0x09,0x01,0xa1,0x00,
  0x05,0x09,0x19,0x01,0x29,0x08,0x15,0x00,0x25,0x01,0x95,0x08,0x75,0x01,0x81,0x02,
  0x05,0x01,0x09,0x30,0x09,0x31,0x16,0x01,0x80,0x26,0xff,0x7f,0x75,0x10,0x95,0x02,0x81,0x06,
  0x09,0x38,0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x01,0x81,0x06,
  0x05,0x0c,0x0a,0x38,0x02,0x95,0x01,0x81,0x06,0xc0,0xc0
};

void BLEManager::begin(uint8_t slot) {
  _configRequests=xQueueCreate(4,sizeof(ConfigRequest));assert(_configRequests);
  _events = xQueueCreate(32, sizeof(Event));
  assert(_events);
  if(!_prefs.begin("multi-host",false)){Serial.println("[Config] Cannot open persistent settings");abort();}
  if (_prefs.isKey("config") && _prefs.getBytesLength("config")==sizeof(_config)) {
    _prefs.getBytes("config",&_config,sizeof(_config));
    if(_config.version!=1||_config.selected>=3){Serial.println("[Config] Invalid persistent settings");abort();}
  } else {
    // One-time import of the working keyboard-only slot assignments.
    _config.selected=slot<3?slot:0;
    for(int i=0;i<3;++i) {
      snprintf(_config.slots[i].name,33,"Computer %d",i+1);
      char key[8];snprintf(key,sizeof(key),"peer%d",i);uint8_t stored[7];
      if(_prefs.isKey(key)&&_prefs.getBytesLength(key)==7&&_prefs.getBytes(key,stored,7)==7) {
        _config.slots[i].assigned=1;_config.slots[i].type=stored[0];memcpy(_config.slots[i].address,stored+1,6);
      }
    }
    if(!saveConfig(_config)){Serial.println("[Config] Cannot save slot assignments");abort();}
  }
  // An interrupted timing probe stays disabled across reboot; normal pairing
  // and input still work. Do not repeatedly trigger a controller fault.
  _intervalTuningDisabled=_prefs.getBool("interval-probe",false);
  if(_prefs.getBytesLength("shortcuts")==sizeof(_shortcuts)){
    ShortcutConfig saved;
    if(_prefs.getBytes("shortcuts",&saved,sizeof(saved))==sizeof(saved)&&validShortcuts(saved))_shortcuts=saved;
  }
  applyIdentities();
  _router.select(_config.selected);
  _deviceName=_prefs.getString("ble-name",DEVICE_NAME);
  if(!validDeviceName(_deviceName.c_str(),_deviceName.length()))_deviceName=DEVICE_NAME;
  NimBLEDevice::init(_deviceName.c_str());
  Serial.printf("[BLE diagnostic] restored bonds=%u\n",NimBLEDevice::getNumBonds());
  // The bridge has no display or passkey entry UI. Use encrypted bonded pairing.
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(true, false, true);
  _server = NimBLEDevice::createServer();
  _server->setCallbacks(this, false);
  _server->advertiseOnDisconnect(false);
  _hid = new NimBLEHIDDevice(_server);
  _hid->setManufacturer(DEVICE_MANUFACTURER);
  _hid->setPnp(0x02, 0x303a, 0x0001, 0x0100);
  _hid->setHidInfo(0, 1);
  _hid->setReportMap(reportMap, sizeof(reportMap));
  _input = _hid->getInputReport(1);
  _input->setCallbacks(this);
  const uint8_t empty[8] = {};
  _input->setValue(empty, sizeof(empty));
  _hid->getOutputReport(1)->setValue(uint8_t(0));
  _mouse=_hid->getInputReport(2);
  _mouse->setCallbacks(this);
  _mouse->setValue(empty,7);
  auto configService=_server->createService("4d4c0001-8a15-4b4e-9d84-891537e66000");
  _configStatus=configService->createCharacteristic("4d4c0002-8a15-4b4e-9d84-891537e66000",NIMBLE_PROPERTY::READ|NIMBLE_PROPERTY::READ_ENC,512);
  _configCommand=configService->createCharacteristic("4d4c0003-8a15-4b4e-9d84-891537e66000",NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_ENC,191);
  _configCommand->setCallbacks(this);
  _configReply=configService->createCharacteristic("4d4c0004-8a15-4b4e-9d84-891537e66000",NIMBLE_PROPERTY::READ|NIMBLE_PROPERTY::READ_ENC,512);
  _configReply->setValue("{}");
  updateConfigStatus();
  _server->start();
  // Standard service-changed indication lets bonded hosts refresh their cache.
  if(!_prefs.getBool("cli-gatt2",false)){_server->sendServiceChangedIndication();_prefs.putBool("cli-gatt2",true);}

  ControllerDiagnostics::reports(_input->getHandle(),_mouse->getHandle());
  // Existing hosts cached the keyboard-only GATT table. Queue the standard
  // Service Changed indication for bonded peers before they reconnect.
  if (!_prefs.getBool("mouse-gatt", false)) {
    _server->sendServiceChangedIndication();
    _prefs.putBool("mouse-gatt", true);
  }
  auto adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(HID_KEYBOARD);
  adv->addServiceUUID(_hid->getHidService()->getUUID());
  adv->addServiceUUID(configService->getUUID());
  adv->enableScanResponse(true);
  adv->setName(_deviceName.c_str());
  adv->start();
  Serial.printf("[BLE] %s: up to 3 simultaneous connections; no reboot switching\n",_deviceName.c_str());
  printStatus();
}

void BLEManager::enqueue(Kind kind, const NimBLEConnInfo &info, uint16_t sub, uint8_t reportId) {
  Event event{kind, info.getConnHandle(), *info.getIdAddress().getBase(),
              info.isEncrypted(), info.isBonded(), sub, reportId};
  if (xQueueSend(_events, &event, 0) != pdTRUE) {
    portENTER_CRITICAL(&_eventMux); _eventOverflow = true; portEXIT_CRITICAL(&_eventMux);
  }
}

BLEManager::Peer *BLEManager::peer(uint16_t handle) {
  for (auto &p : _peers) if (p.handle == handle) return &p;
  return nullptr;
}
int BLEManager::knownSlot(const ble_addr_t &identity) {
  for (int i = 0; i < 3; ++i)
    if (_assigned[i] && _identities[i].type == identity.type &&
        memcmp(_identities[i].val, identity.val, 6) == 0) return i;
  return -1;
}
void BLEManager::updateReady(Peer &p) {
  if (p.slot >= 0) _router.ready(p.slot, p.encrypted, p.subscribed);
}

void BLEManager::handle(const Event &e) {
  if((e.kind==TimingUpdated||e.kind==Disconnected)&&e.handle==_intervalProbeHandle)_intervalProbeComplete=true;
  if(e.kind==Connected||e.kind==Disconnected)_linksSettledSince=0;
  if (e.kind == Disconnected) {
    _router.disconnect(e.handle);
    auto p = peer(e.handle);
    if(p && p->slot==int(selected())){_pendingMouse.clear();_pendingKeyboard.clear();}
    if (p) { Serial.printf("[BLE] Disconnected slot %d\n", p->slot + 1); *p = {}; }
    return;
  }
  // Ignore stale queued events for connections that have already gone away.
  ble_gap_conn_desc live;
  if (ble_gap_conn_find(e.handle, &live) != 0) return;
  auto p = peer(e.handle);
  if (!p) {
    for (auto &candidate : _peers)
      if (candidate.handle == MultiHostRouter::NONE) { p = &candidate; break; }
    if (!p) { _server->disconnect(e.handle); return; }
    *p = {};
    p->handle = e.handle;
    p->recovery.reset(millis());
    p->pairingSlot = _router.selected();
    p->identity = e.identity;
    // Do not replace an occupied slot with a new computer.
    if (knownSlot(e.identity) < 0 && _assigned[p->pairingSlot]) {
      Serial.println("[BLE] Select an unpaired slot before pairing a new computer");
      _server->disconnect(e.handle); return;
    }
    Serial.printf("[BLE] Connected handle %u; authenticating\n", e.handle);
    // Auth/subscription restoration may arrive before the Connected callback.
    // Initialize once per handle; never erase an already restored peer.
    // Keep the central's negotiated timing during authentication. Concurrent
    // security and interval updates triggered a controller fault on this S3.
  }
  if (!p) return;
  if (e.kind == Subscribed) {
    uint8_t bit=1u<<(e.reportId-1);
    if(e.subscription&1)p->subscribed|=bit;else p->subscribed&=~bit;
  }
  if (!p->encrypted && (e.kind == Authenticated || (live.sec_state.encrypted && live.sec_state.bonded))) {
    if (!live.sec_state.encrypted || !live.sec_state.bonded) { _server->disconnect(e.handle); return; }
    const ble_addr_t identity=live.peer_id_addr;
    int slot = knownSlot(identity);
    if (slot < 0) {
      slot = p->pairingSlot;
      if (_assigned[slot]) { _server->disconnect(e.handle); return; }
      SlotConfig next=_config;
      next.slots[slot].assigned=1;next.slots[slot].type=identity.type;
      memcpy(next.slots[slot].address,identity.val,6);++next.generation;
      if(!saveConfig(next)) { Serial.println("[BLE] Could not save pairing slot");_server->disconnect(e.handle);return; }
      _config=next;applyIdentities();
      Serial.printf("[BLE] Saved computer to slot %d\n", slot + 1);
    }
    for (auto &other : _peers)
      if (&other != p && other.handle != MultiHostRouter::NONE && other.slot == slot) {
        _server->disconnect(e.handle); return;
      }
    p->slot = slot; p->identity = identity; p->encrypted = true;
    _router.connect(slot, e.handle);
    Serial.printf("[BLE] Authenticated slot %d\n", slot + 1);
  }
  updateReady(*p);
}


void BLEManager::onWrite(NimBLECharacteristic *characteristic,NimBLEConnInfo &info){
  if(characteristic!=_configCommand||!info.isEncrypted()||!info.isBonded())return;
  const auto value=characteristic->getValue();if(value.size()==0||value.size()>=192)return;
  ConfigRequest request{};request.handle=info.getConnHandle();request.identity=*info.getIdAddress().getBase();
  memcpy(request.text,value.data(),value.size());xQueueSend(_configRequests,&request,0);
}
void BLEManager::processConfigCommand(){
  ConfigRequest request;if(xQueueReceive(_configRequests,&request,0)!=pdTRUE)return;
  auto p=peer(request.handle);ble_gap_conn_desc live;
  if(!p||p->slot<0||ble_gap_conn_find(request.handle,&live)!=0||!live.sec_state.encrypted||!live.sec_state.bonded||
     live.peer_id_addr.type!=request.identity.type||memcmp(live.peer_id_addr.val,request.identity.val,6))return;
  JsonDocument command,response;
  if(deserializeJson(command,request.text))return;
  const char *id=command["id"]|"",*op=command["op"]|"";
  if(strlen(id)!=36)return;
  response["id"]=id;bool ok=false;const char *error="Invalid command or arguments";
  const int slot=command["slot"]|0;
  String shortcutError;
  if(!strncmp(op,"shortcut",8)){
    ok=shortcutCommand(command.as<JsonVariantConst>(),response["result"].to<JsonObject>(),shortcutError);
    if(!ok)error=shortcutError.c_str();
  }else if(!strcmp(op,"select")&&command["slot"].is<int>()&&slot>=1&&slot<=3){selectSlot(slot-1);ok=selected()==unsigned(slot-1);}
  else if(!strcmp(op,"name")&&slot>=1&&slot<=3&&command["name"].is<const char *>()){
    const char *name=command["name"];const size_t len=strlen(name);bool valid=len>0&&len<=32;
    for(size_t i=0;i<len;++i)if(uint8_t(name[i])<32||uint8_t(name[i])==127)valid=false;
    if(valid){uint8_t order[]={0,1,2};char names[3][33];for(int i=0;i<3;++i)memcpy(names[i],_config.slots[i].name,33);memset(names[slot-1],0,33);memcpy(names[slot-1],name,len);ok=configure(order,names,_config.generation);}
  }else if(!strcmp(op,"move")&&slot>=1&&slot<=3&&command["to"].is<int>()){
    const int to=command["to"];if(to>=1&&to<=3){uint8_t order[]={0,1,2};const auto item=order[slot-1];if(slot<to)for(int i=slot-1;i<to-1;++i)order[i]=order[i+1];else for(int i=slot-1;i>to-1;--i)order[i]=order[i-1];order[to-1]=item;char names[3][33];for(int i=0;i<3;++i)memcpy(names[i],_config.slots[order[i]].name,33);ok=configure(order,names,_config.generation);}
  }else if(!strcmp(op,"ble-name")&&command["name"].is<const char *>()){
    const String name=command["name"].as<String>();
    ok=setDeviceName(name);if(!ok)error="Name must be 1-29 UTF-8 bytes; settings must be writable and input not in maintenance";
  }else if(!strcmp(op,"wifi")&&command["enabled"].is<bool>()){
    ok=_wifiControl&&_wifiControl(_wifiContext,command["enabled"].as<bool>());if(!ok)error="Wi-Fi control unavailable or firmware update active";
  }else if(!strcmp(op,"diagnostics")){
    ok=true;auto result=response["result"].to<JsonObject>();result["send_errors"]=_totalTxFailed;result["recoveries"]=_recoveries;
    result["mouse_spacing_ms"]=_mouseSubmissionSpacing.samples.mean()/1000.;result["newest_movement_wait_ms"]=_mouseNewestWait.mean()/1000.;result["interval_ms"]=mouseIntervalUs()/1000.;result["wifi_enabled"]=_wifiEnabled;result["wifi_connected"]=_wifiConnected;
  }
  response["ok"]=ok;if(!ok)response["error"]=error;
  String value;serializeJson(response,value);_configReply->setValue(value.c_str());updateConfigStatus();
}
void BLEManager::updateConfigStatus(){
  JsonDocument doc;doc["protocol"]=1;doc["device"]=_deviceName;doc["selected"]=selected()+1;doc["wifi_enabled"]=_wifiEnabled;doc["wifi_connected"]=_wifiConnected;
  auto slots=doc["slots"].to<JsonArray>();for(unsigned i=0;i<3;++i){auto slot=slots.add<JsonObject>();slot["slot"]=i+1;slot["name"]=_config.slots[i].name;slot["assigned"]=bool(_assigned[i]);slot["connected"]=connected(i);}
  String value;serializeJson(doc,value);_configStatus->setValue(value.c_str());
}
void BLEManager::loop() {
  _shortcutRecorder.tick(millis());
  if(millis()-_lastConfigStatus>=1000){_lastConfigStatus=millis();updateConfigStatus();}

  portENTER_CRITICAL(&_eventMux); bool overflow = _eventOverflow; _eventOverflow = false; portEXIT_CRITICAL(&_eventMux);
  if (overflow) {
    Serial.println("[BLE] Event queue overflow; disconnecting to reset routing safely");
    for (auto &p : _peers) { _router.disconnect(p.handle); p = {}; }
    xQueueReset(_events);
    for (auto handle : _server->getPeerDevices()) _server->disconnect(handle);
  }
  Event e;
  while (xQueueReceive(_events, &e, 0) == pdTRUE) handle(e);
  processConfigCommand();
  for(auto &p:_peers)if(p.handle!=MultiHostRouter::NONE){
    ble_gap_conn_desc live;
    if(ble_gap_conn_find(p.handle,&live)!=0)continue;
    // Bonded reconnections can restore encryption without the callback sequence
    // used during first pairing. Treat the host stack's live state as authority.
    if(!p.encrypted&&live.sec_state.encrypted&&live.sec_state.bonded){
      Event restored{Authenticated,p.handle,live.peer_id_addr,true,true,0,1};handle(restored);
    }
    if(p.encrypted&&p.slot>=0&&!(p.subscribed&1)&&millis()-p.lastSubscriptionCheck>=250){
      p.lastSubscriptionCheck=millis();
      for(uint8_t id=1;id<=2;++id){
        // NimBLE registers the automatic CCCD immediately after a notify value.
        // Read this connection's actual CCCD; never invent a subscription.
        auto value=ble_hs_mbuf_att_pkt();if(!value)continue;
        uint8_t bytes[2]={},error=0;
        const uint16_t descriptor=(id==1?_input:_mouse)->getHandle()+1;
        const int rc=ble_att_svr_read_handle(p.handle,descriptor,0,value,&error);
        if(rc==0&&OS_MBUF_PKTLEN(value)==2&&os_mbuf_copydata(value,0,2,bytes)==0){
          const uint8_t bit=1u<<(id-1);if(bytes[0]&1)p.subscribed|=bit;else p.subscribed&=~bit;
        }
        os_mbuf_free_chain(value);
      }
      updateReady(p);
    }
    const auto action=p.recovery.poll(millis(),p.encrypted,(p.subscribed&1)!=0);
    if(action==ReconnectGuard::Action::Secure){
      int rc=0;p.recovery.securityAccepted=NimBLEDevice::startSecurity(p.handle,&rc);
      Serial.printf("[BLE diagnostic] security request handle=%u rc=%d\n",p.handle,rc);
      if(rc!=0&&rc!=BLE_HS_EALREADY)Serial.printf("[BLE] Security pending for handle %u (rc=%d); retry scheduled\n",p.handle,rc);
    }else if(action==ReconnectGuard::Action::Refresh){
      // Ask only the stalled host to rediscover its cached HID services.
      // Never invalidate the healthy hosts' active keyboard connections.
      const ble_uuid16_t service=BLE_UUID16_INIT(0x1801),changed=BLE_UUID16_INIT(0x2a05);
      uint16_t handle=0;
      if(ble_gatts_find_chr(&service.u,&changed.u,nullptr,&handle)==0){
        const uint8_t range[]={1,0,255,255};
        auto data=ble_hs_mbuf_from_flat(range,sizeof(range));
        const int rc=data?ble_gatts_indicate_custom(p.handle,handle,data):BLE_HS_ENOMEM;
        Serial.printf("[BLE] Requested HID service refresh for handle %u (rc=%d)\n",p.handle,rc);
      }
    }else if(action==ReconnectGuard::Action::Disconnect){
      Serial.printf("[BLE] Recovering stalled connection handle %u; keeping saved pairing\n",p.handle);
      _router.disconnect(p.handle);_server->disconnect(p.handle);
    }
  }
  if(_failedSince&&millis()-_failedSince>=5000){
    _failedSince=0;++_recoveries;++_inputEpoch;releaseAll();
    for(auto &p:_peers)if(p.slot==int(selected())){_router.disconnect(p.handle);_server->disconnect(p.handle);}
    Serial.println("[Recovery] Reconnecting selected Bluetooth host after stalled sends");
  }
  _router.service();
  if(!isConnected()){_pendingMouse.clear();_pendingKeyboard.clear();}
  tuneInterval();
  if (millis() - _lastAdvertise >= 500) {
    _lastAdvertise = millis();
    if(_server->getConnectedCount()<3&&!NimBLEDevice::getAdvertising()->isAdvertising())NimBLEDevice::startAdvertising();
  }
}

void BLEManager::flushInput(){
  if(_maintenance)return;
  for(unsigned i=0;i<4 && _pendingKeyboard.peek();++i){
    if(!_router.tryKeyboard(_pendingKeyboard.peek()))break;
    _pendingKeyboard.accepted();
  }
  // Preserve the negotiated cadence across small loop delays. Pending motion
  // coalesces until due; idle restarts and stalls never produce catch-up bursts.
  const uint32_t intervalUs=mouseIntervalUs();
  const uint32_t now=micros();
  MouseReport mouse;
  if(_pendingMouse.peek(mouse) && _mouseCadence.due(now,intervalUs)) {
    const uint8_t bytes[7]={mouse.buttons,uint8_t(mouse.x),uint8_t(uint16_t(mouse.x)>>8),uint8_t(mouse.y),uint8_t(uint16_t(mouse.y)>>8),uint8_t(mouse.wheel),uint8_t(mouse.pan)};
    if(_router.tryMouse(bytes)){
      const uint32_t submitted=micros();
      _mouseSubmissionSpacing.observe(submitted);
      _mouseOldestWait.add(submitted-_pendingMouse.oldest());
      _mouseNewestWait.add(submitted-_pendingMouse.newest());
      _pendingMouse.accepted(mouse);_mouseCadence.accepted(submitted);
    }
  }
}

uint32_t BLEManager::mouseIntervalUs() const {
  for(const auto &p:_peers)if(p.slot==int(selected())&&p.handle!=MultiHostRouter::NONE){
    ble_gap_conn_desc live;
    if(ble_gap_conn_find(p.handle,&live)==0 && live.conn_itvl>=6)return live.conn_itvl*1250u;
  }
  return 15000;
}

void BLEManager::tuneInterval(){
  if(_maintenance||_intervalTuningDisabled)return;
  const uint32_t now=millis();
  // Serialize requests and retain the persistent crash guard while a procedure
  // could still be pending. Only a completed/rejected procedure clears it.
  if(_intervalProbeSince){
    if(now-_intervalProbeSince<10000)return;
    if(!_intervalProbeComplete)return;
    if(_prefs.putBool("interval-probe",false)!=1){_intervalTuningDisabled=true;return;}
    _intervalProbeSince=0;
  }
  bool settled=true;
  for(const auto &p:_peers)if(p.handle!=MultiHostRouter::NONE){
    ble_gap_conn_desc live;
    if(!p.encrypted||p.subscribed!=3||ble_gap_conn_find(p.handle,&live)!=0||!live.sec_state.encrypted)settled=false;
  }
  if(!settled){_linksSettledSince=0;return;}
  if(!_linksSettledSince){_linksSettledSince=now;return;}
  if(now-_linksSettledSince<5000)return;
  for(auto &p:_peers)if(p.slot==int(selected())&&p.handle!=MultiHostRouter::NONE&&!p.intervalAttempted){
    p.intervalAttempted=true;
    ble_gap_conn_desc live;
    if(ble_gap_conn_find(p.handle,&live)!=0||live.conn_itvl<=9)return;
    if(_prefs.putBool("interval-probe",true)!=1){_intervalTuningDisabled=true;return;}
    _intervalProbeSince=now;_intervalProbeHandle=p.handle;_intervalProbeComplete=false;
    ble_gap_upd_params params{};
    params.itvl_min=9;params.itvl_max=9;params.latency=0;params.supervision_timeout=400;
    int rc=ble_gap_update_params(p.handle,&params);
    if(rc==BLE_HS_HCI_ERR(BLE_ERR_INV_HCI_CMD_PARMS)){
      // Standard peripheral L2CAP request lets the central initiate the update
      // when this controller rejects the local HCI procedure.
      ble_l2cap_sig_update_params request{9,9,0,400};
      rc=ble_l2cap_sig_update(p.handle,&request,[](uint16_t handle,int status,void *context){
        if(status==0)return; // Wait for the actual connection-update event.
        auto self=static_cast<BLEManager *>(context);
        Event event{TimingUpdated,handle,{},false,false,0,1};
        if(xQueueSend(self->_events,&event,0)!=pdTRUE){
          portENTER_CRITICAL(&self->_eventMux);self->_eventOverflow=true;portEXIT_CRITICAL(&self->_eventMux);
        }
      },this);
      Serial.printf("[BLE timing] L2CAP request rc=%d\n",rc);
    }
    if(rc!=0)_intervalProbeComplete=true;
    Serial.printf("[BLE timing] slot=%d requested=11.25ms previous=%.2fms rc=%d\n",p.slot+1,live.conn_itvl*1.25,rc);
    return;
  }
}

bool BLEManager::notifyOne(void *context,uint16_t handle,const uint8_t *report,uint8_t id,size_t length) {
  auto self=static_cast<BLEManager *>(context);auto p=self->peer(handle);
  ble_gap_conn_desc live;
  if(id<1||id>2||!p||p->slot<0||!(p->subscribed&(1u<<(id-1)))||!p->encrypted||
     ble_gap_conn_find(handle,&live)!=0||!live.sec_state.encrypted||self->knownSlot(live.peer_id_addr)!=p->slot)return false;
  if(self->_retryAfterUs&&int32_t(micros()-self->_retryAfterUs)<0)return false;
  // Same directed NimBLE operation as Characteristic::notify, with aggregated
  // errors instead of synchronous per-packet logging under backpressure.
  auto data=ble_hs_mbuf_from_flat(report,length);
  const int rc=data?ble_gattc_notify_custom(handle,(id==1?self->_input:self->_mouse)->getHandle(),data):BLE_HS_ENOMEM;
  const bool sent=rc==0;
  if(!sent)self->_retryAfterUs=micros()+8000;else self->_retryAfterUs=0;
  if(sent){++self->_tx[id-1];self->_failedSince=0;}
  else {++self->_txFailed;++self->_totalTxFailed;if(!self->_failedSince)self->_failedSince=millis();}
  return sent;
}
void BLEManager::selectSlot(uint8_t slot) {
  if(slot>=3)return;
  if(slot!=_config.selected) {
    SlotConfig next=_config;next.selected=slot;++next.generation;
    if(!saveConfig(next)){Serial.println("[BLE] Selection could not be saved");return;}
    _config=next;
  }
  if(slot!=selected()){_pendingMouse.clear();_pendingKeyboard.clear();}
  _router.select(slot);
  Serial.printf("[BLE] Selected computer %u (connections kept open)\n",slot+1);
  printStatus();
}
void BLEManager::sendKeyboardReport(const uint8_t *keys, uint8_t modifiers) {
  if(_maintenance)return;
  ++_keyboardIn;
  uint8_t report[8] = {modifiers, 0}; memcpy(report + 2, keys, 6);
  if(!isConnected()){_pendingKeyboard.clear();return;}
  if(!_pendingKeyboard.push(report)){releaseAll();++_inputEpoch;Serial.println("[Input] Keyboard queue overflow; released input");}
}
void BLEManager::printStatus() {
  ControllerDiagnostics::printStatus();
  static uint32_t last=0;
  const uint32_t now=millis();
  _traffic={now-last,_keyboardIn,_mouseIn,_tx[0],_tx[1],_txFailed};
  Serial.printf("[Input timing] window_ms=%lu keyboard_in=%lu mouse_in=%lu keyboard_tx=%lu mouse_tx=%lu tx_failed=%lu\n",
    (unsigned long)(now-last),(unsigned long)_keyboardIn,(unsigned long)_mouseIn,(unsigned long)_tx[0],(unsigned long)_tx[1],(unsigned long)_txFailed);
  last=now;_keyboardIn=_mouseIn=_tx[0]=_tx[1]=_txFailed=0;

  Serial.printf("[Status] Selected %u |", _router.selected() + 1);
  for (unsigned i = 0; i < 3; ++i)
    Serial.printf(" %u:%s", i + 1, _router.connected(i) ? "ready" : (_assigned[i] ? "offline" : "unpaired"));
  Serial.println();
  for (const auto &p : _peers) if (p.handle != MultiHostRouter::NONE) {
    Serial.printf("[BLE diagnostic] handle=%u slot=%d encrypted=%u subscriptions=%u\n",
                  p.handle,p.slot+1,p.encrypted,p.subscribed);
    ble_gap_conn_desc live;
    if(ble_gap_conn_find(p.handle,&live)==0){
      Serial.printf("[BLE diagnostic] interval=%.2fms latency=%u liveEncrypted=%u liveBonded=%u knownSlot=%d\n",live.conn_itvl*1.25,live.conn_latency,live.sec_state.encrypted,live.sec_state.bonded,knownSlot(live.peer_id_addr)+1);
    }
  }
}

void BLEManager::sendMouseReport(const MouseReport &r,uint32_t received) {
  if(_maintenance)return;
  ++_mouseIn;
  _mouseArrivalSpacing.observe(received);
  _mouseBridgeWait.add(micros()-received);
  if(!isConnected()){_pendingMouse.clear();return;}
  if(!_pendingMouse.push(r,received)) {
    releaseAll();
    Serial.println("[Mouse] Button transition queue overflow; released input");
  }
}
static String shortcutLabel(const ShortcutBinding &b){
  String label;
  const char *mods[]={"Ctrl","Shift","Alt","Cmd"};
  auto append=[&label](const String &part){if(label.length())label+=" + ";label+=part;};
  for(unsigned i=0;i<4;++i)if(b.modifiers&(1u<<i))append(mods[i]);
  if(b.kind==2){
    String mouse=(b.buttons&(b.buttons-1))?"Buttons ":"Button ";bool first=true;
    for(unsigned i=0;i<8;++i)if(b.buttons&(1u<<i)){if(!first)mouse+="+";mouse+=String(i+1);first=false;}
    append(mouse);
  }
  else for(auto key:b.keys){
    if(!key)break;
    if(key>=4&&key<=29)append(String(char('A'+key-4)));
    else if(key>=0x1e&&key<=0x27)append(String((key-0x1d)%10));
    else if(key==0x2b)append("Tab");else if(key==0x28)append("Enter");
    else if(key==0x29)append("Esc");else if(key==0x2c)append("Space");
    else {char value[5];snprintf(value,sizeof(value),"0x%02X",key);append(value);}
  }
  return label;
}
void BLEManager::appendShortcuts(JsonObject result)const{
  result["generation"]=_shortcuts.generation;
  auto list=result["bindings"].to<JsonArray>();
  // Compact tuples keep all seven bindings within one 512-byte ATT value.
  for(unsigned i=0;i<ShortcutBindingCount;++i){const auto &b=_shortcuts.bindings[i];auto item=list.add<JsonArray>();
    item.add(i/2);item.add(b.kind);item.add(b.modifiers);auto keys=item.add<JsonArray>();
    for(auto key:b.keys)if(key)keys.add(key);item.add(b.buttons);
  }
}
bool BLEManager::shortcutCommand(JsonVariantConst command,JsonObject result,String &error){
  const char *op=command["op"]|"";
  if(!strcmp(op,"shortcuts")){appendShortcuts(result);return true;}
  if(_maintenance){error="Wait for the firmware update to finish";return false;}
  if(!strcmp(op,"shortcut-record")){
    if(!command["action"].is<unsigned>()||command["action"].as<unsigned>()>3){error="Choose Cycle, Next, Previous, or Slot";return false;}
    if(_shortcutRecorder.active()){error="Another recording is active; cancel it or wait 60 seconds";return false;}
    char token[33];snprintf(token,sizeof(token),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());_shortcutToken=token;
    releaseAll();
    _shortcutRecorder.begin(command["action"],millis(),_shortcuts.generation);
    result["token"]=_shortcutToken;result["state"]="waiting";return true;
  }
  if(!strcmp(op,"shortcuts-reset")){
    if(_shortcutRecorder.active()){error="Cancel the recording before resetting shortcuts";return false;}
    ShortcutConfig next;next.generation=_shortcuts.generation+1;
    if(_prefs.putBytes("shortcuts",&next,sizeof(next))!=sizeof(next)){error="Could not save shortcuts";return false;}
    _shortcuts=next;releaseAll();appendShortcuts(result);return true;
  }
  if(!strcmp(op,"shortcut-clear")){
    if(_shortcutRecorder.active()){error="Cancel the recording before clearing shortcuts";return false;}
    if(!command["action"].is<unsigned>()||command["action"].as<unsigned>()>3||!command["kind"].is<unsigned>()){error="Choose an action and input type";return false;}
    const unsigned action=command["action"],kind=command["kind"];
    if((kind!=1&&kind!=2)||(action==Slot&&kind!=1)){error="Slot supports keyboard only";return false;}
    ShortcutConfig next=_shortcuts;auto &binding=next.bindings[shortcutBindingIndex(action,kind)];binding={};binding.kind=kind;++next.generation;
    if(_prefs.putBytes("shortcuts",&next,sizeof(next))!=sizeof(next)){error="Could not clear shortcut";return false;}
    _shortcuts=next;releaseAll();appendShortcuts(result);return true;
  }
  const String token=command["token"]|"";
  if(!_shortcutToken.length()||token!=_shortcutToken){error="Recording session expired or belongs to another client";return false;}
  if(!strcmp(op,"shortcut-cancel")){_shortcutRecorder.cancel();result["state"]="cancelled";return true;}
  if(!strcmp(op,"shortcut-status")){
    const char *states[]={"idle","waiting","recording","ready","cancelled","expired"};
    result["state"]=states[_shortcutRecorder.state];result["action"]=_shortcutRecorder.target;
    if(_shortcutRecorder.state==ShortcutRecorder::Ready)result["label"]=shortcutLabel(_shortcutRecorder.candidate)+(_shortcutRecorder.target==Slot?" + 1/2/3":"");
    return true;
  }
  if(!strcmp(op,"shortcut-save")){
    if(_shortcutRecorder.state!=ShortcutRecorder::Ready){error="Record and release a shortcut first";return false;}
    if(_shortcuts.generation!=_shortcutRecorder.generation){error="Shortcuts changed; record again";return false;}
    ShortcutConfig next=_shortcuts;next.bindings[shortcutBindingIndex(_shortcutRecorder.target,_shortcutRecorder.candidate.kind)]=_shortcutRecorder.candidate;++next.generation;
    if(!validShortcuts(next)){error="That combination conflicts with another shortcut";return false;}
    if(_prefs.putBytes("shortcuts",&next,sizeof(next))!=sizeof(next)){error="Could not save shortcut";return false;}
    _shortcuts=next;_shortcutRecorder.state=ShortcutRecorder::Idle;
    releaseAll();appendShortcuts(result);return true;
  }
  error="Unknown shortcut command";return false;
}

bool BLEManager::setDeviceName(const String &name) {
  if(_maintenance || !validDeviceName(name.c_str(),name.length()))return false;
  if(name==_deviceName)return true;
  auto adv=NimBLEDevice::getAdvertising();
  // Refresh only the scan response; leave active HID links and service UUIDs intact.
  auto apply=[adv](const String &value){
    NimBLEAdvertisementData scan;
    return scan.setName(value.c_str()) && NimBLEDevice::setDeviceName(value.c_str()) && adv->setScanResponseData(scan);
  };
  if(!apply(name)){apply(_deviceName);return false;}
  if(_prefs.putString("ble-name",name)!=name.length()){apply(_deviceName);return false;}
  _deviceName=name;updateConfigStatus();return true;
}

bool BLEManager::saveConfig(const SlotConfig &config) { return _prefs.putBytes("config",&config,sizeof(config))==sizeof(config); }
void BLEManager::applyIdentities() {
  for(unsigned i=0;i<3;++i){_assigned[i]=_config.slots[i].assigned;_identities[i].type=_config.slots[i].type;memcpy(_identities[i].val,_config.slots[i].address,6);}
}
bool BLEManager::configure(const uint8_t order[3],const char names[3][33],uint32_t generation) {
  if(generation!=_config.generation)return false;
  // Avoid reassignment during an in-progress pairing handshake.
  for(const auto &p:_peers)if(p.handle!=MultiHostRouter::NONE&&p.slot<0)return false;
  SlotConfig next;if(!reorderedConfig(_config,order,names,next)||!saveConfig(next))return false;
  _pendingMouse.clear();_pendingKeyboard.clear();
  _router.reorder(order);_config=next;applyIdentities();
  for(auto &p:_peers)if(p.slot>=0)p.slot=knownSlot(p.identity);
  printStatus();return true;
}

static void timingJson(JsonObject out,const TimingStats &stats){
  out["samples"]=stats.count;out["min_ms"]=stats.count?stats.min/1000.0:0;
  out["mean_ms"]=stats.mean()/1000.0;out["max_ms"]=stats.max/1000.0;
  out["stddev_ms"]=stats.deviation()/1000.0;out["p95_upper_ms"]=stats.count?stats.p95Upper()/1000.0:0;
}
void BLEManager::appendStatus(JsonObject out){
  ControllerDiagnostics::appendStatus(out["controller"].to<JsonObject>());
  auto timing=out["mouse_timing"].to<JsonObject>();
  timing["scope"]="since_restart";timing["idle_cutoff_ms"]=100;
  timing["arrival_idle_gaps"]=_mouseArrivalSpacing.idleGaps;timing["submission_idle_gaps"]=_mouseSubmissionSpacing.idleGaps;
  timingJson(timing["arrival_spacing"].to<JsonObject>(),_mouseArrivalSpacing.samples);
  timingJson(timing["submission_spacing"].to<JsonObject>(),_mouseSubmissionSpacing.samples);
  timingJson(timing["bridge_queue_wait"].to<JsonObject>(),_mouseBridgeWait);
  timingJson(timing["oldest_to_submission"].to<JsonObject>(),_mouseOldestWait);
  timingJson(timing["newest_to_submission"].to<JsonObject>(),_mouseNewestWait);

  auto traffic=out["traffic"].to<JsonObject>();traffic["window_ms"]=_traffic.window;
  traffic["keyboard_in"]=_traffic.keyboardIn;traffic["mouse_in"]=_traffic.mouseIn;traffic["keyboard_tx"]=_traffic.keyboardTx;traffic["mouse_tx"]=_traffic.mouseTx;traffic["failed"]=_traffic.failed;traffic["mouse_cap_hz"]=1000000.0/mouseIntervalUs();traffic["merge_threshold_ms"]=mouseIntervalUs()/1000.0;

  out["interval_tuning_disabled"]=_intervalTuningDisabled;
  out["send_errors"]=_totalTxFailed;out["recoveries"]=_recoveries;out["maintenance"]=_maintenance;
  auto links=out["links"].to<JsonArray>();
  for(const auto &p:_peers)if(p.handle!=MultiHostRouter::NONE){
    ble_gap_conn_desc live;if(ble_gap_conn_find(p.handle,&live)!=0)continue;
    auto link=links.add<JsonObject>();link["slot"]=p.slot;link["encrypted"]=bool(live.sec_state.encrypted);link["keyboard"]=bool(p.subscribed&1);link["mouse"]=bool(p.subscribed&2);link["interval_ms"]=live.conn_itvl*1.25;link["slave_latency"]=live.conn_latency;
  }
}
