# Screen Layout Presets Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Up to four named monitor-arrangement presets, each with per-computer on-screen state, position, resolution and speed, activated by recorded shortcuts, the setup page or the BOOT menu.

**Architecture:** Pure preset logic lives in a new header (`src/LayoutPresets.h`) and the existing shortcut header, covered by host tests. `BleHid` stores presets and preset shortcuts in two new NVS blobs and always derives the runtime `MonitorLayout` from the active preset, so edge switching keeps reading one layout. The setup page edits presets over the existing `@HID1` JSON protocol.

**Tech Stack:** C++17 (ESP32-S3, Arduino/NimBLE, PlatformIO env `esp32s3_ble_input`), plain ES modules for the setup page, Node test runner.

**Spec:** `docs/superpowers/specs/2026-11-19-layout-presets-design.md`

## Global Constraints

- Up to 4 presets; names 1–24 printable ASCII characters, unique case-insensitively.
- Screen rules unchanged from `MonitorLayout::valid`: sizes 64–16384, positions ±65536, sensitivity 25–400, no overlap among on-screen screens.
- Every preset has at least one screen on screen; at least one preset exists.
- NVS keys: new `layout-presets` and `preset-shortcuts`; `monitor-layout` and `shortcuts` are never rewritten by preset code paths except `monitor-layout`'s existing op, which now edits the active preset instead (it no longer writes `monitor-layout`).
- Presets and preset shortcuts exist only when `PointerMode::absolute`; other builds reject the new ops with an error.
- Existing protocol keeps working: `monitor-layout` op, `status.monitor_layout`, shortcut actions 0–3.
- Host tests: `sh scripts/test-host.sh` must print `Host firmware tests passed`. Firmware: `~/.platformio/penv/bin/pio run -e esp32s3_ble_input` must succeed.

## Review Focus

- Upgrading a board whose stored `monitor-layout` has a paired slot with `enabled=0` overlapping an on-screen screen: migration must still produce a valid "Default" preset with that slot on screen (moved right). Test in Task 1.
- Deleting the active preset: another used preset becomes active and the runtime layout follows; deleting the only preset is rejected. Test in Task 1.
- A recorded shortcut for an unused preset: the keys must pass through to the computer, not be swallowed. Test in Task 2.
- A connected computer that is off screen in the active preset must never be returned by `crossing`, including when it is the only edge neighbour. Test in Task 1.
- A new setup page talking to old firmware (no `presets` in status) must keep the single-layout editor working. Test in Task 5.

---

### Task 1: Preset model and on-screen edge rule

**Files:**
- Create: `src/LayoutPresets.h`
- Modify: `src/MonitorLayout.h` (`crossing`: candidate needs `enabled`)
- Test: `tests/layout-presets.cpp` (new), `tests/monitor-layout.cpp`

**Interfaces:**
- Produces (all in `src/LayoutPresets.h`, header-only, no Arduino includes):
  - `struct LayoutPreset { uint32_t used=0; char name[25]={}; MonitorLayout::Screen screens[3]; };`
  - `struct LayoutPresets { static constexpr unsigned Count=4; uint32_t version=1, generation=0, activeIndex=0, arrangement=0; LayoutPreset presets[Count]; bool valid() const; bool load(const void*, size_t); MonitorLayout runtime() const; uint8_t onScreen() const; }`
    - `runtime()`: `version=2`, `generation=generation`, `active=arrangement`, screens of the active preset.
    - `onScreen()`: bit i set when the active preset's screen i is enabled; returns `7` when `arrangement==0`.
  - `bool validPresetName(const char *name)` — 1–24 chars, 0x20–0x7e, not all spaces.
  - `LayoutPresets migratePresets(const MonitorLayout &old, uint8_t paired)` — one used preset "Default", `arrangement=old.active`, screen i enabled iff paired bit i, overlap fixed with `placeOnScreen`.
  - `void placeOnScreen(MonitorLayout::Screen screens[3], unsigned slot)` — enable slot; if it overlaps an enabled screen, set `x` = max(x+width) of other enabled screens, `y` = that rightmost screen's `y`, `estimated=1`.
  - `void hideSlot(LayoutPresets&, unsigned slot)` — disable in every preset.
  - `void permutePresets(LayoutPresets&, const uint8_t order[3])` — `new.screens[i] = old.screens[order[i]]` for every preset.
  - `bool deletePreset(LayoutPresets&, unsigned index)` — false if unused or last; clears it; if it was active, `activeIndex` = lowest used index.
  - `int nextPreset(const LayoutPresets&)` — next used index after `activeIndex`, wrapping.
  - `int replacementSlot(const LayoutPreset &from, const LayoutPreset &to, unsigned selected, uint8_t connected)` — see Step 3.

- [ ] **Step 1: Write failing tests in `tests/layout-presets.cpp`**

```cpp
#include "LayoutPresets.h"
#include <cassert>
#include <cstring>
int main(){
  // Migration: paired slot 1 stored disabled and overlapping slot 0 -> on screen, moved right.
  MonitorLayout old;old.active=1;
  old.screens[0]={0,917,1920,1243,1,0,100};old.screens[1]={0,0,7680,2160,0,0,100};old.screens[2]={9600,0,7680,2160,1,0,100};
  auto p=migratePresets(old,0b011);
  assert(p.valid()&&p.presets[0].used&&!strcmp(p.presets[0].name,"Default")&&p.arrangement==1);
  assert(p.presets[0].screens[1].enabled&&p.presets[0].screens[1].x==1920&&p.presets[0].screens[1].estimated==1);
  assert(!p.presets[0].screens[2].enabled&&p.onScreen()==0b011);
  // Runtime layout follows the active preset.
  assert(p.runtime().version==2&&p.runtime().active==1&&p.runtime().screens[1].x==1920);
  p.arrangement=0;assert(p.onScreen()==7);p.arrangement=1;
  // Names.
  assert(validPresetName("Split")&&!validPresetName("")&&!validPresetName("   ")&&!validPresetName("0123456789012345678901234"));
  // Duplicate names (case-insensitive) and empty presets are invalid.
  p.presets[1]=p.presets[0];strcpy(p.presets[1].name,"default");assert(!p.valid());
  strcpy(p.presets[1].name,"Home PC");p.presets[1].screens[1].enabled=0;p.presets[1].screens[2]={1920,0,7680,2160,1,0,100};assert(p.valid());
  auto none=p;for(auto &s:none.presets[1].screens)s.enabled=0;assert(!none.valid());
  // Next preset wraps; delete active moves to lowest used; last cannot be deleted.
  assert(nextPreset(p)==1);p.activeIndex=1;assert(nextPreset(p)==0);
  assert(deletePreset(p,1)&&p.activeIndex==0&&!p.presets[1].used);assert(!deletePreset(p,0)&&!deletePreset(p,3));
  // Reorder follows the computer; forget hides everywhere.
  const uint8_t order[3]={2,0,1};auto r=p;permutePresets(r,order);assert(r.presets[0].screens[0].x==p.presets[0].screens[2].x);
  hideSlot(r,0);assert(!r.presets[0].screens[0].enabled);
  // Replacement: Work Mac (1) hidden by "Home PC" preset; Home PC (2) took its rectangle.
  LayoutPreset work{},home{};work.used=home.used=1;
  work.screens[0]={0,917,1920,1243,1,0,100};work.screens[1]={1920,0,7680,2160,1,0,100};work.screens[2]={9600,0,7680,2160,0,0,100};
  home.screens[0]={0,917,1920,1243,1,0,100};home.screens[1]={1920,0,7680,2160,0,0,100};home.screens[2]={1920,0,7680,2160,1,0,100};
  assert(replacementSlot(work,home,1,0b111)==2);
  // Shifted preset: aligned on slot 0, still picks slot 2.
  auto shifted=home;for(auto &s:shifted.screens)s.x+=5000;assert(replacementSlot(work,shifted,1,0b111)==2);
  // Selected still on screen -> unchanged; nothing connected on screen -> unchanged; no overlap -> lowest slot.
  assert(replacementSlot(work,home,0,0b111)==0);assert(replacementSlot(work,home,1,0b010)==1);
  auto far=home;far.screens[2].x=40000;assert(replacementSlot(work,far,1,0b111)==0);
  // Storage round trip.
  LayoutPresets q;assert(q.load(&p,sizeof(p))&&q.activeIndex==p.activeIndex);
  assert(!q.load(&p,sizeof(p)-1));
}
```

Append to `tests/monitor-layout.cpp` before `AbsolutePointer p;`:

```cpp
  // A connected but not-on-screen computer is never an edge destination.
  l.screens[0]={0,0,3440,1440,1};l.screens[1]={3440,0,1920,1080,0};l.screens[2]={5360,0,1920,1080,0};
  assert(l.valid()&&l.crossing(0,1,32767,16384,7,x,y)==-1);
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `sh scripts/test-host.sh`
Expected: compile error (`LayoutPresets.h` missing).

- [ ] **Step 3: Implement `src/LayoutPresets.h` and the `crossing` change**

`replacementSlot`: return `selected` if `to.screens[selected].enabled`. Candidates are slots `i` with `to.screens[i].enabled` and `connected` bit `i`; none → `selected`. Alignment offset: the lowest slot enabled in both `from` and `to` gives `dx = to.x - from.x`, `dy = to.y - from.y` (else 0). Score each candidate by the overlap area (64-bit) between `to.screens[i]` and `from.screens[selected]` shifted by `(dx,dy)`; highest wins, ties → lower slot; if every score is 0 → lowest candidate slot.

`LayoutPresets::valid`: version 1, `activeIndex < Count` and used, `arrangement <= 1`, at least one used, each used preset: `validPresetName`, unique names (case-insensitive), at least one enabled screen, and `MonitorLayout` built from its screens (`active=1`) is `valid()`. Unused presets are ignored.

`load`: accept only `size == sizeof(LayoutPresets)`, then `valid()`.

`crossing`: in the candidate loop skip `i` unless `b.enabled && (ready & (1u<<i))`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `sh scripts/test-host.sh`
Expected: `Host firmware tests passed`

- [ ] **Step 5: Commit**

```bash
git add src/LayoutPresets.h src/MonitorLayout.h tests/layout-presets.cpp tests/monitor-layout.cpp
git commit -m "feat(layout): add preset model and keep hidden screens out of edge switching"
```

---

### Task 2: Preset shortcut bindings

**Files:**
- Modify: `src/ShortcutBindings.h`
- Test: `tests/shortcut-bindings.cpp`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces (in `src/ShortcutBindings.h`):
  - `enum ShortcutAction { Cycle=0, Next=1, Previous=2, Slot=3, NextPreset=4, Preset1=5 };` (presets are `Preset1+i`, i = 0..3; highest action is 8)
  - `constexpr unsigned PresetBindingCount=10;`
  - `inline bool presetAction(unsigned action){return action>=NextPreset&&action<=Preset1+3;}`
  - `inline unsigned presetBindingIndex(unsigned action,unsigned kind){return 2*(action-NextPreset)+(kind==2);}`
  - `struct PresetShortcutConfig { uint32_t version=1,generation=0; ShortcutBinding bindings[PresetBindingCount]; PresetShortcutConfig(); }` — constructor sets `bindings[i].kind = i%2?2:1`, all empty.
  - `bool validShortcutSet(const ShortcutConfig&, const PresetShortcutConfig&)` — `validShortcuts(a)`, preset version 1, kinds alternate, each non-empty preset binding `validShortcut`, no conflict with any other non-empty binding in either config, nor with the Slot chord's three numbered variants.
  - `int matchingPresetShortcut(const PresetShortcutConfig&, const uint8_t *keys, uint8_t modifiers, uint8_t buttons, uint8_t kind, uint8_t usedPresets)` — returns `NextPreset` or `Preset1+i`, or `-1`; a binding for preset i only matches when `usedPresets` bit i is set. Matching rules identical to `matchingShortcut`.

- [ ] **Step 1: Write failing tests (append to `tests/shortcut-bindings.cpp` `main`)**

```cpp
  {
    ShortcutConfig base;PresetShortcutConfig presets;
    assert(validShortcutSet(base,presets));
    const uint8_t w[6]={0x1a};presets.bindings[presetBindingIndex(Preset1,1)]=keyboardBinding(w,0x05);
    assert(validShortcutSet(base,presets));
    assert(matchingPresetShortcut(presets,w,0x05,0,0,0b0001)==Preset1);
    assert(matchingPresetShortcut(presets,w,0x05,0,0,0b0000)==-1); // unused preset: keys pass through
    // Conflict with Cycle (Ctrl+Cmd+Tab) and with the Slot chord's digits.
    const uint8_t tab[6]={0x2b};auto clash=presets;clash.bindings[presetBindingIndex(NextPreset,1)]=keyboardBinding(tab,0x09);
    assert(!validShortcutSet(base,clash));
    const uint8_t one[6]={0x1e};clash=presets;clash.bindings[presetBindingIndex(Preset1+1,1)]=keyboardBinding(one,0x09);
    assert(!validShortcutSet(base,clash));
    // Two presets with the same binding conflict.
    clash=presets;clash.bindings[presetBindingIndex(Preset1+2,1)]=keyboardBinding(w,0x05);assert(!validShortcutSet(base,clash));
    // Mouse binding for Next preset.
    auto mouse=presets;ShortcutBinding m;m.kind=2;m.buttons=0x18;mouse.bindings[presetBindingIndex(NextPreset,2)]=m;
    const uint8_t none[6]={};assert(validShortcutSet(base,mouse)&&matchingPresetShortcut(mouse,none,0,0x18,2,0b0001)==NextPreset);
  }
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `sh scripts/test-host.sh`
Expected: compile error (`PresetShortcutConfig` undeclared).

- [ ] **Step 3: Implement the interfaces above in `src/ShortcutBindings.h`**

Extend the `ShortcutAction` enum in place; existing values keep their numbers. `ShortcutRecorder` needs no change (it only special-cases `Slot`).

- [ ] **Step 4: Run tests to verify they pass**

Run: `sh scripts/test-host.sh`
Expected: `Host firmware tests passed`

- [ ] **Step 5: Commit**

```bash
git add src/ShortcutBindings.h tests/shortcut-bindings.cpp
git commit -m "feat(shortcuts): add preset shortcut bindings with cross-config conflicts"
```

---

### Task 3: Firmware storage, activation and protocol

**Files:**
- Modify: `src/BleHid.h`, `src/BleHid.cpp`, `src/SerialConfig.cpp`, `src/Application.cpp`

**Interfaces:**
- Consumes: Task 1 (`LayoutPresets`, helpers), Task 2 (`PresetShortcutConfig`, `validShortcutSet`, `matchingPresetShortcut`, `presetAction`, `presetBindingIndex`).
- Produces (public on `BleHid`, used by Task 4 and the web page):
  - `const LayoutPresets &presets() const;`
  - `uint8_t onScreenMask() const;` — `_presets.onScreen()` in absolute builds, `7` otherwise.
  - `String activatePreset(unsigned index);` — empty string on success, else a user-facing error.
  - `int presetShortcutAction(const uint8_t *keys,uint8_t modifiers,uint8_t buttons,uint8_t kind=0) const;`
  - JSON ops `preset-save {index,name,screens[3],generation}`, `preset-activate {index}`, `preset-delete {index,generation}`; `status.presets = {generation, active, arrangement, list:[{index,name,screens:[{x,y,width,height,enabled,estimated,sensitivity}]}]}`.

- [ ] **Step 1: Load and migrate at boot**

In `BleHid::begin`, replace reading `monitor-layout` into `_absolutePointer.layout` with: load `layout-presets` into `_presets`; if missing/invalid, read `monitor-layout` (as today, defaults if absent), `migratePresets(old, pairedMask)` where `pairedMask` has bit i when `_config.slots[i].assigned==1`, and save `layout-presets`. Then `_absolutePointer.layout=_presets.runtime()`. Load `preset-shortcuts` into `_presetShortcuts` when its size matches and `validShortcutSet(_shortcuts, saved)`. Add a private `bool savePresets(const LayoutPresets &next)` that writes `layout-presets`, then sets `_presets`, `_absolutePointer.layout=next.runtime()`, `_absolutePointer.reset(millis())`, `_edges.reset(millis())`.

- [ ] **Step 2: Edit, activate, delete, report**

- `setLayout` (op `monitor-layout`): same validation as today, but writes the screens and `active` flag into the active preset via `savePresets` (`arrangement` = request `active`), generation check against `_presets.generation`.
- `preset-save`: index 0–3; name via `validPresetName`; screens validated as in `setLayout`; marks `used`; `++generation`; `next.valid()` else error "Preset names must be unique, and each preset needs one screen on screen without overlaps".
- `activatePreset(index)`: unused → "Choose a saved preset". Compute `replacementSlot(old active preset, new preset, selected(), connectedMask)` where `connectedMask` bit i = `connected(i)`; persist `activeIndex` via `savePresets`; `selectSlot(slot)` if it differs. Log `[Preset] Active: <name>`.
- `preset-delete`: generation check, `deletePreset`, clear that preset's two bindings in `_presetShortcuts` and save both blobs; reject when `deletePreset` is false.
- `appendLayout` keeps reporting the runtime (active preset) layout; add `appendPresets(JsonObject)` used by `status`.
- Route the three ops in `SerialConfig.cpp`; they are writes (not in the `read` list) and reject non-absolute builds with "Presets require absolute-pointer firmware".

- [ ] **Step 3: Hooks for pairing, forgetting, reordering, edges**

- Where a new computer is saved to a slot (`[BLE] Saved computer to slot`), call `placeOnScreen` on the active preset's screens for that slot and `savePresets`.
- `forgetComputer` success → `hideSlot` + `savePresets`.
- `configure` → `permutePresets(next, order)` + `savePresets` after the slot config saves.
- `serviceEdges`: `ready &= onScreenMask()` before `_absolutePointer.sync`.

- [ ] **Step 4: Preset shortcuts in the shortcut ops and dispatch**

- `shortcut-record`, `shortcut-clear`, `shortcut-save` accept actions 0–8 (`presetAction` → preset blob, index `presetBindingIndex`); error text "Choose Cycle, Next, Previous, Slot, Next preset, or a preset". Recording a preset action requires absolute builds. `shortcut-save` validates with `validShortcutSet` against both configs and writes only the blob it changed, bumping that blob's generation; the recorder's generation check uses `_shortcuts.generation + _presetShortcuts.generation`.
- `shortcuts-reset` also resets `preset-shortcuts`.
- `appendShortcuts` adds preset bindings as `[action, kind, label, ...]` rows in the same shape as existing rows (actions 4–8).
- `Application::shortcut`: after `matchingShortcut` misses, call `_ble.presetShortcutAction(...)`; `NextPreset` → `activatePreset(nextPreset(presets()))`; `Preset1+i` → `activatePreset(i)`; return true. For Cycle/Next/Previous use `connected & _ble.onScreenMask()`; direct slot results (3–5) keep using `connected`.
- `presetShortcutAction` passes `usedPresets` built from `_presets.presets[i].used`.

- [ ] **Step 5: Build and run host tests**

Run: `sh scripts/test-host.sh && ~/.platformio/penv/bin/pio run -e esp32s3_ble_input`
Expected: `Host firmware tests passed`, then `[SUCCESS]`.

- [ ] **Step 6: Commit**

```bash
git add src/BleHid.h src/BleHid.cpp src/SerialConfig.cpp src/Application.cpp
git commit -m "feat(layout): store, activate and switch screen layout presets"
```

---

### Task 4: BOOT menu presets page

**Files:**
- Modify: `src/DeviceMenu.h`, `src/MenuBackend.h`
- Test: `tests/text-menu.cpp`

**Interfaces:**
- Consumes: `BleHid::presets()`, `BleHid::activatePreset(unsigned)` (Task 3).
- Produces: backend methods `std::string presetList()` (lines `"<n> <name>"` with ` (active)` on the active one, n = 1-based index of used presets) and `std::string activatePreset(unsigned index)` (empty on success). `FakeBackend` in `tests/text-menu.cpp` gets both.

- [ ] **Step 1: Write the failing test** — in `tests/text-menu.cpp`, with an absolute-mode `FakeBackend` whose `presetList()` returns `"1 Work Mac (active)\n2 Home PC\n"`: open the menu, type `9`, assert the prompt contains `"Presets"` and `"2 Home PC"`; type `2`, assert `activatePreset` was called with index 1 and the menu returns to the main page; in non-absolute mode `9` does nothing.

- [ ] **Step 2: Run to verify it fails**

Run: `sh scripts/test-host.sh` — Expected: FAIL (assertion or missing member).

- [ ] **Step 3: Implement** — main page lists `9 Layout presets` when `absoluteMode()`; new page `Presets` shows `"Layout presets\n" + presetList() + "Choose a number to activate."`; a digit for a used preset calls `activatePreset`, shows its error if non-empty, otherwise returns to main. Back from `Presets` goes to main. Map menu numbers to the used preset indexes in order.

- [ ] **Step 4: Run tests** — `sh scripts/test-host.sh` → `Host firmware tests passed`; `pio run -e esp32s3_ble_input` → `[SUCCESS]`.

- [ ] **Step 5: Commit**

```bash
git add src/DeviceMenu.h src/MenuBackend.h tests/text-menu.cpp
git commit -m "feat(menu): activate layout presets from the BOOT menu"
```

---

### Task 5: Setup page presets and shortcut rows

**Files:**
- Modify: `web/monitor-layout.mjs`, `web/app.mjs`, `web/index.html`, `web/style.css`
- Test: `tests/monitor-layout.mjs`, `tests/web-setup-browser.cjs`

**Interfaces:**
- Consumes: Task 3 protocol (`status.presets`, `preset-save`, `preset-activate`, `preset-delete`, shortcut actions 4–8).
- Produces: exported pure helpers in `web/monitor-layout.mjs` for tests:
  - `presetCopyName(names: string[], base: string): string` — `base`, else `"<base> 2"`, `"<base> 3"`… not in `names` (case-insensitive), truncated to 24 chars.
  - `presetPayload(preset, generation): {index,name,screens,generation}`.

- [ ] **Step 1: Write failing tests in `tests/monitor-layout.mjs`**

```js
assert.equal(presetCopyName(['Default'], 'Default'), 'Default 2');
assert.equal(presetCopyName(['Default', 'default 2'], 'Default'), 'Default 3');
assert.equal(presetCopyName([], 'Split'), 'Split');
const payload = presetPayload({index: 1, name: 'Home PC', screens: [{x:0,y:0,width:1920,height:1080,enabled:false,estimated:false,sensitivity:100}]}, 7);
assert.deepEqual(Object.keys(payload).sort(), ['generation','index','name','screens']);
assert.equal(payload.screens[0].enabled, false);
```

- [ ] **Step 2: Run to verify they fail**

Run: `node --test tests/monitor-layout.mjs` — Expected: FAIL (`presetCopyName` not exported).

- [ ] **Step 3: Implement the editor**

- When `status.presets` exists: render a tab row above the stage (one tab per used preset, active marked "Active"), plus **New preset** (copies the viewed preset, name from `presetCopyName`, first unused index), **Rename**, **Delete** (hidden when one preset), **Activate** (hidden on the active tab). Viewing switches the draft; Save sends `preset-save` for the viewed preset.
- Honour `enabled`: stop overriding it from pairing in `draw`/`seedArrangement` when presets exist. Enabled screens draw on the stage; disabled paired screens render in `.monitor-excluded` as chips with **Show on screen** (sets enabled and places it right of the rightmost screen, `estimated`). Each on-stage screen gets a **Not on screen** action. Unpaired slots stay hidden.
- Without `status.presets` (old firmware): keep today's behaviour and the `monitor-layout` op.
- Shortcuts table (`web/app.mjs`): rows for **Next preset** and each used preset (labelled by preset name), actions 4 and 5+index, same record/clear buttons; hidden without `status.presets`.

- [ ] **Step 4: Update the browser flow test** — `tests/web-setup-browser.cjs` fake device: include `presets` in `status`, accept the three preset ops, and assert that New preset creates a tab, Not on screen moves a screen to the tray, and the old-firmware status (no `presets`) still shows the single editor.

- [ ] **Step 5: Run web tests**

Run: `node --test tests/monitor-layout.mjs tests/web-serial.mjs` → all pass. If Playwright is installed outside the repo, run `python3 scripts/package-web.py` and the browser test per `docs/usb-setup.md`; otherwise note it as not run.

- [ ] **Step 6: Commit**

```bash
git add web tests/monitor-layout.mjs tests/web-setup-browser.cjs
git commit -m "feat(web): manage layout presets and their shortcuts"
```

---

### Task 6: Docs, hardware check, merge

**Files:**
- Modify: `Readme.md`, `docs/absolute-pointer.md`, `docs/usb-setup.md`

- [ ] **Step 1: Document** presets (what they are, not-on-screen, activation rule, shortcuts, BOOT menu item 9, upgrade creates "Default").
- [ ] **Step 2: Flash and verify on hardware** — `pio run -e esp32s3_ble_input -t upload --upload-port /dev/cu.usbmodem5C630518481`; over `@HID1`: `status` shows one preset "Default" equal to the previous `monitor_layout`; `preset-save` two presets ("Work Mac": Home PC off screen; "Home PC": Work Mac off screen, Home PC at the Work Mac rectangle); select slot 2 (index 1), `preset-activate` "Home PC" → log shows `[BLE] Selected computer 3`; `preset-activate` "Work Mac" with slot 3 selected → `Selected computer 2`; reboot → active preset persists.
- [ ] **Step 3: Run all tests** — `sh scripts/test-host.sh` and `node --test tests/monitor-layout.mjs tests/web-serial.mjs`.
- [ ] **Step 4: Commit, merge `--no-ff` into `main`, push**

```bash
git add Readme.md docs/absolute-pointer.md docs/usb-setup.md
git commit -m "docs: describe screen layout presets"
```
