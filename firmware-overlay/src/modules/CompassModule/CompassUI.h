// Captain's Compass — UI rendering and input handling.
// See docs/tdd-issue-002-captains-compass.md §14.
//
// Pure view + input class. Holds a back-pointer to CompassModule so it can
// trigger packet sends in response to UI actions; otherwise stateless.

#pragma once

#include "configuration.h"

#include <stdint.h>

class OLEDDisplay;
class OLEDDisplayUiState;
class CompassModule;
struct _InputEvent;
typedef struct _InputEvent InputEvent;

class CompassUI {
public:
    explicit CompassUI(CompassModule *owner) : _owner(owner) {}

    bool wantUIFrame() const;
    bool interceptingKeyboardInput() const;
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
    int  handleInputEvent(const InputEvent *e);

private:
    CompassModule *_owner;

    // Currently selected node in DISCOVERING list (0..count-1)
    uint8_t _selectedDiscovered = 0;
    // Currently selected option in PAIR_INCOMING (0=ACCEPT, 1=REJECT)
    uint8_t _pairIncomingSelection = 0;

    void drawDiscovering(OLEDDisplay *display, int16_t x, int16_t y);
    void drawTracking(OLEDDisplay *display, int16_t x, int16_t y);
    void drawTrackingTreasure(OLEDDisplay *display, int16_t x, int16_t y);
    void drawPairIncoming(OLEDDisplay *display, int16_t x, int16_t y);
    void drawAwaitingPairAccept(OLEDDisplay *display, int16_t x, int16_t y);
    void drawCalibrating(OLEDDisplay *display, int16_t x, int16_t y);
    void drawSessionPaused(OLEDDisplay *display, int16_t x, int16_t y);

    // Arrow rendering: center is the (x,y) origin passed in;
    // headingErrorDeg is signed (positive = clockwise).
    void drawArrow(OLEDDisplay *display, int16_t cx, int16_t cy, float headingErrorDeg, bool dim);
};
