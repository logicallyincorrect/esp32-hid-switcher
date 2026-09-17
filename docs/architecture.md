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
| EdgeSwitch | Fresh companion state, push threshold, cooldown, and acknowledged handoff |
| macOS companion | Desktop edges, sleep state, and destination pointer placement |
| Board | Pins, serial speed, and default identity |

## Task boundaries

The application loop owns settings, BLE routing, and the menu. BLE callbacks copy events into a bounded queue. USB callbacks copy input into a separate bounded queue. Callbacks do not write settings or run the menu.

The USB host task services the controller. The HID driver task receives reports. A connection worker opens interfaces and reads descriptors. A short lock protects USB interface and mouse state. No driver call, output callback, or serial write runs under that lock.

Startup retains a one-second settling interval before USB installation. An interface-open failure is reported without resetting the bus: an optional interface can exceed the controller channel limit while keyboard and mouse input remain active. Transfer failures still use the bounded recovery policy.

USB callbacks close detached interfaces. The driver owns handle storage; application input queues contain copied reports, not driver pointers. Diagnostics use snapshots and do not register temporary USB clients.

## Input rules

Drain USB input before each BLE submission pass. Preserve keyboard transitions. Accumulate relative mouse movement with the existing bounded queues and negotiated pacing.

Only the selected host receives normal input. Switching releases held input. Menu transitions suppress held shortcut keys and mouse buttons until release. Queue overflow and USB recovery require release even when the last physical state is unknown.

## Persistence and compatibility

Keep the existing NVS namespaces, record formats, partition offsets, BLE report map, and GATT identity. Renaming a source file must not require pairing again. Pairing removal uses a saved pending marker so startup can complete an interrupted removal.

## Changes and validation

Put platform-independent behavior in small state types with host tests. Inject service references into adapters. Keep queues bounded and report callbacks short. Do not add sleeps or allocation to the normal report path.

Run `sh scripts/test-host.sh`, `pio run`, and `python3 tests/partition-layout.py`. Hardware tests must also cover reconnect, setup, switching, and simultaneous keyboard/mouse input. Host tests and controller timing do not prove mouse-to-screen latency.

## Edge switching

The optional encrypted BLE edge service receives bounded samples through the existing event queue. `EdgeSwitch` runs only in the application loop. The companion never selects a slot directly: USB motion must meet the push threshold, and the destination must acknowledge pointer placement. Samples carry an epoch and expire after 600 ms. Reconnection, configuration, and selection changes invalidate them.

The application checks held input and menu state before committing a handoff. Companion traffic is bounded to one write in flight and periodic heartbeats; it does not replace the HID data path. See [protocol](../companion/PROTOCOL.md).
