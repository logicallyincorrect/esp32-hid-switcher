#pragma once
#include <cstdint>

namespace Board {
inline constexpr int setupButton = 0;
inline constexpr int slotLed = 2;
inline constexpr int rgbLed = 48;
inline constexpr unsigned slots = 3;
inline constexpr uint32_t serialBaud = 115200;
inline constexpr char defaultName[] = "HID SWITCHER BLE";
// Preserve the existing GATT identity for paired hosts.
inline constexpr char manufacturer[] = "ESP32 USB HID Bridge";
}
