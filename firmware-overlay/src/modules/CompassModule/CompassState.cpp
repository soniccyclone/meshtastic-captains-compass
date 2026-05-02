// Captain's Compass — FSM + persistence implementation.
// See docs/tdd-issue-002-captains-compass.md §12.

#include "CompassState.h"

#include "CompassMath.h"
#include "FSCommon.h"

#include <Arduino.h>
#include <string.h>

namespace compass {

namespace {

constexpr uint8_t  TREASURES_SCHEMA_VERSION = 1;
constexpr uint8_t  SETTINGS_SCHEMA_VERSION  = 1;

struct CalRecord {
    uint8_t version;
    uint8_t valid;        // 1 if offsets are populated
    int16_t offX, offY, offZ;
};

struct TreasuresHeader {
    uint8_t version;
    uint8_t count;
};

struct SettingsRecord {
    uint8_t  version;
    uint16_t updateIntervalSec;
};

bool ensureCompassDir() {
    if (!FSCom.exists("/compass")) {
        return FSCom.mkdir("/compass");
    }
    return true;
}

void clampString(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    size_t i = 0;
    if (src) {
        for (; i < cap - 1 && src[i] != '\0'; ++i) dst[i] = src[i];
    }
    dst[i] = '\0';
}

}  // namespace

void CompassState::begin() {
    _state = State::IDLE;
    _stateEnteredMs = millis();
    loadCalibrationFile();
    loadTreasuresFile();
    loadSettingsFile();
}

void CompassState::setState(State s) {
    _state = s;
    _stateEnteredMs = millis();
}

// --- Discovery ---------------------------------------------------------

void CompassState::startDiscovery() {
    _discoveredCount = 0;
    _discoveryStartMs = millis();
    setState(State::DISCOVERING);
}

void CompassState::addDiscoveredNode(uint32_t nodeNum, const char *shortName, int8_t rssi) {
    if (_state != State::DISCOVERING || !isDiscoveryWindowOpen()) return;

    // Update existing entry if we already saw this node (refresh rssi).
    for (uint8_t i = 0; i < _discoveredCount; ++i) {
        if (_discovered[i].nodeNum == nodeNum) {
            _discovered[i].rssi = rssi;
            return;
        }
    }
    if (_discoveredCount >= MAX_DISCOVERED_NODES) return;
    _discovered[_discoveredCount].nodeNum = nodeNum;
    clampString(_discovered[_discoveredCount].shortName, sizeof(_discovered[0].shortName), shortName);
    _discovered[_discoveredCount].rssi = rssi;
    ++_discoveredCount;
}

bool CompassState::isDiscoveryWindowOpen() const {
    return _state == State::DISCOVERING &&
           (millis() - _discoveryStartMs) < DISCOVERY_WINDOW_MS;
}

// --- Pairing (Compass side) -------------------------------------------

void CompassState::startPairRequest(uint32_t targetNodeNum) {
    _pendingPairNodeNum = targetNodeNum;
    _pairRequestSentMs  = millis();
    setState(State::AWAITING_PAIR_ACCEPT);
}

void CompassState::onPairAccepted(uint32_t peerNodeNum, const char *peerName) {
    _session.peerNodeNum = peerNodeNum;
    clampString(_session.peerName, sizeof(_session.peerName), peerName);
    _session.peerLatI = 0;
    _session.peerLonI = 0;
    _session.peerLastUpdateSec = 0;
    _session.peerBatteryPct = 0;
    _session.peerHasGPS = false;
    setState(State::TRACKING);
}

void CompassState::onPairRejected() {
    _pendingPairNodeNum = 0;
    setState(State::IDLE);
}

bool CompassState::isPairTimedOut() const {
    return _state == State::AWAITING_PAIR_ACCEPT &&
           (millis() - _pairRequestSentMs) >= PAIR_TIMEOUT_MS;
}

// --- Pairing (Desire side) --------------------------------------------

void CompassState::onPairRequestReceived(uint32_t initiatorNodeNum) {
    if (_state != State::IDLE) return;  // ignore if busy
    _pendingPairNodeNum = initiatorNodeNum;
    setState(State::PAIR_INCOMING);
}

void CompassState::acceptPair() {
    if (_state != State::PAIR_INCOMING) return;
    _session.peerNodeNum = _pendingPairNodeNum;
    _session.peerName[0] = '\0';  // filled by caller from NodeDB
    _session.peerLatI = 0;
    _session.peerLonI = 0;
    _session.peerLastUpdateSec = 0;
    _session.peerBatteryPct = 0;
    _session.peerHasGPS = false;
    _pendingPairNodeNum = 0;
    setState(State::TRACKED);
}

void CompassState::rejectPair() {
    _pendingPairNodeNum = 0;
    setState(State::IDLE);
}

// --- Session management ------------------------------------------------

void CompassState::onPositionUpdate(int32_t latI, int32_t lonI, uint32_t time, uint32_t battPct, bool hasGPS) {
    if (_state != State::TRACKING && _state != State::SESSION_PAUSED) return;
    _session.peerLatI = latI;
    _session.peerLonI = lonI;
    _session.peerLastUpdateSec = time;
    _session.peerBatteryPct = battPct;
    _session.peerHasGPS = hasGPS;
    if (_state == State::SESSION_PAUSED) setState(State::TRACKING);
}

void CompassState::onSessionEnd() {
    setState(State::IDLE);
    _session = {};
}

void CompassState::endSession() {
    setState(State::IDLE);
    _session = {};
}

bool CompassState::isSignalLost() const {
    if (_state != State::TRACKING && _state != State::SESSION_PAUSED) return false;
    if (_session.peerLastUpdateSec == 0) return false;
    const uint32_t nowMs = millis();
    // Approximate: time since last update in ms vs SIGNAL_LOST_MS.
    // We measure ms-since-onPositionUpdate via _stateEnteredMs of last TRACKING entry.
    return (nowMs - _stateEnteredMs) >= SIGNAL_LOST_MS;
}

bool CompassState::isSessionPaused() const {
    return _state == State::SESSION_PAUSED;
}

// --- Treasure navigation ----------------------------------------------

void CompassState::startTreasureNav(uint8_t index) {
    if (index >= _treasureCount) return;
    _activeTreasureIdx = index;
    setState(State::TRACKING_TREASURE);
}

bool CompassState::saveTreasure(const char *label, int32_t latI, int32_t lonI, uint32_t ts) {
    if (_treasureCount >= MAX_TREASURES) return false;
    Treasure &t = _treasures[_treasureCount];
    clampString(t.label, sizeof(t.label), label);
    t.latI = latI;
    t.lonI = lonI;
    t.savedAt = ts;
    ++_treasureCount;
    saveTreasuresFile();
    return true;
}

void CompassState::deleteTreasure(uint8_t i) {
    if (i >= _treasureCount) return;
    for (uint8_t j = i; j < _treasureCount - 1; ++j) {
        _treasures[j] = _treasures[j + 1];
    }
    --_treasureCount;
    saveTreasuresFile();
}

// --- Calibration ------------------------------------------------------

void CompassState::startCalibration() {
    _calMinX = INT16_MAX;
    _calMaxX = INT16_MIN;
    _calMinY = INT16_MAX;
    _calMaxY = INT16_MIN;
    _calMinZ = INT16_MAX;
    _calMaxZ = INT16_MIN;
    _calSamples = 0;
    setState(State::CALIBRATING);
}

void CompassState::feedCalSample(int16_t x, int16_t y, int16_t z) {
    if (_state != State::CALIBRATING) return;
    if (x < _calMinX) _calMinX = x;
    if (x > _calMaxX) _calMaxX = x;
    if (y < _calMinY) _calMinY = y;
    if (y > _calMaxY) _calMaxY = y;
    if (z < _calMinZ) _calMinZ = z;
    if (z > _calMaxZ) _calMaxZ = z;
    ++_calSamples;
}

bool CompassState::finishCalibration() {
    if (_state != State::CALIBRATING) return false;
    if (_calSamples < MIN_CAL_SAMPLES) return false;
    _calOX = static_cast<int16_t>((_calMaxX + _calMinX) / 2);
    _calOY = static_cast<int16_t>((_calMaxY + _calMinY) / 2);
    _calOZ = static_cast<int16_t>((_calMaxZ + _calMinZ) / 2);
    _calValid = true;
    saveCalibrationFile();
    setState(State::IDLE);
    return true;
}

void CompassState::loadCalibration(int16_t &ox, int16_t &oy, int16_t &oz) const {
    ox = _calValid ? _calOX : 0;
    oy = _calValid ? _calOY : 0;
    oz = _calValid ? _calOZ : 0;
}

// --- Settings ---------------------------------------------------------

void CompassState::setUpdateIntervalSec(uint16_t sec) {
    _updateIntervalSec = sec;
    saveSettingsFile();
}

// --- Desire-side update suppression -----------------------------------

bool CompassState::shouldSendUpdate(int32_t currentLatI, int32_t currentLonI) const {
    if (_lastSentMs == 0) return true;  // first send
    const uint32_t dtMs = millis() - _lastSentMs;
    if (dtMs >= STATIONARY_SUPPRESS_MS) return true;  // 5min keepalive
    const float distM = CompassMath::distanceMeters(_lastSentLatI, _lastSentLonI, currentLatI, currentLonI);
    return distM > GPS_NOISE_FLOOR_M;
}

void CompassState::onUpdateSent(int32_t latI, int32_t lonI) {
    _lastSentLatI = latI;
    _lastSentLonI = lonI;
    _lastSentMs   = millis();
}

// --- Persistence ------------------------------------------------------
//
// Layout under /compass/:
//   cal        — CalRecord (v1)
//   treasures  — TreasuresHeader + Treasure[count]
//   settings   — SettingsRecord (v1)
// All blobs are fixed-layout binary structs; readers check the version
// byte and ignore mismatched files (callers receive zero offsets / empty
// state, prompting re-cal / re-config). Schema bumps are forward-only.

void CompassState::loadCalibrationFile() {
    File f = FSCom.open(CAL_PATH, FILE_O_READ);
    if (!f) return;
    CalRecord rec = {};
    if (f.read(reinterpret_cast<uint8_t *>(&rec), sizeof(rec)) != sizeof(rec)) {
        f.close();
        return;
    }
    f.close();
    if (rec.version != CAL_SCHEMA_VERSION) return;  // version mismatch → discard
    _calValid = (rec.valid != 0);
    _calOX = rec.offX;
    _calOY = rec.offY;
    _calOZ = rec.offZ;
}

void CompassState::saveCalibrationFile() {
    if (!ensureCompassDir()) return;
    FSCom.remove(CAL_PATH);
    File f = FSCom.open(CAL_PATH, FILE_O_WRITE);
    if (!f) return;
    CalRecord rec = {};
    rec.version = CAL_SCHEMA_VERSION;
    rec.valid = _calValid ? 1 : 0;
    rec.offX = _calOX;
    rec.offY = _calOY;
    rec.offZ = _calOZ;
    f.write(reinterpret_cast<const uint8_t *>(&rec), sizeof(rec));
    f.close();
}

void CompassState::loadTreasuresFile() {
    File f = FSCom.open(TREASURES_PATH, FILE_O_READ);
    if (!f) return;
    TreasuresHeader hdr = {};
    if (f.read(reinterpret_cast<uint8_t *>(&hdr), sizeof(hdr)) != sizeof(hdr)) {
        f.close();
        return;
    }
    if (hdr.version != TREASURES_SCHEMA_VERSION) {
        f.close();
        return;
    }
    if (hdr.count > MAX_TREASURES) hdr.count = MAX_TREASURES;
    for (uint8_t i = 0; i < hdr.count; ++i) {
        if (f.read(reinterpret_cast<uint8_t *>(&_treasures[i]), sizeof(Treasure)) != sizeof(Treasure)) {
            _treasureCount = i;
            f.close();
            return;
        }
    }
    _treasureCount = hdr.count;
    f.close();
}

void CompassState::saveTreasuresFile() {
    if (!ensureCompassDir()) return;
    FSCom.remove(TREASURES_PATH);
    File f = FSCom.open(TREASURES_PATH, FILE_O_WRITE);
    if (!f) return;
    TreasuresHeader hdr = {};
    hdr.version = TREASURES_SCHEMA_VERSION;
    hdr.count = _treasureCount;
    f.write(reinterpret_cast<const uint8_t *>(&hdr), sizeof(hdr));
    for (uint8_t i = 0; i < _treasureCount; ++i) {
        f.write(reinterpret_cast<const uint8_t *>(&_treasures[i]), sizeof(Treasure));
    }
    f.close();
}

void CompassState::loadSettingsFile() {
    File f = FSCom.open(SETTINGS_PATH, FILE_O_READ);
    if (!f) return;
    SettingsRecord rec = {};
    if (f.read(reinterpret_cast<uint8_t *>(&rec), sizeof(rec)) != sizeof(rec)) {
        f.close();
        return;
    }
    f.close();
    if (rec.version != SETTINGS_SCHEMA_VERSION) return;
    _updateIntervalSec = rec.updateIntervalSec;
}

void CompassState::saveSettingsFile() {
    if (!ensureCompassDir()) return;
    FSCom.remove(SETTINGS_PATH);
    File f = FSCom.open(SETTINGS_PATH, FILE_O_WRITE);
    if (!f) return;
    SettingsRecord rec = {};
    rec.version = SETTINGS_SCHEMA_VERSION;
    rec.updateIntervalSec = _updateIntervalSec;
    f.write(reinterpret_cast<const uint8_t *>(&rec), sizeof(rec));
    f.close();
}

}  // namespace compass
