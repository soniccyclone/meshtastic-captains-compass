# Captain's Compass — Issue #9 Manual QA Test Plan

**Issue:** [#9 — Long-press "select" on the Compass menu entry does not open the Compass submenu](https://github.com/soniccyclone/meshtastic-captains-compass/issues/9)
**Firmware pin:** `v2.7.15.567b8ea`
**Audience:** Same audience and ground rules as [`docs/qa-issue-002-captains-compass.md`](./qa-issue-002-captains-compass.md). This plan only covers the issue-#9 fixes; for v0.2-wide regression coverage run that document too.

---

## 0. What this checklist verifies

The three nested defects from issue #9, end-to-end:

- **Bug 1** — Long-press on the home menu's `Compass` entry pushes a banner-menu (it did nothing visible before).
- **Bug 2** — Local loopback of an outbound Compass packet is processed by `CompassModule::handleReceived()` (was silently dropped before).
- **Bug 3** — Documented PortNum value matches the firmware's actual value of `300`.

Each test maps 1:1 to a line in [`docs/prd-issue-009-compass-submenu.md`](./prd-issue-009-compass-submenu.md) §6.

## 1. Equipment

Same as the parent QA doc: one T114 (Tests 1–11). A second T114 is **not** required for issue #9 because none of these journeys cross the radio boundary.

You will need a USB-serial console open the entire time. Recommended baud: 115200, line-buffered. Filter for `[UserButton]`, `[ModuleManager]`, `[Screen]`, and `Compass:` log prefixes — that's all you need.

## 2. Pre-flight

| # | Check | Expected |
|---|---|---|
| 0.1 | `output/firmware.uf2` exists and was built from the issue-#9 trunk | UF2 matches the SHA in the GitHub Actions artifact for the latest \`feat/9-compass-submenu\` build |
| 0.2 | T114 in DFU mode, flash UF2, power-cycle | Device boots, home banner shows |
| 0.3 | Serial console opens, you see `[Screen]` redraw lines on boot | If you don't see these you have a console-buffering problem to fix before continuing |

## 3. Tests

### Test 1 — Submenu opens (Bug 1, primary)
**Maps to:** PRD §6 line 1 ("Long-press USER on the home menu's `Compass` entry pushes the Compass banner-menu screen.")

**Setup:** On home screen rotation. No active Compass session.

**Steps:**
1. Short-press USER repeatedly until the home banner-menu appears with `Compass` highlighted.
2. Long-press USER (≥1 s).

**Expected:**
- Screen pushes a new banner-menu titled `Compass`.
- First entry highlighted is `Find Desire`.
- Within 1 s of the long-press, the serial console shows a `[Screen]` redraw line.
- Console does **not** show `[UserButton] Compass: sent CAPABILITY_QUERY`.
- Console does **not** show `No modules interested in portnum=300`.

Pass / Fail / Notes:

---

### Test 2 — Submenu inventory matches PRD §5
**Maps to:** PRD §6 line 2 ("The pushed screen contains the entries in §5, in that order.")

**Setup:** Compass submenu open from Test 1.

**Steps:**
1. Short-press USER to scroll through every entry in the submenu, top to bottom.

**Expected, in this order:** `Back`, `Find Desire`, `Treasures`, `Save Treasure`, `Calibrate`, `End Session`, `Status`, `Settings`. Eight entries total.

Pass / Fail / Notes:

---

### Test 3 — Back returns home with no side effects
**Maps to:** PRD §6 line 5 ("Pressing `Back` from the submenu returns to the home banner-menu and emits no LoRa packets.")

**Setup:** Compass submenu open.

**Steps:**
1. Scroll to `Back` (or just open the submenu fresh — `Back` is the first entry).
2. Long-press USER.

**Expected:**
- Screen returns to home banner-menu.
- No `[RadioIf] Started Tx` lines in the console between the long-press and the return.
- No `Compass:` log lines emitted.

Pass / Fail / Notes:

---

### Test 4 — Find Desire transitions to DISCOVERING
**Maps to:** PRD §6 line 6 ("Selecting `Find Desire` emits `Compass: sent CAPABILITY_QUERY` and transitions to the `DISCOVERING` screen.")

**Setup:** Compass submenu open.

**Steps:**
1. Scroll to `Find Desire`. Long-press USER.

**Expected:**
- Screen transitions to the `DISCOVERING` screen (shows `Find Desire` title; either `Searching...` or a list of nearby Compass nodes).
- Console shows `Compass: sent CAPABILITY_QUERY`.
- Console shows `[RadioIf] Started Tx (... Portnum=300 ...)`.
- **NEW (Bug 2 fix):** within ~100 ms of the Tx, console shows the local loopback being delivered to CompassModule. **Specifically, the line `No modules interested in portnum=300, src=LOCAL` should NOT appear.**

Pass / Fail / Notes:

---

### Test 5 — Calibrate transitions to CALIBRATING
**Maps to:** PRD §6 line 7.

**Setup:** Compass submenu open.

**Steps:**
1. Scroll to `Calibrate`. Long-press USER.

**Expected:**
- Screen transitions to the `Calibrating` screen with the sample counter visible.
- Long-press USER or BACK returns to home menu.

Pass / Fail / Notes:

---

### Test 6 — Save Treasure with valid GPS
**Maps to:** PRD §6 line 8.

**Setup:** Outdoor location with GPS lock (verify the home screen shows a fix). Compass submenu open.

**Steps:**
1. Scroll to `Save Treasure`. Long-press USER.

**Expected:**
- A toast appears for ~2 s: `T-NNNN saved` (where NNNN is unix-time mod 10000).
- After the toast, screen returns to home banner-menu.
- Console shows no errors.

**Verification follow-up:**
2. Re-open Compass submenu → `Treasures`. The new treasure appears in the list.

Pass / Fail / Notes:

---

### Test 7 — Save Treasure without GPS fix
**Maps to:** PRD §6 line 9.

**Setup:** Indoors / GPS not yet acquired (home screen shows no fix). Compass submenu open.

**Steps:**
1. Scroll to `Save Treasure`. Long-press USER.

**Expected:**
- Toast appears for ~2 s: `No GPS fix`.
- Treasure count is unchanged. (Re-open `Treasures` to verify.)

Pass / Fail / Notes:

---

### Test 8 — Bug 2 self-loopback receives but doesn't echo
**Maps to:** PRD §6 line 10 ("Sending a `CompassPacket` to self (loopback) is processed by `CompassModule::handleReceived()`. The `[ModuleManager] No modules interested in portnum=...` log line does not appear for that packet.")

**Setup:** No paired peer. Compass submenu open.

**Steps:**
1. Select `Find Desire`. (This emits a CAPABILITY_QUERY — a self-loopback case.)
2. Wait 5 seconds.
3. Long-press BACK to return to home.

**Expected console sequence within ~100 ms of step 1:**

```
[UserButton] Compass: sent CAPABILITY_QUERY
[RadioIf] Started Tx (... Portnum=300 ...)
... handleReceived(LOCAL) ... Portnum=300 ...
```

**Must NOT appear:**

- `No modules interested in portnum=300, src=LOCAL`
- A second `Compass: sent CAPABILITY_ADV` to ourselves (the self-guard in `handleCapabilityQuery` short-circuits before the reply).

If the second `CAPABILITY_ADV` line **does** appear, that's a regression in the self-guard — file as a follow-up with the log excerpt.

Pass / Fail / Notes:

---

### Test 9 — Status screen
**Maps to:** PRD §5 row 6.

**Setup:** Compass submenu open.

**Steps:**
1. Scroll to `Status`. Long-press USER.

**Expected:**
- Screen pushes a read-only Status view with four lines:
  - `mag: OK` *or* `mag: not found` (matches the boot-time mag init log)
  - `session: (none)` (no active peer)
  - `treasures: N` (matches the count from Test 6/7)
  - `port: 300` (literal — matches `compass.proto`)
- Footer: `(any input dismisses)`.
2. Press any user-button input.

**Expected:** screen returns to home banner-menu.

Pass / Fail / Notes:

---

### Test 10 — Settings placeholder
**Maps to:** PRD §5 row 7.

**Setup:** Compass submenu open.

**Steps:**
1. Scroll to `Settings`. Long-press USER.

**Expected:**
- Toast appears for ~2 s: `Coming soon`.
- After the toast, screen returns to home banner-menu.

Pass / Fail / Notes:

---

### Test 11 — Bug 3 doc reconciliation (no hardware needed)
**Maps to:** PRD §6 line 11.

**Steps:**
1. Open `docs/prd-issue-002-captains-compass.md` §7.

**Expected:** the prose reads `meshtastic_PortNum_COMPASS_APP = 300 — chosen from Meshtastic's 256–511 private-app range...` (or equivalent). The "512–1023 prototyping range" wording from the original v0.1 PRD is **not** present.

2. Open `firmware-overlay/protobufs/meshtastic/compass.proto`. Confirm the comment header says `port (300)`.

3. `grep -n "COMPASS_APP" patches/apply.py` — confirm the patcher inserts `COMPASS_APP = 300` into upstream's `meshtastic_PortNum`.

Pass / Fail / Notes:

---

## 4. Issue template

If any test fails, copy this into a new GitHub issue:

```markdown
## QA failure on Captain's Compass issue #9 fix

**Test:** <test number + name from this doc>
**Hardware:** Heltec T114 rev <2.0 / other>, mag breakout <model>
**Firmware:** UF2 from <branch / SHA>
**Expected:** <copy from above>
**Actual:** <what actually happened>

### Console log (relevant excerpt)
\`\`\`
<paste>
\`\`\`

### Screen photo
<attach>
```
