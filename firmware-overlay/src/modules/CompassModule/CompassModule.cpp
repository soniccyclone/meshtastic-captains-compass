// Captain's Compass — central coordinator.
// See docs/tdd-issue-002-captains-compass.md §13.

#include "CompassModule.h"

#include "MeshService.h"
#include "NodeDB.h"
#include "PowerStatus.h"
#include "Router.h"
#include "configuration.h"
#include "input/InputBroker.h"
#include "mesh/mesh-pb-constants.h"

CompassModule *CompassModule::instance = nullptr;

CompassModule::CompassModule()
    : SinglePortModule("compass", meshtastic_PortNum_COMPASS_APP),
      concurrency::OSThread("Compass") {
    instance = this;
    _state.begin();
    Magnetometer::InitResult magInit = _mag.begin();
    LOG_INFO("Captain's Compass: mag init %s",
             magInit == Magnetometer::OK         ? "OK"
             : magInit == Magnetometer::NOT_FOUND ? "NOT_FOUND"
                                                  : "INIT_FAILED");

    int16_t ox, oy, oz;
    _state.loadCalibration(ox, oy, oz);
    _mag.setCalibration(ox, oy, oz);

    if (inputBroker) _inputObserver.observe(inputBroker);
}

// --- UI refresh helper -------------------------------------------------

void CompassModule::notifyUIChanged() {
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REGENERATE_FRAMESET;
    notifyObservers(&e);
}

// --- UI overrides (delegate to CompassUI) -----------------------------

bool CompassModule::wantUIFrame() {
    return _ui.wantUIFrame();
}

bool CompassModule::interceptingKeyboardInput() {
    return _ui.interceptingKeyboardInput();
}

void CompassModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) {
    _ui.drawFrame(display, state, x, y);
}

int CompassModule::handleInputEvent(const InputEvent *e) {
    return _ui.handleInputEvent(e);
}

// --- Packet dispatch ---------------------------------------------------

ProcessMessage CompassModule::handleReceived(const meshtastic_MeshPacket &mp) {
    meshtastic_CompassPacket pkt = meshtastic_CompassPacket_init_default;
    if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size,
                              &meshtastic_CompassPacket_msg, &pkt)) {
        return ProcessMessage::CONTINUE;
    }

    switch (pkt.type) {
        case meshtastic_CompassMsgType_CAPABILITY_QUERY:
            handleCapabilityQuery(mp);
            return ProcessMessage::STOP;
        case meshtastic_CompassMsgType_CAPABILITY_ADV:
            handleCapabilityAdv(mp, pkt);
            return ProcessMessage::STOP;
        case meshtastic_CompassMsgType_PAIR_REQUEST:
            handlePairRequest(mp);
            return ProcessMessage::STOP;
        case meshtastic_CompassMsgType_PAIR_ACCEPT:
            handlePairAccept(mp);
            return ProcessMessage::STOP;
        case meshtastic_CompassMsgType_PAIR_CONFIRM:
            handlePairConfirm(mp);
            return ProcessMessage::STOP;
        case meshtastic_CompassMsgType_PAIR_REJECT:
            handlePairReject(mp);
            return ProcessMessage::STOP;
        case meshtastic_CompassMsgType_POSITION_UPDATE:
            handlePositionUpdate(mp, pkt);
            return ProcessMessage::STOP;
        case meshtastic_CompassMsgType_SESSION_END:
            handleSessionEnd(mp);
            return ProcessMessage::STOP;
        default:
            return ProcessMessage::CONTINUE;
    }
}

// --- Send helpers ------------------------------------------------------

void CompassModule::sendPacket(uint32_t to, uint8_t hopLimit, const meshtastic_CompassPacket &pkt) {
    meshtastic_MeshPacket *p = allocDataPacket();
    if (!p) return;
    p->to = to;
    p->hop_limit = hopLimit;
    p->want_ack = false;
    p->decoded.payload.size = pb_encode_to_bytes(
        p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
        &meshtastic_CompassPacket_msg, &pkt);
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}

void CompassModule::sendCapabilityQuery() {
    _state.startDiscovery();
    notifyUIChanged();
    meshtastic_CompassPacket pkt = meshtastic_CompassPacket_init_default;
    pkt.type = meshtastic_CompassMsgType_CAPABILITY_QUERY;
    sendPacket(NODENUM_BROADCAST, /*hop_limit=*/0, pkt);
    LOG_INFO("Compass: sent CAPABILITY_QUERY");
}

void CompassModule::sendCapabilityAdv(uint32_t toNodeNum) {
    meshtastic_CompassPacket pkt = meshtastic_CompassPacket_init_default;
    pkt.type = meshtastic_CompassMsgType_CAPABILITY_ADV;
    sendPacket(toNodeNum, /*hop_limit=*/0, pkt);
}

void CompassModule::sendPairRequest(uint32_t targetNodeNum) {
    _state.startPairRequest(targetNodeNum);
    notifyUIChanged();
    meshtastic_CompassPacket pkt = meshtastic_CompassPacket_init_default;
    pkt.type = meshtastic_CompassMsgType_PAIR_REQUEST;
    sendPacket(targetNodeNum, /*hop_limit=*/0, pkt);
    LOG_INFO("Compass: sent PAIR_REQUEST to 0x%x", targetNodeNum);
}

void CompassModule::sendPairAccept(uint32_t toNodeNum) {
    meshtastic_CompassPacket pkt = meshtastic_CompassPacket_init_default;
    pkt.type = meshtastic_CompassMsgType_PAIR_ACCEPT;
    sendPacket(toNodeNum, /*hop_limit=*/0, pkt);
}

void CompassModule::sendPairConfirm(uint32_t toNodeNum) {
    meshtastic_CompassPacket pkt = meshtastic_CompassPacket_init_default;
    pkt.type = meshtastic_CompassMsgType_PAIR_CONFIRM;
    sendPacket(toNodeNum, /*hop_limit=*/0, pkt);
}

void CompassModule::sendPairReject(uint32_t toNodeNum) {
    meshtastic_CompassPacket pkt = meshtastic_CompassPacket_init_default;
    pkt.type = meshtastic_CompassMsgType_PAIR_REJECT;
    sendPacket(toNodeNum, /*hop_limit=*/0, pkt);
}

void CompassModule::sendSessionEnd() {
    const uint32_t peer = _state.session().peerNodeNum;
    if (peer == 0) return;
    meshtastic_CompassPacket pkt = meshtastic_CompassPacket_init_default;
    pkt.type = meshtastic_CompassMsgType_SESSION_END;
    sendPacket(peer, /*hop_limit=*/3, pkt);
    _state.endSession();
    notifyUIChanged();
}

// --- Per-message handlers ---------------------------------------------

void CompassModule::handleCapabilityQuery(const meshtastic_MeshPacket &mp) {
    // Anyone running CompassModule responds, regardless of UI state.
    sendCapabilityAdv(mp.from);
}

void CompassModule::handleCapabilityAdv(const meshtastic_MeshPacket &mp,
                                        const meshtastic_CompassPacket & /*pkt*/) {
    if (!_state.isDiscoveryWindowOpen()) return;
    const meshtastic_NodeInfoLite *info = nodeDB ? nodeDB->getMeshNode(mp.from) : nullptr;
    const char *shortName = (info && info->has_user) ? info->user.short_name : "";
    _state.addDiscoveredNode(mp.from, shortName, mp.rx_rssi);
}

void CompassModule::handlePairRequest(const meshtastic_MeshPacket &mp) {
    _state.onPairRequestReceived(mp.from);
    notifyUIChanged();
}

void CompassModule::handlePairAccept(const meshtastic_MeshPacket &mp) {
    const meshtastic_NodeInfoLite *info = nodeDB ? nodeDB->getMeshNode(mp.from) : nullptr;
    const char *peerName = (info && info->has_user) ? info->user.long_name : "";
    _state.onPairAccepted(mp.from, peerName);
    sendPairConfirm(mp.from);
    notifyUIChanged();
}

void CompassModule::handlePairConfirm(const meshtastic_MeshPacket &mp) {
    // Desire side: pair handshake complete. Already in TRACKED if we acceptPair'd.
    (void)mp;
}

void CompassModule::handlePairReject(const meshtastic_MeshPacket & /*mp*/) {
    _state.onPairRejected();
    notifyUIChanged();
}

void CompassModule::handlePositionUpdate(const meshtastic_MeshPacket &mp,
                                         const meshtastic_CompassPacket &pkt) {
    if (mp.from != _state.session().peerNodeNum) return;
    _state.onPositionUpdate(pkt.latitude_i, pkt.longitude_i, pkt.time, pkt.battery_pct,
                            /*hasGPS=*/(pkt.latitude_i != 0 || pkt.longitude_i != 0));
}

void CompassModule::handleSessionEnd(const meshtastic_MeshPacket &mp) {
    if (mp.from != _state.session().peerNodeNum) return;
    _state.onSessionEnd();
    notifyUIChanged();
}

// --- Scheduler ---------------------------------------------------------

int32_t CompassModule::runOnce() {
    using compass::State;

    // Backstop: catch any state transition that happened since the last
    // tick (auto SESSION_PAUSED in particular, which has no explicit call
    // site). Inline notifyUIChanged() calls in send/handle helpers cover
    // user-action latency; this loop catches whatever they miss.
    State current = _state.getState();
    if (current != _prevState) {
        notifyUIChanged();
        _prevState = current;
    }

    if (current == State::CALIBRATING && _mag.isReady()) {
        int16_t mx = 0, my = 0, mz = 0;
        if (_mag.read(mx, my, mz)) _state.feedCalSample(mx, my, mz);
    }

    switch (current) {
        case State::IDLE:
        case State::DISCOVERING:
        case State::AWAITING_PAIR_ACCEPT:
        case State::PAIR_INCOMING:
        case State::CALIBRATING:
            return 500;
        case State::TRACKED:
            return tickTracked();
        case State::TRACKING:
        case State::SESSION_PAUSED:
            return tickTracking();
        case State::TRACKING_TREASURE:
            return 1000;
        case State::STATUS:
            return 1000;
    }
    return 1000;
}



int32_t CompassModule::tickTracked() {
    if (_state.session().peerNodeNum == 0) {
        // No paired peer — drop back to idle.
        return 1000;
    }
    if (_state.shouldSendUpdate(localPosition.latitude_i, localPosition.longitude_i)) {
        meshtastic_CompassPacket pkt = meshtastic_CompassPacket_init_default;
        pkt.type = meshtastic_CompassMsgType_POSITION_UPDATE;
        pkt.latitude_i = localPosition.latitude_i;
        pkt.longitude_i = localPosition.longitude_i;
        pkt.time = localPosition.time;
        pkt.battery_pct = powerStatus ? powerStatus->getBatteryChargePercent() : 0;
        sendPacket(_state.session().peerNodeNum, /*hop_limit=*/3, pkt);
        _state.onUpdateSent(localPosition.latitude_i, localPosition.longitude_i);
    }
    return static_cast<int32_t>(_state.updateIntervalSec()) * 1000;
}

int32_t CompassModule::tickTracking() {
    return 1000;
}
