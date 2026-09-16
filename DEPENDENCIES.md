# Dependencies

- [ESP32_USB_Host_HID](https://github.com/esp32beans/ESP32_USB_Host_HID): vendored USB HID driver. Its [license](lib/ESP32_USB_Host_HID/LICENSE) and [local change record](lib/ESP32_USB_Host_HID/LOCAL-CHANGES.md) remain with the source.
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino): BLE stack and HID service.
- [ArduinoJson](https://github.com/bblanchon/ArduinoJson): structured configuration commands and diagnostics.
- Espressif Arduino core and ESP-IDF, supplied by the pinned pioarduino platform: board runtime, USB host controller, storage, and radio support.

Dependency versions and board settings are in `platformio.ini`. Each dependency retains its own license.
