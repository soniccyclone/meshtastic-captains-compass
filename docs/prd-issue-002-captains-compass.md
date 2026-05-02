# Captain's Compass — CUJ PRD
**Issue:** #2
**Version:** 0.1
**Target hardware:** Heltec Mesh Node T114 Rev 2.0 + QMC5883L magnetometer (external, wired to GPIO header)
**Target firmware base:** Meshtastic `develop`, variant `heltec_mesh_node_t114`

---

## 1. Problem Statement

A Meshtastic node knows the GPS coordinates of every node on the mesh. What it cannot do is tell you which physical direction to walk to get there. This firmware adds that capability: a compass that points toward a specific person or place, rendered on the T114's display, driven by an attached magnetometer.

The feature has to be invisible when not in use. A user who never activates Compass mode should experience a completely stock Meshtastic device.

---

## 2. Personas

**Field user (primary):** Carries the device outdoors — hiking, events, conventions, search & rescue. Wants a low-friction "point me at Alice" workflow without pulling out a phone.

**Pairing partner (secondary):** Another T114 running this firmware, owned by the person being tracked (*Desire*). Must consent to being tracked. May or may not actively use their own Compass.

---

## 3. Hardware Constraints

| Item | Detail |
|---|---|
| MCU | nRF52840 |
| Onboard I2C (TWI0) | PCF8563TS RTC on P0.26/P0.27 — not exposed, do not touch |
| External I2C bus | TWI1, SDA=P0.13, SCL=P0.16 (exposed header, confirmed free on Rev 2.0) |
| Magnetometer | QMC5883L only (I2C addr 0x0D). No HMC5883L support — do not attempt chip detection fallback |
| Display | SSD1306-compatible 128x64 OLED via existing Meshtastic Screen driver |
| Input | Single button (UP/DOWN/SELECT via existing InputBroker) |

---

## 4. Glossary

| Term | Meaning |
|---|---|
| Compass | The local device in an active tracking session |
| Desire | A remote node being tracked; must consent via pairing handshake |
| Treasure | A locally-saved GPS waypoint (lat/lon + label, no remote device required) |
| Pair | Mutually accepted relationship authorizing fine-grained location sharing |
| Tracking session | Active period where Compass is pulling position updates from a Desire |
| Bearing | Compass heading in degrees (0–359, 0=North) |
| Heading error | Difference between current heading and bearing-to-target; drives arrow display |

---

## 5. Critical User Journeys

### CUJ-1: First-Time Hardware Setup

**Actor:** Field user, building their device for the first time.

**Pre-conditions:**
- T114 with this firmware flashed
- QMC5883L breakout wired: SDA→P0.13, SCL→P0.16, VCC→3.3V, GND→GND
- Device booted; normal Meshtastic mesh operation confirmed

**Journey:**
1. User navigates to **Menu → Compass → Status**.
2. Display shows magnetometer scan result: `Mag: QMC5883L OK` (I2C address 0x0D found and chip ID verified) or `Mag: NOT FOUND` with pin diagram hint.
3. If `NOT FOUND`: device remains fully functional as a stock Meshtastic node. Compass menu remains accessible but all active features are gated behind the error state. No crash, no assert.
4. If `OK`: user proceeds to CUJ-2 (calibration).

**Success state:** Chip found and ID verified. Status screen shows `Mag: QMC5883L OK` and firmware version.

**Error cases:**
- Wrong wiring: `NOT FOUND` — user fixes hardware, re-enters menu, re-scans.
- Chip found but ID read returns garbage: `Mag: INIT FAILED` — distinct from not-found. Means wiring is present but chip is misbehaving (power issue, bad solder joint).

---

### CUJ-2: Magnetometer Calibration

**Actor:** Field user, on first use or after significant environment change (new metal objects nearby, different vehicle).

**Pre-conditions:** CUJ-1 complete, mag chip `OK`.

**Why this matters:** QMC5883L raw readings contain hard-iron and soft-iron distortion. Without calibration the heading is wrong by a variable offset. This is not optional.

**Journey:**
1. User navigates to **Menu → Compass → Calibrate**.
2. Display shows: `Rotate device slowly in all directions. Press SELECT when done.`
3. Firmware enters calibration loop: streams raw X/Y/Z samples, tracking running min/max per axis.
4. User rotates device through full 3D orientations (figure-8 motion, ~30 seconds).
5. User presses SELECT.
6. Firmware computes hard-iron offsets (midpoint of min/max per axis) and stores to NVS. Soft-iron scale is not computed (complexity vs. benefit tradeoff — acceptable for field use).
7. Display shows: `Cal saved. Offset: X=±NNN Y=±NNN Z=±NNN`. User can repeat if values look unreasonable.

**Success state:** Offsets stored to NVS. Applied on every subsequent heading read. Survive device reboot.

**Error cases:**
- User presses SELECT too quickly (< 5 seconds of samples): `Not enough data — keep rotating`.
- NVS write fails: `Cal failed to save — check flash`. Offsets applied for session only.

---

### CUJ-3: Pairing with a Desire

**Actor:** Two field users who want to track each other. Alice initiates; Bob accepts.

**Pre-conditions:**
- Both devices on the same Meshtastic mesh (not necessarily adjacent — mesh routing applies)
- Both running this firmware
- Neither currently in an active tracking session

**Journey:**
1. Alice navigates to **Menu → Compass → Find Desire → [Node list]**.
2. Display shows Meshtastic node list filtered to nodes that have advertised `COMPASS_APP` capability. Nodes are sorted by last-heard time.
3. Alice selects Bob's node. Display shows: `Send pair request to Bob? [YES / NO]`.
4. Alice confirms. Firmware broadcasts a `PAIR_REQUEST` packet with `hop_limit=3` on port `COMPASS_APP`. Payload: Alice's node ID.
5. Bob's device receives the request. If Bob is in **Menu → Compass → Find Desire** (discovery mode): Bob's display shows `Alice wants to track you. [ACCEPT / REJECT]`.
6. Bob selects ACCEPT. Firmware sends unicast `PAIR_ACCEPT` back to Alice, `hop_limit=3`.
7. Alice's device receives ACCEPT. Firmware sends unicast `PAIR_CONFIRM` to Bob.
8. Both devices transition to **paired state**. Display on each shows `Paired: [other node name]`.
9. Tracking session begins automatically (see CUJ-4).

**Success state:** Both devices in paired state. Alice is tracking Bob (Desire). Compass arrow is live.

**Error cases:**
- Bob rejects: Alice's display shows `Pair rejected by Bob`. Returns to node list.
- No response within 60s: `No response from Bob. Try again?` Returns to node list.
- Bob is not in discovery mode: request is queued on Bob's device; Bob sees a notification banner on next screen wake: `Alice wants to track you`. Bob can accept or reject from notifications.
- Alice selects a node that doesn't support COMPASS_APP (filtered out of list — shouldn't happen, but if stale capability data): pair request sent, no response, timeout as above.

**Protocol note:** `hop_limit=3` on all pairing packets, unlike Friend Finder's `hop_limit=1`. This firmware is intended for mesh use, not just direct-link. Mesh relay is a first-class requirement, not an afterthought.

---

### CUJ-4: Active Compass Tracking (Desire Mode)

**Actor:** Alice, with an active paired session to Bob (Desire).

**Pre-conditions:** CUJ-3 complete. Calibration complete (CUJ-2). GPS fix on at least one device.

**Journey:**
1. Upon session start, firmware takes over display via `wantUIFrame()` / `drawFrame()`. Normal Meshtastic screen rotation pauses.
2. Compass screen shows:
   - Large arrow pointing toward Bob's last known position, rotated by heading error (magnetometer heading vs. bearing-to-target).
   - Bob's name and distance (meters or feet, per user locale setting).
   - Time since last position update from Bob (`Bob: 23s ago`).
   - Small battery indicator for Bob (if included in position packet).
3. Bob's device sends position updates every **30 seconds** (default, configurable in settings) via unicast `POSITION_UPDATE` packet on `COMPASS_APP` port.
4. Alice's firmware receives update, recalculates bearing, redraws arrow.
5. If no update received for **90 seconds**: arrow begins blinking. Label changes to `Bob: signal lost`.
6. If no update for **5 minutes**: tracking session pauses. Display shows `Session paused — Bob unreachable`. Normal screen rotation resumes. Session can be resumed from menu if Bob comes back.
7. User can exit the compass screen at any time by long-pressing SELECT. This does **not** end the session — it returns to normal screen rotation while tracking continues in background. A banner `[Tracking Bob]` appears on the status bar.
8. To end the session: **Menu → Compass → End Session → [YES]**. Firmware sends `SESSION_END` unicast to Bob.

**Success state:** Live arrow display pointing toward Bob. Updates every 30s. Distance shrinks as Alice approaches.

**Error cases:**
- Alice has no GPS fix: arrow replaced with `No GPS fix`. Distance shown as `---`. Bearing cannot be computed — arrow is frozen at last known bearing, visually dimmed.
- Bob has no GPS fix: `Bob: no GPS` label. Alice's arrow frozen at last-known bearing.
- Both have no GPS: `Waiting for GPS fix...`

---

### CUJ-5: Saving a Treasure (Waypoint)

**Actor:** Field user who wants to navigate back to a point of interest.

**Pre-conditions:** GPS fix acquired.

**Journey:**
1. User navigates to **Menu → Compass → Save Treasure**.
2. Display shows current GPS coordinates and prompts for a label (up to 12 characters, entered via scroll-select input).
3. User confirms. Firmware writes to NVS: `{label, lat_i, lon_i, timestamp}`.
4. Up to **5 Treasures** stored. If full: list is shown with option to overwrite the oldest.
5. Saved Treasure is immediately available in the Treasure list.

**Success state:** Treasure saved and visible in list. Survives reboot.

**Error cases:**
- No GPS fix: menu entry is grayed out / disabled. User sees `Waiting for GPS fix`.
- NVS write fails: `Save failed`. Current session Treasure list is held in RAM; next reboot it's gone.

---

### CUJ-6: Navigating to a Treasure (Waypoint Mode)

**Actor:** Field user navigating to a previously saved waypoint.

**Pre-conditions:** At least one Treasure saved. GPS fix acquired. Calibration complete.

**Journey:**
1. User navigates to **Menu → Compass → Treasures → [list]**.
2. User selects a Treasure. Display shows label, coordinates, distance from current position.
3. User confirms. Compass screen takes over (same layout as CUJ-4 but no remote device).
4. Arrow points toward Treasure. Distance updates as user moves.
5. When distance drops below **15 meters**: arrow replaced with `YOU'RE HERE` text and display stays on. This threshold accounts for GPS accuracy limits.
6. User long-presses SELECT to exit compass view. Session persists in background (same as CUJ-4).
7. To end: **Menu → Compass → End Session**.

**Success state:** Arrow points at Treasure. Distance closes to zero.

**Error cases:**
- GPS fix lost during navigation: arrow freezes, `GPS lost` label. Resumes when fix returns.
- Treasure deleted while session active (edge case): `Treasure no longer available`. Session ends.

---

### CUJ-7: Ending a Tracking Session

**Actor:** Either party in a Desire tracking session, or a user navigating to a Treasure.

**Journey:**
1. User navigates to **Menu → Compass → End Session**.
2. Display prompts: `End session with Bob? [YES / NO]`.
3. On YES: firmware sends `SESSION_END` unicast to Bob (Desire mode only — not applicable for Treasure). Both devices return to idle state. Normal screen rotation resumes.
4. Bob's device shows: `Alice ended the session`.

**Error cases:**
- Bob is unreachable when END is sent: session ends locally anyway. No retry. Bob's device will detect the session as timed out when no updates arrive.
- Device reboots mid-session: session is not persisted across reboots. Both devices return to idle on next boot.

---

### CUJ-8: Being Tracked (Desire Role)

**Actor:** Bob, who has accepted a pair request from Alice and is now being tracked.

**Pre-conditions:** CUJ-3 complete from Bob's side.

**Journey:**
1. Bob's device sends position updates every 30s automatically. Bob does not need to interact with the device for this to work.
2. Bob's status bar shows `[Tracked by Alice]`.
3. Bob can navigate the normal Meshtastic UI normally while being tracked. No UI takeover on the Desire side.
4. Bob can end the session at any time: **Menu → Compass → End Session**. This sends `SESSION_END` to Alice and stops updates.

**Success state:** Bob's device sends updates silently in background. Alice gets live tracking. Bob's battery impact is minimal (30s interval, small packet, no display changes).

---

## 6. Out of Scope (v0.1)

- Multi-target tracking (only one active session at a time per device)
- Web companion app or phone integration
- HMC5883L support (different chip, different register map — add later if needed)
- Bearing-only mode (without GPS — magnetometer only shows heading, not direction to target)
- LED feedback (T114 has onboard RGB LEDs but spec removed this requirement; clean path to add later)
- Persistent session across reboot

---

## 7. Protocol Specification

**Port:** `meshtastic_PortNum_COMPASS_APP` (register with Meshtastic upstream; use reserved range 512–1023 for prototyping)

**Message types** (`compass.proto`):

```protobuf
enum CompassMsgType {
  POSITION_UPDATE = 0;   // Desire → Compass: lat/lon/time/battery
  PAIR_REQUEST    = 1;   // Compass → Desire: initiator node ID
  PAIR_ACCEPT     = 2;   // Desire → Compass: acceptance
  PAIR_CONFIRM    = 3;   // Compass → Desire: handshake complete
  PAIR_REJECT     = 4;   // Desire → Compass: rejection
  SESSION_END     = 5;   // Either → Either: terminate session
  CAPABILITY_ADV  = 6;   // Broadcast: "I support COMPASS_APP" (for node list filtering)
}

message CompassPacket {
  CompassMsgType type = 1;
  sint32 latitude_i  = 2;   // WGS84 * 1e7, only in POSITION_UPDATE
  sint32 longitude_i = 3;
  uint32 time        = 4;   // Unix epoch seconds
  uint32 battery_pct = 5;   // 0–100, optional
}
```

**Routing:** All packets `hop_limit=3`. Unicast for PAIR_ACCEPT, PAIR_CONFIRM, PAIR_REJECT, SESSION_END, POSITION_UPDATE. Broadcast for PAIR_REQUEST, CAPABILITY_ADV.

**Update interval:** 30s default, stored in NVS, configurable via **Menu → Compass → Settings → Update Interval** (10s / 30s / 60s / 120s).

---

## 8. File Tree

```
+ src/modules/CompassModule/CompassModule.{h,cpp}     // MeshModule subclass, packet handler
+ src/modules/CompassModule/CompassState.{h,cpp}      // Session state machine
+ src/modules/CompassModule/Magnetometer.{h,cpp}      // QMC5883L driver on Wire1
+ src/modules/CompassModule/CompassMath.{h,cpp}       // Bearing, heading error, distance
+ src/modules/CompassModule/CompassUI.{h,cpp}         // drawFrame(), wantUIFrame(), input handling
+ protobufs/meshtastic/compass.proto
M src/modules/Modules.cpp                             (+2 lines: include + register)
M src/graphics/Screen.cpp                             (+1 menu entry under main menu)
M variants/nrf52840/heltec_mesh_node_t114/variant.h   (+4 defines: MAG_SDA, MAG_SCL, WIRE_INTERFACES_COUNT=2, WIRE1_INTERFACES_COUNT=1)
M variants/nrf52840/heltec_mesh_node_t114/variant.cpp (+TwoWire Wire1(NRF_TWIM1, NRF_TWIS1, SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQn, MAG_SDA, MAG_SCL))
```

---

## 9. Risk Register

| ID | Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|---|
| R-1 | I2C address collision on TWI1 | Low | High | QMC5883L is 0x0D; verify no other device shares the header. Scan at init and log. |
| R-2 | TWI1 initialization conflicts with Meshtastic SPI on same NRF peripheral group | Low | High | Verify SPIM1/TWIM1 sharing in nRF52840 PS. If conflict: move to TWI0 via multiplexer or negotiate with upstream. |
| R-3 | Mesh routing adds > 5s latency to position updates | Medium | Medium | Acceptable for 30s interval. Surface last-update timestamp to user so they know. |
| R-4 | Calibration data lost on firmware update (NVS layout change) | Medium | Low | Version-stamp calibration NVS key. On version mismatch, prompt re-calibration rather than crash. |
| R-5 | CompassUI conflicts with existing ScreenLock or power-save display logic | Medium | Medium | Test `wantUIFrame()` return behavior under all existing screen states. Do not hold frame unless session is active. |

---

## 10. Decisions

1. **NVS namespace:** Use `compass`. Meshtastic uses `prefs`; a dedicated namespace avoids key collisions and makes wipe/reset straightforward.
2. **CAPABILITY_ADV timing:** Not on boot — too noisy. Broadcast only when the user enters the Compass menu (on-demand). Acceptable latency for discovery; saves mesh bandwidth for nodes that never use the feature.
3. **Stationary update suppression:** If the Desire's GPS position has not changed beyond GPS noise floor, suppress position updates for up to **5 minutes**. After 5 minutes, send an update regardless (keepalive). Resume normal 30s interval as soon as movement is detected.
4. **Buzzer:** No buzzer support in v0.1. There is an outstanding power draw issue with the T114's buzzer circuit that must be resolved first. "Arrived at Treasure" notification is visual only (arrow replaced with `YOU'RE HERE`, display stays on).

---

*This document is the authoritative CUJ spec for issue #2. Functional requirements live here; implementation details live in code comments.*
