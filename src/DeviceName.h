#pragma once
#include <stddef.h>
#include <stdint.h>

// Legacy scan responses have 31 bytes, including a two-byte name field header.
constexpr size_t MAX_DEVICE_NAME_BYTES = 29;
inline bool validDeviceName(const char *name, size_t length) {
  if (!name || !length || length > MAX_DEVICE_NAME_BYTES) return false;
  bool visible = false;
  for (size_t i = 0; i < length;) {
    const uint8_t first = uint8_t(name[i++]);
    if (first < 0x20 || first == 0x7f) return false;
    if (first < 0x80) { visible |= first != ' '; continue; }
    unsigned extra; uint32_t value, minimum;
    if (first >= 0xc2 && first <= 0xdf) { extra=1; value=first&0x1f; minimum=0x80; }
    else if (first >= 0xe0 && first <= 0xef) { extra=2; value=first&0x0f; minimum=0x800; }
    else if (first >= 0xf0 && first <= 0xf4) { extra=3; value=first&0x07; minimum=0x10000; }
    else return false;
    if (i + extra > length) return false;
    while (extra--) { const uint8_t byte=uint8_t(name[i++]); if ((byte&0xc0)!=0x80) return false; value=(value<<6)|(byte&0x3f); }
    if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff) || (value >= 0x80 && value <= 0x9f)) return false;
    visible = true;
  }
  return visible;
}
