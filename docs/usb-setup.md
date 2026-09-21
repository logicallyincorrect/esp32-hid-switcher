# Browser and CLI setup over USB

The static setup page installs firmware and sends configuration directly to the ESP32 through its UART/programming USB port. It does not need Wi-Fi, a companion application, a text document, or a keyboard already connected to the switcher. Use a computer with desktop Chrome or Edge, a mouse/trackpad, and a USB data cable. Initial host Bluetooth pairing still takes place in each computer's operating-system Bluetooth settings.

## First-time setup

1. Open the hosted setup page (or the local server below). Connect the Freenove ESP32-S3 **16 MB flash / 8 MB PSRAM** UART port, leaving the native USB host port for wired input devices.
2. If needed, click **Install firmware** beside **Connect board**, choose a version in the modal, then click **Install selected version**. Install remains available while setup is connected: it asks permission, cancels pending setup work, releases the USB port, and opens the installer using that same port. Declining leaves setup connected. The packaged build includes experimental BLE keyboard/relative-trackpad input. Leave erase disabled on updates to preserve settings.
3. Close the install dialog, then choose **Connect board** and select its serial port. Only one browser tab or serial monitor can own the port at a time. Opening it may reset the ESP32.
4. Put the wireless keyboard in Bluetooth pairing mode. Click **Scan for keyboards**, then its name. For a keyboard passkey, type the displayed code on the wireless keyboard and press Enter. For numeric comparison, check both displays and click **Codes match** only if they agree. Use **Cancel pairing** to reject. Scanning never pairs automatically. Wait for the connected result; accepted means the operation started, not that pairing succeeded.
5. Select a computer slot on the page. In that computer's Bluetooth settings, pair with the displayed switcher name. New destination-computer passkeys appear on the page while it is connected. Repeat for other slots. Default names let you finish setup without typing names.
6. Configure names, left-to-right slot order, shortcuts, and optional seamless switching. Enable works immediately. Shared sensitivity applies to all monitors; calibration is optional. Calibration progress and results appear on the page; serial-started calibration never types feedback into another application or opens the BOOT menu.
7. Disconnect when finished. The board saves settings immediately through the existing NVS settings code. BOOT configuration still works afterward. BLE input reconnect is explicit in this first experiment: use the page, CLI, or BOOT menu after a reboot.

The browser uses [Web Serial](https://developer.chrome.com/docs/capabilities/serial); flashing uses [ESP Web Tools](https://esphome.github.io/esp-web-tools/). Web Serial needs HTTPS or localhost. The installer component is pinned to ESP Web Tools 10.1.0 and loaded from unpkg; normal configuration uses this site's own JavaScript. Firmware must match this board's memory layout, not just the ESP32-S3 chip family.

## Build / serve / publish

```sh
pio run -e esp32s3_ble_input
python3 scripts/package-web.py --version YOUR_VERSION
python3 -m http.server 8765 --bind 127.0.0.1 --directory artifacts/web-setup
```

Open `http://localhost:8765`. The packager copies the webpage plus separate bootloader, partition, OTA-boot selection, and application images. It validates image sizes and NVS/application offsets, and does not include an image spanning the NVS partition. Flashing resets OTA boot selection to app0. Do not select a full erase if you want to retain pairings/calibration. It does not copy the gap-filled factory image over saved settings.

For GitHub Pages, select **Settings → Pages → Source: GitHub Actions**, then manually run **Browser setup installer**. The workflow builds and packages the BLE input firmware and deploys the page. It does not publish on every push. This repository change alone neither enables Pages nor publishes a URL. Treat the published firmware as experimental until tested with real keyboards and host computers.

## CLI

```sh
python3 -m venv .venv-cli
.venv-cli/bin/pip install -r cli/requirements.txt
.venv-cli/bin/python cli/hid-switcher.py --port /dev/cu.YOUR_UART status
.venv-cli/bin/python cli/hid-switcher.py --port /dev/cu.YOUR_UART shell
```

The interactive shell keeps one serial connection open, avoiding repeated UART-open resets. Enter one JSON command per prompt:

```json
{"op":"input","command":"scan"}
{"op":"input-status"}
{"op":"input","command":"connect 1"}
{"op":"input-status"}
{"op":"select","slot":0}
{"op":"name","name":"Desk Switcher"}
```

Wait for the scan to finish before selecting a result. `input-status` displays the pairing passkey and result. One-shot examples:

```sh
.venv-cli/bin/python cli/hid-switcher.py --port /dev/cu.YOUR_UART select '{"slot":1}'
.venv-cli/bin/python cli/hid-switcher.py --port /dev/cu.YOUR_UART --watch input-status
```

The original UART `1`/`2`/`3`, `?`, `u`, `v` and `b ...` commands remain available. Do not mix clients while a serial operation is pending.

## Protocol v1

115200 baud, newline-delimited UTF-8. Requests begin with `@HID1 `, followed by a JSON object with unsigned integer `id` and string `op`. Responses use the same prefix and echo `id`, `ok`, and either `result` or `error`. Ignore unrelated diagnostic lines. Requests are bounded at 2048 characters after `@`; overlong or frames taking more than three seconds are discarded through the next newline. Clients send one request at a time and must not automatically retry writes after a timeout: query status first.

```text
@HID1 {"id":1,"op":"status"}
@HID1 {"id":1,"ok":true,"result":{...}}
```

Slot/action/field indices in JSON are **zero based**; scan-result numbers are **one based**. Changes share the BOOT menu's existing validation, storage and release barriers. Mutations are rejected while the BOOT menu or incompatible calibration/recording flow owns configuration.

| Operation | Arguments / behavior |
| --- | --- |
| `status` | Capabilities, name, slots, settings generation, selected slot, shortcuts, calibration progress/result |
| `input-status` | Availability, busy state, last status/passkey, numbered devices, revision |
| `input` | `command`: `scan`, `connect N`, `reconnect`, `disconnect`, `forget`, or `status`; acknowledgement means queued |
| `input-cancel` | Cancel pending input scan/connection |
| `select` | `slot`: 0–2; selects forwarding/pairing slot |
| `forget` | `slot`: 0–2 and `confirm`: true; forget that computer |
| `name` | `name`: Bluetooth device name, existing 29-byte validation |
| `slots` | `generation` from status, `order`: permutation of [0,1,2], `names`: three names in new order, each up to 32 bytes |
| `edge` | `value`: existing validated edge-distance setting |
| `monitor-layout` | `generation` from status `monitor_layout`, `active` boolean, three `screens` with integer `x`, `y`, `width`, `height` and boolean `enabled`, `estimated`; saves absolute-mode monitor geometry |
| `pointer-speed` | `value`: shared sensitivity percentage, 1–1600 (100 = one configured desktop pixel per count) |
| `pointer-probe` | Diagnostic only: `x`, `y` normalized 0–32767; sends a button-free absolute position on the selected connected computer, requiring seamless mode and released input |
| `pointer` | Legacy stored per-slot percentages (`slot`, `field`, `value`); retained for older firmware, not used by shared-speed motion. Use `pointer-speed` for current motion. |
| `seamless` | `enabled`: boolean; takes effect without calibration |
| `calibration-start` | `mask`: slot bitmask 1–7, `enable`: enable seamless after successful capture; serial feedback only |
| `calibration-cancel` | Cancel and return to the original slot |
| `shortcuts` | Read bindings |
| `shortcut-record` | `action`: 0 cycle / 1 next / 2 previous / 3 slot; optional `kind`: 1 keyboard / 2 mouse (omitted accepts either); returns session `token` |
| `shortcut-status`, `shortcut-save`, `shortcut-cancel` | `token` from recording; poll until ready, then save explicitly |
| `shortcut-clear` | `action`, `kind`: 1 keyboard / 2 mouse |
| `shortcuts-reset` | Restore defaults |

Recording suppresses forwarding while waiting and capturing. On release, capture ends and forwarding resumes. In the web shortcut table, the pencil records that cell’s input type and saves automatically on release; × removes an existing binding or cancels active recording. Keyboard chords finish after all keys and modifiers are released. The BOOT menu and CLI retain their explicit save flow. A dropped browser session does not erase settings; pending recordings/calibration retain their existing expiration timers. Explicit page Disconnect cancels its pending work. A bare USB unplug is not a configuration command.

## Verification

Host parser/menu tests: `sh scripts/test-host.sh`. Browser transport tests: `node --test tests/web-serial.mjs`. CLI tests: `python3 tests/serial-cli.py`. Browser flow test: serve the packaged site, install Playwright outside the firmware tree, then run `NODE_PATH=PATH_TO_NODE_MODULES node tests/web-setup-browser.cjs`. This uses simulated USB responses and checks 320/390/430/1440 widths; it does not flash hardware or prove physical pairing/latency.

`input-status` also returns `ready`, `saved`, `device`, `saved_name`, `state`, and a `pairing` object (`kind`, `active`, `id`, `code`, `seconds_left`). `input-confirm` takes the displayed `attempt` ID and numeric `code`; expired or mismatched prompts are rejected. UART `b confirm XXXXXX` remains available.

## Page organization and version history

- **Connect / Firmware** connects over USB and reports the installed application version/build. Use the Install firmware button to open a modal and choose any retained package, including an older build. Selection changes the ESP Web Tools manifest; installation remains explicit.
- **Input Devices** lists USB inputs grouped by physical USB address and VID/PID, plus the connected or saved BLE input. USB labels use reported HID capabilities rather than invented product names. **Add BLE device** opens a modal for scanning, selection, pairing and results. Closing it or pressing Escape cancels pending BLE setup, but keeps a completed connection.
- **Outputs** retains the computer-slot controls. Switching and shortcuts remain below.

The packager retains versioned binaries and `firmware/versions.json` in its output directory. Older entries are real retained images, not Git tags without binaries. Repackaging the same version/image does not add duplicates. The Pages workflow restores its previous `web-setup-history` artifact before packaging and uploads the accumulated catalog with 90-day artifact retention. If no prior artifact remains, a fresh deployment contains only the new package; preserve a copy of the site for longer-term archival. No public deployment is implied by a local package.

For an update or downgrade, leave the installer erase option unchecked. NVS stays intact, but compatibility with settings from newer firmware depends on the selected version.


Screen resolution is edited inside the arrangement: click the pencil beside a screen's dimensions, enter width and height, and choose Done. Drag screens to position them, then save the arrangement. Calibration supplies estimated starting proportions for an unsaved arrangement; it does not measure pixel resolution or replace saved dimensions. Arrange in a row resets visual positions.

The selected screen's speed adjustment remains visible (25–400%, default 100%). Effective speed is shared speed × screen percentage / 100 on both axes; geometry and edge alignment stay unchanged. Existing arrangements migrate with all adjustments at 100%. Calibration and edge travel remain directly accessible.
