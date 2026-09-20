# USB stability work — 2026-09-19

## Current device state

The latest application is flashed and the user confirms keyboard and mouse work.
The final capture contains both keyboard and mouse reports with no USB transfer
errors or BLE transmit failures. Pairings/settings, bootloader, partition table
and OTA metadata were not overwritten. Verbose USB logging is off by default;
UART `v` toggles it for diagnosis.

**Physical hub replug remains unresolved.** Further resets, flashes and physical
reconnect tests are deferred while the user works. Do not describe startup
success or software recovery as proof that physical reconnect is fixed.

## Changes installed

- A build-local patch to the pinned ESP-IDF 5.5.5 root-hub source handles a missing
  root device record during disconnect instead of aborting. The original SDK
  package and its archives are not modified.
- A second patch returns a pending-event power-off error and restores the prior
  root state, instead of asserting that every power-off command succeeds.
- Port power operations now run in the USB event task after event dispatch.
  Rejected commands retry after further dispatch; power-on waits for device
  cleanup and the minimum off interval.
- Only successfully started HID interfaces occupy application tracking entries;
  failed optional interfaces no longer leave stale handles after reconnect.
- The HID driver allocates its transfer before claiming the interface and frees
  it on a failed claim. Allocation failure cannot leak a claimed interface.
- Status exposes host-device count separately from task liveness. `healthy`
  still represents task/recovery health, not proof of working input.

The SDK patches fail closed if their source anchors change; compilation also
checks the pinned SDK version. `src/UsbHub.c` links the corrected root-hub object
in place of the archive's original object. The link map was checked.

## Evidence and limits

The host regression suite and firmware builds pass. Regression tests execute
actual recovery helpers and the HID open function with missing nodes, pending
port events, delayed cleanup, allocation failure and repeated claim failure.

The earlier revision passed four on-board software recoveries with normal
logging, including the formerly crashing recovery immediately after startup:
one boot, four active interfaces after each recovery, no USB transfer errors or
event drops. These results do **not** cover the final revision's second
power-off race fix or physical replug reliability.

The subsequent physical test disconnected cleanly, but replug then repeatedly
failed `SET_FEATURE(PORT_POWER)` on hub port 1. Turning verbose logging on at
runtime did not clear that failure. Manually recovering in that failed state
exposed the second SDK power-off assertion, which prompted the event-task/retry
changes above. Those changes have passed host tests and live startup/input,
but the failed physical-replug state has not yet been retested with them.

Observed devices: VIA hub `2109:2813`, Logitech receiver `046d:c548`, Moonlander
`3297:1969`. The user confirms only keyboard and receiver are attached. The SDK
also reports a connection on hub port 4; its origin is unconfirmed. Do not infer
another user-connected peripheral from that event. Optional HID interfaces and
that connection hit controller allocation limits, while active input interfaces
remain usable. Capacity errors are distinct from the earlier port-power failure.

Ignored local captures and the installed application hash are in
`artifacts/usb-diagnostics/`. Next hardware check, when interruption is acceptable:
capture physical replug on the current build, then test the serialized recovery
against any reproduced port-power failure. No unverified delay or automatic
reset loop has been added to mask the failure.
