# Screen layout presets

Status: draft for approval · 2026-11-19

## Why

The user's desk has three computers and two physical screens:

- the Air laptop on the left, always its own screen;
- one 7680×2160 ultrawide whose input is switched on the monitor itself between the
  Work Mac, the Home PC, or a side-by-side split showing both halves.

The switcher stores one monitor arrangement, so seamless edge switching is only right
for one of those monitor states. When the ultrawide shows the Home PC, the stored
arrangement still sends the pointer to the Work Mac, which is connected but not visible.

Presets let the user save one arrangement per monitor state and tell the switcher which
one is current with a shortcut, recorded like the existing computer-switching shortcuts.
The switcher cannot see what a monitor displays; the preset shortcut is how it learns.

## Goals

- Up to 4 named presets, each a complete arrangement: per computer (slot) whether it is
  on screen, plus its position, resolution and speed (sensitivity).
- A preset can leave a connected computer **not on screen**. Edge switching never leads
  to it while that preset is active.
- Each preset has its own recordable keyboard shortcut and mouse shortcut. One more
  pair of shortcuts, **Next preset**, steps through the presets.
- Activating a preset that hides the computer currently being controlled moves keyboard
  and mouse to the computer that took its place on screen.
- Cycle, Next and Previous (computer shortcuts) skip computers that are not on screen in
  the active preset.
- The active preset survives restarts.
- Presets are created, named, edited, deleted and activated on the setup page. The BOOT
  menu can activate a preset.
- Upgrading keeps the user's current arrangement and shortcuts.

## Non-goals

- Detecting the monitor's input or split mode automatically.
- Picture-in-picture overlays. A split preset is side-by-side rectangles; a small window
  floating over another screen is not modelled.
- More than one screen per computer (unchanged: one screen per slot).
- Per-preset seamless on/off, per-preset shortcuts for computers, or per-preset
  pointer calibration. Calibration and per-computer pointer tuning stay per computer.
- Editing presets from the BOOT menu (activation only).

## Anti-goals (failure even if it technically works)

- The pointer crossing onto a computer that the active preset says is not on screen.
- An upgrade that loses the existing arrangement or recorded shortcuts.
- A preset shortcut that silently shadows an existing shortcut.
- Activating a preset leaving keyboard/mouse on a computer the user cannot see, when a
  visible connected computer is available.

## Constraints

- Firmware is ESP32-S3, settings in NVS (`Preferences`, namespace `multi-host`, 20 KB
  partition). Storage must be versioned with an explicit migration like
  `MonitorLayout::load` (Legacy struct) and `validShortcuts` (version field).
- Monitor arrangements only exist in absolute-pointer firmware (`PointerMode::absolute`);
  `setLayout` already rejects other builds. Presets inherit that restriction.
- `MonitorLayout` semantics stay: one rectangle per slot in desktop pixels, no overlaps
  between included screens, sizes 64–16384, positions ±65536, sensitivity 25–400.
- The setup page talks to the firmware with JSON over UART (`@HID1`, `SerialConfig.cpp`),
  using `generation` numbers to reject stale writes. The hosted page and the board's
  firmware can be different versions.
- Host tests (`scripts/test-host.sh`) cover the pure headers; web tests in `tests/*.mjs`
  and `tests/web-setup-browser.cjs`.

## How it works today (relevant parts)

- `MonitorLayout` (`src/MonitorLayout.h`): `version`, `generation`, `active` (custom
  arrangement on/off), three `Screen{x,y,width,height,enabled,estimated,sensitivity}`.
  Stored as one blob under the NVS key `monitor-layout`; held at runtime in
  `_absolutePointer.layout`. `BleHid::setLayout` validates and saves it
  (`op: "monitor-layout"`); `appendLayout` reports it in `status.monitor_layout`.
- `MonitorLayout::crossing` finds the edge neighbour. Offline-but-included screens are
  collapsed out of the geometry (`projected`). A screen's `enabled=0` is **not** honoured
  for a connected computer: a candidate is skipped only when it is not ready.
- The setup page (`web/monitor-layout.mjs`) overrides `enabled` from pairing
  (`paired||connected`), so a paired computer cannot be excluded today.
- `ShortcutConfig` (`src/ShortcutBindings.h`), version 1: seven bindings (keyboard and
  mouse for Cycle, Next, Previous; one modifier chord for Slot + 1/2/3). Stored under
  `shortcuts`. Recorded through `shortcut-record` / `shortcut-status` / `shortcut-save`
  with a token; cleared with `shortcut-clear`; conflicts rejected by `validShortcuts`.
  `matchingShortcut` returns 0–2 for Cycle/Next/Previous and 3–5 for a direct slot;
  `Application::shortcut` turns that into a destination with the connected mask.
- The BOOT menu (`src/DeviceMenu.h`, `MenuBackend.h`) is a typed text menu with pages.

## Approaches considered

1. **Each preset gets its own recorded keyboard and mouse shortcut, plus Next preset**
   — chosen by the user. Most flexible; reuses the recorder, labels and conflict checks.
   Costs ten more stored bindings and a shortcut-config migration.
2. One modifier chord plus a number (like Slot + 1/2/3). Fewer settings, but presets
   become numbers, no mouse buttons, and the digits must not collide with the slot chord.
3. Presets stored only in the setup page, pushed to the board on demand. Rejected:
   shortcuts must work without the page open.

## Design

### Preset storage (firmware)

- New versioned blob `layout-presets` (separate NVS key):
  `version`, `generation`, `activeIndex`, `arrangement` (the existing on/off `active`
  flag, now global), and 4 entries of `{used, name[25], screens[3]}` where
  `screens` uses the existing `MonitorLayout::Screen`.
- Validation: at least one used preset; `activeIndex` names a used preset; names 1–24
  printable characters, unique case-insensitively; each preset's screens pass the
  existing `MonitorLayout` rules; every preset has at least one screen on screen.
- Migration: if `layout-presets` is missing, build preset 1 named "Default" from the
  stored `monitor-layout` (including its `active` flag) and make it active. If that is
  missing or invalid too, use `MonitorLayout` defaults. Leave the `monitor-layout` key
  untouched, so a firmware rollback still finds the pre-upgrade arrangement (it will not
  know about presets made later).
- Today's stored `enabled` values are not trustworthy: the page wrote them from pairing
  state at first save, and nothing honoured them for connected computers. Migration
  therefore puts every **paired** slot on screen and every unpaired slot off screen. If
  turning a slot on would overlap another screen, move it to the right of the rightmost
  on-screen rectangle, top-aligned, and mark it `estimated`.
- Pairing a computer into an empty slot puts it on screen in the **active** preset only,
  placed the same way (right of the rightmost screen, `estimated`). Other presets keep it
  off screen until the user places it; the page shows it in their not-on-screen tray.
- Presets follow the computer, not the slot number: when slots are reordered
  (`configure`), every preset's screens are permuted in the same save as the slot
  configuration, as pointer positions already are. Forgetting a slot turns it off screen
  in every preset.
- Runtime: `_absolutePointer.layout` is always a `MonitorLayout` built from the active
  preset plus the global flag, so edge switching, calibration estimates and status keep
  using one layout. Activating or saving the active preset rebuilds it and calls
  `_absolutePointer.reset`.

### "Not on screen"

- Hiding applies whenever the arrangement flag is on, whether seamless edge switching is on
  or off. With the flag off, presets have no effect: every connected computer counts as
  on screen and the linear slot order works as today. Presets and their shortcuts exist
  only in absolute-pointer builds; other builds reject preset ops and preset recording.
- A preset screen with `enabled=0` is not on screen. Invariant: **a computer that is not
  on screen in the active preset is never an edge-switching destination**, connected or
  not. Implement by passing `reachable = ready & onScreen` as the mask edge switching uses
  (`serviceEdges` → `AbsolutePointer::sync` → `crossing`), and make `crossing` skip
  candidates that are not enabled.
- Offline-but-on-screen computers keep today's behaviour: collapsed out so the next screen
  takes their place. Not-on-screen computers take no space at all.
- Cycle/Next/Previous use `connected & onScreen` instead of `connected`. The direct slot
  shortcut (Slot + 1/2/3) and BOOT-menu selection still reach any connected computer.
- Edge switching out of a not-on-screen computer that was selected directly does nothing
  (it has no rectangle); shortcuts still work.

### Activating a preset

1. Save `activeIndex` (one NVS write), rebuild the runtime layout, reset edge state.
2. If the selected computer is on screen in the new preset, keep the selection.
3. Otherwise pick its replacement among computers that are connected and on screen in the
   new preset: the one whose new rectangle overlaps the old selected computer's
   rectangle (from the previous preset) by the largest area; if none overlaps, the lowest
   slot number. If no such computer exists, keep the selection.
   Presets need not share a coordinate origin ("Arrange in a row" and dragging can shift
   one). Before comparing, translate the previous preset so that a computer on screen in
   both presets sits at the same position in each (the lowest such slot). With no common
   on-screen computer, compare raw coordinates.
4. Release held keys/buttons through the existing selection path (`selectSlot`).

Example: Work Mac selected, switch "Work Mac" → "Home PC": the Work Mac is hidden; the
Home PC's rectangle sits where the Work Mac's was, so the Home PC is selected.

### Shortcuts

- The ten new bindings (keyboard and mouse for Next preset and for presets 1–4) live in a
  separate versioned blob, `preset-shortcuts`, next to the unchanged `shortcuts` blob.
  Older firmware only loads `shortcuts` when its size matches, so this keeps a rollback
  from resetting recorded shortcuts. Missing `preset-shortcuts` means all ten are empty.
  No default bindings for presets.
- New actions for recording/clearing: Next preset and Preset 1–4 (numbering left to the
  builder; the protocol must keep accepting 0–3 for the existing actions).
- Conflict checks span both blobs: every binding, including the Slot chord's numbered
  variants. A save that touches one blob validates against the other.
- Matching extends `matchingShortcut` with Next preset and Preset N results.
  `Application::shortcut` activates presets for those results.
- Preset N's shortcut does nothing while preset N is unused. Deleting a preset clears its
  shortcuts in the same save.
- Next preset steps to the next used preset after the active one, wrapping.

### Setup page

- Above the arrangement editor: one tab per preset (name; active one marked), **New
  preset** (copies the preset being viewed), **Rename**, **Delete** (not the last one),
  **Activate**. Viewing a preset does not activate it.
- The editor edits the preset being viewed; Save writes that preset only.
- Each screen has an on-screen/not-on-screen control. Not-on-screen screens leave the
  canvas and appear in the existing excluded tray (`.monitor-excluded`), from which they
  can be put back. Unpaired slots stay hidden as today.
- Shortcuts table: rows for Next preset and for each used preset (shown by name), with
  the same record/clear controls.
- The page uses the preset protocol when the firmware reports presets, and the current
  single-layout protocol when it does not (older firmware).

### Protocol

- `status` gains `presets`: `generation`, `active`, and a list of
  `{index, name, screens}`. `status.monitor_layout` stays and reports the active preset,
  so older pages keep working.
- New ops: `preset-save` (`index`, `name`, `screens`, `generation`; creates when the index
  is unused), `preset-activate` (`index`), `preset-delete` (`index`, `generation`).
  `preset-save` and `preset-delete` reject a stale `generation` like `monitor-layout`.
- `monitor-layout` keeps working and edits the active preset.

### BOOT menu

- A Presets page lists used presets with numbers and the active one marked; choosing a
  number activates it (same activation path as the shortcut).

### Docs and tests

- Update `Readme.md`, `docs/absolute-pointer.md` and `docs/usb-setup.md`.
- Host tests: preset validation and migration from `monitor-layout`; activation
  replacement rule (overlap, fallback, none connected); `crossing` never returning a
  not-on-screen computer, including connected ones; Cycle/Next/Previous skipping hidden
  computers; shortcut v1→v2 migration, conflicts and matching for preset bindings; BOOT
  menu presets page.
- Web tests: preset tabs, not-on-screen tray, shortcut rows, and the old-firmware path.

## Calls made by Claude (not discussed with the user)

- Migration puts paired slots on screen (stored `enabled` values are unreliable); a slot
  that would overlap is moved right of the rightmost screen.
- A newly paired computer appears on screen in the active preset only.
- Presets follow the computer when slots are reordered; forgetting a slot hides it everywhere.
- Hiding applies whenever the arrangement flag is on, including with seamless off.
- Preset shortcuts are stored separately from the existing shortcuts (rollback-safe).
- The replacement rule aligns presets on a computer visible in both before comparing.

- Up to 4 presets; names up to 24 characters.
- The arrangement on/off flag is global, not per preset.
- `monitor-layout` is left untouched on upgrade instead of being kept in sync, so
  rollback restores the pre-upgrade arrangement.
- Replacement rule on activation: largest overlap with the old rectangle, then lowest slot.
- No default preset shortcuts.
- Presets can be activated but not edited from the BOOT menu.

## Left to the builder

- Exact struct layouts, action numbering, op argument names beyond those listed, and
  helper placement (keep pure logic in headers so host tests cover it).
- Visual details of the preset tabs and tray, following `web/style.css`.
