# Issue #9 — Compass Submenu Technical Design

**Status:** Draft
**Owner:** Captain's Compass team
**Implements:** [`docs/prd-issue-009-compass-submenu.md`](./prd-issue-009-compass-submenu.md)
**Parent TDD:** [`docs/tdd-issue-002-captains-compass.md`](./tdd-issue-002-captains-compass.md)

---

## 1. Scope

This TDD addresses three nested defects surfaced together in [issue #9](https://github.com/soniccyclone/meshtastic-captains-compass/issues/9):

| Bug | Visibility    | Fix locus                                  |
|-----|---------------|---------------------------------------------|
| 1   | User-visible  | `patches/apply.py::patch_screen_cpp`, plus a small new `CompassMenu` source-overlay surface |
| 2   | Log-visible   | `firmware-overlay/src/modules/CompassModule/CompassModule.{h,cpp}` (likely `wantPacket` override) — pending root-cause confirmation against upstream source at the pinned ref |
| 3   | Doc only      | `docs/prd-issue-002-captains-compass.md` §7                              |

The PRD treats Bug 1 as the headline. Bugs 2 and 3 are caught in the same code path and fixed in the same trunk to avoid issue-fragmentation, but each gets its own bd issue and PR into the trunk.

## 2. Existing Code Map (read before changing anything)

The state of the world today, by file, with line refs against `main` at the time of this TDD's first commit:

- [`firmware-overlay/src/modules/CompassModule/CompassModule.h`](../firmware-overlay/src/modules/CompassModule/CompassModule.h) — declares `CompassModule : public SinglePortModule`. Already exposes `sendCapabilityQuery()`, `sendSessionEnd()`, `notifyUIChanged()`, plus a public `instance` static pointer. The handlers we need to wire from a submenu (`startCalibration`, `saveTreasure`, `startTreasureNav`, `endSession`) live on `CompassState` and are reachable via `CompassModule::instance->state()`.
- [`firmware-overlay/src/modules/CompassModule/CompassModule.cpp`](../firmware-overlay/src/modules/CompassModule/CompassModule.cpp) — `SinglePortModule("compass", meshtastic_PortNum_COMPASS_APP)` is the registration. `handleReceived()` decodes a `CompassPacket` and dispatches by `pkt.type`. No `wantPacket` override.
- [`firmware-overlay/src/modules/CompassModule/CompassUI.cpp`](../firmware-overlay/src/modules/CompassModule/CompassUI.cpp) — `handleInputEvent` switches on `state->getState()` and only handles `DISCOVERING`, `PAIR_INCOMING`, `CALIBRATING`, `TRACKING`, `TRACKING_TREASURE`, `SESSION_PAUSED`. There is no `IDLE` case — which is correct: when the home menu is up the module's UI must not eat input. The submenu is therefore *not* a `CompassUI` state; it is an upstream-banner-menu screen pushed by the home menu's callback.
- [`patches/apply.py::patch_screen_cpp`](../patches/apply.py) — the *current* integration. Three substitutions in `src/graphics/draw/MenuHandler.cpp`:
  1. enum extension: `Sleep, Compass, enumEnd`
  2. options-array entry: `optionsArray[options] = "Compass"; optionsEnumArray[options++] = Compass;`
  3. bannerCallback case: `else if (selected == Compass) { CompassModule::instance->sendCapabilityQuery(); }` ← **this is Bug 1**
- [`firmware-overlay/protobufs/meshtastic/compass.proto`](../firmware-overlay/protobufs/meshtastic/compass.proto) — header comment says "port (300)". Patch in `patches/apply.py::patch_portnums_proto` adds `COMPASS_APP = 300;` to upstream's `meshtastic_PortNum` enum, in the private-app range 256–511.

## 3. Bug 1 — Architectural Fix

### 3.1 Root cause

The home menu's `Compass` entry was wired as a *terminal action* (fires `sendCapabilityQuery()`), not as a *submenu pusher*. The parent PRD's CUJ-1..CUJ-8 every assume `Menu → Compass → <child>`, and the implementation conflated `Compass` (the parent) with `Find Desire` (one specific child).

### 3.2 Fix shape

Replace the home menu's `Compass` callback with a call into a new `CompassMenu` overlay class that:

1. Builds an `optionsArray` / `optionsEnumArray` for the seven entries from PRD §5.
2. Calls upstream's banner-menu primitive to push itself onto the screen with its own `bannerCallback` lambda.
3. In its callback, maps each entry to one of:
   - `CompassModule::instance->sendCapabilityQuery()` for `Find Desire`
   - `CompassModule::instance->state()->startCalibration(); CompassModule::instance->notifyUIChanged()` for `Calibrate`
   - `CompassModule::instance->state()->saveTreasure(label, lat, lon, ts)` for `Save Treasure` (with toast on success / `No GPS fix` on failure)
   - A nested treasure-list submenu for `Treasures` (entries dynamically built from `state()->treasureCount()`; selecting one calls `state()->startTreasureNav(i)` then `notifyUIChanged()`)
   - `CompassModule::instance->sendSessionEnd()` for `End Session` (no-op + toast if no active session)
   - A new full-screen `Status` view (see §3.4) for `Status`
   - A `Coming soon` toast for `Settings`

The existing `Compass` enum entry, options-array slot, and `Compass` home-menu label all stay; only the callback body changes.

### 3.3 Why an overlay class instead of a third entry in `patch_screen_cpp`

Two reasons:

1. **Patch readability.** The seven-entry callback body is ~80 lines. Inlining 80 lines of generated string concatenation into `apply.py` makes the patch unreviewable. Pulling the body into `firmware-overlay/src/modules/CompassModule/CompassMenu.{h,cpp}` lets `patch_screen_cpp`'s callback stay a one-liner: `CompassMenu::open();`.
2. **Single source of truth for the inventory.** The submenu inventory is a product fact (PRD §5). It belongs in code that's part of the overlay, not in a Python string buried in `apply.py`. Future entry additions become one-file edits.

The `firmware-overlay/src/modules/Modules.cpp` patch already includes `CompassModule.h`; we extend the Modules.cpp include with `CompassMenu.h` (one extra line in `patch_modules_cpp`) so the home-menu callback's `CompassMenu::open()` symbol resolves.

### 3.4 `Status` screen

A new `CompassUI` state (`STATUS`) plus a `drawStatus()` renderer. Read-only:

```
Captain's Compass — Status
mag: OK            (or NOT_FOUND / INIT_FAILED)
peers: 0           (paired-peer count from CompassState)
treasures: 0       (CompassState::treasureCount())
port: 300          (literal — ties to Bug 3 reconciliation)
```

`STATUS` is added to `wantUIFrame()` and `interceptingKeyboardInput()` so any input (or BACK) returns to home. State is set by the menu callback via a new `CompassState::startStatus()` and cleared on input.

### 3.5 Banner-menu primitive — implementation-phase discovery

The exact upstream call to "push a banner-menu" depends on `MenuHandler.cpp` at the pinned firmware ref `v2.7.15.567b8ea`. The implementation bd issue starts with `make setup` to fetch firmware source, then reads `MenuHandler.cpp` to find:

1. The signature for setting up a transient banner-menu (probably along the lines of how `Bluetooth` or `Mesh` parent entries push their children — locate by grepping for entries with submenus).
2. Whether banner-menus stack natively or require manual back-handling.
3. The toast / transient-message API used by upstream (for `Coming soon`, `No GPS fix`, `No active session`).

The TDD does not pre-commit to specific symbol names because the patch system's anchor-substitution model means we need to verify against the actual ref before naming things. **The implementation issue's first acceptance criterion is "documented the upstream API for `Bluetooth`'s submenu push and re-used the same primitive."**

## 4. Bug 2 — Loopback Receive-Handler

### 4.1 Symptom

```
[UserButton] handleReceived(LOCAL) (... Portnum=300 ...)
[UserButton] No modules interested in portnum=300, src=LOCAL
```

This fires on the local loopback of the device's *own* outbound `CAPABILITY_QUERY`. The "no modules interested" line means no module's `wantPacket()` returned true for this packet under whatever filter the local-loopback dispatcher uses.

### 4.2 What we know without firmware source on disk

- `CompassModule` derives from `SinglePortModule("compass", meshtastic_PortNum_COMPASS_APP)`. The default `SinglePortModule::wantPacket` (per upstream conventions) is `return p->decoded.portnum == ourPortNum;` and `ourPortNum = 300`. Both the module's claimed port and the inbound packet's port are 300, so a naïve `wantPacket` match should succeed.
- The outbound packet has `Portnum=300` in its log, so `ourPortNum` is set correctly at construction time. The module is alive and registered.
- Therefore the filter that's rejecting this is *not* a portnum mismatch. Most likely candidates:
  1. **Self-source filter.** Upstream may exclude packets where `mp.from == nodeDB->getNodeNum()` to prevent infinite echoes. Local-loopback is by definition self-sourced.
  2. **Broadcast-address filter on local source.** The packet's `to` is `0xffffffff`. Upstream may not deliver broadcast packets back to local modules even if a module wants the portnum.
  3. **`src=LOCAL` short-circuit.** The `RX_SRC_LOCAL` enum value may bypass module dispatch entirely for some packet shapes.

### 4.3 Investigation plan (first commit on the impl branch)

1. `make setup` — fetch the pinned firmware source.
2. `grep -nR "No modules interested" vendor/meshtastic-firmware/src` — find the log call site.
3. Read the function containing it. Identify which filter (`wantPacket` return, source filter, address filter) caused the early-out.
4. Decide the fix:
   - If the filter is *intended* and our use case is the exception, override `CompassModule::wantPacket` (or whichever method is checked) to return true for our packets even on the LOCAL path.
   - If the filter is *not intended* and just hasn't been exercised, fix it upstream and patch via `patches/apply.py` (preferring an overlay-only fix if at all possible to avoid forking upstream behavior).

### 4.4 Acceptance evidence

After the fix, on a local-loopback, the log should show the CompassModule's `handleReceived()` running on its own outbound packet — i.e., `handleCapabilityQuery(self) → sendCapabilityAdv(self)`. To prevent infinite echoes, `handleCapabilityQuery` must early-out when `mp.from == nodeDB->getNodeNum()`. That guard is added in the same commit as the receive-filter fix.

```cpp
void CompassModule::handleCapabilityQuery(const meshtastic_MeshPacket &mp) {
    if (nodeDB && mp.from == nodeDB->getNodeNum()) return;   // self-loopback, ignore
    sendCapabilityAdv(mp.from);
}
```

(Same self-guard added defensively to all `handle*` methods that send a reply.)

## 5. Bug 3 — Doc Reconciliation

The parent PRD `docs/prd-issue-002-captains-compass.md` §7 currently reads:

> **Port:** `meshtastic_PortNum_COMPASS_APP` (register with Meshtastic upstream; use reserved range 512–1023 for prototyping)

The proto patch (`patches/apply.py::patch_portnums_proto`) places the port at `300`, in Meshtastic's **256–511 private-app range** — not the 512–1023 range the PRD prose mentions. The proto-side comment in `apply.py` is correct ("private-app range; not registered upstream"). The PRD prose is the stale piece.

Fix: update PRD §7 to read:

> **Port:** `meshtastic_PortNum_COMPASS_APP = 300` — chosen from Meshtastic's 256–511 private-app range. Not registered upstream; if Captain's Compass ships beyond this fork, propose a stable allocation against `meshtastic/protobufs#portnums.proto`.

This is a one-paragraph change. It lands as part of the same trunk merge as Bug 2's code fix, so the doc and the proto agree at every commit on `main`.

## 6. Patch System Changes

`patches/apply.py` modifications:

### 6.1 `patch_modules_cpp`
Extend the include block to also include `CompassMenu.h`:

```python
text = text.replace(
    include_anchor,
    include_anchor + f'\n// {MARKER}\n'
    '#include "modules/CompassModule/CompassModule.h"\n'
    '#include "modules/CompassModule/CompassMenu.h"',
    1,
)
```

No new statement in `setupModules()` — the menu is opened lazily on user input, no construction needed at boot.

### 6.2 `patch_screen_cpp`
Replace the bannerCallback case body. Before:

```python
cb_replacement = (
    cb_anchor + " "
    f"/* {MARKER} */ else if (selected == Compass) {{\n"
    "            if (CompassModule::instance) {\n"
    "                CompassModule::instance->sendCapabilityQuery();\n"
    "            }\n"
    "        }"
)
```

After:

```python
cb_replacement = (
    cb_anchor + " "
    f"/* {MARKER} */ else if (selected == Compass) {{\n"
    "            CompassMenu::open();\n"
    "        }"
)
```

`CompassMenu::open()` does its own `nullptr` check on `CompassModule::instance` and is a no-op if the module isn't constructed (defensive — shouldn't happen given setupModules() ordering, but cheap to guard).

The include anchor in `patch_screen_cpp` already pulls in `CompassModule.h`; we extend it to also pull `CompassMenu.h`.

### 6.3 No new top-level patches
Bug 2's fix is overlay-only (lives in `firmware-overlay/`). Bug 3 is doc-only. The patch system gains no new entry points.

## 7. File Changes Summary

```
+ firmware-overlay/src/modules/CompassModule/CompassMenu.h            (~40 lines: open(), CompassMenu class)
+ firmware-overlay/src/modules/CompassModule/CompassMenu.cpp          (~120 lines: 7 entries + dispatch)
M firmware-overlay/src/modules/CompassModule/CompassModule.cpp        (+~10 lines: self-loopback guards in handle* methods, wantPacket override if §4.3 finds it needed)
M firmware-overlay/src/modules/CompassModule/CompassModule.h          (+1 line if wantPacket is overridden)
M firmware-overlay/src/modules/CompassModule/CompassUI.cpp            (+~30 lines: drawStatus, STATUS-state input handling)
M firmware-overlay/src/modules/CompassModule/CompassUI.h              (+1 method declaration)
M firmware-overlay/src/modules/CompassModule/CompassState.cpp         (+~5 lines: startStatus())
M firmware-overlay/src/modules/CompassModule/CompassState.h           (+~3 lines: STATUS enum value, startStatus decl)
M patches/apply.py                                                    (~10 lines net: include extension, callback body swap)
M docs/prd-issue-002-captains-compass.md                              (~3 lines: §7 PortNum prose)
+ docs/qa-issue-009-compass-submenu.md                                (manual QA test plan, mirrors §6 of the CUJ PRD's acceptance criteria)
```

## 8. Bead Breakdown

Each bead is one PR into `feat/9-compass-submenu`. Listed in dependency order; deps modeled in beads.

- **bd-9-1 — Submenu skeleton.** New `CompassMenu.{h,cpp}`. Stubs all seven callbacks to `LOG_INFO("Compass menu: %s clicked")`. Wire `patch_screen_cpp` to call `CompassMenu::open()`. Acceptance: long-press on `Compass` opens a banner-menu with all seven entries; selecting any entry logs and returns to home menu.
- **bd-9-2 — Wire existing actions.** `Find Desire` → `sendCapabilityQuery`. `Calibrate` → `state()->startCalibration() + notifyUIChanged()`. `End Session` → `sendSessionEnd()` (no-op + toast if no peer). Depends on bd-9-1.
- **bd-9-3 — `Save Treasure` action.** Calls `state()->saveTreasure(...)` with auto-label, current GPS, current time. Toast on success; `No GPS fix` if `localPosition.latitude_i == 0 && longitude_i == 0`. Depends on bd-9-1.
- **bd-9-4 — `Treasures` nested submenu.** Build entries dynamically from `state()->treasureCount()`. Selecting an entry calls `state()->startTreasureNav(i) + notifyUIChanged()`. Empty state shows a `No treasures saved` entry. Depends on bd-9-1.
- **bd-9-5 — `Status` screen.** New `STATUS` state in CompassState/CompassUI. `drawStatus()` renderer. Any input or BACK returns to home. Depends on bd-9-1.
- **bd-9-6 — `Settings` placeholder.** `Coming soon` toast. Depends on bd-9-1. Trivial; can ride along with bd-9-1 if reviewer prefers.
- **bd-9-7 — Bug 2 root-cause + fix.** `make setup` + investigation per §4.3. Lands the `wantPacket`/handler-guard fix and the corresponding test evidence (paste of fixed log lines into the bd issue's notes). Independent of bd-9-1..bd-9-6 — can land in parallel.
- **bd-9-8 — Bug 3 PRD prose update.** Three-line edit to `prd-issue-002-captains-compass.md` §7. Depends on bd-9-7's actual landed PortNum value being `300` (no-op confirmation step before merge).
- **bd-9-9 — QA test plan.** New `docs/qa-issue-009-compass-submenu.md` mirroring CUJ PRD §6 acceptance criteria as a manual test checklist. Depends on bd-9-1..bd-9-7 closed (test plan needs the actual UX to mirror).
- **bd-9-10 — Trunk → main PR.** After all above are closed and trunk is green, open `feat/9-compass-submenu` → `main` PR. Closes #9.

## 9. Test Plan

Hardware-only — there is no Portduino smoke test for the menu pipeline at this firmware ref. Manual QA on a T114, captured in `docs/qa-issue-009-compass-submenu.md` (bd-9-9).

Pre-merge gate per bead:
1. `make build` succeeds.
2. UF2 flashes.
3. The bead's specific acceptance criterion (PRD §6 line) is observably true on the device, with the relevant log line pasted into the bd issue's notes.

## 10. Open Questions

1. **Banner-menu primitive.** Resolved by reading `MenuHandler.cpp` at the pinned ref before bd-9-1 starts. If upstream doesn't expose a clean "push child menu" API, fallback is a custom `CompassMenu` screen subclass that owns its own input intercept — uglier but feasible.
2. **Toast primitive.** Same — discover from `MenuHandler.cpp` or fall back to a 2-second `CompassUI` state for transient messages.
3. **`Status` screen vs. banner-menu.** The PRD §8 noted this is a TDD decision. Going with a CompassUI state (full screen, dismissed by any input). Banner-menu would force a pseudo-entry just for the back action; the full-screen path is cleaner and matches `CALIBRATING`'s pattern.
4. **`Save Treasure` label.** Going with `T-<unix-time-mod-10000>` per PRD §8.3 default. Future bd issue can add a label-edit screen if users complain.

## 11. Risk Register

| ID | Risk | Likelihood | Impact | Mitigation |
|----|------|------------|--------|------------|
| R-9-1 | `MenuHandler.cpp` at v2.7.15.567b8ea has no clean "push submenu" primitive | Medium | Medium | Fallback custom `CompassMenu` screen subclass with its own input intercept (§3.5). Ugly but unblocked. |
| R-9-2 | Bug 2's filter is in upstream code we don't want to fork | Low | High | Prefer overlay-side `wantPacket` override; only patch upstream if no overlay path works. |
| R-9-3 | `patch_screen_cpp` anchors drift in a future firmware bump | Low | Low | Existing `--dry-run` mode already detects drift; CI runs it weekly. |
| R-9-4 | Treasure-list submenu hits banner-menu's max-entries limit on a device with many treasures | Very Low | Low | Cap at first N entries; show a `… more` entry. Out of scope for v0.2 unless `MAX_TREASURES` is already > banner-menu max. Verify in bd-9-4. |
