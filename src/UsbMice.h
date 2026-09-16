#pragma once
#include "HidMouseParser.h"

// Caller serializes access. Handles are opaque identities, never dereferenced.
template<class Handle, unsigned Capacity = 4>
class UsbMice {
  struct Entry {
    Handle handle = {};
    HidMouseParser parser;
    uint8_t buttons = 0;
  };
  Entry _entries[Capacity];
  uint8_t buttons() const {
    uint8_t result = 0;
    for (const auto &entry : _entries) result |= entry.buttons;
    return result;
  }
public:
  bool attach(Handle handle, const HidMouseParser &parser) {
    if (!handle) return false;
    for (const auto &entry : _entries) if (entry.handle == handle) return false;
    for (auto &entry : _entries) if (!entry.handle) {
      entry.parser = parser; entry.buttons = 0; entry.handle = handle;
      return true;
    }
    return false;
  }
  bool detach(Handle handle, MouseReport &release) {
    if (!handle) return false;
    for (auto &entry : _entries) if (entry.handle == handle) {
      entry.handle = {}; entry.buttons = 0;
      release = {}; release.buttons = buttons();
      return true;
    }
    return false;
  }
  bool decode(Handle handle, const uint8_t *bytes, size_t length, MouseReport &report) {
    if (!handle) return false;
    for (auto &entry : _entries) if (entry.handle == handle) {
      bool hasButtons = false;
      if (!entry.parser.decode(bytes, length, report, hasButtons)) return false;
      if (hasButtons) entry.buttons = report.buttons;
      report.buttons = buttons();
      return true;
    }
    return false;
  }
};
