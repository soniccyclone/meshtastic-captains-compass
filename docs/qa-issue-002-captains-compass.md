# Captain's Compass — Manual QA Test Plan
**Issue:** #2
**Version:** 0.2 (firmware pin: `b7a9555905cf9bc22ef01028e108ab9fbf5a1a01`)
**Audience:** A tester with physical T114 hardware. No firmware-engineering experience required, but you do need to be comfortable with: flashing UF2 files, opening a USB serial console, and following sequenced steps.

---

## 0. What This Document Is

This is a checklist of every behavior in the v0.2 firmware that needs human eyes on real hardware to verify. The build pipeline already proves the code compiles and links; the build artifact is a `firmware.uf2` file that you flash onto a Heltec T114. What it doesn't prove is that the device, once flashed, actually does what we want when held in your hand.

Each test below has:
- **Setup** — what to prepare before running
- **Steps** — what to do, in order
- **Expected** — what should happen
- **Pass / Fail** — your verdict
- **Notes** — anything weird, screen photos, log snippets

When something fails, file an issue (template in §10) and keep going to the next test if you can — partial results are useful.

---

## 1. Equipment

You need:

| Item | Quantity | Notes |
|---|---|---|
| Heltec Mesh Node T114 (Rev 2.0+) | **1 minimum, 2 recommended** | Two devices unlocks the pairing/tracking tests (Tests 4–8). One device covers Tests 0–3 only. |
| QMC5883L magnetometer breakout | 1 | **Must be QMC5883L specifically.** Many "GY-271" boards on Amazon are actually HMC5883L (different chip, different register map). Check the silkscreen — the chip is labeled `QMC5883L` or has a 5×5×1mm 16-pin LGA package. Do NOT substitute HMC5883L; the firmware does not support it and will report `INIT_FAILED`. |
| Jumper wires | 4 | Female-to-female if the T114's I2C header is male; female-to-male if you're soldering to a breadboard. |
| USB-C cable | 1–2 | T114 charges and exposes its bootloader over USB-C. |
| Computer | 1 | macOS, Linux, or Windows. Needs to: open a serial console, drag-and-drop UF2 files. |
| Outdoor area with GPS view | for tracking tests | The T114's L76K GPS needs a clear sky view to lock. Indoor tests of pairing work without GPS but tracking tests need real coordinates. |

---

## 2. Wiring the QMC5883L to the T114

The T114's external I2C bus (TWI1) is exposed on the GPIO header. Per upstream `variants/nrf52840/heltec_mesh_node_t114/variant.h`:

| QMC5883L pin | T114 pin (nRF52840 GPIO) | Notes |
|---|---|---|
| VCC | 3V3 | 3.3V — **NOT 5V**. The QMC5883L is a 3.3V part; 5V will damage it. |
| GND | GND | Any ground pin |
| SDA | P0.16 | This is `PIN_WIRE1_SDA` in the firmware |
| SCL | P0.13 | This is `PIN_WIRE1_SCL` in the firmware |
| DRDY | (not connected) | Driver polls the STATUS register instead |

**Note on pin labels:** the T114's silkscreen labels GPIO pins by their nRF52840 port/pin (e.g., "P0.16"), not by Arduino-style pin numbers. Find the pins labeled P0.16 and P0.13 on the T114's pinout diagram (search "Heltec Mesh Node T114 pinout" — Heltec's wiki has a labeled photo).

**The PRD originally said SDA=P0.13 / SCL=P0.16. That was wrong; upstream variant.h is the source of truth and we follow it.** If you wire it the PRD's old way, the device will report `Mag: NOT_FOUND` because it'll be reading from the wrong pins.

---

## 3. Getting the Firmware

The build pipeline produces a `firmware.uf2` file via GitHub Actions on every PR push and main-branch merge.

**Source:** GitHub release page for this repo. Look for the most recent prerelease tagged `v<date>-pr<N>.<run>` (per-PR build) or `v<date>-<run>` (main-branch release). The release attaches both:
- `firmware.uf2` — the canonical filename
- `firmware-heltec-mesh-node-t114-<version>.<sha7>.uf2` — same content, with the upstream version + pinned commit hash baked into the name (handy for record-keeping)

Either file works. Download to your computer.

**To flash:**
1. Connect the T114 via USB-C to your computer.
2. Double-press the **reset** button on the T114 quickly (within ~500ms).
3. A USB drive named `T114BOOT` (or similar) appears on your computer.
4. Drag `firmware.uf2` onto that drive.
5. The drive disappears within a few seconds; the T114 reboots and is now running the new firmware.

If the drive doesn't appear: try the double-press again, slower or faster. The bootloader window is sensitive.

**To open a serial console** (needed for Test 1):
- macOS: `screen /dev/cu.usbmodem*` at 115200 baud. Exit with `Ctrl+A` then `K`.
- Linux: `screen /dev/ttyACM0` at 115200. Or `picocom -b 115200 /dev/ttyACM0`.
- Windows: PuTTY or Tera Term, COM port (whichever appears in Device Manager when the T114 is plugged in), 115200 8N1.

---

## 4. Pre-flight Test (Test 0): Stock Meshtastic Sanity

**Purpose:** Make sure flashing the new firmware didn't break the device. We've added ~8KB of compass code; nothing else should have changed.

**Setup:** One T114, freshly flashed, no QMC5883L wired yet.

**Steps:**
1. Power the device on.
2. Watch the screen for the boot sequence (Meshtastic logo, then the home screen).
3. Verify the device joins the mesh as expected (look for the mesh status indicator, signal bars, etc.).
4. Send a text message to another node on the mesh (or to yourself via the Meshtastic phone app). Confirm it sends and receives.
5. If you have GPS view, wait for a GPS fix (the T114's GPS indicator should switch from blinking to solid).

**Expected:**
- Device boots normally to the Meshtastic home screen.
- All UI navigation works (button presses move through menus normally).
- Text messaging works.
- GPS acquires a fix within 1–2 minutes outdoors.

**Pass / Fail:** ☐ Pass ☐ Fail

**If fail:** File an issue with the symptom + serial console log of the boot. Stop testing further.

---

## 5. Single-Device Tests

These need one T114. Skip §6 if you only have one device.

### Test 1: Magnetometer Detection (BEAD-5, BEAD-7)

**Purpose:** Verify the firmware correctly detects the QMC5883L on Wire1.

**Setup:** One T114, USB serial console open. **Three sub-tests** — run all three.

#### Test 1a: No magnetometer wired

**Steps:**
1. Power on the T114 with no QMC5883L connected to the I2C header.
2. In the serial console, search the boot log for `Captain's Compass: mag init`.

**Expected:**
- Serial log contains: `Captain's Compass: mag init NOT_FOUND`
- Device continues booting normally; no hang, no crash, no reset loop.

**Pass / Fail:** ☐ Pass ☐ Fail

#### Test 1b: QMC5883L wired correctly

**Steps:**
1. Power off the T114.
2. Wire the QMC5883L per §2 (VCC=3V3, GND=GND, SDA=P0.16, SCL=P0.13).
3. Power on, watch serial.

**Expected:**
- Serial log contains: `Captain's Compass: mag init OK`

**Pass / Fail:** ☐ Pass ☐ Fail

**If you see `INIT_FAILED` instead of `OK`:** the chip is on the bus and ACKing, but its chip-ID register doesn't read 0xFF. Most likely cause: it's actually an HMC5883L (different chip ID), not a QMC5883L. Check the chip silkscreen.

#### Test 1c: SDA/SCL swapped

**Steps:**
1. Power off.
2. Swap the SDA and SCL wires (so SDA goes to P0.13, SCL goes to P0.16).
3. Power on, watch serial.

**Expected:**
- Serial log contains: `Captain's Compass: mag init NOT_FOUND` (chip never ACKs because we're talking on the wrong pins).

**Pass / Fail:** ☐ Pass ☐ Fail

**Why this matters:** the PRD originally had the pin labels reversed. If a future T114 board revision really did expose I2C on those pins instead, this test would unexpectedly *succeed*, and we'd want to know.

After this test, restore the correct wiring (SDA=P0.16, SCL=P0.13).

---

### Test 2: Compass Menu Entry (BEAD-12)

**Purpose:** Verify the patched `MenuHandler.cpp` exposes the Compass entry in the home banner menu.

**Setup:** One T114 with QMC5883L wired (results from Test 1b).

**Steps:**
1. From the home screen, open the **home banner menu** — on most T114 firmwares this is the long-press of the home/select button. (If you can't find it, try short-pressing or different button combinations until a popup appears with options like "Send Position", "Sleep Screen", etc.)
2. Scroll through the options.

**Expected:**
- One of the options is labeled `Compass`.
- Selecting it makes the screen change to a list view labeled `Find Desire` (the DISCOVERING state).
- If no other Compass-capable nodes are nearby, after ~3 seconds the list shows `No Compass nodes nearby`.

**Pass / Fail:** ☐ Pass ☐ Fail

**Notes:** Photo of the home banner menu showing the Compass entry would be useful.

---

### Test 3: Magnetometer Heading Sweep (BEAD-7)

**Purpose:** Verify the QMC5883L returns rotating heading values as the device is rotated.

**Setup:** One T114 with QMC5883L wired. We don't have a calibration menu in v0.2 (see §9), so heading values will reflect uncalibrated raw data — they should still rotate smoothly even if the absolute value is offset from true north.

**Steps:**
1. Open serial console. We don't currently print heading on a tick — instead, trigger any state that displays a heading. The simplest path: have a second T114 nearby and pair (Test 4–5). With paired tracking active, the arrow will rotate as you turn the device. *If you don't have a second T114, this test is currently not performable without flashing a firmware with extra logging — skip and report it as deferred.*
2. With pairing active and the arrow rendered, slowly rotate the T114 device 360° while keeping it level.

**Expected:**
- The arrow direction changes smoothly as the device rotates. There should be no jumpy or stuck behavior.
- Rotating the device 90° should rotate the arrow approximately 90° in the opposite direction (the arrow points toward the peer's bearing in *world* coordinates, so as you turn the device the arrow appears to swing relative to the screen).

**Pass / Fail:** ☐ Pass ☐ Fail

**Notes:** If the arrow is jittery or stuck at one value, the QMC5883L may not be initializing the continuous-mode register correctly. Capture serial log and any `mag init` messages.

---

## 6. Two-Device Tests

These need two T114s. Call them **Alice** (the Compass — has the QMC5883L wired) and **Bob** (the Desire — does NOT need a magnetometer; just GPS).

Both devices must be running the same firmware build.

### Test 4: Capability Discovery (BEAD-10)

**Purpose:** Alice's CAPABILITY_QUERY broadcast triggers Bob's automatic CAPABILITY_ADV response.

**Setup:** Alice + Bob within radio range (a few feet is fine). Both powered on and joined to the same mesh.

**Steps:**
1. On Alice, open the home banner menu and select **Compass**.
2. Watch Alice's screen for the discovery list.

**Expected:**
- Within ~3 seconds, Alice's discovery list shows Bob's short name (whatever Bob's Meshtastic node name is) with an RSSI number next to it.
- Selecting Bob's entry advances Alice to the AWAITING_PAIR_ACCEPT screen ("Waiting for accept...").

**Pass / Fail:** ☐ Pass ☐ Fail

**Failure modes:**
- Alice's list says `No Compass nodes nearby` → Bob isn't responding. Check both devices are running the same firmware build; check radio range; check both are joined to the same channel.
- Alice's list shows Bob's nodeNum (a hex number) instead of a short name → NodeDB lookup didn't return user info; not a bug per se, but a usability nit. File and continue.

---

### Test 5: Pairing Handshake (BEAD-10, BEAD-11)

**Purpose:** Alice sends PAIR_REQUEST → Bob shows the prompt → Bob accepts → Alice receives PAIR_ACCEPT and confirms.

**Setup:** Continuing from Test 4.

**Steps:**
1. From Alice's discovery list (if not already there), select Bob.
2. Within ~5 seconds, Bob's screen should switch to a `Pair Request` overlay showing "<Alice> wants to track you. [ACCEPT] [REJECT]".
3. On Bob, navigate to ACCEPT (LEFT or UP arrow if not already highlighted) and press SELECT.
4. Watch Alice's screen.

**Expected:**
- Bob enters the TRACKED state. Bob's UI returns to its normal screens (TRACKED has no UI takeover by design — Bob just sends position updates silently).
- Alice's screen switches to the TRACKING view: an arrow on the left side, peer name + distance + age-of-last-update on the right.

**Pass / Fail:** ☐ Pass ☐ Fail

**Variant: REJECT path**

Repeat the test, but on step 3 select REJECT instead of ACCEPT. Expected: Alice returns to IDLE (home screen) within a few seconds. (Currently we don't show a "Pair rejected" message — we just go back to idle. Note this as a known limitation if you're filing a polish bug.)

**Pass / Fail (REJECT path):** ☐ Pass ☐ Fail

---

### Test 6: Active Tracking (BEAD-7, BEAD-10, BEAD-11)

**Purpose:** With pairing established, Alice's arrow points at Bob's location and updates as Bob moves. Distance display closes as Alice approaches Bob.

**Setup:** Continuing from Test 5. Both devices outdoors with GPS fix. **Alice has the QMC5883L wired.**

**Steps:**
1. Confirm both devices have a GPS fix. (Indicator on each.)
2. Walk Bob ~30 meters away from Alice. Wait 30 seconds for at least one position update to arrive at Alice.
3. Walk Bob another 30 meters in a different direction.
4. Walk Alice toward Bob, watching the arrow + distance display.

**Expected:**
- Alice's arrow points roughly in Bob's direction (allowing for magnetic-vs-true-north offset; we don't apply declination correction in v0.2). At short ranges (<100m) the bearing should visibly track Bob's actual position.
- Distance display decreases as Alice approaches Bob.
- Time-since-update display ("Xs ago") resets to a small number when each new update arrives. The default update interval is 30s.

**Pass / Fail:** ☐ Pass ☐ Fail

**Notes:** Distance accuracy depends on GPS quality on both devices. ±10m is normal; ±50m+ suggests a GPS fix issue, not a Compass issue.

---

### Test 7: Signal-Lost Behavior (BEAD-9, BEAD-11)

**Purpose:** Alice's UI reflects "signal lost" (>90s no update) by blinking the arrow and changing the label.

**Setup:** Continuing from Test 6, with active tracking confirmed.

**Steps:**
1. Power off Bob.
2. Watch Alice's screen for ~2 minutes.

**Expected:**
- After 90s with no updates, Alice's arrow starts blinking (visible/invisible at 500ms period).
- The label area shows "signal lost" instead of the time-since-update.
- The peer name is still displayed.

**Pass / Fail:** ☐ Pass ☐ Fail

---

### Test 8: Session-Paused Behavior (BEAD-9, BEAD-11)

**Purpose:** After 5 minutes of no updates, the session enters SESSION_PAUSED state and shows a "Session paused" screen.

**Setup:** Continuing from Test 7. Bob still powered off.

**Steps:**
1. Continue waiting after Test 7. Total elapsed should be 5+ minutes since Bob's last update.
2. Watch Alice's screen.

**Expected:**
- Alice's screen switches from the blinking-arrow TRACKING view to a "Session paused — \<Bob\> unreachable" message.
- If Bob is powered back on and his next position update reaches Alice, Alice's screen should transition back to TRACKING.

**Pass / Fail:** ☐ Pass ☐ Fail

**Notes:** This test has a 5-minute clock. Easy to miss the transition by accident.

---

## 7. Persistence Test

### Test 9: Reboot During Tracking

**Purpose:** Confirm tracking sessions are *not* persisted across reboot (per PRD §6 explicit out-of-scope).

**Setup:** Both devices, active tracking session in progress.

**Steps:**
1. Power-cycle Alice (unplug USB, wait 5 seconds, plug back in).
2. Wait for boot to complete.

**Expected:**
- Alice boots into the normal Meshtastic home screen, **not** the TRACKING view.
- Bob's device still thinks the session is active (Bob will continue sending position updates for up to 5 minutes until they time out on Bob's side too).

**Pass / Fail:** ☐ Pass ☐ Fail

---

## 8. Stock Functionality Regression

### Test 10: Existing Meshtastic Functionality Unaffected

**Purpose:** Repeat Test 0 but after exercising the Compass features. We want to confirm that pairing/tracking didn't leave the device in a stuck state.

**Setup:** One T114 (or both) that have just finished Tests 4–9.

**Steps:**
1. From whatever state the device is currently in, return to the home screen (use back button or wait out the session).
2. Send a text message via Meshtastic. Confirm it sends and receives.
3. Send a position via the home banner menu's "Send Position". Confirm.
4. Verify the device still responds to button presses, screen still updates normally.

**Expected:**
- All stock Meshtastic functionality works normally.

**Pass / Fail:** ☐ Pass ☐ Fail

---

## 9. Known Limitations (Not Yet Testable in v0.2)

These were planned in the PRD but their UI surface wasn't wired up in v0.2. Each will be filed as a follow-up bead. **Do not waste time looking for these in the menus** — they aren't there yet.

- **CUJ-2 — Magnetometer calibration menu.** The firmware has a calibration FSM (`CompassState::startCalibration` / `feedCalSample` / `finishCalibration`) but no UI entry to trigger it. As a result, all heading readings are uncalibrated (raw QMC5883L output without hard-iron correction). The arrow direction will track magnetic field changes correctly *relatively*, but the absolute bearing may be biased by a fixed offset depending on the magnetic environment. For Test 6, "arrow points roughly toward Bob" should hold; "arrow points at exactly Bob's bearing" may not.
- **CUJ-5 — Save Treasure menu.** The firmware can save and load Treasures via `CompassState::saveTreasure`, but there's no menu entry to invoke it. Treasures FSM is reachable only by code, not by user.
- **CUJ-6 — Treasure navigation menu.** Same — `startTreasureNav` exists but no UI to choose a Treasure.
- **CUJ-7 — Manual "End Session" menu entry.** Sessions only end automatically (5min pause) or via `SESSION_END` packet from the peer. No UI to manually end a session from the Compass side.
- **CUJ-1 status screen.** The mag init result is logged to serial but not shown on screen. Test 1 above uses the serial log as a substitute.
- **Update interval setting.** Stuck at the default (30s). No UI to change.
- **Long-press in TRACKING.** The handler returns "intercepted" but doesn't actually release the UI; you remain in TRACKING. Workaround: power-cycle the device, or wait for the 5min auto-pause.

---

## 10. Bug Reporting

When something fails, file a GitHub issue with:

1. **Test number** (e.g., "Test 5b: REJECT path").
2. **What happened** vs **what you expected**.
3. **Firmware UF2 filename** (the versioned form, `firmware-heltec-mesh-node-t114-<ver>.<sha7>.uf2`, gives us the exact build).
4. **Device(s) involved** (if a Compass/Desire pair, name which is which).
5. **Serial console log** for the relevant time window — at minimum, capture the boot log up to "Captain's Compass: mag init …" and any output during the failing step.
6. **A screen photo** if the failure is visual (arrow geometry wrong, text overflow, etc.).
7. **Reproducibility:** does the failure happen every time, or only sometimes?

Tag the issue with `qa-failure`. We'll triage and either fix it directly or file a follow-up bead.

---

## 11. Test Results Summary

Print this and check off as you go.

| # | Test | Pass | Fail | N/A |
|---|------|:---:|:---:|:---:|
| 0 | Pre-flight: stock Meshtastic sanity | ☐ | ☐ | ☐ |
| 1a | Mag detection: no chip wired (NOT_FOUND) | ☐ | ☐ | ☐ |
| 1b | Mag detection: chip wired (OK) | ☐ | ☐ | ☐ |
| 1c | Mag detection: SDA/SCL swapped (NOT_FOUND) | ☐ | ☐ | ☐ |
| 2 | Compass menu entry visible + DISCOVERING transition | ☐ | ☐ | ☐ |
| 3 | Heading sweep rotates smoothly | ☐ | ☐ | ☐ |
| 4 | Capability discovery: Bob shows in Alice's list | ☐ | ☐ | ☐ |
| 5 | Pairing handshake: ACCEPT path | ☐ | ☐ | ☐ |
| 5b | Pairing handshake: REJECT path | ☐ | ☐ | ☐ |
| 6 | Active tracking: arrow + distance | ☐ | ☐ | ☐ |
| 7 | Signal lost (>90s): blink + label | ☐ | ☐ | ☐ |
| 8 | Session paused (>5min): pause screen | ☐ | ☐ | ☐ |
| 9 | Reboot during tracking: not persisted | ☐ | ☐ | ☐ |
| 10 | Stock functionality regression-free | ☐ | ☐ | ☐ |

Tester name: ___________________  Date: __________  Firmware build: __________________

---

*This document covers v0.2 — patch-overlay firmware against pinned upstream `b7a9555`. v2 will add the missing menu surfaces (§9) and a native/Portduino smoke-test harness (TDD §16.1) so that protocol regressions can be caught in CI without needing to repeat all of Tests 4–9 by hand.*
