# Experimental BLE keyboard and pointer input

This build lets the ESP32 act as a BLE central for **one selected keyboard or keyboard/trackpad**, while remaining a BLE HID peripheral to the existing three computer slots. USB input remains available. Decoded BLE input uses the same shortcuts, configuration menu, release barriers, and selected-slot forwarding as USB input.

It is a separate experiment. The validated USB firmware does not scan for or connect to BLE input devices. No automatic pairing, background scanning, or bond-store eviction is added. An Android nRF Connect boot-keyboard simulation has been physically verified through encryption, HID discovery, notifications, and forwarding. Real keyboard, trackpad, reconnect stability, and four-link timing validation remain pending.

## Build and install

```sh
pio run -e esp32s3_ble_input
pio device list
pio run -e esp32s3_ble_input -t upload --upload-port YOUR_UART_PORT
pio device monitor --port YOUR_UART_PORT --baud 115200
```

Use the board's UART/programming port. The experiment inherits the calibration-capable absolute-pointer firmware. Its separate SDK configuration reserves **four BLE connections**: three computers plus one input peripheral. Computer slots, existing pairings and pointer calibration are retained. Controller diagnostics also account for four links.

To restore the validated USB-input firmware, flash `esp32s3_usb_ble_absolute`; do not erase flash.

## Connect your input device

For setup without a wired keyboard, use the [browser setup page or serial CLI](usb-setup.md). The page scans, pairs, and displays passkeys over USB serial. The BOOT-menu flow below remains available.

Put the keyboard in **Bluetooth pairing mode**, not its proprietary 2.4 GHz dongle mode. Use a USB keyboard for initial setup:

1. Open a blank text document on the selected computer, using US layout with Caps Lock off. Hold BOOT to open configuration.
2. Choose **8 BLE input devices → 1 Scan and add**. The 12-second scan lists nearby BLE HID devices by name and address suffix.
3. Press the number of your keyboard. Selection starts connection and pairing; scanning alone never pairs.
4. If a passkey appears in the document, type those six digits on the **wireless keyboard** and press Enter.
5. Wait for the connected result and return to the configuration flow. Exit with BOOT, then test typing, releases, switching, and the trackpad if supported.

The same menu offers reconnect, disconnect, and confirmation before forgetting the input pairing. ESC cancels a pending scan/pairing operation and returns; BOOT exits. Losing the original computer/configuration session cancels a pending operation so prompts are not typed onto another computer.

Pairing prompts are also printed over UART. Display-with-confirmation IO is enabled while securing the input keyboard, then restored to no-input/no-output for destination-computer pairing. Finish input pairing before pairing computers; already bonded computers reuse their existing keys. Peer-display passkey entry is not supported.

If the peer supports Secure Connections numeric comparison (for example a phone simulating a keyboard), compare the six-digit code on both devices. Only if they match, confirm on the peer and click **Codes match** in web setup, press **Y** at the board menu prompt, or send `b confirm XXXXXX` over UART. The firmware accepts only the pending code, once; cancel or disconnect clears it. Use **Cancel operation** for mismatched codes. A normal keyboard can still request that you type a code on it and press Enter. Web setup and the board menu bind confirmation to the displayed pairing attempt; stale prompts cannot confirm a later attempt. N or ESC rejects the board prompt, and BOOT cancels and exits.

On security failure, the final status retains `pairing=display passkey`, `enter passkey`, `compare passkey`, or `no callback`. This records which input-client callback ran, even if a quick failure replaced its prompt before the webpage polled. `no callback` alone does not establish which security method was negotiated. Include this result and the peer's log when diagnosing pairing; the numeric security error and connection-parameter errors are separate evidence.

Input security starts asynchronously and waits up to 30 seconds for encryption, observing encryption-change errors for the input connection only. MTU and identity events cannot complete this wait. Cancellation and disconnect stop it; discovery and forwarding require an encrypted link. This avoids NimBLE 2.5.1's shared blocking waiter being released by unrelated GAP events.

UART commands remain available for diagnostics; send each with a newline. `b status` shows readiness and decoded/unsupported report counts without logging keys or report contents.

Commands:

| Command | Behavior |
| --- | --- |
| `b scan` | List nearby advertising BLE HID devices; disconnect the current input first |
| `b scan all` | List up to 32 connectable BLE devices regardless of advertised type; also available as Scan all BLE devices in web setup |
| `b connect N` | Connect the selected scan result |
| `b reconnect` | Connect the saved input identity; wake the device first |
| `b disconnect` | Release held input and disconnect the input peripheral |
| `b forget` | Remove only the saved input peripheral's bond and target |
| `b status` | Show input link/readiness/report counters |

There is no automatic reconnect in this first experiment. Use **Reconnect saved device** (or `b reconnect`) after a reboot or input disconnect. If the address changed and reconnect fails, put the device into pairing mode and scan again. Forget an old input before replacing it with another. A full bond store causes new input pairing to be rejected; existing bonds are not evicted.

If a phone simulator or keyboard is absent from the normal HID scan, use **Scan all BLE devices**. The scan retains up to 96 advertisements and reports advertisement, connectable, and displayed counts, with a warning if a limit is reached. Non-connectable advertisers cannot be selected. Broad scanning does not pair automatically or bypass encryption or HID report validation. The BOOT menu offers **5 Scan all BLE devices**, with nine choices per page and N/P for next/previous. The ordinary HID scan remains limited to nine choices.

## Supported reports and limits

- Keyboard usage-page key arrays and one-bit NKRO bitmaps, converted to the bridge's existing six-key-plus-modifiers output. More than six simultaneous non-modifier keys produces HID rollover rather than silently dropping held keys.
- Relative mouse X/Y, buttons, vertical wheel and horizontal pan under a Mouse collection. The existing absolute switching mode can then transform this relative motion using per-slot calibration.
- Boot keyboard/mouse fallback when a device exposes those characteristics and supports switching to Boot Protocol.
- Up to two HID services and eight input-report endpoints on the selected peripheral. HOGP report-reference IDs select the descriptor layout; the ID is not assumed to be a byte in the notification payload.

A combined keyboard/trackpad can work as both devices when it exposes compatible reports. Native multitouch/digitizer contacts, gestures, consumer/media keys, vendor protocols, keyboard LED output, and Bluetooth Classic are not forwarded. A supported keyboard can still work when the trackpad's layout is unsupported. Absolute output retains the existing five-button/vertical-wheel limitation; relative output supports eight buttons and horizontal pan.

USB and BLE keyboard states are merged without one source releasing another source's held keys. Mouse button state is likewise retained per source. Disconnect clears BLE-owned state and releases the destination; held USB input must be released before forwarding resumes. Queue overflow retains the existing release barrier and clears BLE state when a BLE event cannot be queued.

Discovery, connecting, encryption and subscriptions run on a worker task, outside the forwarding loop. Radio airtime is still shared: scanning, pairing and a fourth connection may affect latency. Build and host-test success are not evidence of acceptable physical latency or a particular trackpad's compatibility.

Reference: the pinned [NimBLE-Arduino client example](https://github.com/h2zero/NimBLE-Arduino/blob/2.5.1/examples/NimBLE_Client/NimBLE_Client.ino) and the local 2.5.1 client/security APIs.

## Pairing and management UI

The Input Devices section shows USB devices and the current/saved BLE input name, connection state, and selected destination computer. Add BLE device opens a dedicated dialog; closing it cancels only pending setup. It enables reconnect only for a saved disconnected input, disconnect only for a ready input, and forget only when an input is saved. Disconnect before scanning for another device. Saved input names are retained across reboot; older records display a generic saved-input label until the next successful connection. Only one input target is saved. Reconnection is manual in this version.

Pairing prompts have a dedicated code panel and remaining time. Code entry on the wireless keyboard and numeric comparison have different instructions; numeric comparison uses a single confirmation button. Completed or failed prompts move to Last pairing prompt instead of disappearing. Diagnostic failure codes remain visible in the result.

On-board acceptance checklist (requires a USB keyboard and a connected destination): hold BOOT to enter the menu in a blank US-layout document; choose 8, scan/select the input, follow its pairing prompt, then press BOOT to exit and verify ordinary typing. Repeat cancellation with ESC, rejection with N, reconnect/disconnect, and the Forget confirmation. Automated menu tests cover these control paths, pagination, stale confirmation, host-session loss, and input pass-through after exit; they do not establish physical on-board acceptance.

Discovery merges raw advertising and scan-response packets by address and address type. A scan response arriving first does not permanently mark the device non-connectable, and later advertising packets do not erase a learned name or HID identity. Scans run for 12 seconds with a 60 ms listening window per 80 ms interval; duplicate reports remain enabled for updates. This increases discovery opportunities but shares radio time with computer links.
