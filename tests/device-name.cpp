#include "DeviceName.h"
#include <cassert>
#include <string>
int main() {
  auto valid=[](const std::string &s){return validDeviceName(s.data(),s.size());};
  assert(valid("HID SWITCHER BLE"));assert(valid("Desk 🎹"));
  assert(valid(std::string(29,'x')));assert(!valid(std::string(30,'x')));
  assert(!valid(""));assert(!valid("   "));assert(!valid("Desk\n"));
  assert(!valid(std::string("a\0b",3)));
  assert(!valid("\xc0\xaf"));assert(!valid("\xed\xa0\x80"));
  assert(!valid("\xf4\x90\x80\x80"));assert(!valid("\xf0\x9f"));
  assert(!valid("\xc2\x85"));assert(!valid("\x80"));
  assert(valid(std::string(25,'x')+"🎹"));
  assert(!valid(std::string(26,'x')+"🎹"));
}
