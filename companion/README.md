# Edge-switching companion

The optional macOS companion switches computers when you push the pointer against the left or right edge of your desktop. Right selects the next available slot; left selects the previous slot. It skips disconnected computers. The destination does not need a companion.

Install it on each Mac from which you want to switch at a screen edge. It requires macOS 13 or later, Bluetooth, Accessibility permission, and firmware with the edge service. Pair HID Switcher in macOS Bluetooth settings first. Existing shortcuts and device configuration work without this companion. No Wi-Fi or network listener is used.

## Open the app

Copy `HID-Switcher-Companion-macOS.zip` to the other Mac and unzip it. Move the complete `HID Switcher Companion.app` to Applications, then double-click it. The app supports Intel and Apple Silicon Macs; do not copy only the executable from inside the bundle. A mouse icon appears in the macOS menu bar. Open its menu, select **Choose device**, and select your HID Switcher. The app remembers your selection. Other connected HID devices can appear in the list.

The menu shows connection status and provides **Allow Accessibility**, **Bluetooth Settings**, and **Quit HID Switcher**. Edge switching requires updated firmware on the board.

## macOS blocks the app

The local build is ad-hoc signed and is not notarized by Apple. ZIP and signature checks do not establish Gatekeeper approval on another Mac.

If macOS reports an unidentified developer or says Apple cannot check the app, use **System Settings → Privacy & Security → Open Anyway** for this app. See [Apple's instructions](https://support.apple.com/en-us/102445). A managed Mac may require IT approval. A generic “cannot be opened” error can have a different cause; check the exact message before changing security settings.

## Build and try

From the repository root:

```sh
sh companion/test.sh
sh companion/build.sh
"companion/build/HID Switcher Companion.app/Contents/MacOS/hid-switcher-companion" list
```

Copy the UUID for your HID Switcher. The list can also contain other connected keyboards; choose the correct device. Discovery uses service IDs, so renaming the switcher does not prevent discovery.

```sh
"companion/build/HID Switcher Companion.app/Contents/MacOS/hid-switcher-companion" run --device YOUR_UUID
```

Allow Bluetooth when macOS asks. Enable Accessibility for the companion in System Settings. Use `check` to inspect permissions without connecting or moving the pointer. Use `permissions` to open the Accessibility permission prompt. Press Ctrl+C to stop a foreground run.

The companion does not pair devices or change the keyboard layout. If it reports a missing edge service, update the firmware before running it.

## Run at login

```sh
/usr/bin/python3 companion/agent.py install --device YOUR_UUID
/usr/bin/python3 companion/agent.py status
```

This copies the app to `~/Applications/HID Switcher Companion.app` and installs a LaunchAgent for your user. Grant permissions to that installed copy. Logs go to `~/Library/Logs/HID Switcher/companion.log`.

```sh
/usr/bin/python3 companion/agent.py stop
/usr/bin/python3 companion/agent.py start
/usr/bin/python3 companion/agent.py uninstall
```

Uninstall removes the LaunchAgent and retains the app. These commands do not change firmware or saved pairings.

## Behavior

- Move the pointer away from an edge once after connecting or switching to arm the feature.
- The first outward mouse movement at an edge switches input. There is no dwell timer or movement threshold.
- A switch has an 800 ms cooldown. Resting at an edge does not switch.
- Shared monitor boundaries do not switch computers. Exposed left and right desktop edges do.
- The destination pointer stays where it was. No reposition request or acknowledgment is sent.
- Held keys, dragging, and the device setup menu disable switching. Sleep, inactive sessions, missing permissions, and stale companion reports disable the feature as well.

The firmware remains responsible for keyboard and mouse input. The companion sends edge/drag state; it does not send keypresses, screen content, or application names.

This feature requires hardware testing across the participating computers. The host tests cover edge detection and protocol/state behavior; they do not establish end-to-end latency.
