# HID Switcher CLI

Build with `sh cli/build.sh`. Run `cli/build/hid-switcher --help` for status, slot naming/order, selection, diagnostics, and Wi-Fi commands. Pair the bridge in macOS first. Discovery uses its configuration service UUID. Wi-Fi is off after every boot; `wifi on` enables setup and `wifi off` closes it.

Use `ble-name "Desk Switcher"` to set a persistent Bluetooth name (1–29 UTF-8 bytes) without resetting pairings. The default is HID SWITCHER BLE.
