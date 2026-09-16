#include "UsbMice.h"
#include <cassert>
int main() {
  const uint8_t descriptor[] = {0x05,1,0x09,2,0xa1,1,0x09,1,0xa1,0,0x05,9,0x19,1,0x29,3,0x15,0,0x25,1,0x95,3,0x75,1,0x81,2,0x95,1,0x75,5,0x81,1,0x05,1,0x09,0x30,0x09,0x31,0x09,0x38,0x15,0x81,0x25,0x7f,0x75,8,0x95,3,0x81,6,0xc0,0xc0};
  HidMouseParser parser; assert(parser.parse(descriptor, sizeof(descriptor)));
  UsbMice<unsigned, 2> mice;
  assert(!mice.attach(0, parser));
  assert(mice.attach(1, parser) && mice.attach(2, parser));
  assert(!mice.attach(1, parser) && !mice.attach(3, parser));
  MouseReport report;
  const uint8_t first[] = {1, 4, 0, 0}, second[] = {2, 0, 5, 0};
  assert(mice.decode(1, first, sizeof(first), report) && report.buttons == 1 && report.x == 4);
  assert(mice.decode(2, second, sizeof(second), report) && report.buttons == 3 && report.y == 5);
  assert(!mice.decode(2, second, 1, report)); // Truncation must not change held buttons.
  assert(!mice.detach(3, report));
  assert(mice.detach(1, report) && report.buttons == 2 && report.x == 0 && report.y == 0);
  assert(!mice.decode(1, first, sizeof(first), report));
  assert(mice.attach(3, parser));
  const uint8_t empty[] = {0, 0, 0, 0};
  assert(mice.decode(3, empty, sizeof(empty), report) && report.buttons == 2);
  assert(mice.detach(2, report) && report.buttons == 0);
  assert(mice.detach(3, report) && report.buttons == 0);
}
