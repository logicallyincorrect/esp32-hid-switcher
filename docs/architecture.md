# Architecture

`main.cpp` owns one `Application` for the firmware lifetime. Arduino calls its startup and tick functions.

| Component | Responsibility |
| --- | --- |
| Application | Input queue, physical input state, shortcuts, menu, LEDs, UART |
| UsbHost | Native USB startup, HID interfaces, mouse decoding, USB recovery |
| BleHid | BLE peers, saved configuration, input submission, connection recovery |
| MenuBackend | Connect the text menu to application services |
| DeviceMenu, TextConsole | Menu state and paced text output |
| InputState, UsbMice | Release barriers and mouse button state |
| MultiHostRouter, pending queues, MouseCadence | Selected-host routing and bounded input scheduling |
| EdgeSwitch | Fresh companion state, cooldown, and immediate switching |
| AbsolutePointer, PointerTuning | Per-slot absolute position, calibrated axis scaling and companion-free switching |
| PointerCalibration, CalibrationFeedback | Two-corner captures, origin-host results and configuration return |
| macOS companion | Desktop edges and sleep state |
| Board | Pins, serial speed, and default identity |

## Task boundaries

The application loop owns settings, BLE routing, and the menu. BLE callbacks copy events into a bounded queue. USB callbacks copy input into a separate bounded queue. Callbacks do not write settings or run the menu.

The USB host task services the controller and executes root-port recovery after event dispatch; the application loop only requests recovery. The HID driver task receives reports. A connection worker opens interfaces and reads descriptors. A short lock protects USB interface and mouse state. No driver call, output callback, or serial write runs under that lock.

Startup retains a one-second settling interval before USB installation. An interface-open failure is reported without resetting the bus: an optional interface can exceed the controller channel limit while keyboard and mouse input remain active. Transfer failures still use the bounded recovery policy.

USB callbacks close detached interfaces. The driver owns handle storage; application input queues contain copied reports, not driver pointers. Diagnostics use snapshots and do not register temporary USB clients.

## Input rules

Drain USB input before each BLE submission pass. Preserve keyboard transitions. Accumulate relative mouse movement with the existing bounded queues and negotiated pacing.

Only the selected host receives normal input. Switching releases held input. Menu transitions suppress held shortcut keys and mouse buttons until release. Queue overflow and USB recovery require release even when the last physical state is unknown.

## Persistence and compatibility

Preserve existing NVS namespaces, partition offsets and bonded identities. Version new settings records and HID layouts explicitly; request GATT Service Changed when a report map or service layout changes, without erasing pairings or calibration. Renaming a source file must not require pairing again. Pairing removal uses a saved pending marker so startup can complete an interrupted removal.

## Changes and validation

Put platform-independent behavior in small state types with host tests. Inject service references into adapters. Keep queues bounded and report callbacks short. Do not add sleeps or allocation to the normal report path.

Run `sh scripts/test-host.sh`, `pio run -e esp32s3_usb_ble -e esp32s3_usb_ble_absolute`, and `python3 tests/partition-layout.py`. Hardware tests must also cover reconnect, setup, switching, and simultaneous keyboard/mouse input. Host tests and controller timing do not prove mouse-to-screen latency.

## Edge switching

The optional encrypted BLE edge service receives bounded samples through the existing event queue. `EdgeSwitch` runs only in the application loop. The companion never selects a slot directly: Accumulated outward USB motion at the configured distance (default 100 counts) at the reported edge triggers switching, without a dwell timer. Reversing or leaving the edge clears the accumulation. Only the source needs a companion; the destination needs a ready HID connection. The destination pointer is not repositioned. Samples carry an epoch and expire after 600 ms. Reconnection, configuration, and selection changes invalidate them.

The application checks held input and menu state before committing a handoff. Companion traffic is bounded to one write in flight and periodic heartbeats; it does not replace the HID data path. See [protocol](../companion/PROTOCOL.md).

The edge distance is one NVS unsigned integer (`edge-distance`) in the existing settings namespace. Values outside 1-10000 are rejected. A save must succeed before the runtime threshold changes; updating it clears pending edge motion. Missing or invalid stored values use 100.

## Companion-free absolute pointer

The absolute build exposes two HID services with independent report maps. The primary service contains keyboard report 1 and six-byte absolute mouse report 2 (five buttons, X/Y and vertical wheel). The second service contains relative report 3 for calibration and seamless-off movement. Device Information is shared. Normal gestures use exactly one mouse path; absolute clicks, drags and wheel events are never mirrored onto the relative service. This replaces the combined mouse map that caused macOS to release held clicks during absolute movement.

Seamless switching is opt-in and requires calibrated connected slots. Calibration measures raw movement while sending relative output, suppresses marking clicks, persists per-bonded-host spans, and returns to the origin host for feedback and the normal menu. The primary absolute report uses the NanoKVM-style descriptor validated on both user computers. See [calibration and transport details](absolute-pointer.md) for limitations and the diagnostic fallback.
