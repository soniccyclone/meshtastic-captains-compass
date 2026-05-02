// Captain's Compass — central coordinator.
// See docs/tdd-issue-002-captains-compass.md §13.

#pragma once

#include "configuration.h"

#include "CompassState.h"
#include "CompassUI.h"
#include "Magnetometer.h"
#include "Observer.h"
#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "graphics/ScreenFonts.h"
#include "mesh/generated/meshtastic/compass.pb.h"

class OLEDDisplay;
class OLEDDisplayUiState;
struct UIFrameEvent;
struct _InputEvent;
typedef struct _InputEvent InputEvent;

class CompassModule : public SinglePortModule,
                      public Observable<const UIFrameEvent *>,
                      private concurrency::OSThread {
public:
    CompassModule();

    // SinglePortModule
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    // MeshModule UI overrides
    bool wantUIFrame() override;
    bool interceptingKeyboardInput() override;
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
    int  handleInputEvent(const InputEvent *e);  // wired via inputObserver

    // CompassUI calls these
    void sendCapabilityQuery();
    void sendPairRequest(uint32_t targetNodeNum);
    void sendPairAccept(uint32_t toNodeNum);
    void sendPairReject(uint32_t toNodeNum);
    void sendPairConfirm(uint32_t toNodeNum);
    void sendSessionEnd();

    compass::CompassState *state() { return &_state; }
    Magnetometer          *mag()   { return &_mag; }

    // Tell the Screen subsystem to re-poll wantUIFrame() on every module
    // and rebuild its frame list. Must be called whenever we transition into
    // or out of a state where wantUIFrame() flips. Without this call, the
    // screen continues to show the previous frame rotation regardless of
    // our internal state. (See WaypointModule / CannedMessageModule for
    // upstream's usage pattern.)
    void notifyUIChanged();

    static CompassModule *instance;

protected:
    int32_t runOnce() override;

private:
    compass::CompassState _state;
    compass::State        _prevState = compass::State::IDLE;  // tick-based UI-refresh backstop
    Magnetometer          _mag;
    CompassUI             _ui{this};
    CallbackObserver<CompassModule, const InputEvent *> _inputObserver{
        this, &CompassModule::handleInputEvent};

    // Send helpers
    void sendPacket(uint32_t to, uint8_t hopLimit, const meshtastic_CompassPacket &pkt);
    void sendCapabilityAdv(uint32_t toNodeNum);

    // Per-message handlers
    void handleCapabilityQuery(const meshtastic_MeshPacket &mp);
    void handleCapabilityAdv(const meshtastic_MeshPacket &mp, const meshtastic_CompassPacket &pkt);
    void handlePairRequest(const meshtastic_MeshPacket &mp);
    void handlePairAccept(const meshtastic_MeshPacket &mp);
    void handlePairConfirm(const meshtastic_MeshPacket &mp);
    void handlePairReject(const meshtastic_MeshPacket &mp);
    void handlePositionUpdate(const meshtastic_MeshPacket &mp, const meshtastic_CompassPacket &pkt);
    void handleSessionEnd(const meshtastic_MeshPacket &mp);

    // Tick helpers
    int32_t tickTracked();
    int32_t tickTracking();
};
