# Captain's Compass — Technical Design Document
**Issue:** #2
**Version:** 0.1
**PRD:** [prd-issue-002-captains-compass.md](prd-issue-002-captains-compass.md)
**Target:** Meshtastic `develop` branch, variant `heltec_mesh_node_t114`

---

## 1. Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│                    Meshtastic Firmware                      │
│                                                             │
│  ┌──────────────┐   ┌──────────────┐   ┌────────────────┐  │
│  │  Screen.cpp  │   │  Modules.cpp │   │   Router /     │  │
│  │  (1 menu     │   │  (register   │   │   Service      │  │
│  │   entry)     │   │   module)    │   │   (send/recv)  │  │
│  └──────┬───────┘   └──────┬───────┘   └───────┬────────┘  │
│         │                  │                   │           │
│  ┌──────▼───────────────────▼───────────────────▼────────┐  │
│  │                    CompassModule                      │  │
│  │         (SinglePortModule + OSThread)                 │  │
│  │                                                       │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌────────────┐  │  │
│  │  │ CompassState │  │  CompassUI   │  │CompassMath │  │  │
│  │  │ (FSM + NVS)  │  │(drawFrame,   │  │(bearing,   │  │  │
│  │  │              │  │ input)       │  │ distance,  │  │  │
│  │  └──────────────┘  └──────────────┘  │ heading)   │  │  │
│  │                                      └────────────┘  │  │
│  │  ┌──────────────────────────────────────────────────┐ │  │
│  │  │              Magnetometer                        │ │  │
│  │  │         (QMC5883L driver on Wire1)               │ │  │
│  │  └──────────────────────────────────────────────────┘ │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
         │
    ┌────▼─────┐
    │  Wire1   │  TWI1 — P0.13 SDA, P0.16 SCL
    └────┬─────┘
         │
    ┌────▼──────────┐
    │  QMC5883L     │  I2C addr 0x0D
    └───────────────┘
```

**Read these Meshtastic source files before implementing any bead:**
- `src/mesh/MeshModule.h` — base class interface, `wantUIFrame`, `drawFrame`, `interceptingKeyboardInput`, `handleInputEvent`
- `src/mesh/SinglePortModule.h` — packet handling base, `handleReceived`, `allocDataPacket`
- `src/concurrency/OSThread.h` — cooperative scheduler, `runOnce` return semantics
- `src/graphics/Screen.h` / `Screen.cpp` — frame registration, `setFrames`, `showOverlayBanner`
- `src/mesh/Router.h` — `sendToMesh`, `allocForSending`, `NODENUM_BROADCAST`
- `src/NodeDB.h` — `nodeDB->getNodeByNum`, `getNode`, `getMyNodeNum`
- `src/mesh/generated/meshtastic/portnum.pb.h` — existing PortNum enum, find the private app range
- `variants/nrf52840/heltec_mesh_node_t114/variant.h` — current GPIO assignments, WIRE defines
- `variants/nrf52840/heltec_mesh_node_t114/variant.cpp` — Wire0 instantiation pattern to copy for Wire1
- Any existing module that uses NVS (e.g., `src/modules/StoreForwardModule.cpp`) — `Preferences` usage pattern

---

## 2. Build System

No new PlatformIO lib dependencies. The QMC5883L is driven directly over Wire1 — no third-party driver library. nanopb is already in the build; compass.proto follows the existing pattern.

The build system auto-generates nanopb from `.proto` files via `bin/generate-proto.sh`. After adding `compass.proto`, run that script once to produce:
```
src/mesh/generated/meshtastic/compass.pb.h
src/mesh/generated/meshtastic/compass.pb.c
```
These generated files are committed to the repo (same as all other generated protobufs).

---

## 3. Variant Patch

**File:** `variants/nrf52840/heltec_mesh_node_t114/variant.h`

Add after the existing GPIO defines block:
```c
// Captain's Compass — external QMC5883L magnetometer on TWI1
#define MAG_SDA             (13)   // P0.13
#define MAG_SCL             (16)   // P0.16
#define WIRE_INTERFACES_COUNT    2
#define WIRE1_INTERFACES_COUNT   1
#define PIN_WIRE1_SDA       MAG_SDA
#define PIN_WIRE1_SCL       MAG_SCL
```

**File:** `variants/nrf52840/heltec_mesh_node_t114/variant.cpp`

Add after the existing `TwoWire Wire(NRF_TWIM0, ...)` line, copying its exact pattern but using TWI1 peripherals:
```c
TwoWire Wire1(NRF_TWIM1, NRF_TWIS1, SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQn,
              PIN_WIRE1_SDA, PIN_WIRE1_SCL);
```

**Verification:** After this patch, `Wire1.begin()` must return without error and `Wire1.beginTransmission(0x0D)` / `Wire1.endTransmission()` must return 0 (ACK) when the QMC5883L is wired. Confirm SPIM1 is not claimed elsewhere in the T114 variant before merging.

---

## 4. Protobuf Definition

**File:** `protobufs/meshtastic/compass.proto`

```protobuf
syntax = "proto3";
package meshtastic;
option java_package = "com.geeksville.mesh";
option optimize_for = LITE_RUNTIME;

enum CompassMsgType {
  POSITION_UPDATE  = 0;
  PAIR_REQUEST     = 1;
  PAIR_ACCEPT      = 2;
  PAIR_CONFIRM     = 3;
  PAIR_REJECT      = 4;
  SESSION_END      = 5;
  CAPABILITY_QUERY = 6;
  CAPABILITY_ADV   = 7;
}

message CompassPacket {
  CompassMsgType type = 1;
  sint32 latitude_i   = 2;
  sint32 longitude_i  = 3;
  uint32 time         = 4;
  uint32 battery_pct  = 5;
}
```

**Port number:** Add `COMPASS_APP = 300;` to `meshtastic_PortNum` in `protobufs/meshtastic/portnums.proto`. Value 300 is in the private-app reserved range (256–511). Verify it is not already taken before committing.

**Integration:** `compass.pb.h` / `compass.pb.c` are generated via nanopb. Include `compass.pb.h` in `CompassModule.h`. The nanopb encode/decode calls follow the exact same pattern used in any existing SinglePortModule — read one for reference before writing any encode/decode code.

---

## 5. Magnetometer Driver

**Files:** `src/modules/CompassModule/Magnetometer.h`, `Magnetometer.cpp`

### QMC5883L Register Map

| Addr | Name       | R/W | Notes |
|------|------------|-----|-------|
| 0x00 | XOUT_LSB   | R   | |
| 0x01 | XOUT_MSB   | R   | |
| 0x02 | YOUT_LSB   | R   | |
| 0x03 | YOUT_MSB   | R   | |
| 0x04 | ZOUT_LSB   | R   | |
| 0x05 | ZOUT_MSB   | R   | |
| 0x06 | STATUS     | R   | Bit0=DRDY (data ready), Bit1=OVL (overflow), Bit2=DOR (skip) |
| 0x09 | CTRL1      | R/W | Mode[1:0], ODR[3:2], RNG[5:4], OSR[7:6] |
| 0x0A | CTRL2      | R/W | Bit6=ROL_PTR, Bit7=SOFT_RST |
| 0x0B | SETPERIOD  | R/W | Write 0x01 on init |
| 0x0D | CHIP_ID    | R   | Reads 0xFF on valid QMC5883L |

**CTRL1 init value:** `0x1D` — continuous mode (01), 200 Hz ODR (11), 8 G range (01), OSR 512 (00).
**CTRL2 init value:** `0x40` — ROL_PTR enabled.

### Class Interface

```cpp
class Magnetometer {
public:
    enum InitResult { OK, NOT_FOUND, INIT_FAILED };

    InitResult begin();          // call once; initializes Wire1 and chip
    bool       read(int16_t &x, int16_t &y, int16_t &z); // raw, blocking wait for DRDY
    float      heading();        // calibrated heading, degrees [0, 360)
    bool       isReady() const;  // true if begin() returned OK

    // Calibration offsets (hard-iron only)
    void  setCalibration(int16_t ox, int16_t oy, int16_t oz);
    void  getCalibration(int16_t &ox, int16_t &oy, int16_t &oz) const;

private:
    static constexpr uint8_t  I2C_ADDR   = 0x0D;
    static constexpr uint8_t  CHIP_ID_VAL = 0xFF;
    static constexpr uint16_t DRDY_TIMEOUT_MS = 10;

    bool    _ready = false;
    int16_t _calX = 0, _calY = 0, _calZ = 0;

    bool    writeReg(uint8_t reg, uint8_t val);
    uint8_t readReg(uint8_t reg);
    bool    burstRead(uint8_t startReg, uint8_t *buf, uint8_t len);
};
```

### Init Sequence

```
begin():
  Wire1.begin()
  Wire1.beginTransmission(I2C_ADDR)
  if endTransmission() != 0 → return NOT_FOUND
  if readReg(0x0D) != 0xFF  → return INIT_FAILED
  writeReg(0x0B, 0x01)       // SET/RESET period
  writeReg(0x09, 0x1D)       // CTRL1: continuous, 200Hz, 8G, OSR512
  writeReg(0x0A, 0x40)       // CTRL2: ROL_PTR
  _ready = true
  return OK
```

### Read Sequence

```
read(x, y, z):
  wait for DRDY bit in STATUS (0x06), max DRDY_TIMEOUT_MS
  if timeout → return false
  burstRead(0x00, buf, 6)
  x = (int16_t)((buf[1] << 8) | buf[0])
  y = (int16_t)((buf[3] << 8) | buf[2])
  z = (int16_t)((buf[5] << 8) | buf[4])
  return true

heading():
  read(rx, ry, rz)
  cx = rx - _calX
  cy = ry - _calY
  h = atan2f((float)cy, (float)cx) * 180.0f / M_PI
  if h < 0: h += 360.0f
  return h
```

**Note on magnetic declination:** Not corrected in v0.1. The heading is magnetic north, not true north. The bearing-to-target is computed from GPS coordinates (true north). For short-range tracking (< 1 km) at most latitudes this error is small enough to be acceptable. Add declination correction in a future bead.

---

## 6. CompassMath

**Files:** `src/modules/CompassModule/CompassMath.h`, `CompassMath.cpp`

Pure functions, no state, no dependencies on Meshtastic types. Takes integer lat/lon (WGS84 × 1e7) to match the wire protocol.

```cpp
namespace CompassMath {
    // Bearing from (fromLat, fromLon) to (toLat, toLon), degrees [0, 360).
    // Inputs are integer lat/lon × 1e7 (matching GPS and protobuf convention).
    float bearing(int32_t fromLat, int32_t fromLon,
                  int32_t toLat,   int32_t toLon);

    // Great-circle distance in meters.
    float distanceMeters(int32_t fromLat, int32_t fromLon,
                         int32_t toLat,   int32_t toLon);

    // Signed heading error in degrees [-180, 180].
    // Positive = target is clockwise from current heading (turn right).
    float headingError(float currentHeadingDeg, float bearingDeg);
}
```

### Algorithms

**Bearing (forward azimuth):**
```
dLon = (toLon - fromLon) / 1e7 * π/180
lat1 = fromLat / 1e7 * π/180
lat2 = toLat   / 1e7 * π/180

x = sin(dLon) * cos(lat2)
y = cos(lat1)*sin(lat2) - sin(lat1)*cos(lat2)*cos(dLon)
θ = atan2(x, y) * 180/π
return fmod(θ + 360, 360)
```

**Distance (Haversine):**
```
dLat = (toLat - fromLat) / 1e7 * π/180
dLon = (toLon - fromLon) / 1e7 * π/180
lat1 = fromLat / 1e7 * π/180
lat2 = toLat   / 1e7 * π/180

a = sin²(dLat/2) + cos(lat1)*cos(lat2)*sin²(dLon/2)
c = 2 * atan2(√a, √(1-a))
return 6371000 * c    // meters
```

**Heading error:**
```
err = fmod(bearingDeg - currentHeadingDeg + 360, 360)
if err > 180: err -= 360
return err   // [-180, 180]
```

---

## 7. CompassState

**Files:** `src/modules/CompassModule/CompassState.h`, `CompassState.cpp`

Owns the FSM, NVS persistence, Treasure store, and all session data. No I/O — no Wire, no radio, no display. Called by CompassModule.

### State Machine

```cpp
enum class State {
    IDLE,
    DISCOVERING,         // CAPABILITY_QUERY sent, collecting ADVs (3s window)
    AWAITING_PAIR_ACCEPT,// PAIR_REQUEST sent, 60s timeout
    PAIR_INCOMING,       // PAIR_REQUEST received, showing prompt
    TRACKING,            // Active as Compass (pointing at Desire)
    TRACKED,             // Active as Desire (sending position updates)
    TRACKING_TREASURE,   // Navigating to a saved waypoint
    SESSION_PAUSED,      // No Desire updates for 5min; backgrounded
    CALIBRATING,         // Magnetometer calibration loop
};
```

### Key Structs

```cpp
struct DiscoveredNode {
    uint32_t nodeNum;
    char     shortName[5];  // from NodeDB
    int8_t   rssi;          // from last CAPABILITY_ADV
};

struct SessionData {
    uint32_t peerNodeNum;
    char     peerName[12];
    int32_t  peerLatI;
    int32_t  peerLonI;
    uint32_t peerLastUpdateSec;   // Unix time of last POSITION_UPDATE
    uint32_t peerBatteryPct;
    bool     peerHasGPS;
};

struct Treasure {
    char     label[13];    // 12 chars + null
    int32_t  latI;
    int32_t  lonI;
    uint32_t savedAt;      // Unix timestamp
};
```

### Class Interface

```cpp
class CompassState {
public:
    static constexpr uint8_t  MAX_TREASURES       = 5;
    static constexpr uint8_t  MAX_DISCOVERED_NODES = 8;
    static constexpr uint32_t DISCOVERY_WINDOW_MS  = 3000;
    static constexpr uint32_t PAIR_TIMEOUT_MS      = 60000;
    static constexpr uint32_t SIGNAL_LOST_MS       = 90000;
    static constexpr uint32_t SESSION_PAUSE_MS     = 300000; // 5 min
    static constexpr uint32_t STATIONARY_SUPPRESS_MS = 300000; // 5 min keepalive
    static constexpr float    ARRIVAL_THRESHOLD_M  = 15.0f;
    static constexpr float    GPS_NOISE_FLOOR_M    = 5.0f;

    void begin();  // load NVS, set state=IDLE

    State getState() const;

    // Discovery
    void     startDiscovery();
    void     addDiscoveredNode(uint32_t nodeNum, int8_t rssi);
    bool     isDiscoveryWindowOpen() const;  // true if within 3s of startDiscovery()
    uint8_t  discoveredNodeCount() const;
    const DiscoveredNode& discoveredNode(uint8_t i) const;

    // Pairing (Compass side)
    void startPairRequest(uint32_t targetNodeNum);
    void onPairAccepted(uint32_t peerNodeNum);
    void onPairRejected();
    bool isPairTimedOut() const;

    // Pairing (Desire side)
    void onPairRequestReceived(uint32_t initiatorNodeNum);
    void acceptPair();
    void rejectPair();
    uint32_t pendingPairNodeNum() const;  // valid in PAIR_INCOMING

    // Session management
    void onPositionUpdate(int32_t latI, int32_t lonI, uint32_t time, uint32_t battPct, bool hasGPS);
    void onSessionEnd();
    void endSession();    // local user-initiated

    // Session queries (valid in TRACKING / SESSION_PAUSED)
    const SessionData& session() const;
    bool  isSignalLost() const;    // >90s since last update
    bool  isSessionPaused() const; // >5min since last update

    // Treasure navigation
    void startTreasureNav(uint8_t index);
    const Treasure& activeTreasure() const;  // valid in TRACKING_TREASURE

    // Treasure CRUD
    bool saveTreasure(const char *label, int32_t latI, int32_t lonI, uint32_t ts);
    uint8_t treasureCount() const;
    const Treasure& treasure(uint8_t i) const;
    void deleteTreasure(uint8_t i);

    // Calibration
    void startCalibration();
    void feedCalSample(int16_t x, int16_t y, int16_t z);
    bool finishCalibration();  // false if < MIN_CAL_SAMPLES; saves to NVS on true
    uint32_t calSampleCount() const;
    void     loadCalibration(int16_t &ox, int16_t &oy, int16_t &oz) const;

    // Desire-side update suppression
    bool shouldSendUpdate(int32_t currentLatI, int32_t currentLonI) const;
    void onUpdateSent(int32_t latI, int32_t lonI);

private:
    static constexpr uint16_t MIN_CAL_SAMPLES = 50; // ~5s at typical rotation speed
    static constexpr char NVS_NS[] = "compass";

    State    _state = State::IDLE;
    uint32_t _stateEnteredMs = 0;

    DiscoveredNode _discovered[MAX_DISCOVERED_NODES];
    uint8_t        _discoveredCount = 0;
    uint32_t       _discoveryStartMs = 0;

    uint32_t _pendingPairNodeNum = 0;
    uint32_t _pairRequestSentMs  = 0;

    SessionData _session = {};

    Treasure _treasures[MAX_TREASURES];
    uint8_t  _treasureCount = 0;
    uint8_t  _activeTreasureIdx = 0xFF;

    // Calibration state
    int16_t _calMinX, _calMaxX;
    int16_t _calMinY, _calMaxY;
    int16_t _calMinZ, _calMaxZ;
    uint32_t _calSamples = 0;

    // Stored calibration offsets
    int16_t _calOX = 0, _calOY = 0, _calOZ = 0;

    // Desire-side suppression
    int32_t  _lastSentLatI   = 0;
    int32_t  _lastSentLonI   = 0;
    uint32_t _lastSentMs     = 0;

    void     setState(State s);
    void     loadNVS();
    void     saveTreasuresToNVS();
    void     saveCalToNVS();
};
```

### NVS Layout

Namespace: `"compass"`

| Key        | Type    | Description |
|------------|---------|-------------|
| `cal_ver`  | uint8   | Schema version; current = 1. If mismatch on load, discard and prompt re-cal. |
| `cal_x`    | int16   | Hard-iron X offset |
| `cal_y`    | int16   | Hard-iron Y offset |
| `cal_z`    | int16   | Hard-iron Z offset |
| `upd_int`  | uint16  | Position update interval, seconds (10/30/60/120; default 30) |
| `tr_cnt`   | uint8   | Number of saved Treasures (0–5) |
| `tr0_lat` … `tr4_lat` | int32 | Treasure latitude_i |
| `tr0_lon` … `tr4_lon` | int32 | Treasure longitude_i |
| `tr0_ts`  … `tr4_ts`  | uint32 | Treasure save timestamp |
| `tr0_lbl` … `tr4_lbl` | string | Treasure label (max 12 chars) |

Use `Preferences` class (`#include <Preferences.h>`). Open read-write for writes, read-only for loads. Always call `prefs.end()` after each operation block.

---

## 8. CompassModule

**Files:** `src/modules/CompassModule/CompassModule.h`, `CompassModule.cpp`

The central coordinator. Extends `SinglePortModule` for packet handling and `OSThread` for the periodic position-update timer.

### Class Interface

```cpp
class CompassModule : public SinglePortModule, private OSThread {
public:
    CompassModule();

    // MeshModule overrides
    bool wantUIFrame()  override;
    bool interceptingKeyboardInput() override;

    // SinglePortModule override
    ProcessMessage handleReceived(const MeshPacket &mp) override;

    // OSThread override
    int32_t runOnce() override;

    // Called by CompassUI when user takes UI actions
    void sendCapabilityQuery();
    void sendPairRequest(uint32_t targetNodeNum);
    void sendPairAccept(uint32_t toNodeNum);
    void sendPairReject(uint32_t toNodeNum);
    void sendSessionEnd();

    CompassState   *state();
    Magnetometer   *mag();

    static CompassModule *instance;

private:
    CompassState  _state;
    Magnetometer  _mag;

    void sendPacket(uint32_t to, uint8_t hopLimit, const meshtastic_CompassPacket &pkt);
    void handleCapabilityQuery(const MeshPacket &mp);
    void handleCapabilityAdv(const MeshPacket &mp);
    void handlePairRequest(const MeshPacket &mp);
    void handlePairAccept(const MeshPacket &mp);
    void handlePairConfirm(const MeshPacket &mp);
    void handlePairReject(const MeshPacket &mp);
    void handlePositionUpdate(const MeshPacket &mp);
    void handleSessionEnd(const MeshPacket &mp);

    int32_t tickTracked();   // called from runOnce() when state=TRACKED
    int32_t tickTracking();  // called from runOnce() when state=TRACKING/SESSION_PAUSED
};
```

### Packet Dispatch

`handleReceived` decodes the nanopb payload, then dispatches to the appropriate private handler. Unrecognized message types return `ProcessMessage::CONTINUE` (let other modules see it). All recognized types return `ProcessMessage::STOP`.

### OSThread::runOnce Behavior

`runOnce` is the heartbeat. Return value is milliseconds until next call.

```
switch state:
  IDLE, DISCOVERING, AWAITING_PAIR_ACCEPT, PAIR_INCOMING, CALIBRATING:
    return 500  // fast tick to detect timeouts
  TRACKED:
    return tickTracked()
  TRACKING, SESSION_PAUSED:
    return tickTracking()
  TRACKING_TREASURE:
    return 1000  // update distance display
```

**tickTracked():**
```
if _state.shouldSendUpdate(myLatI, myLonI):
    build POSITION_UPDATE packet with current GPS + battery
    sendPacket(peerNodeNum, 3, pkt)
    _state.onUpdateSent(myLatI, myLonI)
return updateIntervalMs  // from NVS setting
```

**tickTracking():**
```
if _state.isSessionPaused() and was not paused last tick:
    screen->setFrames()  // release UI frame, return to normal rotation
if _state.isDiscoveryWindowOpen():
    return 100  // fast tick during 3s discovery window
return 1000
```

**CAPABILITY_QUERY / discovery:**
When `sendCapabilityQuery()` is called, it broadcasts the packet and calls `_state.startDiscovery()`. Incoming `CAPABILITY_ADV` packets are fed to `_state.addDiscoveredNode()` for 3 seconds, then the UI renders the sorted list.

### Packet Construction

Hop limits per message type:
```
hop_limit=0: CAPABILITY_QUERY, CAPABILITY_ADV, PAIR_REQUEST,
             PAIR_ACCEPT, PAIR_CONFIRM, PAIR_REJECT
hop_limit=3: POSITION_UPDATE, SESSION_END
```

Broadcast (to=NODENUM_BROADCAST): CAPABILITY_QUERY, CAPABILITY_ADV, PAIR_REQUEST
Unicast (to=peerNodeNum): everything else

---

## 9. CompassUI

**Files:** `src/modules/CompassModule/CompassUI.h`, `CompassUI.cpp`

Handles all rendering and input. No radio calls — delegates to `CompassModule` for any packet sends.

### wantUIFrame

Returns `true` when any of these states are active:
- DISCOVERING, AWAITING_PAIR_ACCEPT, PAIR_INCOMING
- TRACKING, SESSION_PAUSED
- TRACKING_TREASURE
- CALIBRATING

Returns `false` in IDLE and TRACKED (Desire role has no UI takeover).

### interceptingKeyboardInput

Returns `true` whenever `wantUIFrame()` returns `true`.

### Screen Layout (128×64 px)

**DISCOVERING (node list):**
```
┌────────────────────────────┐
│ Find Desire         [3s..]  │  row 0 (font 10px)
│ > Alice  ████               │  row 12 (RSSI bar)
│   Bob    ███                │  row 24
│   Carol  █                  │  row 36
│ [refresh]                   │  row 56
└────────────────────────────┘
```

**TRACKING (compass arrow, full screen):**
```
┌────────────────────────────┐
│        /\                   │  arrow tip (rotated)
│       /  \                  │
│      / ↑  \  BOB            │  name right side
│      \    /  1.2 km         │  distance
│       \  /   23s ago  [▮▮▯] │  time + battery
│        \/                   │
└────────────────────────────┘
```

Arrow is drawn in a 48×48 px bounding box centered at (40, 28) relative to frame origin.
Right column (x=82 to x=128) shows peer name, distance, time-since-update, battery.

**Arrow geometry (before rotation, pointing up = 0° heading error):**
```
tip:        ( 0, -18)   relative to arrow center
left wing:  (-9,  -4)
right wing: ( 9,  -4)
tail:        ( 0,  12)
```
Draw as: `fillTriangle(tip, leftWing, rightWing)` + `drawLine(center, tail)`

**Rotation (heading error θ in radians):**
```
x' = x * cos(θ) - y * sin(θ)
y' = x * sin(θ) + y * cos(θ)
```
nRF52840 has hardware FPU; use `sinf`/`cosf` directly.

**Blink behavior (signal lost >90s):** Toggle arrow visibility every 500ms. Track with a `millis()` timestamp, flip a bool on each `drawFrame` call when the toggle interval has elapsed.

**PAIR_INCOMING:**
```
┌────────────────────────────┐
│ Pair Request               │
│                            │
│ Alice wants to             │
│ track you.                 │
│ [ACCEPT]    [REJECT]       │
└────────────────────────────┘
```

**CALIBRATING:**
```
┌────────────────────────────┐
│ Calibrating...             │
│                            │
│ Rotate device slowly       │
│ in all directions.         │
│ [NNN samples] [DONE]       │
└────────────────────────────┘
```

**TRACKING_TREASURE:**
Same layout as TRACKING but label shows Treasure name and no battery indicator. When within 15m, replace the arrow area with large text `YOU'RE HERE` and hold the display on.

### Input Handling

Button events come from `InputBroker` via `handleInputEvent(const InputEvent *e)`.

Map events to UI actions:
```
State::DISCOVERING:
  UP/DOWN           → scroll node list
  SELECT (short)    → confirm selected node → module->sendPairRequest()
  SELECT (long)     → exit to IDLE

State::PAIR_INCOMING:
  LEFT / UP         → highlight ACCEPT
  RIGHT / DOWN      → highlight REJECT
  SELECT            → confirm highlighted choice

State::TRACKING:
  SELECT (long)     → release UI frame (module keeps tracking in background)
  any other         → pass through to Screen normally

State::CALIBRATING:
  SELECT (short)    → _state.finishCalibration()
  SELECT (long)     → abort, return to IDLE

State::TRACKING_TREASURE:
  SELECT (long)     → release UI frame
```

Long-press threshold: 800ms. Track `pressStartMs` on key-down event; evaluate duration on key-up.

### Menu Entry Integration (Screen.cpp)

Add one entry to the main Meshtastic menu (the exact insertion point depends on current Screen.cpp structure — find where other module menu entries are added and follow the same pattern):

- Label: `"Compass"`
- Action: call `CompassModule::instance->state()->startDiscovery()` then `CompassModule::instance->sendCapabilityQuery()`, then call `screen->setFrames()` to give the module its UI frame

The rest of the Compass sub-menu (Save Treasure, Treasures list, End Session, Settings, Status/calibrate) lives entirely within `CompassUI::drawFrame` state transitions — not as separate Screen.cpp entries.

---

## 10. Integration

### Modules.cpp

Two lines:
```cpp
#include "modules/CompassModule/CompassModule.h"
// in setupModules():
new CompassModule();
```

The constructor registers the module with the `SinglePortModule` dispatcher and the `OSThread` scheduler. No other changes needed.

### Screen.cpp

One menu entry. Follow the pattern of whichever existing module adds a menu entry most recently — copy its exact registration approach. The entry just needs to trigger `CompassModule::instance->sendCapabilityQuery()` and a `setFrames()` call.

---

## 11. Bead Breakdown

Each bead is independently committable. Implement in dependency order; parallel tracks are marked.

---

### BEAD-1: Variant Patch
**Depends on:** nothing
**Files:** `variants/nrf52840/heltec_mesh_node_t114/variant.h`, `variant.cpp`
**What to do:** Add the four `#define`s and the `Wire1` TwoWire instance per section 3 of this doc.
**Acceptance:** Firmware compiles for `heltec_mesh_node_t114`. `Wire1.begin()` does not assert. A minimal I2C scan sketch on the T114 with QMC5883L wired returns address 0x0D.
**Risk:** Verify `NRF_TWIM1` / `SPIM1_SPIS1_TWIM1_TWIS1_SPI1_TWI1_IRQn` identifiers exist in the nRF52840 ArduinoCore headers. Check existing `Wire` instantiation in the same file and copy the exact constructor form.

---

### BEAD-2: Protobuf Definition
**Depends on:** nothing (parallel with BEAD-1)
**Files:** `protobufs/meshtastic/compass.proto`, `protobufs/meshtastic/portnums.proto`, generated `compass.pb.h` / `compass.pb.c`
**What to do:** Write `compass.proto` per section 4. Add `COMPASS_APP = 300` to `portnums.proto`. Run `bin/generate-proto.sh`. Commit generated files.
**Acceptance:** `compass.pb.h` exists, compiles, and `meshtastic_CompassMsgType` and `meshtastic_CompassPacket` are accessible. `meshtastic_PortNum_COMPASS_APP` resolves.

---

### BEAD-3: Magnetometer Driver
**Depends on:** BEAD-1 (needs Wire1 to exist)
**Files:** `src/modules/CompassModule/Magnetometer.h`, `Magnetometer.cpp`
**What to do:** Implement per section 5. Init sequence, DRDY-gated read, heading computation, calibration offset application.
**Acceptance:**
- `begin()` returns `OK` with QMC5883L connected, `NOT_FOUND` with nothing on bus, `INIT_FAILED` with bus present but chip ID wrong.
- `heading()` returns a value in [0, 360) that rotates smoothly as the device is rotated.
- `setCalibration(0,0,0)` and `setCalibration(measured offsets)` both compile and affect output.
- No blocking delay in the hot path longer than `DRDY_TIMEOUT_MS`.

---

### BEAD-4: CompassMath
**Depends on:** nothing (parallel with BEAD-1, BEAD-2)
**Files:** `src/modules/CompassModule/CompassMath.h`, `CompassMath.cpp`
**What to do:** Implement the three functions in section 6.
**Acceptance (unit-testable on host):**
- `bearing(0, 0, 1e7, 0)` ≈ 0° (due north)
- `bearing(0, 0, 0, 1e7)` ≈ 90° (due east)
- `distanceMeters(0, 0, 0, 1e7)` ≈ 1111950m (1° longitude at equator)
- `headingError(350, 10)` ≈ 20° (turn right 20°)
- `headingError(10, 350)` ≈ -20° (turn left 20°)

---

### BEAD-5: CompassState
**Depends on:** BEAD-4 (imports CompassMath types)
**Files:** `src/modules/CompassModule/CompassState.h`, `CompassState.cpp`
**What to do:** Implement FSM, NVS load/save, Treasure CRUD, calibration sample accumulation, and Desire-side suppression logic per section 7.
**Acceptance:**
- State transitions follow the table in section 7 exactly. No invalid transitions compile.
- `saveTreasure` / `treasure` round-trip through NVS correctly (call `begin()`, save, create new instance, call `begin()`, read back).
- `shouldSendUpdate`: returns true immediately after `begin()`, returns false on second call with same position within `STATIONARY_SUPPRESS_MS`, returns true again after `STATIONARY_SUPPRESS_MS` even with same position.
- `finishCalibration()` returns false if `calSampleCount() < MIN_CAL_SAMPLES`, true otherwise.
- NVS cal version mismatch results in zero offsets returned, not a crash.

---

### BEAD-6: CompassModule
**Depends on:** BEAD-2 (proto types), BEAD-3 (Magnetometer), BEAD-5 (CompassState)
**Files:** `src/modules/CompassModule/CompassModule.h`, `CompassModule.cpp`
**What to do:** Implement packet dispatch, `runOnce` scheduler, send helpers per section 8.
**Acceptance:**
- Module registers on `COMPASS_APP` port. Other port packets are ignored.
- `sendCapabilityQuery()` produces a broadcast packet with `hop_limit=0` and type `CAPABILITY_QUERY`. Verify with a packet sniffer or log.
- `handleReceived` dispatches correctly — inject a mock `PAIR_REQUEST` packet and verify `_state.getState() == State::PAIR_INCOMING`.
- `runOnce` in `TRACKED` state calls `sendPacket` at approximately the configured interval and calls `_state.onUpdateSent`.
- `wantUIFrame()` returns `true` in DISCOVERING, TRACKING, PAIR_INCOMING, CALIBRATING; `false` in IDLE and TRACKED.

---

### BEAD-7: CompassUI
**Depends on:** BEAD-4 (CompassMath), BEAD-5 (CompassState), BEAD-6 (CompassModule, for send calls)
**Files:** `src/modules/CompassModule/CompassUI.h`, `CompassUI.cpp`
**What to do:** Implement `drawFrame`, `interceptingKeyboardInput`, `handleInputEvent` per section 9. All rendering geometry and input state machine live here.
**Acceptance:**
- Arrow tip points visually upward when heading error = 0.
- Arrow rotates clockwise for positive heading error, counter-clockwise for negative. Verified at ±45°, ±90°, ±180°.
- Arrow blinks (500ms period) when `_state.isSignalLost()` is true.
- PAIR_INCOMING screen shows node name; ACCEPT selection calls `module->sendPairAccept`.
- CALIBRATING screen increments sample count in real time.
- Long-press (>800ms) SELECT in TRACKING returns `false` from `interceptingKeyboardInput` and calls `screen->setFrames()`.
- TRACKING_TREASURE shows `YOU'RE HERE` when distance < 15m.

---

### BEAD-8: Integration
**Depends on:** BEAD-6, BEAD-7
**Files:** `src/modules/Modules.cpp`, `src/graphics/Screen.cpp`
**What to do:** Wire up per section 10. Two lines in Modules.cpp. One menu entry in Screen.cpp.
**Acceptance:**
- Firmware compiles and boots on T114 without assert or hang.
- "Compass" entry appears in the Meshtastic main menu.
- Selecting it triggers DISCOVERING state and the node list frame appears.
- All existing Meshtastic functionality (text, position share, mesh routing) is unaffected when Compass is idle.

---

*This document is the authoritative implementation spec for issue #2. PRD owns the what and why; this doc owns the how. When these conflict, fix this doc — not the PRD.*
