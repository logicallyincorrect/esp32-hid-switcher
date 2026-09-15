#pragma once
#include <cstring>
#include <cstdint>
struct WiFiCredentials {
  uint32_t version=1;
  char ssid[33]={};
  char password[64]={};
};
inline bool validWiFiCredentials(const WiFiCredentials &c){
  if(c.version!=1||!memchr(c.ssid,0,sizeof(c.ssid))||!memchr(c.password,0,sizeof(c.password)))return false;
  const auto ssid=std::strlen(c.ssid),password=std::strlen(c.password);
  return ssid>0&&ssid<=32&&(password==0||(password>=8&&password<=63));
}
