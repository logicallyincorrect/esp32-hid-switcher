#pragma once
#include <WebServer.h>
#include <DNSServer.h>
#include "BLEManager.h"
class SetupServer {
public:
  explicit SetupServer(BLEManager &ble):_ble(ble),_server(80){}
  void begin();
  void loop();
  void toggle();
  void radioComparison();
private:
  BLEManager &_ble;
  WebServer _server;
  DNSServer _dns;
  bool _active=false,_ap=false,_handled=false,_joining=false,_online=false,_mdns=false;
  uint32_t _pressed=0,_joinAt=0,_lostAt=0,_closeApAt=0;
  String _token,_ssid,_password,_pendingSsid,_pendingPassword,_networkError;
  bool _pending=false,_radioPaused=false;
  uint32_t _radioPauseAt=0;
  void state();
  void startPortal();
  void stopPortal();
  void joinNetwork();
  bool authorize();
  void error(int status,const char *message);
};
