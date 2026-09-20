# Companion-free pointer calibration (experimental)

The absolute-pointer firmware now starts with **seamless switching OFF**. It forwards normal relative mouse movement until you explicitly enable seamless switching. Calibration and automatic edge switching target **one display per computer**, in slot order 1, 2, 3.

Build without flashing:

```sh
pio run -e esp32s3_usb_ble_absolute
```

The binary is `.pio/build/esp32s3_usb_ble_absolute/firmware.bin`. The normal `esp32s3_usb_ble` build retains relative mouse output and optional companion routing. Both builds require explicit opt-in for seamless switching and preserve existing pairing/slot settings. The companion build uses companion position feedback and does not require this absolute-pointer calibration.

## Enable seamless switching

Open a blank text document on the selected computer, use a US keyboard layout with Caps Lock off, and hold BOOT for three seconds. Choose **6 Seamless switching → 1 Enable**.

If a connected computer lacks calibration, Enable starts calibration for those slots first. Switching is enabled only after valid measurements are saved. Cancelling or failing an unfinished calibration does not enable it. The choice survives reboot.

Uncalibrated computers are excluded from edge switching. If a new or previously offline uncalibrated computer is selected manually, its mouse stays relative; use Enable again to calibrate the newly connected computer. Keyboard/mouse switching shortcuts remain available outside calibration.

To disable, use **6 Seamless switching → 2 Disable**. Mouse movement returns to relative output.

## Calibrate one slot or all

Choose **6 Seamless switching → 3 Calibrate pointer**. Select slot **1**, **2**, **3**, or **4 All ready computers**. All snapshots the connected, calibration-ready slots and visits them in ascending order. It does not discover multiple monitors attached to a single computer.

Keep the original text document focused on the computer where you started setup. The instructions finish there before any slot change. On each selected computer:

1. Move the cursor to the **top-left corner**, then left-click and release.
2. Wait for the LED to show **two cyan flashes**.
3. Move smoothly to the **bottom-right corner**, then left-click and release. Stop at the corner instead of continuing to push against the edge.
4. The switch returns to the original computer and types **“Slot N calibration OK.”** It types the next slot's instructions there, completes the final key release, then switches to that slot.
5. At the end it stays on the original computer and types **“Calibration complete.”** If calibration was started by Enable, it also reports whether seamless switching was enabled successfully. After the final message finishes, the normal configuration menu reappears on that computer. ESC or BOOT exits setup as usual.

The LED shows one cyan flash for top-left and two for bottom-right. Green after the flow means captures completed; amber means cancellation; red means failure. While results are being typed, it stays green. ESC or BOOT cancels and returns to the original slot. Normal keyboard input, mouse buttons, scrolling, shortcuts and automatic edge switching are suppressed during calibration; marking clicks never reach an application.

There is a two-minute inactivity limit per capture. Disconnects, stale input, transport/session changes and invalid spans abort the active capture. Completed slots stay saved if a later slot fails or the flow is cancelled; an unfinished/invalid slot keeps its old settings. Slots that disconnect before their turn are skipped and listed in the final message. If the original host disconnects during feedback, output stops rather than typing the result on another host.

## What the measurement means

During calibration, the firmware sends a separate **relative mouse report**, so each host moves the cursor using its normal relative-pointer behavior. It counts raw X/Y movement between the two human-marked corners, then maps those spans to the absolute range 0–32767. Measuring while using the old absolute gains would merely reproduce those gains and could not correct the ultrawide problem.

No resolution or DPI entry is required. This is an empirical calibration at the speed of your sweep, not an automatic display-resolution measurement. Host acceleration, changing mouse DPI, overshooting the edges, other pointing devices, or different host pointer settings can affect the result. Move at a normal, steady speed and repeat calibration after display or mouse-setting changes. It cannot reproduce the host's full acceleration curve at every speed or guarantee identical physical speed across machines with different settings.

Saved calibration follows the bonded computer when slots are reordered. Advanced **7 Pointer tuning** offers horizontal/vertical percentages (1–1600) relative to the measured scale; 100% uses the captured span. Recalibration resets both percentages to 100. Fractional scaling preserves small movements. Wheel scrolling remains unchanged outside calibration.

## Edge behavior

After opt-in, firmware owns the absolute cursor position. Push beyond the left/right boundary by the distance configured under **5 Edge switching** (default 100 raw counts). Calibrated, connected slots are searched in that direction without wrapping. Entry is at the opposite edge with the same normalized Y.

Dragging, held keys, the setup menu and calibration block edge switching. A switch has an 800 ms cooldown. Companion routing does not drive the absolute-pointer build. Cursor movements made by another mouse, trackpad or an application cannot be observed; the next absolute report can jump back to the firmware's position.

## HID transport and compatibility

The primary HID service contains keyboard report 1 and the NanoKVM-style absolute mouse report 2. The mouse carries five buttons, 16-bit absolute X/Y with logical and physical ranges 0–32767, and vertical wheel in one six-byte report. Press, drag, release, and scrolling all stay on that device. Neutral releases retain the last position.

A second HID service contains only relative report 3 (eight buttons, signed 16-bit X/Y, wheel and horizontal pan). It is used during calibration and when seamless switching is disabled or the selected computer lacks calibration. Its report map is separate so the host does not merge its relative axes with the absolute device's buttons. There is one shared Device Information service. Firmware requests GATT Service Changed on migration (descriptor revision 8), retaining bonds and saved calibration. Calibration requires both services' reports to be subscribed; missing input is never treated as success.

Absolute mode currently supports five mouse buttons and vertical scrolling; horizontal pan and buttons 6–8 remain available in relative mode only. The absolute descriptor matches NanoKVM's `hid.GS2` layout, with a report ID added for BLE. Reference: https://github.com/sipeed/NanoKVM/blob/bd070b2f5ce27d22618e8a1c8d2bdca543429882/kvmapp/system/init.d/S03usbdev

## Validation and diagnostic fallback

The user confirmed the isolated absolute mouse works on both computers. A macOS trace also confirmed sustained leftMouseDragged events with the button held and release only when the raw report released it. The earlier combined absolute/relative report map generated unwanted button releases; a puck descriptor experiment lost cursor movement on one Mac. Those implementations are no longer used.

The two-service integration restores calibration without mixing the two mouse layouts in one report map. Host tests verify descriptor separation, report widths/ranges, calibration state transitions, feedback routing and menu return. On the integrated build, macOS enumerated two independent HID devices: five absolute buttons with no RelativePointer group, and eight relative buttons on report 3. A fresh trace confirmed held drag events and matching release with both services present. The user subsequently confirmed that the integrated build works on both computers. This is device-specific acceptance, not validation of all operating systems or long-term reconnect stability.

`pio run -e esp32s3_usb_ble_absolute_only` retains the validated diagnostic fallback: the same absolute mouse without the relative HID service. It keeps existing calibration and pairings, but cannot recalibrate and always outputs absolute movement even with automatic edge switching disabled. The full calibration-capable mode is `esp32s3_usb_ble_absolute`.
