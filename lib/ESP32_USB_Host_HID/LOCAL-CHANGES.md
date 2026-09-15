Source: https://github.com/esp32beans/ESP32_USB_Host_HID at e5cc022.

Vendored source and headers retain upstream LICENSE. Local change: resolve completed interrupt transfers using both parent device identity and endpoint address. USB endpoint addresses are device-local; matching endpoint alone selected another device behind a hub and triggered in_xfer_done assertion line 708. The assertion is preserved. Regression test executes the actual lookup function with colliding endpoints on different devices.
