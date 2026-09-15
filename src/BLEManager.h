#pragma once
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <Preferences.h>
#include "MultiHostRouter.h"
#include "MouseReport.h"
#include "MousePending.h"
#include "TimingStats.h"
#include "KeyboardPending.h"
#include "MouseCadence.h"
#include <ArduinoJson.h>
#include "SlotConfig.h"
#include "ReconnectGuard.h"

class BLEManager : public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
public:
  BLEManager() : _router(notifyOne, this) {}
  void begin(uint8_t slot);
  void loop();
  void flushInput();
  void setWiFiControl(bool (*fn)(void *,bool),void *context){_wifiControl=fn;_wifiContext=context;}
  void setWiFiState(bool enabled,bool connected){_wifiEnabled=enabled;_wifiConnected=connected;}

  void selectSlot(uint8_t slot);
  bool isConnected() { return _router.connected(_router.selected()); }
  void printStatus();
  void appendStatus(JsonObject out);
  void setMaintenance(bool value){if(value!=_maintenance){++_inputEpoch;if(value)releaseAll();}_maintenance=value;}
  uint32_t inputEpoch()const{return _inputEpoch;}
  void sendKeyboardReport(const uint8_t *keys, uint8_t modifiers);
  void sendMouseReport(const MouseReport &report,uint32_t received);
  const SlotConfig &config() const { return _config; }
  bool connected(unsigned slot) const { return _router.connected(slot); }
  unsigned selected() const { return _router.selected(); }
  bool configure(const uint8_t order[3], const char names[3][33], uint32_t generation);
  void releaseAll() { _pendingMouse.clear(); _pendingKeyboard.clear(); _router.releaseAll(); }
private:
  enum Kind : uint8_t { Connected, Authenticated, Disconnected, Subscribed, TimingUpdated };
  struct Event {
    Kind kind;
    uint16_t handle;
    ble_addr_t identity;
    bool encrypted;
    bool bonded;
    uint16_t subscription;
    uint8_t reportId;
  };
  struct Peer {
    uint16_t handle = MultiHostRouter::NONE;
    int slot = -1;
    uint8_t pairingSlot = 0;
    bool encrypted = false;
    uint8_t subscribed = 0;
    ble_addr_t identity = {};
    ReconnectGuard recovery;
    uint32_t lastSubscriptionCheck=0;
    bool intervalAttempted=false;
  };
  MousePending _pendingMouse;
  ReportSpacing _mouseArrivalSpacing,_mouseSubmissionSpacing;
  TimingStats _mouseBridgeWait,_mouseOldestWait,_mouseNewestWait;
  KeyboardPending _pendingKeyboard;
  uint32_t _retryAfterUs=0;
  struct Traffic {uint32_t window=0,keyboardIn=0,mouseIn=0,keyboardTx=0,mouseTx=0,failed=0;} _traffic;
  MouseCadence _mouseCadence;
  uint32_t _linksSettledSince=0,_intervalProbeSince=0;
  bool _intervalTuningDisabled=false,_intervalProbeComplete=false;
  uint16_t _intervalProbeHandle=MultiHostRouter::NONE;
  uint32_t mouseIntervalUs() const;
  void tuneInterval();
  uint32_t _keyboardIn=0,_mouseIn=0,_tx[2]={},_txFailed=0;
  bool _maintenance=false;
  uint32_t _inputEpoch=0;
  uint32_t _failedSince=0,_recoveries=0,_totalTxFailed=0;
  Peer _peers[3];
  ble_addr_t _identities[3] = {};
  bool _assigned[3] = {};
  Preferences _prefs;
  SlotConfig _config;
  bool saveConfig(const SlotConfig &config);
  void applyIdentities();
  QueueHandle_t _events = nullptr;
  portMUX_TYPE _eventMux = portMUX_INITIALIZER_UNLOCKED;
  bool _eventOverflow = false;
  NimBLEServer *_server = nullptr;
  NimBLEHIDDevice *_hid = nullptr;
  NimBLECharacteristic *_input = nullptr;
  NimBLECharacteristic *_mouse = nullptr;
  NimBLECharacteristic *_configStatus = nullptr,*_configCommand=nullptr,*_configReply=nullptr;
  struct ConfigRequest {uint16_t handle;ble_addr_t identity;char text[192];};
  QueueHandle_t _configRequests=nullptr;
  bool (*_wifiControl)(void *,bool)=nullptr;void *_wifiContext=nullptr;
  bool _wifiEnabled=false,_wifiConnected=false;
  void processConfigCommand();
  void onWrite(NimBLECharacteristic *,NimBLEConnInfo &) override;

  uint32_t _lastConfigStatus=0;
  void updateConfigStatus();
  MultiHostRouter _router;
  uint32_t _lastAdvertise = 0;
  void enqueue(Kind kind, const NimBLEConnInfo &info, uint16_t sub = 0, uint8_t reportId=1);
  void handle(const Event &event);
  Peer *peer(uint16_t handle);
  int knownSlot(const ble_addr_t &identity);
  void updateReady(Peer &peer);
  static bool notifyOne(void *context, uint16_t handle, const uint8_t *report, uint8_t id, size_t length);
  void onConnParamsUpdate(NimBLEConnInfo &info) override {enqueue(TimingUpdated,info);}
  void onConnect(NimBLEServer *, NimBLEConnInfo &info) override { enqueue(Connected, info); }
  void onDisconnect(NimBLEServer *, NimBLEConnInfo &info, int) override { enqueue(Disconnected, info); }
  void onAuthenticationComplete(NimBLEConnInfo &info) override { enqueue(Authenticated, info); }
  void onSubscribe(NimBLECharacteristic *characteristic, NimBLEConnInfo &info, uint16_t sub) override {
    enqueue(Subscribed, info, sub, characteristic==_mouse?2:1);
  }
};
