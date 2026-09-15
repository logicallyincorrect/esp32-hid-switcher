# ESP32-S3 USB to BLE HID Switcher

Connect a USB keyboard and relative mouse to an ESP32-S3, then switch both between up to three Bluetooth computers without rebooting. It includes USB hub support, persistent simultaneous BLE connections, configuration over BLE and Wi-Fi, and diagnostics.

## Hardware and pairing

- Use an ESP32-S3 with native USB OTG. A USB-to-UART chip such as CH340 does not provide USB host support.
- This build targets a Freenove ESP32-S3 with **16 MB flash and 8 MB PSRAM**. Adjust board memory, partition layout, and LED pins before using other hardware.
- Connect a keyboard and mouse through a USB hub to the native USB/OTG port. A powered hub avoids relying on the development board for peripheral power. The separate UART/COM port is used for power, logs, and flashing.
- Select slot 1 and pair **HID SWITCHER BLE** on the first computer. Select slots 2 and 3 before pairing the other computers.

| Shortcut | Action |
| --- | --- |
| Ctrl + Command + 1 / 2 / 3 | Select that slot, including an empty slot for pairing |
| Ctrl + Command + Tab | Cycle forward through connected slots, wrapping and skipping disconnected computers |

Either side's Control and GUI modifier is accepted; GUI is Command on macOS and the Windows/Super key on other keyboards. Release the full shortcut before repeating. With no alternative connected computer, cycling leaves the selection unchanged. Switching releases held keys and mouse buttons; a mouse button held across a switch is suppressed until released. Input for an offline selected computer is discarded.

GPIO2 pulses the selected slot number. RGB48 uses blue/green/purple for slots 1/2/3; steady means keyboard-ready, blinking means offline/unpaired. Pins are configurable in `src/Config.h`.

## Configure over Bluetooth

Wi-Fi is **off after every boot**. The macOS CLI uses an encrypted custom service alongside HID:

```sh
sh cli/build.sh
cli/build/hid-switcher status
cli/build/hid-switcher name 1 "Desktop"
cli/build/hid-switcher move 1 2
cli/build/hid-switcher ble-name "Desk Switcher"
cli/build/hid-switcher diagnostics
```

The default Bluetooth name is **HID SWITCHER BLE**. `ble-name` accepts 1–29 UTF-8 bytes, saves the name across reboots, and updates advertising without disconnecting computers or deleting pairings. Computers may display a cached previous name. CLI discovery uses a stable service UUID rather than the name. See [installation, commands, and release packaging](cli/README.md).

## Optional Wi-Fi setup

Run `hid-switcher wifi on` to join saved Wi-Fi. With no saved network it opens **Moonlander Setup**, the existing recovery hotspot name. Hold **BOOT for three seconds**, or send `w` over UART at 115200 baud, to open setup manually. The generated hotspot password is printed on UART. Open **http://192.168.4.1** if the captive portal does not appear.

Enter a 2.4 GHz Wi-Fi network. The hotspot has no inactivity timeout and closes ten seconds after a successful saved connection. Then open **http://moonlander.local** or the LAN IP shown in the UI. These existing network names remain unchanged when the Bluetooth name changes.

The page supports Bluetooth naming, computer labels and ordering, selection, connection status, Wi-Fi settings, diagnostics, and firmware updates. Wi-Fi credentials are saved only after connecting; failed attempts retain the previous record. The home network password is not returned through the API or printed in logs.

Run `hid-switcher wifi off` to close Wi-Fi. Rebooting also leaves Wi-Fi off. When enabled, unavailable saved networks cause the recovery hotspot to open after 30 seconds. There is no cloud service.

## Build and flash

Use PlatformIO with the pinned pioarduino platform in `platformio.ini`:

```sh
pio run
pio device list
pio run -t upload --upload-port YOUR_UART_PORT
```

Identify the board and UART port before flashing, especially with multiple ESP32 boards attached. The build uses pioarduino 55.03.311, NimBLE-Arduino 2.5.1, and ArduinoJson 7.4.2. `sdkconfig.hub.defaults` rebuilds the SDK with USB hub support and bootloader rollback. `scripts/embed_setup.py` generates the web page header from `web/index.html`.

Initial installation requires the bootloader, partition table, OTA boot metadata, and application over USB. The two application slots are 3 MB each; this layout requires 16 MB flash. Do not apply an application-only update to a board with a different partition table. The new BLE identity/slot scheme differs from upstream's reboot-based switching; first-time upgraders may need to pair again. The one-time identity import applies to the intermediate multi-host firmware, not arbitrary older bond formats.

Later web updates accept the application's `firmware.bin`, **not** the combined factory image. The updater checks chip/project identity and the complete image before selecting the inactive slot. Input pauses during installation. Invalid or interrupted uploads leave the running slot selected. Starting an upload replaces the previous backup. A trial boot has a startup health check and rollback support; that check does not prove physical keyboard/mouse functionality. Wi-Fi is off again after restart, so use the CLI to enable it before reopening the update page.

## Tests

```sh
sh scripts/test-host.sh
python3 tests/partition-layout.py  # after pio run
npm ci --prefix tests
npx --prefix tests playwright install chromium
npm test --prefix tests
sh cli/build.sh
sh cli/test.sh
```

Host tests cover shortcut validation, connected-slot cycling, name validation, HID parsing, routing and releases, slot ordering, report timing, controller accounting, and bounded recovery. The USB endpoint regression exercises the vendored driver lookup with colliding endpoint addresses. Browser tests use mocked API responses and cover form validation, safe rendering, and a 375px layout. They do not replace physical-device tests.

Validated hardware includes a Moonlander keyboard and Logitech MX Master 3S receiver through a USB hub, with two Macs connected simultaneously. Support for three links exists in firmware; other host operating systems and three-host physical behavior need broader testing. The USB driver retains its upstream license and documents the local fix in [LOCAL-CHANGES.md](lib/ESP32_USB_Host_HID/LOCAL-CHANGES.md).

## Scope and diagnostics

Supports one hub level, boot-format keyboards, and relative HID mice with up to eight buttons, 8/16-bit X/Y, wheel, and horizontal pan. ESP32-S3 USB host channel limits can leave unused composite/vendor interfaces unavailable. NKRO, media-key forwarding, absolute pointers, vendor mouse configuration, and Logitech application passthrough are not implemented.

Mouse motion is coalesced to the negotiated BLE interval while preserving button transitions. With the tested Macs this is 15 ms, approximately 66.7 submissions/s. Increasing production above the Bluetooth link's capacity can increase buffering. This is not the mouse's sensor polling rate.

The UI separates USB arrivals, BLE submissions, queue wait, and the age of coalesced movement. Fixed-size counters report mean/max, variation, and histogram p95 bounds. Spacing excludes gaps over 100 ms and reports how many were excluded. Wait statistics include long gaps. All aggregate since restart and across decoded mouse sources.

Controller completion instrumentation measures return of HCI packet credits, which includes controller reporting delay; it is not exact radio delivery or mouse-to-screen latency. Wi-Fi and Bluetooth share radio resources, so Wi-Fi is optional and disabled during normal use. See [TODO.md](TODO.md) for remaining features.
