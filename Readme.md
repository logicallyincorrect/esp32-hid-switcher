# HID Switcher

![HID Switcher — one keyboard and mouse, three computers](docs/images/header.svg)

Use one USB keyboard and mouse with up to three Bluetooth-enabled computers.

Only the selected computer receives input. The other Bluetooth connections stay open. Names, pairings, and shortcuts stay in memory after a restart.

## Hardware

- Freenove ESP32-S3: 16 MB flash and 8 MB PSRAM.
- USB keyboard and relative HID mouse.
- USB hub, if you connect both devices.

Connect the hub to the native USB/OTG port. Use the UART/COM port for power and firmware installation. Use a powered hub if the board cannot supply sufficient power.

A CH340 USB-to-UART port cannot operate as a USB host.

An optional [BLE keyboard/trackpad input experiment](docs/ble-input.md) adds one wireless input peripheral alongside USB, using a separate four-connection build. It is not enabled in the normal firmware.

Printable case files, measured board dimensions, and assembly instructions are in [stls](stls/README.md).

For a print-ready folded handout, use the two-page [A4 quick-start sheet](docs/quick-start-a4.html) (print landscape, duplex, short-edge binding).

## Install or update

Identify the correct board and UART port before installation. Use PlatformIO with [platformio.ini](platformio.ini).

```sh
pio run -e esp32s3_usb_ble_absolute
pio device list
pio run -e esp32s3_usb_ble_absolute -t upload --upload-port YOUR_UART_PORT
```

These commands install the companion-free absolute-pointer build. Use `-e esp32s3_usb_ble` instead for relative-only firmware with optional macOS companion routing. The `esp32s3_usb_ble_absolute_only` environment is a diagnostic fallback, not the full calibration-capable firmware.

All firmware updates use USB. The [browser setup page and serial CLI](docs/usb-setup.md) can configure the device over its UART/programming port, including first-time BLE keyboard pairing without an existing keyboard. The BOOT menu remains available; no Wi-Fi is needed. The existing partition layout remains compatible with saved settings. Do not erase flash unless you want to remove pairings and settings.

## Connect computers

1. Press Ctrl + Cmd + 1.
2. Pair **HID SWITCHER BLE** in the first computer's Bluetooth settings.
3. Press Ctrl + Cmd + 2.
4. Pair the second computer.
5. Repeat for slot 3, if necessary.

Cmd means Command on macOS or Windows/Super on other keyboards. Left and right modifier keys have the same function.

## Switch at the edge of the screen

Both options provide seamless edge switching, **off by default**. Choose the firmware for the behavior you want:

| | Companion-free mode | macOS companion mode |
| --- | --- | --- |
| Firmware environment | `esp32s3_usb_ble_absolute` | `esp32s3_usb_ble` |
| Software on each computer | None | macOS companion on each source Mac |
| Pointer output | Absolute when enabled; relative otherwise | Relative |
| Setup | Enable in menu; calibrate each slot or choose All | Install companion, then enable in menu |
| Screen information | Two-corner calibration, one display per computer | Companion reports desktop edges; shared monitor boundaries stay on the same computer |
| Pointer after switching | Opposite edge, retaining normalized vertical position | Destination pointer stays where it was |
| Mouse controls | Five buttons and vertical scroll in absolute mode | Eight buttons and horizontal/vertical scroll |

Both use slots 1 → 2 → 3 from left to right, skip disconnected computers, and do not wrap at an outer edge. Dragging, held keys and configuration block automatic switching. Keyboard/mouse switching shortcuts work in either mode.

For companion-free mode, select **6 Seamless switching → 1 Enable**. Missing calibration starts the two-corner procedure before switching can turn on. Recalibrate one slot or all connected computers through **6 → 3 Calibrate pointer**. The integrated build works on both tested computers, including absolute dragging. See [calibration instructions and limitations](docs/absolute-pointer.md).

Download the companion ZIP from [Releases](https://github.com/logicallyincorrect/esp32-hid-switcher/releases), or get a development build from [Actions](https://github.com/logicallyincorrect/esp32-hid-switcher/actions/workflows/companion-macos.yml). See [build and installation instructions](companion/README.md).

An optional [macOS companion](companion/README.md) lets you push the pointer against the right edge to select the next computer, or the left edge to select the previous one. Install it on each Mac from which you want to switch at a screen edge. Disconnected computers are skipped. Slots run left to right as 1, 2, 3. Edge switching stops at either end and does not wrap. Switching does not require a companion on the destination. Push outward after reaching an edge to switch input (100 mouse counts by default); the destination pointer stays where it was.

Shared monitor boundaries stay on the same computer. Dragging, held keys, and the setup menu disable edge switching. Keyboard and mouse shortcuts remain available.

The normal build requires the companion, updated firmware, and **6 Seamless switching → 1 Enable**. Hardware validation is pending.

## Configure the device

![Streaming device configuration in a text editor](docs/images/configuration.gif)

1. Open a blank document in a text editor on the selected computer.
2. Select the US keyboard layout on that computer.
3. Set Caps Lock to off.
4. Hold BOOT for three seconds.
5. Wait for the menu and its `>` prompt.
6. Press a menu number.

The device types the menu into the document. It reads configuration input directly from the attached USB keyboard and mouse. No software installation is necessary.

Use a text editor, not a command shell. Keep the document selected until you exit. The device cannot detect which application receives its text.

| Menu | Function |
| --- | --- |
| Computers | Rename, move, select, or forget a computer |
| Shortcuts | Record, clear, or restore shortcuts |
| Bluetooth name | Change the device name |
| Diagnostics | Show USB status, Bluetooth errors, and report times |
| Edge switching | Set the outward movement distance (1-10000 counts) |
| Seamless switching | Opt in/out; calibrate one slot or All in the absolute build |
| Pointer tuning | Advanced per-computer axis percentages (absolute build) |
| BLE input devices | Scan, select, pair, reconnect, disconnect, or forget a wireless keyboard/trackpad (experimental BLE input build) |

To change the edge distance, select **5 Edge switching**, enter a value, press Enter, then `y`. The value applies immediately and stays saved after restart.

Each configuration menu shows the current settings. Unassigned shortcuts show **Not set**.

Enter names with printable ASCII characters. Press Enter to review a name. Press `y` to save or `n` to cancel. Uppercase `Y` and `N` also work.

Press Escape to go back without saving the current input. Escape at the main menu exits setup. Press BOOT to exit from any screen. The menu also closes after two minutes without input or if its Bluetooth connection is lost. Selecting another computer closes the menu before the switch.

The RGB LED is yellow in configuration mode. Normal keyboard and mouse input stops while the menu is open.

## Shortcuts

| Action | Function | Default | Input device |
| --- | --- | --- | --- |
| Cycle | Next connected computer | Ctrl + Cmd + Tab | Keyboard, mouse, or both |
| Next | Next connected computer | None | Keyboard, mouse, or both |
| Previous | Previous connected computer | None | Keyboard, mouse, or both |
| Slot | Any slot, including disconnected slots | Ctrl + Cmd + 1/2/3 | Keyboard only |

Cycle, Next, and Previous skip disconnected computers and repeat through the slot list. With no other connected computer, selection stays unchanged.

To change a shortcut:

1. Select **Shortcuts** from the menu.
2. Select the action.
3. Select **Record**.
4. Wait for the prompt.
5. Press and release the combination on the attached USB keyboard or mouse.
6. Press `y` to save after the confirmation prompt.

For Slot, record only modifier keys. Use those keys with 1, 2, or 3 to select a slot. Mouse shortcuts can include keyboard modifiers. Wheel and movement gestures are not supported.

**Clear keyboard** and **Clear mouse** remove only the specified input type. Recording one input type keeps the other shortcut. **Restore defaults** resets all shortcuts. Each change requires confirmation.

## Forget a pairing

1. Select **Computers**, then **Forget pairing**.
2. Select the computer's slot.
3. Check the displayed name.
4. Press `y` or `Y` to confirm.

The bridge disconnects that computer and clears its pairing. The slot name, shortcuts, and other pairings stay saved. If you forget the current computer, setup closes when it disconnects. Press Esc or `n` to cancel before removal.

Also forget HID SWITCHER BLE in that computer's Bluetooth settings before pairing again. Select the empty slot to pair a replacement computer.

## Status and recovery

GPIO2 flashes the selected slot number. The RGB LED shows blue/green/purple for slots 1/2/3. A steady light means ready; a flashing light means disconnected.

Initial pairing does not require the menu. Use the default Slot shortcut to select an empty slot. If Bluetooth is unavailable, use UART at 115200 baud: `1`/`2`/`3` selects a slot, `?` shows status, `u` restarts USB devices, and `v` toggles verbose USB diagnostics (off by default).

## Limits and tests

Tested hardware: Moonlander, Logitech MX Master 3S receiver, USB hub, and two Macs. Three simultaneous computers and other operating systems require more tests.

Connect your keyboard and mouse directly or through one USB hub. Hubs cannot be chained together. Keyboards must support sending up to six held keys at once, plus modifier keys such as Ctrl and Shift. Relative mode supports eight mouse buttons and horizontal/vertical scrolling. Absolute mode currently supports five mouse buttons and vertical scrolling.

Media keys, touchscreens, and drawing tablets are not supported. Vendor-specific configuration protocols, including HID++, are not supported yet.

Menu text requires the US keyboard layout and Caps Lock off. Normal input does not have this restriction. Diagnostics do not measure total mouse-to-screen delay.

```sh
sh scripts/test-host.sh
pio run -e esp32s3_usb_ble -e esp32s3_usb_ble_absolute
python3 tests/partition-layout.py
```

Host tests cover menu commands, output flow, shortcuts, and input routing. They do not replace tests with physical devices.

See [architecture](docs/architecture.md), [dependencies](DEPENDENCIES.md), and [planned work](TODO.md).
