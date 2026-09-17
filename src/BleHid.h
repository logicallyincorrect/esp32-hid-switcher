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
#include "DeviceName.h"
#include "ShortcutBindings.h"
#include "ReconnectGuard.h"
#include "EdgeSwitch.h"

class BleHid : public NimBLEServerCallbacks, public NimBLECharacteristicCallbacks {
public:
  BleHid() : _router(notifyOne, this) {}
  BleHid(const BleHid &) = delete;
  BleHid &operator=(const BleHid &) = delete;
  void begin(uint8_t slot);
  void loop();
  void flushInput();
  bool edgeMouse(const MouseReport &report,uint32_t received,bool blocked);
  void serviceEdges(bool allowed);

  const String &deviceName() const { return _deviceName; }
  bool setDeviceName(const String &name);
  bool shortcutCommand(JsonVariantConst command,JsonObject result,String &error);
  void appendShortcuts(JsonObject result) const;
  bool captureKeyboard(const uint8_t *keys,uint8_t modifiers){return _shortcutRecorder.keyboard(keys,modifiers);}
  bool captureMouse(uint8_t buttons){return _shortcutRecorder.mouse(buttons);}
  int shortcutAction(const uint8_t *keys,uint8_t modifiers,uint8_t buttons,uint8_t kind=0)const{return matchingShortcut(_shortcuts,keys,modifiers,buttons,kind);}
  void selectSlot(uint8_t slot);
  String forgetComputer(unsigned slot);
  uint32_t menuSession()const{return _connectionRevision;}
  uint32_t menuReportSpacing()const{return (mouseIntervalUs()+999)/1000;}
  bool sendMenuReport(const uint8_t *report){return _router.tryKeyboard(report);}
  int recordState()const{return int(_shortcutRecorder.state);}
  String recordedLabel()const;
  String bindingLabel(unsigned action,unsigned kind)const;
  bool isConnected() { return _router.connected(_router.selected()); }
  void printStatus();
  void appendStatus(JsonObject out);
  uint32_t inputEpoch()const{return _inputEpoch;}
  void sendKeyboardReport(const uint8_t *keys, uint8_t modifiers);
  void sendMouseReport(const MouseReport &report,uint32_t received);
  const SlotConfig &config() const { return _config; }
  bool connected(unsigned slot) const { return _router.connected(slot); }
  unsigned selected() const { return _router.selected(); }
  bool configure(const uint8_t order[3], const char names[3][33], uint32_t generation);
  void releaseAll() { _pendingMouse.clear(); _pendingKeyboard.clear(); _router.releaseAll(); }
private:
  enum Kind : uint8_t { Connected, Authenticated, Disconnected, Subscribed, TimingUpdated, EdgeSample, EdgeSubscription };
  struct Event {
    Kind kind;
    uint16_t handle;
    ble_addr_t identity;
    bool encrypted;
    bool bonded;
    uint16_t subscription;
    uint8_t reportId;
    EdgeProtocol::Sample edge = {};
    uint32_t received = 0;
  };
  struct Peer {
    uint16_t handle = MultiHostRouter::NONE;
    int slot = -1;
    uint8_t pairingSlot = 0;
    bool encrypted = false;
    bool forgetting = false;
    uint8_t subscribed = 0;
    ble_addr_t identity = {};
    ReconnectGuard recovery;
    uint32_t lastSubscriptionCheck=0;
    bool intervalAttempted=false;
    bool edgeSubscribed=false;
    uint32_t edgeSentEpoch=0,edgeSentAt=0;
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
  uint32_t _connectionRevision=0;
  uint32_t _inputEpoch=0;
  uint32_t _failedSince=0,_recoveries=0,_totalTxFailed=0;
  Peer _peers[3];
  ble_addr_t _identities[3] = {};
  bool _assigned[3] = {};
  Preferences _prefs;
  SlotConfig _config;
  String _deviceName;
  ShortcutConfig _shortcuts;
  ShortcutRecorder _shortcutRecorder;
  String _shortcutToken;
  bool saveConfig(const SlotConfig &config);
  void applyIdentities();
  QueueHandle_t _events = nullptr;
  portMUX_TYPE _eventMux = portMUX_INITIALIZER_UNLOCKED;
  bool _eventOverflow = false;
  NimBLEServer *_server = nullptr;
  NimBLEHIDDevice *_hid = nullptr;
  NimBLECharacteristic *_input = nullptr;
  NimBLECharacteristic *_mouse = nullptr;
  MultiHostRouter _router;
  EdgeSwitch _edges;
  uint32_t _edgeConfigGeneration=0;
  NimBLECharacteristic *_edgeSample=nullptr,*_edgeStatus=nullptr;
  void onWrite(NimBLECharacteristic *,NimBLEConnInfo &) override;
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
    if(characteristic==_edgeStatus)enqueue(EdgeSubscription,info,sub);
    else enqueue(Subscribed, info, sub, characteristic==_mouse?2:1);
  }
};
