#include "BleHid.h"
#include "BleInputConfig.h"
#include "Board.h"
#include "ForgetPairing.h"
#include <nimble/nimble/host/include/host/ble_store.h>
#include "ControllerDiagnostics.h"
#include <nimble/nimble/host/include/host/ble_gatt.h>
#include <nimble/nimble/host/include/host/ble_l2cap.h>
#include <nimble/nimble/host/include/host/ble_hs_mbuf.h>
#include <nimble/nimble/host/src/ble_att_priv.h>

static_assert(MYNEWT_VAL(BLE_SM_MAX_PROCS)>=3,"Need one security procedure per host");
static_assert(MYNEWT_VAL(BLE_STORE_MAX_CCCDS)>=12,"Need persistent subscriptions for all hosts");

#include "HidReportMap.h"

void BleHid::begin(uint8_t slot) {
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
  const size_t layoutSize=_prefs.getBytesLength("monitor-layout");
  if(layoutSize==sizeof(MonitorLayout)||layoutSize==sizeof(MonitorLayout::Legacy)){
    uint8_t bytes[sizeof(MonitorLayout)];
    if(_prefs.getBytes("monitor-layout",bytes,layoutSize)==layoutSize)_absolutePointer.layout.load(bytes,layoutSize);
  }
  _sharedSpeed=_prefs.getUInt("pointer-speed",SharedPointerScale::defaultSpeed);
  if(!SharedPointerScale::valid(_sharedSpeed))_sharedSpeed=SharedPointerScale::defaultSpeed;
  _edges.setThreshold(_prefs.getUInt("edge-distance",EdgeSettings::defaultDistance),millis());
  // An interrupted timing probe stays disabled across reboot; normal pairing
  // and input still work. Do not repeatedly trigger a controller fault.
  _intervalTuningDisabled=_prefs.getBool("interval-probe",false);
  if(_prefs.getBytesLength("shortcuts")==sizeof(_shortcuts)){
    ShortcutConfig saved;
    if(_prefs.getBytes("shortcuts",&saved,sizeof(saved))==sizeof(saved)&&validShortcuts(saved))_shortcuts=saved;
  }
  applyIdentities();
  _seamlessEnabled=_prefs.getBool("seamless",false);
  _router.absoluteOutput(useAbsolutePointer());
  _router.select(_config.selected);
  _absolutePointer.select(_config.selected,millis());
  _deviceName=_prefs.getString("ble-name",Board::defaultName);
  if(!validDeviceName(_deviceName.c_str(),_deviceName.length()))_deviceName=Board::defaultName;
  NimBLEDevice::init(_deviceName.c_str());
  for(unsigned i=0;i<3;++i)if(_config.slots[i].assigned==2)forgetComputer(i);
  Serial.printf("[BLE diagnostic] restored bonds=%u\n",NimBLEDevice::getNumBonds());
  // Destination pairing must also work before an input keyboard is usable.
  // The BLE input worker temporarily enables passkey display for its pairing.
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(true, false, true);
  _server = NimBLEDevice::createServer();
  _server->setCallbacks(this, false);
  _server->advertiseOnDisconnect(false);
  _hid = new NimBLEHIDDevice(_server);
  _hid->setManufacturer(Board::manufacturer);
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
  const uint8_t initialMouse[7]={0,0,uint8_t(PointerMode::absolute?64:0),0,uint8_t(PointerMode::absolute?64:0),0,0};
  _mouse->setValue(initialMouse,PointerMode::absolute?6:7);
#if HID_ABSOLUTE_POINTER && !HID_ABSOLUTE_ONLY_TEST
  {
    // Separate HID service, like separate USB interfaces on NanoKVM. Do not
    // instantiate another NimBLEHIDDevice: Device Information is single-instance.
    auto relativeHid=_server->createService(NimBLEUUID(uint16_t(0x1812)));
    const uint8_t info[4]={0x11,0x01,0,1};
    relativeHid->createCharacteristic(NimBLEUUID(uint16_t(0x2a4a)),NIMBLE_PROPERTY::READ)->setValue(info,sizeof(info));
    relativeHid->createCharacteristic(NimBLEUUID(uint16_t(0x2a4b)),NIMBLE_PROPERTY::READ)->setValue(relativeReportMap,sizeof(relativeReportMap));
    relativeHid->createCharacteristic(NimBLEUUID(uint16_t(0x2a4c)),NIMBLE_PROPERTY::WRITE_NR);
    _relativeMouse=relativeHid->createCharacteristic(NimBLEUUID(uint16_t(0x2a4d)),NIMBLE_PROPERTY::READ|NIMBLE_PROPERTY::READ_ENC|NIMBLE_PROPERTY::NOTIFY);
    const uint8_t reference[2]={3,1};
    _relativeMouse->createDescriptor(NimBLEUUID(uint16_t(0x2908)),NIMBLE_PROPERTY::READ|NIMBLE_PROPERTY::READ_ENC)->setValue(reference,sizeof(reference));
    _relativeMouse->setCallbacks(this);_relativeMouse->setValue(empty,7);
  }
#endif
  auto edgeService=_server->createService(EdgeProtocol::service);
  _edgeSample=edgeService->createCharacteristic(EdgeProtocol::sample,NIMBLE_PROPERTY::WRITE|NIMBLE_PROPERTY::WRITE_ENC,EdgeProtocol::size);
  _edgeStatus=edgeService->createCharacteristic(EdgeProtocol::status,NIMBLE_PROPERTY::NOTIFY);
  _edgeSample->setCallbacks(this);_edgeStatus->setCallbacks(this);
  _edges.epoch=esp_random()|1u;
  _server->start();
  // Invalidate cached report semantics on either build-mode transition.
  if(_prefs.getBool("abs-report",false)!=PointerMode::absolute){
    _server->sendServiceChangedIndication();
    _prefs.putBool("abs-report",PointerMode::absolute);
  }
  const unsigned reportVersion=PointerMode::absoluteOnly?7:(PointerMode::absolute?8:0);
  if(_prefs.getUInt("pointer-map",0)!=reportVersion){
    _server->sendServiceChangedIndication();_prefs.putUInt("pointer-map",reportVersion);
  }
  if(!_prefs.getBool("edge-gatt-v1",false)){
    _server->sendServiceChangedIndication();_prefs.putBool("edge-gatt-v1",true);
  }
  // Remove the former configuration GATT service without deleting saved bonds.
  if(!_prefs.getBool("text-menu-gatt",false)){
    _server->sendServiceChangedIndication();_prefs.putBool("text-menu-gatt",true);
  }

  ControllerDiagnostics::reports(_input->getHandle(),_mouse->getHandle(),_relativeMouse?_relativeMouse->getHandle():0xffff);
  // Existing hosts cached the keyboard-only GATT table. Queue the standard
  // Service Changed indication for bonded peers before they reconnect.
  if (!_prefs.getBool("mouse-gatt", false)) {
    _server->sendServiceChangedIndication();
    _prefs.putBool("mouse-gatt", true);
  }
  auto adv = NimBLEDevice::getAdvertising();
  // Generic appearance avoids advertising a keyboard classification while
  // acting as an input host. HID services still describe computer-facing input.
  adv->setAppearance(0x0000);
  adv->addServiceUUID(_hid->getHidService()->getUUID());
  adv->addServiceUUID(EdgeProtocol::service);
  adv->enableScanResponse(true);
  adv->setName(_deviceName.c_str());
  adv->start();
  Serial.printf("[BLE] %s: up to 3 simultaneous connections; no reboot switching\n",_deviceName.c_str());
  printStatus();
}

void BleHid::enqueue(Kind kind, const NimBLEConnInfo &info, uint16_t sub, uint8_t reportId) {
  // Incoming keyboard links belong to the BLE central, never a computer slot.
  if(!info.isSlave())return;
  Event event{kind, info.getConnHandle(), *info.getIdAddress().getBase(),
              info.isEncrypted(), info.isBonded(), sub, reportId};
  if (xQueueSend(_events, &event, 0) != pdTRUE) {
    portENTER_CRITICAL(&_eventMux); _eventOverflow = true; portEXIT_CRITICAL(&_eventMux);
  }
}

BleHid::Peer *BleHid::peer(uint16_t handle) {
  for (auto &p : _peers) if (p.handle == handle) return &p;
  return nullptr;
}
int BleHid::knownSlot(const ble_addr_t &identity) {
  for (int i = 0; i < 3; ++i)
    if (_config.slots[i].assigned==1 && _identities[i].type == identity.type &&
        memcmp(_identities[i].val, identity.val, 6) == 0) return i;
  return -1;
}
void BleHid::updateReady(Peer &p) {
  if (p.slot >= 0) _router.ready(p.slot, p.encrypted, p.subscribed);
}

void BleHid::handle(const Event &e) {
  if((e.kind==TimingUpdated||e.kind==Disconnected)&&e.handle==_intervalProbeHandle)_intervalProbeComplete=true;
  if(e.kind==Connected||e.kind==Disconnected)_linksSettledSince=0;
  if (e.kind == Disconnected) {
    _edges.reset(millis());
    _router.disconnect(e.handle);
    auto p = peer(e.handle);
    if(p && p->slot==int(selected())){++_connectionRevision;_calibrationPending.clear();_pendingMouse.clear();_pendingKeyboard.clear();}
    if (p) { Serial.printf("[BLE] Disconnected slot %d\n", p->slot + 1); *p = {}; }
    return;
  }
  // Ignore stale queued events for connections that have already gone away.
  ble_gap_conn_desc live;
  if (ble_gap_conn_find(e.handle, &live) != 0 || live.role != BLE_GAP_ROLE_SLAVE) return;
  for(const auto &stored:_config.slots)if(stored.assigned==2&&stored.type==live.peer_id_addr.type&&
    !memcmp(stored.address,live.peer_id_addr.val,6)){_server->disconnect(e.handle);return;}
  auto p = peer(e.handle);
  if(p&&p->forgetting){_server->disconnect(e.handle);return;}
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
  if(e.kind==EdgeSubscription||e.kind==EdgeSample){
    // Match the live bonded identity as well as the handle (handles can be reused).
    if(live.peer_id_addr.type!=e.identity.type||memcmp(live.peer_id_addr.val,e.identity.val,6))return;
    // Remember CCCD restoration before authentication completes. Status delivery
    // and samples still require the live encrypted, bonded connection.
    if(e.kind==EdgeSubscription){p->edgeSubscribed=(e.subscription&1)!=0;p->edgeSentEpoch=0;_edges.reset(millis());}
    else if(live.sec_state.encrypted&&live.sec_state.bonded&&p->encrypted&&p->slot>=0&&p->edgeSubscribed)
      _edges.sample(unsigned(p->slot),e.edge,e.received,millis());
    return;
  }
}


void BleHid::loop() {
  _shortcutRecorder.tick(millis());

  portENTER_CRITICAL(&_eventMux); bool overflow = _eventOverflow; _eventOverflow = false; portEXIT_CRITICAL(&_eventMux);
  if (overflow) {
    ++_connectionRevision;_edges.reset(millis());
    Serial.println("[BLE] Event queue overflow; disconnecting to reset routing safely");
    for (auto &p : _peers) { _router.disconnect(p.handle); p = {}; }
    xQueueReset(_events);
    for (auto handle : _server->getPeerDevices()) _server->disconnect(handle);
  }
  Event e;
  while (xQueueReceive(_events, &e, 0) == pdTRUE) handle(e);
  for(auto &p:_peers)if(p.handle!=MultiHostRouter::NONE&&!p.forgetting){
    ble_gap_conn_desc live;
    if(ble_gap_conn_find(p.handle,&live)!=0)continue;
    // Bonded reconnections can restore encryption without the callback sequence
    // used during first pairing. Treat the host stack's live state as authority.
    if(!p.encrypted&&live.sec_state.encrypted&&live.sec_state.bonded){
      Event restored{Authenticated,p.handle,live.peer_id_addr,true,true,0,1};handle(restored);
    }
    if(p.encrypted&&p.slot>=0&&(p.subscribed&PointerMode::subscriptions)!=PointerMode::subscriptions&&millis()-p.lastSubscriptionCheck>=250){
      p.lastSubscriptionCheck=millis();
      for(uint8_t id=1;id<=(PointerMode::relativeReport?3:2);++id){
        // NimBLE registers the automatic CCCD immediately after a notify value.
        // Read this connection's actual CCCD; never invent a subscription.
        auto value=ble_hs_mbuf_att_pkt();if(!value)continue;
        uint8_t bytes[2]={},error=0;
        const uint16_t descriptor=(id==1?_input:(id==2?_mouse:_relativeMouse))->getHandle()+1;
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
  if(!isConnected()){_calibrationPending.clear();_pendingMouse.clear();_pendingKeyboard.clear();}
  tuneInterval();
  if (millis() - _lastAdvertise >= 500) {
    _lastAdvertise = millis();
    if(_server->getConnectedCount()<3&&!NimBLEDevice::getAdvertising()->isAdvertising())NimBLEDevice::startAdvertising();
  }
}

void BleHid::flushInput(){
  if(calibrationActive()){serviceCalibration();return;}
  for(unsigned i=0;i<4 && _pendingKeyboard.peek();++i){
    if(!_router.tryKeyboard(_pendingKeyboard.peek()))break;
    _pendingKeyboard.accepted();
  }
  // Preserve the negotiated cadence across small loop delays. Pending motion
  // coalesces until due; idle restarts and stalls never produce catch-up bursts.
  const uint32_t intervalUs=mouseIntervalUs();
  const uint32_t now=micros();
  MouseReport mouse;
  const bool relative=PointerMode::absolute&&!useAbsolutePointer();
  auto &queue=relative?_calibrationPending:_pendingMouse;
  if(queue.peek(mouse) && _mouseCadence.due(now,intervalUs)) {
    const uint8_t bytes[7]={uint8_t(PointerMode::absolute&&!relative?(mouse.buttons&0x1f):mouse.buttons),uint8_t(mouse.x),uint8_t(uint16_t(mouse.x)>>8),uint8_t(mouse.y),uint8_t(uint16_t(mouse.y)>>8),uint8_t(mouse.wheel),uint8_t(mouse.pan)};
    // Exactly one mouse path owns the entire gesture, including scrolling.
    const bool sent=relative?_router.tryRelativeMouse(bytes):_router.tryMouse(bytes);
    if(sent){
      const uint32_t submitted=micros();
      _mouseSubmissionSpacing.observe(submitted);
      _mouseOldestWait.add(submitted-queue.oldest());
      _mouseNewestWait.add(submitted-queue.newest());
      queue.accepted(mouse);_mouseCadence.accepted(submitted);
    }
  }
}

uint32_t BleHid::mouseIntervalUs() const {
  for(const auto &p:_peers)if(p.slot==int(selected())&&p.handle!=MultiHostRouter::NONE){
    ble_gap_conn_desc live;
    if(ble_gap_conn_find(p.handle,&live)==0 && live.conn_itvl>=6)return live.conn_itvl*1250u;
  }
  return 15000;
}

void BleHid::tuneInterval(){
  if(_intervalTuningDisabled)return;
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
        auto self=static_cast<BleHid *>(context);
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

bool BleHid::notifyOne(void *context,uint16_t handle,const uint8_t *report,uint8_t id,size_t length) {
  auto self=static_cast<BleHid *>(context);auto p=self->peer(handle);
  ble_gap_conn_desc live;
  if(id<1||id>3||(id==3&&!self->_relativeMouse)||!p||p->slot<0||!(p->subscribed&(1u<<(id-1)))||!p->encrypted||
     ble_gap_conn_find(handle,&live)!=0||!live.sec_state.encrypted||self->knownSlot(live.peer_id_addr)!=p->slot)return false;
  if(self->_retryAfterUs&&int32_t(micros()-self->_retryAfterUs)<0)return false;
  // Same directed NimBLE operation as Characteristic::notify, with aggregated
  // errors instead of synchronous per-packet logging under backpressure.
  // The NanoKVM-format absolute mouse has a six-byte wire report; the router
  // retains its seven-byte internal storage for position-preserving releases.
  if(PointerMode::absolute&&id==2){if(length!=7)return false;length=6;}
  auto data=ble_hs_mbuf_from_flat(report,length);
  const int rc=data?ble_gattc_notify_custom(handle,(id==1?self->_input:(id==2?self->_mouse:self->_relativeMouse))->getHandle(),data):BLE_HS_ENOMEM;
  const bool sent=rc==0;
  if(!sent)self->_retryAfterUs=micros()+8000;else self->_retryAfterUs=0;
  if(sent){++self->_tx[id==1?0:1];self->_failedSince=0;}
  else {++self->_txFailed;++self->_totalTxFailed;if(!self->_failedSince)self->_failedSince=millis();}
  return sent;
}
void BleHid::selectSlot(uint8_t slot) {
  if(slot>=3)return;
  if(slot!=_config.selected) {
    SlotConfig next=_config;next.selected=slot;++next.generation;
    if(!saveConfig(next)){Serial.println("[BLE] Selection could not be saved");return;}
    _config=next;
  }
  if(slot!=selected()){_pendingMouse.clear();_calibrationPending.clear();_pendingKeyboard.clear();}
  _router.select(slot);
  _absolutePointer.select(slot,millis());
  _router.absoluteOutput(useAbsolutePointer()&&!calibrationActive());
  Serial.printf("[BLE] Selected computer %u (connections kept open)\n",slot+1);
  printStatus();
}
void BleHid::sendKeyboardReport(const uint8_t *keys, uint8_t modifiers) {
  ++_keyboardIn;
  uint8_t report[8] = {modifiers, 0}; memcpy(report + 2, keys, 6);
  if(!isConnected()){_pendingKeyboard.clear();return;}
  if(!_pendingKeyboard.push(report)){releaseAll();++_inputEpoch;Serial.println("[Input] Keyboard queue overflow; released input");}
}
void BleHid::printStatus() {
  Serial.printf("[Pointer] mode=%s gain=%d/%d edge-distance=%u\n",PointerMode::absolute?"absolute-experimental":"relative",PointerMode::gainX,PointerMode::gainY,edgeThreshold());
  if(PointerMode::absolute){const auto &t=_pointerTuning[selected()];Serial.printf("[Pointer tuning] slot=%u horizontal=%lu%% vertical=%lu%%\n",selected()+1,(unsigned long)t.horizontal,(unsigned long)t.vertical);}
  Serial.printf("[Seamless] %s; output=%s; calibration=%u\n",_seamlessEnabled?"enabled":"disabled",useAbsolutePointer()?"absolute":"relative",calibrationStage());
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

void BleHid::sendMouseReport(const MouseReport &r,uint32_t received) {
  ++_mouseIn;
  _mouseArrivalSpacing.observe(received);
  _mouseBridgeWait.add(micros()-received);
  if(!isConnected()){_calibrationPending.clear();_pendingMouse.clear();return;}
  auto &queue=PointerMode::absolute&&!useAbsolutePointer()?_calibrationPending:_pendingMouse;
  if(!queue.push(r,received)) {
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
String BleHid::recordedLabel()const{
  return shortcutLabel(_shortcutRecorder.candidate)+(_shortcutRecorder.target==Slot?" + 1/2/3":"");
}
String BleHid::bindingLabel(unsigned action,unsigned kind)const{
  if(action>3||(kind!=1&&kind!=2)||(action==3&&kind==2))return "Not available";
  const auto &binding=_shortcuts.bindings[shortcutBindingIndex(action,kind)];
  if(emptyShortcut(binding))return "Not set";
  return shortcutLabel(binding)+(action==Slot?" + 1/2/3":"");
}
void BleHid::appendShortcuts(JsonObject result)const{
  result["generation"]=_shortcuts.generation;
  auto list=result["bindings"].to<JsonArray>();
  // Compact tuples keep all seven bindings within one 512-byte ATT value.
  for(unsigned i=0;i<ShortcutBindingCount;++i){const auto &b=_shortcuts.bindings[i];auto item=list.add<JsonArray>();
    item.add(i/2);item.add(b.kind);item.add(b.modifiers);auto keys=item.add<JsonArray>();
    for(auto key:b.keys)if(key)keys.add(key);item.add(b.buttons);
  }
}
bool BleHid::shortcutCommand(JsonVariantConst command,JsonObject result,String &error){
  const char *op=command["op"]|"";
  if(!strcmp(op,"shortcuts")){appendShortcuts(result);return true;}
  if(!strcmp(op,"shortcut-record")){
    if(!command["action"].is<unsigned>()||command["action"].as<unsigned>()>3){error="Choose Cycle, Next, Previous, or Slot";return false;}
    if(_shortcutRecorder.active()){error="Another recording is active; cancel it or wait 60 seconds";return false;}
    const unsigned kind=command["kind"]|0u;
    if(kind>2||(command["action"].as<unsigned>()==Slot&&kind==2)){error="Unsupported shortcut input type";return false;}
    char token[33];snprintf(token,sizeof(token),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());_shortcutToken=token;
    releaseAll();
    _shortcutRecorder.begin(command["action"],millis(),_shortcuts.generation,kind);
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

bool BleHid::setDeviceName(const String &name) {
  if(!validDeviceName(name.c_str(),name.length()))return false;
  if(name==_deviceName)return true;
  auto adv=NimBLEDevice::getAdvertising();
  // Refresh only the scan response; leave active HID links and service UUIDs intact.
  auto apply=[adv](const String &value){
    NimBLEAdvertisementData scan;
    return scan.setName(value.c_str()) && NimBLEDevice::setDeviceName(value.c_str()) && adv->setScanResponseData(scan);
  };
  if(!apply(name)){apply(_deviceName);return false;}
  if(_prefs.putString("ble-name",name)!=name.length()){apply(_deviceName);return false;}
  _deviceName=name;return true;
}

String BleHid::forgetComputer(unsigned slot){
  if(slot>=3)return "Choose a slot from 1 to 3.";
  for(const auto &p:_peers)if(p.handle!=MultiHostRouter::NONE&&p.slot<0)return "Wait for pairing to finish, then retry.";
  const auto result=forgetPairing(_config,slot,
    [this](const SlotConfig &next){return saveConfig(next);},
    [this,slot](const SlotConfig::Slot &stored){
      // Block stale queued authentication/subscription events until disconnect.
      for(auto &p:_peers)if(p.slot==int(slot)&&p.handle!=MultiHostRouter::NONE){
        p.forgetting=true;
        if(slot==selected()){releaseAll();++_connectionRevision;}
        _router.disconnect(p.handle);
        if(_server)_server->disconnect(p.handle);
      }
      applyIdentities();
      // NimBLE requires advertising to stop before removing a peer's IRK.
      auto advertising=_server?NimBLEDevice::getAdvertising():nullptr;
      const bool restart=advertising&&advertising->isAdvertising();
      if(restart&&!advertising->stop())return false;
      const NimBLEAddress address(stored.address,stored.type);
      const int rc=ble_gap_unpair(address.getBase());
      // A retry after power loss may find the bond already absent. Complete
      // all per-peer records explicitly; unpair alone can hide store errors.
      const bool removed=(rc==0||rc==BLE_HS_ENOENT)&&ble_store_util_delete_peer(address.getBase())==0;
      if(restart)advertising->start();
      return removed;
    });
  applyIdentities();
  if(result==ForgetResult::Done)return "";
  Serial.printf("[Config] Pairing removal incomplete for slot %u; retry after restart if necessary\n",slot+1);
  return "Could not finish. Retry Forget pairing; a restart also retries pending removal.";
}

bool BleHid::saveConfig(const SlotConfig &config) { return _prefs.putBytes("config",&config,sizeof(config))==sizeof(config); }
void BleHid::applyIdentities() {
  for(unsigned i=0;i<3;++i){_assigned[i]=_config.slots[i].assigned;_identities[i].type=_config.slots[i].type;memcpy(_identities[i].val,_config.slots[i].address,6);}
  loadPointerTuning();
}
bool BleHid::configure(const uint8_t order[3],const char names[3][33],uint32_t generation) {
  if(generation!=_config.generation)return false;
  // Avoid reassignment during an in-progress pairing handshake.
  for(const auto &p:_peers)if(p.handle!=MultiHostRouter::NONE&&p.slot<0)return false;
  SlotConfig next;if(!reorderedConfig(_config,order,names,next)||!saveConfig(next))return false;
  _pendingMouse.clear();_pendingKeyboard.clear();
  _absolutePointer.reorder(order,millis());
  _router.reorder(order);_config=next;applyIdentities();
  _absolutePointer.select(selected(),millis());
  _router.absoluteOutput(useAbsolutePointer());
  for(auto &p:_peers)if(p.slot>=0)p.slot=knownSlot(p.identity);
  printStatus();return true;
}

static void timingJson(JsonObject out,const TimingStats &stats){
  out["samples"]=stats.count;out["min_ms"]=stats.count?stats.min/1000.0:0;
  out["mean_ms"]=stats.mean()/1000.0;out["max_ms"]=stats.max/1000.0;
  out["stddev_ms"]=stats.deviation()/1000.0;out["p95_upper_ms"]=stats.count?stats.p95Upper()/1000.0:0;
}
void BleHid::appendStatus(JsonObject out){
  out["seamless_enabled"]=_seamlessEnabled;
  out["calibration_stage"]=calibrationStage();
  out["pointer_mode"]=PointerMode::absolute?"absolute-experimental":"relative";
  if(PointerMode::absolute){out["pointer_gain_x"]=PointerMode::gainX;out["pointer_gain_y"]=PointerMode::gainY;}
  if(PointerMode::absolute){auto profiles=out["pointer_tuning"].to<JsonArray>();for(const auto &t:_pointerTuning){auto item=profiles.add<JsonObject>();item["horizontal_percent"]=t.horizontal;item["vertical_percent"]=t.vertical;item["span_x"]=t.spanX;item["span_y"]=t.spanY;}}
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
  out["send_errors"]=_totalTxFailed;out["recoveries"]=_recoveries;
  auto links=out["links"].to<JsonArray>();
  for(const auto &p:_peers)if(p.handle!=MultiHostRouter::NONE){
    ble_gap_conn_desc live;if(ble_gap_conn_find(p.handle,&live)!=0)continue;
    auto link=links.add<JsonObject>();link["slot"]=p.slot;link["encrypted"]=bool(live.sec_state.encrypted);link["keyboard"]=bool(p.subscribed&1);link["mouse"]=bool(p.subscribed&2);link["interval_ms"]=live.conn_itvl*1.25;link["slave_latency"]=live.conn_latency;
  }
}


void BleHid::onWrite(NimBLECharacteristic *characteristic,NimBLEConnInfo &info){
  if(characteristic!=_edgeSample||!info.isEncrypted()||!info.isBonded())return;
  const auto value=characteristic->getValue();
  Event event{EdgeSample,info.getConnHandle(),*info.getIdAddress().getBase(),true,true,0,0};
  if(!EdgeProtocol::decode(value.data(),value.size(),event.edge))return;
  event.received=millis();
  if(xQueueSend(_events,&event,0)!=pdTRUE){
    portENTER_CRITICAL(&_eventMux);_eventOverflow=true;portEXIT_CRITICAL(&_eventMux);
  }
}

bool BleHid::edgeMouse(MouseReport &report,uint32_t received,bool blocked){
  if(PointerMode::absolute){
    if(!useAbsolutePointer())return false;
    const auto &screen=_absolutePointer.layout.screens[selected()];
    return _absolutePointer.motion(report,blocked,(micros()-received)/1000,millis(),edgeThreshold(),SharedPointerScale::gain(_sharedSpeed,screen.sensitivity),SharedPointerScale::gain(_sharedSpeed,screen.sensitivity),SharedPointerScale::denominator(screen.width),SharedPointerScale::denominator(screen.height));
  }
  if(blocked){_edges.motion(0,1,millis(),millis());return false;}
  return _edges.motion(report.x,report.buttons,millis()-(micros()-received)/1000,millis());
}

void BleHid::serviceEdges(bool allowed){
  const uint32_t now=millis();uint8_t ready=0;
  if(calibrationActive()||!_seamlessEnabled)allowed=false;
  for(const auto &p:_peers)if(p.slot>=0&&!p.forgetting&&p.encrypted&&
      (p.subscribed&PointerMode::subscriptions)==PointerMode::subscriptions&&connected(unsigned(p.slot)))ready|=1u<<p.slot;
  if(_edgeConfigGeneration!=_config.generation){_edges.reset(now);_absolutePointer.reset(now);_edgeConfigGeneration=_config.generation;}
  if(PointerMode::absolute){
    _absolutePointer.sync(selected(),ready,allowed,now);
    const int destination=_absolutePointer.destination();
    if(destination>=0){
      selectSlot(uint8_t(destination));
      if(selected()==unsigned(destination)){
        const auto entry=_absolutePointer.enter(unsigned(destination),now);
        sendMouseReport(entry,micros());
      }else _absolutePointer.reset(now);
      _edgeConfigGeneration=_config.generation;
    }
    // Disable companion routing while firmware owns the cursor.
    allowed=false;
  }
  _edges.sync(selected(),ready,allowed,now);
  const int destination=_edges.finish(now);
  if(destination>=0){
    selectSlot(uint8_t(destination));
    _edges.sync(selected(),ready,allowed,now);
    _edgeConfigGeneration=_config.generation;
  }
  for(auto &p:_peers){
    if(p.slot<0||p.forgetting||!p.edgeSubscribed||!p.encrypted)continue;
    if(p.edgeSentEpoch==_edges.epoch&&now-p.edgeSentAt<500)continue;
    ble_gap_conn_desc live;
    if(ble_gap_conn_find(p.handle,&live)||!live.sec_state.encrypted||!live.sec_state.bonded)continue;
    uint8_t packet[EdgeProtocol::size];_edges.packet(unsigned(p.slot),packet);
    if(_edgeStatus->notify(packet,sizeof(packet),p.handle)){p.edgeSentEpoch=_edges.epoch;p.edgeSentAt=now;}
  }
}

bool BleHid::setEdgeThreshold(unsigned value){
  if(!EdgeSettings::valid(value))return false;
  if(value==_edges.threshold())return true;
  if(_prefs.putUInt("edge-distance",value)!=sizeof(uint32_t))return false;
  _absolutePointer.reset(millis());
  return _edges.setThreshold(value,millis());
}

// Key by bonded identity so tuning follows a computer when slots are reordered.
bool BleHid::pointerKey(unsigned slot,char key[16])const{
  if(slot>=3||_config.slots[slot].assigned!=1)return false;
  const auto &s=_config.slots[slot];
  snprintf(key,16,"p%02x%02x%02x%02x%02x%02x%02x",s.type,s.address[0],s.address[1],s.address[2],s.address[3],s.address[4],s.address[5]);
  return true;
}
void BleHid::loadPointerTuning(){
  for(unsigned slot=0;slot<3;++slot){
    _pointerTuning[slot]={};char key[16];PointerTuning saved;
    if(pointerKey(slot,key)&&_prefs.isKey(key)&&_prefs.getBytesLength(key)==sizeof(saved)&&
      _prefs.getBytes(key,&saved,sizeof(saved))==sizeof(saved)&&saved.valid())_pointerTuning[slot]=saved;
    // Preserve earlier manual tuning without altering the saved slot layout.
    else if(pointerKey(slot,key)&&_prefs.isKey(key)&&_prefs.getBytesLength(key)==3*sizeof(uint32_t)){
      uint32_t old[3];
      if(_prefs.getBytes(key,old,sizeof(old))==sizeof(old)&&old[0]==1&&PointerTuning::validValue(0,old[1])&&PointerTuning::validValue(1,old[2])){
        _pointerTuning[slot].horizontal=old[1];_pointerTuning[slot].vertical=old[2];
      }
    }
  }
}
bool BleHid::setPointerValue(unsigned slot,unsigned field,unsigned value){
  char key[16];
  if(!PointerMode::absolute||!PointerTuning::validValue(field,value)||!pointerKey(slot,key))return false;
  auto next=_pointerTuning[slot];next.set(field,value);
  if(_prefs.putBytes(key,&next,sizeof(next))!=sizeof(next))return false;
  _pointerTuning[slot]=next;
  if(slot==selected())releaseAll();
  return true;
}

uint8_t BleHid::calibrationReady()const{
  uint8_t mask=0;
  if(!PointerMode::relativeReport)return mask;
  for(const auto &p:_peers)if(p.slot>=0&&!p.forgetting&&p.encrypted&&(p.subscribed&7)==7&&connected(p.slot))mask|=1u<<p.slot;
  return mask;
}
bool BleHid::beginCalibration(uint8_t mask,bool enableAfter,bool textFeedback){
  if(calibrationActive()||!mask||(mask&~calibrationReady()))return false;
  const unsigned original=selected();const int first=PointerCalibration::first(mask);
  releaseAll();selectSlot(unsigned(first));
  if(selected()!=unsigned(first))return false;
  _router.absoluteOutput(false);
  _calibrationMenuReturn=false;_calibrationTextFeedback=textFeedback;_calibrationFailed=false;_calibrationComplete=false;
  _enableAfterCalibration=enableAfter;_calibrationRequested=mask;_calibrationSaved=0;_calibrationEnableFailed=false;_calibrationFeedback.clear();
  _calibrationPending.clear();_calibration.begin(mask,original,millis());
  _calibrationSession=_connectionRevision;_calibrationLastSend=millis();_calibrationFeedbackAt=0;
  ++_inputEpoch;
  Serial.printf("[Calibration] Slot %u: top-left, click and release left button\n",selected()+1);
  return true;
}
void BleHid::cancelCalibration(bool failed,bool complete){
  if(!calibrationActive())return;
  const unsigned original=_calibration.origin;
  _calibrationFeedback.clear();_calibration.cancel();_enableAfterCalibration=false;_calibrationPending.clear();releaseAll();
  selectSlot(original);_router.absoluteOutput(useAbsolutePointer());++_inputEpoch;
  _calibrationMenuReturn=_calibrationTextFeedback&&complete&&selected()==original&&isConnected();
  _calibrationMenuSession=_connectionRevision;
  _calibrationFailed=failed;_calibrationComplete=complete;_calibrationFeedbackAt=millis();
  Serial.println(failed?"[Calibration] Failed; current slot unchanged. Returning to original slot.":"[Calibration] Ended; returning to original slot.");
}
void BleHid::calibrationMouse(const MouseReport &report,uint32_t received){
  if(!calibrationActive()||_calibrationFeedback.active())return;
  if(micros()-received>100000){cancelCalibration(true);return;}
  if(!_calibration.motion(report,millis()))return;
  // Marking clicks, wheel events and all other mouse buttons stay local.
  const MouseReport relative{0,report.x,report.y,0,0};
  if(!_calibrationPending.push(relative,received))cancelCalibration(true);
}
bool BleHid::saveCalibration(){
  if(!_calibration.valid())return false;
  char key[16];if(!pointerKey(selected(),key))return false;
  PointerTuning next;
  next.spanX=uint32_t(_calibration.x);next.spanY=uint32_t(_calibration.y);
  if(_prefs.putBytes(key,&next,sizeof(next))!=sizeof(next))return false;
  _pointerTuning[selected()]=next;
  _absolutePointer.positions[selected()]={32767,32767};
  Serial.printf("[Calibration] Saved slot %u: span=%lu/%lu counts\n",selected()+1,(unsigned long)next.spanX,(unsigned long)next.spanY);
  return true;
}
void BleHid::serviceCalibration(){
  if(_calibrationFeedback.active()){serviceCalibrationFeedback();return;}
  const uint32_t now=millis();
  if(_calibration.expired(now)||selected()!=_calibration.slot||_connectionRevision!=_calibrationSession||!(calibrationReady()&(1u<<selected()))){cancelCalibration(true);return;}
  MouseReport r;
  if(_calibrationPending.peek(r)&&_mouseCadence.due(micros(),mouseIntervalUs())){
    const uint8_t bytes[7]={0,uint8_t(r.x),uint8_t(uint16_t(r.x)>>8),uint8_t(r.y),uint8_t(uint16_t(r.y)>>8),0,0};
    if(_router.tryRelativeMouse(bytes)){
      _calibrationPending.accepted(r);_mouseCadence.accepted(micros());_calibrationLastSend=now;
    }
  }
  const auto previous=_calibration.stage;
  _calibration.settle(!_calibrationPending.peek(r)&&now-_calibrationLastSend>=PointerCalibration::settleTime,now);
  if(previous!=_calibration.stage&&_calibration.stage==PointerCalibration::BottomRight)
    Serial.println("[Calibration] Bottom-right: move smoothly, then click and release left button");
  if(_calibration.stage!=PointerCalibration::Ready)return;
  if(!saveCalibration()){cancelCalibration(true);return;}
  _calibrationSaved|=1u<<_calibration.slot;
  const int next=_calibration.next(calibrationReady());
  String message="\nSlot "+String(_calibration.slot+1)+" calibration OK.\n";
  if(next>=0){
    message+="Next: slot "+String(next+1)+". Move to TOP LEFT, left-click and release; then BOTTOM RIGHT, left-click and release.\n";
  }else{
    const uint8_t skipped=_calibrationRequested&~_calibrationSaved;
    // Optional calibration estimates one shared scale, rather than assigning
    // different movement scales to each screen. Apply only after completion.
    unsigned sum=0,count=0;
    for(unsigned i=0;i<3;++i)if(_calibrationSaved&(1u<<i)){
      const auto &screen=_absolutePointer.layout.screens[i];const auto &t=_pointerTuning[i];
      sum+=SharedPointerScale::estimate(screen.width,screen.height,t.spanX,t.spanY,screen.sensitivity);++count;
    }
    const unsigned speed=count?(sum+count/2)/count:_sharedSpeed;
    if(_prefs.putUInt("pointer-speed",speed)==sizeof(uint32_t)){_sharedSpeed=speed;message+="Shared sensitivity: "+String(speed)+"%.\n";}
    else {_calibrationEnableFailed=true;message+="Could not save shared sensitivity. Previous speed retained.\n";}
    message+="Calibration complete.";
    if(skipped){message+=" Skipped disconnected slots:";for(unsigned slot=0;slot<3;++slot)if(skipped&(1u<<slot))message+=" "+String(slot+1);}
    message+="\n";
    if(_enableAfterCalibration){
      // All requested captures have finished. Commit opt-in before reporting it.
      if(_prefs.putBool("seamless",true)==1){_seamlessEnabled=true;message+="Seamless switching enabled.\n";}
      else {_calibrationEnableFailed=true;message+="Calibration saved, but seamless switching could not be enabled. Retry Enable from setup.\n";}
      _enableAfterCalibration=false;
    }else message+=_seamlessEnabled?"Seamless switching remains enabled.\n":"Seamless switching is off. Enable it from setup when ready.\n";
  }
  if(!calibrationMessage(message,next))cancelCalibration(true);
}
bool BleHid::calibrationMessage(const String &message,int next){
  if(!_calibrationTextFeedback){
    Serial.printf("[Calibration] %s",message.c_str());
    if(next<0){cancelCalibration(_calibrationEnableFailed,true);return true;}
    if(!(calibrationReady()&(1u<<next)))return false;
    releaseAll();selectSlot(unsigned(next));if(selected()!=unsigned(next))return false;
    _calibration.start(unsigned(next),millis());_calibrationSession=_connectionRevision;
    _calibrationPending.clear();_calibrationLastSend=millis();++_inputEpoch;return true;
  }
  _calibrationPending.clear();releaseAll();selectSlot(_calibration.origin);
  if(selected()!=_calibration.origin||!connected(_calibration.origin))return false;
  _calibrationNext=next;++_inputEpoch;
  return _calibrationFeedback.begin(message.c_str(),selected(),_connectionRevision,millis());
}
void BleHid::serviceCalibrationFeedback(){
  const auto result=_calibrationFeedback.tick(millis(),menuReportSpacing(),selected(),_connectionRevision,isConnected(),
    [this](const uint8_t *report){return _router.tryKeyboard(report);});
  if(result==CalibrationFeedback::Waiting)return;
  if(result==CalibrationFeedback::LostHost){cancelCalibration(true);return;}
  if(_calibrationNext<0){cancelCalibration(_calibrationEnableFailed,true);return;}
  // Recheck after the message; never send calibration motion to a lost slot.
  if(!(calibrationReady()&(1u<<_calibrationNext))){cancelCalibration(true);return;}
  selectSlot(unsigned(_calibrationNext));
  if(selected()!=unsigned(_calibrationNext)){cancelCalibration(true);return;}
  _calibration.start(unsigned(_calibrationNext),millis());_calibrationSession=_connectionRevision;
  _calibrationPending.clear();_calibrationLastSend=millis();++_inputEpoch;
  Serial.printf("[Calibration] Slot %u: top-left, click and release left button\n",selected()+1);
}

uint32_t BleHid::calibrationColor(uint32_t now)const{
  if(calibrationActive()){
    if(_calibrationFeedback.active())return 0x001800;
    const unsigned pulses=_calibration.stage==PointerCalibration::TopLeft?1:2;
    return now%1200<pulses*300&&now%300<150?0x001414:0;
  }
  if(_calibrationFeedbackAt&&now-_calibrationFeedbackAt<2000)return _calibrationFailed?0x180000:(_calibrationComplete?0x001800:0x141000);
  return 0xffffffff;
}

uint8_t BleHid::calibrationNeeded()const{
  uint8_t mask=0;
  if(PointerMode::absolute)for(unsigned slot=0;slot<3;++slot)
    if(connected(slot)&&!_pointerTuning[slot].spanX)mask|=1u<<slot;
  return mask;
}
bool BleHid::setSeamlessEnabled(bool enabled){
  if(calibrationActive())return false;
  if(_prefs.putBool("seamless",enabled)!=1)return false;
  releaseAll();_seamlessEnabled=enabled;_router.absoluteOutput(useAbsolutePointer());
  _edges.reset(millis());++_inputEpoch;return true;
}

void BleHid::appendLayout(JsonObject out)const{
  const auto &layout=_absolutePointer.layout;
  out["active"]=bool(layout.active);out["generation"]=layout.generation;
  auto screens=out["screens"].to<JsonArray>();
  for(unsigned i=0;i<3;++i){const auto &s=layout.screens[i];auto item=screens.add<JsonObject>();item["x"]=s.x;item["y"]=s.y;item["width"]=s.width;item["height"]=s.height;item["enabled"]=bool(s.enabled);item["estimated"]=bool(s.estimated);item["sensitivity"]=s.sensitivity;item["span_x"]=_pointerTuning[i].spanX;item["span_y"]=_pointerTuning[i].spanY;}
}
bool BleHid::setLayout(JsonVariantConst request,String &error){
  auto next=_absolutePointer.layout;
  if(!PointerMode::absolute){error="Monitor arrangement requires absolute-pointer firmware";return false;}
  if(!request["generation"].is<uint32_t>()||request["generation"].as<uint32_t>()!=next.generation){error="Arrangement changed. Reload it before saving.";return false;}
  if(!request["active"].is<bool>()){error="Choose an arrangement mode";return false;}
  auto screens=request["screens"].as<JsonArrayConst>();
  if(screens.size()!=3){error="Provide all three slot rectangles";return false;}
  for(unsigned i=0;i<3;++i){auto v=screens[i];
    if(!v["x"].is<int32_t>()||!v["y"].is<int32_t>()||!v["width"].is<uint32_t>()||!v["height"].is<uint32_t>()||!v["enabled"].is<bool>()||!v["estimated"].is<bool>()){error="Invalid monitor dimensions";return false;}
    if(!v["sensitivity"].isNull()&&!v["sensitivity"].is<unsigned>()){error="Screen sensitivity must be 25–400%";return false;}
    const unsigned sensitivity=v["sensitivity"]|next.screens[i].sensitivity;
    if(sensitivity<25||sensitivity>400){error="Screen sensitivity must be 25–400%";return false;}
    next.screens[i]={v["x"],v["y"],v["width"],v["height"],v["enabled"].as<bool>()?1u:0u,v["estimated"].as<bool>()?1u:0u,sensitivity};
  }
  next.active=request["active"].as<bool>();++next.generation;
  if(!next.valid()){error="Monitors must not overlap. Dimensions: 64–16384 pixels; positions: -65536–65536.";return false;}
  if(_prefs.putBytes("monitor-layout",&next,sizeof(next))!=sizeof(next)){error="Could not save arrangement";return false;}
  _absolutePointer.layout=next;_absolutePointer.reset(millis());return true;
}

bool BleHid::setSharedSpeed(unsigned value){
  if(!SharedPointerScale::valid(value)||calibrationActive())return false;
  if(_prefs.putUInt("pointer-speed",value)!=sizeof(uint32_t))return false;
  _sharedSpeed=value;releaseAll();return true;
}
void BleHid::appendPointerPosition(JsonObject out)const{
  out["x"]=_absolutePointer.positions[selected()].x;out["y"]=_absolutePointer.positions[selected()].y;
}
bool BleHid::probePointer(int x,int y){
  if(!useAbsolutePointer()||calibrationActive()||!isConnected()||x<0||x>32767||y<0||y>32767)return false;
  releaseAll();_absolutePointer.positions[selected()]={int16_t(x),int16_t(y)};
  sendMouseReport({0,int16_t(x),int16_t(y),0,0},micros());return true;
}
