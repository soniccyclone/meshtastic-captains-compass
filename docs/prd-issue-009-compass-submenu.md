# Issue #9 — Compass Submenu CUJ PRD

**Status:** Draft
**Owner:** Captain's Compass team
**Parent issue:** [#9 — Long-press "select" on the Compass menu entry does not open the Compass submenu](https://github.com/soniccyclone/meshtastic-captains-compass/issues/9)
**Parent PRD:** [`docs/prd-issue-002-captains-compass.md`](./prd-issue-002-captains-compass.md)

---

## 1. Problem Statement

The v0.1 PRD (issue #002) describes every user journey as starting from `Menu → Compass → <action>` — i.e., it assumes a **Compass parent menu with child entries**. The shipped firmware never built that submenu. Instead, the home banner-menu's `Compass` entry was wired directly to `CompassModule::sendCapabilityQuery()`, which silently starts a discovery broadcast.

Effect for the user:

- Long-press USER on the highlighted `Compass` home-menu entry produces no visible UI change. The screen stays on the home menu.
- A discovery LoRa packet is transmitted, but with no on-screen indication.
- All five other journeys described in the v0.1 PRD (`Calibrate`, `Save Treasure`, `Treasures`, `End Session`, status check) have **no entry point at all** — there is nowhere in the UI for the user to invoke them.

The visible "long-press does nothing" symptom is the surface; the underlying defect is that the parent-menu node and its children were never wired in.

## 2. Personas

Carried forward from the parent PRD without change. Primary user is a hiking/biking field user on a Heltec T114; secondary is the *Desire* (the person being tracked).

## 3. Scope

**In scope.** The single critical user journey of *getting from the home menu into the Compass feature surface*. Specifically:

- Selecting `Compass` from the home banner-menu pushes a child banner-menu, not a single action.
- Each child entry routes to either an existing implemented surface or an explicit "not yet implemented" placeholder. No child entry should silently do nothing.
- Pressing `Back` from the child menu returns to the home menu without side effects.

**Out of scope.** The internals of each child journey (calibration UX, treasure-save UX, etc.) are already specified in the parent PRD's CUJ-1..CUJ-8. This PRD only defines the *menu entry point* for each.

Bugs 2 (PortNum receive-handler not registered) and 3 (PortNum value drift between PRD prose and proto) from issue #9 are implementation concerns — they are tracked here for completeness but their resolution lives in the technical design doc.

## 4. Critical User Journey: Opening the Compass Submenu

**Pre-conditions:** Device is booted and on the home screen rotation. User has not yet entered any Compass surface this session.

**Steps:**

1. Short-press USER repeatedly to open the home banner-menu and scroll until `Compass` is highlighted.
2. Long-press USER (≥ 1 s) to "select" the `Compass` entry.
3. The screen pushes a **Compass banner-menu** containing the entries listed in §5. The first entry (`Find Desire`) is highlighted.
4. Short-press USER to scroll within the Compass menu. Long-press USER to select an entry.
5. Selecting an entry transitions to that entry's screen (existing entries) or shows a transient `Coming soon` toast and remains on the Compass menu (placeholder entries).
6. Pressing `Back` (or selecting the `Back` entry) returns to the home banner-menu without sending any LoRa packets and without modifying any persisted state.

**Success state:** User can reach `Find Desire`, `Calibrate`, and `Save Treasure` from the home menu via two long-presses. Every other entry from the parent PRD's vocabulary either works or visibly identifies itself as not-yet-built.

**Logging contract (testability hook):**

- Every time the Compass submenu is *opened*, exactly one `[Screen]` redraw line is emitted within the same second as the `LONG PRESS RELEASE` log line.
- Opening the submenu does **not** emit `[UserButton] Compass: sent CAPABILITY_QUERY`. The query is sent only when the user explicitly selects `Find Desire` inside the submenu.
- Opening the submenu does **not** emit `No modules interested in portnum=...`.

## 5. Submenu Inventory

The Compass child banner-menu contains these entries, in this order. Wording matches the parent PRD's CUJ section titles where one exists.

| # | Label              | Action on select                                         | Status (this PR)   | Backing CUJ in parent PRD |
|---|--------------------|----------------------------------------------------------|--------------------|---------------------------|
| 1 | `Find Desire`      | Calls `CompassModule::sendCapabilityQuery()` (existing). | Implemented        | CUJ-3                     |
| 2 | `Treasures`        | Pushes the saved-treasures list. Selecting one starts treasure navigation via `CompassState::startTreasureNav(i)`. | Implemented        | CUJ-6                     |
| 3 | `Save Treasure`    | Calls `CompassState::saveTreasure(...)` with current GPS position and an auto-generated label. | Implemented        | CUJ-5                     |
| 4 | `Calibrate`        | Calls `CompassState::startCalibration()` (existing).     | Implemented        | CUJ-2                     |
| 5 | `End Session`      | Calls `CompassModule::sendSessionEnd()` if a session is active; otherwise shows transient `No active session`. | Implemented        | CUJ-7                     |
| 6 | `Status`           | Shows magnetometer init result + paired-peer count + treasure count. Read-only screen, dismissed by `Back` or any input. | Implemented (new screen) | CUJ-1                     |
| 7 | `Settings`         | Shows `Coming soon` toast.                               | Placeholder        | (not yet specified)       |
| 8 | `Back`             | Returns to home banner-menu.                             | Implemented        | n/a                       |

**Justification for `Status` over the issue-reporter's `Manage`.** The reporter's recall listed `Manage` as one of the entries. The parent PRD never uses that word; CUJ-1 ("First-Time Hardware Setup") describes a status-check journey. We adopt `Status` to align with the parent PRD's vocabulary. `Manage` (paired-peer pruning, treasure deletion UI) is deferred to v0.3 and tracked as a separate issue.

**Justification for `Pair New Desire` collapsing into `Find Desire`.** The reporter listed both. In the parent PRD these are the same journey — discovery is the entry point to pairing. We use the parent PRD's wording (`Find Desire`) and do not duplicate.

## 6. Acceptance Criteria

Tied 1:1 to the issue's acceptance criteria, with logging assertions made explicit.

- [ ] Long-press USER on the home menu's `Compass` entry pushes the Compass banner-menu screen.
- [ ] The pushed screen contains the entries in §5, in that order.
- [ ] The `LONG PRESS RELEASE` log line is followed within 1 s by a `[Screen]` redraw line.
- [ ] Opening the submenu does **not** emit `[UserButton] Compass: sent CAPABILITY_QUERY`.
- [ ] Pressing `Back` from the submenu returns to the home banner-menu and emits no LoRa packets.
- [ ] Selecting `Find Desire` emits `Compass: sent CAPABILITY_QUERY` and transitions to the `DISCOVERING` screen (existing behavior).
- [ ] Selecting `Calibrate` transitions to the `CALIBRATING` screen.
- [ ] Selecting `Save Treasure` while GPS has a fix calls `CompassState::saveTreasure(...)` and shows a confirmation toast.
- [ ] Selecting `Save Treasure` *without* a GPS fix shows `No GPS fix` and does not write to flash.
- [ ] Sending a `CompassPacket` to self (loopback) is processed by `CompassModule::handleReceived()`. The `[ModuleManager] No modules interested in portnum=...` log line does not appear for that packet. (Bug 2 — surface-level acceptance only; root cause analysis lives in the TDD.)
- [ ] The PortNum value used by the firmware matches the value documented in the parent PRD's §7. (Bug 3 — reconciliation only; the source of truth is the proto, the PRD prose follows.)

## 7. Out of Scope (deferred)

- Full implementation of `Settings`. Tracked as a follow-up bd issue.
- A `Manage` entry for pruning paired peers / deleting treasures. Tracked as v0.3.
- Replacing the banner-menu pattern with a richer in-app menu. The banner-menu is consistent with how upstream Meshtastic surfaces all sub-menus (Bluetooth, Mesh, etc.); changing the pattern is a v2 concern.
- Animations / transitions on submenu push.

## 8. Open Questions

1. **Banner-menu vs full screen takeover for `Status`?** The other entries route to existing full-screen states. `Status` is a new read-only screen. Decision to be made in the TDD; default is "render via the existing module-frame mechanism, dismissed by any input".
2. **Default selection on submenu open** — sticky (last-used) vs. always `Find Desire`? Default is always `Find Desire` until a user complaint warrants stickiness. (Sticky requires NVS state; not worth the complexity for v0.2.)
3. **Treasure auto-label format on `Save Treasure`.** Default proposal: `T-<unix-time-mod-10000>`. Bikeshed in TDD review.

## 9. Decisions

- The Compass entry on the home banner-menu pushes a child banner-menu. It does **not** also fire a side-effect packet. (This is the core fix for Bug 1.)
- The submenu uses the same banner-menu UI primitive as `Mesh`, `Bluetooth`, etc. We do not introduce a new menu pattern.
- The submenu inventory tracks the parent PRD's CUJ vocabulary, not the issue reporter's recall. Where they disagree, the parent PRD wins and we note the substitution in §5.
- The PortNum reconciliation (Bug 3) is doc-side only: the firmware proto's value of `300` is correct (in the private-app range as commented in `patches/apply.py`); any prose that implies a different value is stale and gets updated.
