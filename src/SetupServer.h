#pragma once
#include <WebServer.h>
#include <DNSServer.h>
#include "BLEManager.h"
#include "FirmwareUpdate.h"
class SetupServer {
public:
  explicit SetupServer(BLEManager &ble):_ble(ble),_server(80){}
  void begin();
  void loop();
  void toggle();
  void radioComparison();
  bool setRadio(bool enabled);
private:
  BLEManager &_ble;
  FirmwareUpdate _firmware;
  bool _uploadAuthorized=false;
  unsigned _uploadFiles=0;
  uint32_t _uploadActivity=0;
  WebServer _server;
  DNSServer _dns;
  bool _active=false,_ap=false,_handled=false,_joining=false,_online=false,_mdns=false;
  uint32_t _pressed=0,_joinAt=0,_lostAt=0,_closeApAt=0;
  String _token,_ssid,_password,_pendingSsid,_pendingPassword,_networkError;
  bool _pending=false,_radioPaused=false,_radioEnabled=false;
  uint32_t _radioPauseAt=0;
  void state();
  void startPortal();
  void stopPortal();
  void joinNetwork();
  bool authorize();
  void error(int status,const char *message);
};
