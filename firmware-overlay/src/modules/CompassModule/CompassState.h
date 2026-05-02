// Captain's Compass — FSM, session data, calibration, Treasure store.
// See docs/tdd-issue-002-captains-compass.md §12.
//
// No I/O on the radio or display from this class — just state. Persistence
// is via FSCom (cross-arch filesystem; LittleFS on nRF52, LittleFS on
// ESP32, Portduino fs on native).

#pragma once

#include <stdint.h>

namespace compass {

enum class State : uint8_t {
    IDLE,
    DISCOVERING,           // CAPABILITY_QUERY sent, collecting ADVs (3s window)
    AWAITING_PAIR_ACCEPT,  // PAIR_REQUEST sent, 60s timeout
    PAIR_INCOMING,         // PAIR_REQUEST received, showing prompt
    TRACKING,              // Active as Compass (pointing at Desire)
    TRACKED,               // Active as Desire (sending position updates)
    TRACKING_TREASURE,     // Navigating to a saved waypoint
    SESSION_PAUSED,        // No Desire updates for 5min; backgrounded
    CALIBRATING,           // Magnetometer calibration loop
    STATUS,                // Read-only status screen (any input dismisses)
};

struct DiscoveredNode {
    uint32_t nodeNum;
    char     shortName[5];  // null-terminated, up to 4 chars
    int8_t   rssi;          // from last CAPABILITY_ADV
};

struct SessionData {
    uint32_t peerNodeNum;
    char     peerName[12];           // null-terminated
    int32_t  peerLatI;
    int32_t  peerLonI;
    uint32_t peerLastUpdateSec;      // Unix time of last POSITION_UPDATE
    uint32_t peerBatteryPct;
    bool     peerHasGPS;
};

struct Treasure {
    char     label[13];   // null-terminated, up to 12 chars
    int32_t  latI;
    int32_t  lonI;
    uint32_t savedAt;     // Unix timestamp
};

class CompassState {
public:
    static constexpr uint8_t  MAX_TREASURES          = 5;
    static constexpr uint8_t  MAX_DISCOVERED_NODES   = 8;
    static constexpr uint32_t DISCOVERY_WINDOW_MS    = 3000;
    static constexpr uint32_t PAIR_TIMEOUT_MS        = 60000;
    static constexpr uint32_t SIGNAL_LOST_MS         = 90000;
    static constexpr uint32_t SESSION_PAUSE_MS       = 300000;  // 5 min
    static constexpr uint32_t STATIONARY_SUPPRESS_MS = 300000;  // 5 min keepalive
    static constexpr float    ARRIVAL_THRESHOLD_M    = 15.0f;
    static constexpr float    GPS_NOISE_FLOOR_M      = 5.0f;

    void  begin();             // load NVS, set state=IDLE
    State getState() const { return _state; }

    // Discovery
    void                  startDiscovery();
    void                  addDiscoveredNode(uint32_t nodeNum, const char *shortName, int8_t rssi);
    bool                  isDiscoveryWindowOpen() const;
    uint8_t               discoveredNodeCount() const { return _discoveredCount; }
    const DiscoveredNode &discoveredNode(uint8_t i) const { return _discovered[i]; }

    // Pairing — Compass side
    void startPairRequest(uint32_t targetNodeNum);
    void onPairAccepted(uint32_t peerNodeNum, const char *peerName);
    void onPairRejected();
    bool isPairTimedOut() const;

    // Pairing — Desire side
    void     onPairRequestReceived(uint32_t initiatorNodeNum);
    void     acceptPair();
    void     rejectPair();
    uint32_t pendingPairNodeNum() const { return _pendingPairNodeNum; }

    // Session
    void onPositionUpdate(int32_t latI, int32_t lonI, uint32_t time, uint32_t battPct, bool hasGPS);
    void onSessionEnd();        // remote ended
    void endSession();          // local user-initiated

    const SessionData &session() const { return _session; }
    bool               isSignalLost() const;
    bool               isSessionPaused() const;

    // Treasure navigation
    void            startTreasureNav(uint8_t index);
    const Treasure &activeTreasure() const { return _treasures[_activeTreasureIdx]; }

    // Treasure CRUD
    bool            saveTreasure(const char *label, int32_t latI, int32_t lonI, uint32_t ts);
    uint8_t         treasureCount() const { return _treasureCount; }
    const Treasure &treasure(uint8_t i) const { return _treasures[i]; }
    void            deleteTreasure(uint8_t i);

    // Status screen (read-only; transitions to IDLE on any input)
    void     startStatus();

    // Calibration
    void     startCalibration();
    void     feedCalSample(int16_t x, int16_t y, int16_t z);
    bool     finishCalibration();   // false if < MIN_CAL_SAMPLES; saves on true
    uint32_t calSampleCount() const { return _calSamples; }
    void     loadCalibration(int16_t &ox, int16_t &oy, int16_t &oz) const;

    // Settings
    uint16_t updateIntervalSec() const { return _updateIntervalSec; }
    void     setUpdateIntervalSec(uint16_t sec);

    // Desire-side update suppression
    bool shouldSendUpdate(int32_t currentLatI, int32_t currentLonI) const;
    void onUpdateSent(int32_t latI, int32_t lonI);

private:
    static constexpr uint16_t MIN_CAL_SAMPLES = 50;        // ~5s at typical rotation speed
    static constexpr uint8_t  CAL_SCHEMA_VERSION = 1;
    static constexpr const char *CAL_PATH       = "/compass/cal";
    static constexpr const char *TREASURES_PATH = "/compass/treasures";
    static constexpr const char *SETTINGS_PATH  = "/compass/settings";

    State    _state            = State::IDLE;
    uint32_t _stateEnteredMs   = 0;

    DiscoveredNode _discovered[MAX_DISCOVERED_NODES] = {};
    uint8_t        _discoveredCount = 0;
    uint32_t       _discoveryStartMs = 0;

    uint32_t _pendingPairNodeNum = 0;
    uint32_t _pairRequestSentMs  = 0;

    SessionData _session = {};

    Treasure _treasures[MAX_TREASURES] = {};
    uint8_t  _treasureCount   = 0;
    uint8_t  _activeTreasureIdx = 0xFF;

    int16_t  _calMinX = 0, _calMaxX = 0;
    int16_t  _calMinY = 0, _calMaxY = 0;
    int16_t  _calMinZ = 0, _calMaxZ = 0;
    uint32_t _calSamples = 0;
    int16_t  _calOX = 0, _calOY = 0, _calOZ = 0;
    bool     _calValid = false;

    uint16_t _updateIntervalSec = 30;

    int32_t  _lastSentLatI = 0;
    int32_t  _lastSentLonI = 0;
    uint32_t _lastSentMs   = 0;

    void setState(State s);
    void loadCalibrationFile();
    void saveCalibrationFile();
    void loadTreasuresFile();
    void saveTreasuresFile();
    void loadSettingsFile();
    void saveSettingsFile();
};

}  // namespace compass
