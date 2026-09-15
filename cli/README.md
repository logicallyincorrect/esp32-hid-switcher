# HID Switcher CLI for macOS

Configure an ESP32-S3 HID Switcher over its existing Bluetooth connection. Pair it in macOS Bluetooth settings first. The default Bluetooth name is **HID SWITCHER BLE**.

## Install

Download `hid-switcher-macos-arm64.tar.gz` for Apple Silicon or `hid-switcher-macos-x86_64.tar.gz` for Intel from this repository's GitHub Releases. Verify the accompanying checksum with `shasum -a 256 -c <archive>.sha256`, extract the archive, and run `./hid-switcher --help`. Binaries are built for macOS 15 or later, ad-hoc signed, and not notarized. macOS may request Bluetooth permission for the invoking terminal application.

To build locally, install Xcode Command Line Tools, then run `sh cli/build.sh` from the repository root. The executable is `cli/build/hid-switcher`. Run `sh cli/test.sh` for checks that do not access Bluetooth.

## Commands

```sh
hid-switcher status
hid-switcher diagnostics
hid-switcher select 1
hid-switcher name 1 "Desktop"
hid-switcher move 1 2
hid-switcher ble-name "Desk Switcher"
hid-switcher wifi on
hid-switcher wifi off
hid-switcher probe
hid-switcher --device 00000000-0000-0000-0000-000000000000 status
```

Slots are 1–3. `name` changes a computer's slot label; `ble-name` changes the bridge's Bluetooth name. Names of computers accept up to 32 UTF-8 bytes. Bluetooth names accept 1–29 UTF-8 bytes without control characters, persist across reboots, and update without resetting pairings. Computers may keep displaying a cached old Bluetooth name. Discovery uses the configuration service UUID, so the CLI continues working after renaming.

`move 1 2` inserts computer 1 at position 2 and shifts intervening computers, preserving pairings and names. `probe` prints connected device identifiers; use `--device UUID` before the command to select a particular bridge. Without an identifier, the CLI rejects multiple connected bridges. When scanning for a disconnected bridge, it connects to the first matching advertisement; use an identifier if several bridges are nearby.

Status refreshes once per second. Diagnostics are since-boot firmware timing summaries, not end-to-end latency measurements.

Wi-Fi is off after every restart. `wifi on` joins saved Wi-Fi, or opens setup if no network is saved. Joining is asynchronous: use `status` to check `wifi_connected`. `wifi off` closes Wi-Fi without deleting credentials. Hold BOOT for three seconds to open recovery setup. USB firmware updates remain available; enable Wi-Fi explicitly for web updates. The setup network and hostname retain their existing names, `Moonlander Setup` and `moonlander.local`; `ble-name` only changes the Bluetooth name.

## Protocol and limits

The CLI uses an encrypted custom GATT service alongside HID and leaves the shared OS keyboard/mouse connection open when it exits. Configuration writes require a bonded connection. Use one CLI command at a time: replies share one firmware buffer. Requests have unique IDs; a 20-second timeout may mean a command applied but its reply was missed. Check status before retrying a mutation, especially `move`. Mutations are not retried automatically.

Protocol 1 uses service `4d4c0001-8a15-4b4e-9d84-891537e66000`, with status, command, and reply characteristics numbered `0002`, `0003`, and `0004`. Commands and replies are JSON with a correlated `id`; firmware processes bounded requests outside the Bluetooth callback. Pairing deletion, automatic host fallback, and firmware transfer over BLE are not implemented.

## Release automation

The macOS CLI workflow builds and checks both architectures on pull requests and main-branch changes. Publishing a GitHub release builds its tag and attaches archives and SHA-256 files. A manual workflow run can also attach binaries to an existing release by specifying `release_tag`; leaving it blank builds CI artifacts only. The release tag must contain only letters, digits, dots, underscores, plus signs, or hyphens.

For repositories with immutable releases, run the workflow against the existing **draft** release tag before publishing; assets cannot be added after that release is immutable. The workflow uses read-only permissions for builds and grants `contents: write` only to the upload job. See [GitHub release upload](https://cli.github.com/manual/gh_release_upload) and [workflow permissions](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#permissions).
