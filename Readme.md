# ESP32-S3 USB to BLE HID Switcher

Use one USB keyboard and mouse with up to three Bluetooth computers. Select a computer without a restart.

Only the selected computer receives input. The other Bluetooth connections stay open. Settings stay in memory after a restart.

## Hardware

- Freenove ESP32-S3: 16 MB flash and 8 MB PSRAM.
- USB keyboard and relative HID mouse.
- USB hub, if you connect both devices.

Connect the hub to the native USB/OTG port. Use the UART/COM port for power and firmware installation. Use a powered hub if the board cannot supply sufficient power.

A CH340 USB-to-UART port cannot operate as a USB host.

## Install the firmware

Identify the correct board and UART port before installation. Use PlatformIO with the settings in [platformio.ini](platformio.ini).

```sh
pio run
pio device list
pio run -t upload --upload-port YOUR_UART_PORT
```

The first USB installation includes the bootloader and partition table. An upgrade from the original firmware can require new pairings.

## Connect computers

1. Press Ctrl + Cmd + 1.
2. Pair **HID SWITCHER BLE** in the first computer's Bluetooth settings.
3. Press Ctrl + Cmd + 2.
4. Pair the second computer.
5. Repeat for slot 3, if necessary.

Cmd means Command on macOS or Windows/Super on other keyboards. Left and right modifier keys have the same function.

GPIO2 flashes the slot number. The RGB LED shows blue/green/purple for slots 1/2/3. A steady light means ready; a flashing light means disconnected.

## Switch shortcuts

| Action | Function | Default shortcut | Input device |
| --- | --- | --- | --- |
| Cycle | Select the next connected computer | Ctrl + Cmd + Tab | Keyboard, mouse, or both |
| Next | Select the next connected computer | None | Keyboard, mouse, or both |
| Previous | Select the previous connected computer | None | Keyboard, mouse, or both |
| Slot | Select any slot, including an empty or disconnected slot | Ctrl + Cmd + 1/2/3 | Keyboard only |

Cycle, Next, and Previous skip disconnected computers and repeat through the slot list. With no other connected computer, selection stays unchanged.

### Set a shortcut

Run the command below. Use `next`, `prev`, or `slot` instead of `cycle` for another action.

```sh
hid-switcher shortcut record cycle
```

1. Press and release the combination on the USB keyboard or mouse connected to the bridge.
2. Check the captured combination.
3. Enter `y` to save it.

For Slot, record only the modifier keys. Use these keys with 1, 2, or 3 to select a slot.

Mouse shortcuts can include keyboard modifiers. Mouse wheel and movement gestures are not supported.

Input stops during capture. Press Escape to cancel. The session expires after 60 seconds. The web page has the same controls.

### Remove a shortcut

```sh
hid-switcher shortcut clear cycle mouse
hid-switcher shortcut clear cycle keyboard
hid-switcher shortcuts
hid-switcher shortcuts reset
```

Each `clear` command removes only the specified input type. The web page has **Clear keyboard** and **Clear mouse** buttons. `reset` restores all default shortcuts.

## Configure the bridge

Use the macOS CLI over encrypted Bluetooth. See [CLI installation and commands](cli/README.md) for release downloads and build instructions.

```sh
hid-switcher status
hid-switcher name 1 "Desktop"
hid-switcher move 1 2
hid-switcher ble-name "Desk Switcher"
hid-switcher diagnostics
```

Name and position changes keep existing pairings. Computers can continue to show the previous Bluetooth name.

### Use the web page

Wi-Fi is off after each restart.

```sh
hid-switcher wifi on
hid-switcher wifi off
```

`wifi on` connects to saved Wi-Fi or opens **Moonlander Setup**. If saved Wi-Fi is unavailable, setup opens after 30 seconds.

For manual recovery, hold BOOT for three seconds. Read the hotspot password through UART at 115200 baud.

1. Connect to **Moonlander Setup**.
2. Open **http://192.168.4.1**.
3. Enter your 2.4 GHz Wi-Fi settings.
4. After connection, open **http://moonlander.local** on your normal network.

The hotspot closes ten seconds after Wi-Fi connects. The page provides computer settings, shortcuts, diagnostics, and firmware updates.

### Update the firmware

Use the web page to upload `.pio/build/esp32s3_usb_ble/firmware.bin`. Do not upload `firmware.factory.bin` or use this method with a different partition table.

Input stops during installation. An upload replaces the previous backup. A failed trial boot returns to the previous firmware. Wi-Fi is off after the restart.

## Limits and tests

Tested hardware: Moonlander, Logitech MX Master 3S receiver, USB hub, and two Macs. Three simultaneous computers and other operating systems require more tests.

The bridge supports one hub level, boot-format keyboards, and relative mice with up to eight buttons. NKRO, media keys, absolute pointers, and Logitech application access are not supported.

The tested Macs use 15 ms Bluetooth intervals. Diagnostics do not measure total mouse-to-screen delay.

```sh
sh scripts/test-host.sh
python3 tests/partition-layout.py
npm ci --prefix tests
npx --prefix tests playwright install chromium
npm test --prefix tests
sh cli/build.sh
sh cli/test.sh
```

Run the partition check after a firmware build. Browser tests use simulated API responses. Software tests do not replace tests with physical devices.

See [planned work](TODO.md), the [USB driver change](lib/ESP32_USB_Host_HID/LOCAL-CHANGES.md).
