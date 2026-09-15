#include "WiFiCredentials.h"
#include <cassert>
int main(){
  WiFiCredentials c;assert(!validWiFiCredentials(c));
  std::strcpy(c.ssid,"Home WiFi");assert(validWiFiCredentials(c));
  std::strcpy(c.password,"1234567");assert(!validWiFiCredentials(c));
  std::strcpy(c.password,"12345678");assert(validWiFiCredentials(c));
  c.version=2;assert(!validWiFiCredentials(c));c.version=1;
  std::memset(c.ssid,'s',sizeof(c.ssid));assert(!validWiFiCredentials(c));c.ssid[32]=0;assert(validWiFiCredentials(c));
  std::memset(c.password,'p',sizeof(c.password));assert(!validWiFiCredentials(c));c.password[63]=0;assert(validWiFiCredentials(c));
}
