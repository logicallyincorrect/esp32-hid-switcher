#pragma once
#include "BleInputConfig.h"
#if HID_BLE_INPUT
#include <Arduino.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <atomic>
#include <ArduinoJson.h>
#include "HidKeyboardParser.h"
#include "HidMouseParser.h"
#include "BleScanCache.h"

// One explicitly selected HOGP peripheral. Discovery/security run on a worker,
// never on the forwarding loop. Notifications only decode and copy into its queue.
class BleInput : private NimBLEClientCallbacks {
public:
  struct Sink {
    void *context;
    void (*keyboard)(void *,unsigned,const uint8_t *,size_t);
    void (*mouse)(void *,unsigned,const MouseReport &);
    void (*reset)(void *);
  };
  void begin(Sink sink);
  bool command(const char *line);
  bool busy()const{return _busy.load();}
  void cancel();
  std::string status();
  std::string devices();
  unsigned deviceCount();
  uint32_t revision();
  void appendStatus(JsonObject out);
  uint32_t pairingId()const{return _attempt.load();}
  uint32_t comparison()const{return _comparison.load();}
  bool confirmPairing(uint32_t attempt,uint32_t code);
private:
  struct Target {uint8_t version=1,type=0,address[6]={};};
  struct Request {enum Type {Scan,Connect,Reconnect,Disconnect,Forget,Status} type;unsigned index=0;bool all=false;};
  struct Map {HidKeyboardParser keyboard;HidMouseParser mouse;bool keys=false,pointer=false;} _maps[2];
  struct Endpoint {NimBLERemoteCharacteristic *value=nullptr;uint8_t map=0,id=0,boot=0,buttons=0;} _endpoints[8];
  static constexpr unsigned ScanCapacity=32;
  NimBLEAddress _targets[ScanCapacity],_saved;
  unsigned _targetCount=0,_endpointCount=0;
  std::atomic<bool> _hasSaved{false};
  Sink _sink{};
  QueueHandle_t _commands=nullptr;
  NimBLEClient *_client=nullptr;
  Preferences _prefs;
  std::atomic<bool> _accept{false},_ready{false};
  std::atomic<bool> _busy{false},_cancelled{false};
  std::atomic<int> _pairingIssue{0},_disconnectReason{0};
  enum PairingAction { NoCallback, DisplayPasskey, EnterPasskey, ComparePasskey };
  std::atomic<PairingAction> _pairingAction{NoCallback};
  // Zero means none; store code + 1 so 000000 remains a valid comparison.
  std::atomic<uint32_t> _comparison{0};
  std::atomic<uint32_t> _attempt{0},_pairingCode{0},_securityStarted{0};
  std::atomic<unsigned> _operation{0}; // 0 idle, 1 scan, 2 connect, 3 pairing, 4 discovery, 5 management
  BleScanCache _scanCache;
  std::atomic<bool> _collectScan{false};
  portMUX_TYPE _scanLock=portMUX_INITIALIZER_UNLOCKED;
  std::atomic<int> _securityStatus{-1};
  static int securityEvent(ble_gap_event *event,void *context);
  portMUX_TYPE _viewLock=portMUX_INITIALIZER_UNLOCKED;
  char _status[192]="Not connected. Scan to add a BLE keyboard or keyboard/trackpad.";
  char _currentName[56]={},_savedName[56]={};
  char _names[ScanCapacity][56]={};unsigned _visibleCount=0;uint32_t _revision=0;
  void message(const char *text);
  std::atomic<uint32_t> _reports{0},_unsupported{0};
  static void task(void *context);
  void run();
  void scan(bool all=false);
  void connect(const NimBLEAddress &address);
  bool discover();
  bool subscribe(NimBLERemoteCharacteristic *value,unsigned map,uint8_t id,uint8_t boot=0);
  void receive(unsigned endpoint,const uint8_t *data,size_t length);
  void release();
  void onDisconnect(NimBLEClient *,int reason)override;
  uint32_t onPassKeyDisplay(NimBLEConnInfo &)override;
  void onPassKeyEntry(NimBLEConnInfo &info)override;
  void onConfirmPasskey(NimBLEConnInfo &info,uint32_t)override;
};
#endif
