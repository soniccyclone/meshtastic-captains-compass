// Captain's Compass — UI rendering and input handling.
// See docs/tdd-issue-002-captains-compass.md §14.

#include "CompassUI.h"

#include "CompassMath.h"
#include "CompassModule.h"
#include "CompassState.h"
#include "Magnetometer.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "input/InputBroker.h"
#include "mesh/NodeDB.h"

#include <Arduino.h>
#include <OLEDDisplay.h>
#include <math.h>
#include <stdio.h>

using namespace compass;

namespace {
// Arrow geometry (centered at 0,0; tip points up at headingError = 0)
constexpr int16_t ARROW_TIP_X    = 0;
constexpr int16_t ARROW_TIP_Y    = -40;
constexpr int16_t ARROW_WING_X   = 20;   // ± from center
constexpr int16_t ARROW_WING_Y   = -10;
constexpr int16_t ARROW_TAIL_X   = 0;
constexpr int16_t ARROW_TAIL_Y   = 25;

constexpr uint32_t BLINK_PERIOD_MS = 500;

inline void rotate(int16_t &x, int16_t &y, float c, float s) {
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    x = static_cast<int16_t>(fx * c - fy * s);
    y = static_cast<int16_t>(fx * s + fy * c);
}
}  // namespace

bool CompassUI::wantUIFrame() const {
    if (!_owner) return false;
    switch (_owner->state()->getState()) {
        case State::DISCOVERING:
        case State::AWAITING_PAIR_ACCEPT:
        case State::PAIR_INCOMING:
        case State::TRACKING:
        case State::SESSION_PAUSED:
        case State::TRACKING_TREASURE:
        case State::CALIBRATING:
        case State::STATUS:
            return true;
        default:
            return false;
    }
}

bool CompassUI::interceptingKeyboardInput() const {
    return wantUIFrame();
}

void CompassUI::drawFrame(OLEDDisplay *display, OLEDDisplayUiState * /*state*/, int16_t x, int16_t y) {
    if (!_owner || !display) return;
    switch (_owner->state()->getState()) {
        case State::DISCOVERING:        drawDiscovering(display, x, y);        break;
        case State::AWAITING_PAIR_ACCEPT: drawAwaitingPairAccept(display, x, y); break;
        case State::PAIR_INCOMING:      drawPairIncoming(display, x, y);       break;
        case State::TRACKING:           drawTracking(display, x, y);           break;
        case State::SESSION_PAUSED:     drawSessionPaused(display, x, y);      break;
        case State::TRACKING_TREASURE:  drawTrackingTreasure(display, x, y);   break;
        case State::CALIBRATING:        drawCalibrating(display, x, y);        break;
        case State::STATUS:             drawStatus(display, x, y);             break;
        default:                                                               break;
    }
}

// --- Per-state renderers ---------------------------------------------

void CompassUI::drawDiscovering(OLEDDisplay *display, int16_t x, int16_t y) {
    auto *st = _owner->state();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(x + 4, y + 0, "Find Desire");
    const uint8_t count = st->discoveredNodeCount();
    if (count == 0) {
        display->drawString(x + 4, y + 24, st->isDiscoveryWindowOpen() ? "Searching..."
                                                                       : "No Compass nodes nearby");
        return;
    }
    if (_selectedDiscovered >= count) _selectedDiscovered = 0;
    for (uint8_t i = 0; i < count; ++i) {
        const auto &n = st->discoveredNode(i);
        char line[40];
        snprintf(line, sizeof(line), "%c %-4s  %d", (i == _selectedDiscovered) ? '>' : ' ',
                 n.shortName, static_cast<int>(n.rssi));
        display->drawString(x + 4, y + 18 + i * 14, line);
    }
}

void CompassUI::drawTracking(OLEDDisplay *display, int16_t x, int16_t y) {
    auto *st = _owner->state();
    auto *mg = _owner->mag();
    const auto &sess = st->session();
    const float heading = mg->isReady() ? mg->heading() : 0.0f;
    const float bearingDeg = CompassMath::bearing(localPosition.latitude_i, localPosition.longitude_i,
                                                  sess.peerLatI, sess.peerLonI);
    const float errDeg = CompassMath::headingError(heading, bearingDeg);

    const bool signalLost = st->isSignalLost();
    const bool blinkOff   = signalLost && (((millis() / BLINK_PERIOD_MS) & 1) == 0);

    if (!blinkOff) {
        const int16_t cx = x + 60;
        const int16_t cy = y + 60;
        drawArrow(display, cx, cy, errDeg, /*dim=*/!mg->isReady());
    }

    // Right column: name, distance, time, battery
    const int16_t rx = x + 130;
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(rx, y + 6, sess.peerName[0] ? sess.peerName : "(peer)");
    const float dist = CompassMath::distanceMeters(localPosition.latitude_i, localPosition.longitude_i,
                                                   sess.peerLatI, sess.peerLonI);
    char buf[32];
    if (dist >= 1000.0f) snprintf(buf, sizeof(buf), "%.1f km", dist / 1000.0f);
    else                 snprintf(buf, sizeof(buf), "%d m", static_cast<int>(dist));
    display->drawString(rx, y + 26, buf);

    if (signalLost) {
        display->drawString(rx, y + 46, "signal lost");
    } else if (sess.peerLastUpdateSec) {
        const uint32_t nowSec = millis() / 1000;
        const uint32_t ageSec = nowSec > sess.peerLastUpdateSec ? nowSec - sess.peerLastUpdateSec : 0;
        snprintf(buf, sizeof(buf), "%us ago", static_cast<unsigned>(ageSec));
        display->drawString(rx, y + 46, buf);
    }
    if (sess.peerBatteryPct) {
        snprintf(buf, sizeof(buf), "bat %u%%", static_cast<unsigned>(sess.peerBatteryPct));
        display->drawString(rx, y + 66, buf);
    }
}

void CompassUI::drawTrackingTreasure(OLEDDisplay *display, int16_t x, int16_t y) {
    auto *st = _owner->state();
    auto *mg = _owner->mag();
    const auto &t = st->activeTreasure();
    const float dist = CompassMath::distanceMeters(localPosition.latitude_i, localPosition.longitude_i,
                                                   t.latI, t.lonI);
    if (dist < CompassState::ARRIVAL_THRESHOLD_M) {
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + 120, y + 50, "YOU'RE HERE");
        return;
    }
    const float heading = mg->isReady() ? mg->heading() : 0.0f;
    const float bearingDeg = CompassMath::bearing(localPosition.latitude_i, localPosition.longitude_i,
                                                  t.latI, t.lonI);
    drawArrow(display, x + 60, y + 60, CompassMath::headingError(heading, bearingDeg),
              /*dim=*/!mg->isReady());
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(x + 130, y + 6, t.label);
    char buf[32];
    if (dist >= 1000.0f) snprintf(buf, sizeof(buf), "%.1f km", dist / 1000.0f);
    else                 snprintf(buf, sizeof(buf), "%d m", static_cast<int>(dist));
    display->drawString(x + 130, y + 26, buf);
}

void CompassUI::drawPairIncoming(OLEDDisplay *display, int16_t x, int16_t y) {
    auto *st = _owner->state();
    const meshtastic_NodeInfoLite *info = nodeDB ? nodeDB->getMeshNode(st->pendingPairNodeNum())
                                                  : nullptr;
    const char *name = (info && info->has_user) ? info->user.long_name : "A node";
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(x + 4, y + 4, "Pair Request");
    char buf[64];
    snprintf(buf, sizeof(buf), "%s wants to track you.", name);
    display->drawStringMaxWidth(x + 4, y + 28, 230, buf);
    const char *acceptStr = (_pairIncomingSelection == 0) ? "[ACCEPT]" : " ACCEPT ";
    const char *rejectStr = (_pairIncomingSelection == 1) ? "[REJECT]" : " REJECT ";
    display->drawString(x + 20,  y + 100, acceptStr);
    display->drawString(x + 140, y + 100, rejectStr);
}

void CompassUI::drawAwaitingPairAccept(OLEDDisplay *display, int16_t x, int16_t y) {
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(x + 4, y + 4, "Pair Request Sent");
    display->drawString(x + 4, y + 50, "Waiting for accept...");
}

void CompassUI::drawCalibrating(OLEDDisplay *display, int16_t x, int16_t y) {
    auto *st = _owner->state();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(x + 4, y + 4, "Calibrating");
    display->drawStringMaxWidth(x + 4, y + 24, 230, "Rotate device slowly in all directions.");
    char buf[32];
    snprintf(buf, sizeof(buf), "%u samples", static_cast<unsigned>(st->calSampleCount()));
    display->drawString(x + 4, y + 80, buf);
    display->drawString(x + 4, y + 110, "SELECT to finish");
}

void CompassUI::drawSessionPaused(OLEDDisplay *display, int16_t x, int16_t y) {
    auto *st = _owner->state();
    const auto &sess = st->session();
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(x + 4, y + 4, "Session paused");
    char buf[64];
    snprintf(buf, sizeof(buf), "%s unreachable", sess.peerName[0] ? sess.peerName : "peer");
    display->drawString(x + 4, y + 50, buf);
}

void CompassUI::drawStatus(OLEDDisplay *display, int16_t x, int16_t y) {
    auto *st = _owner->state();
    auto *mg = _owner->mag();
    const auto &sess = st->session();

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(x + 4, y + 0,  "Captain's Compass");
    display->drawString(x + 4, y + 16, mg->isReady() ? "mag: OK" : "mag: not found");

    char buf[40];
    if (sess.peerNodeNum != 0) {
        snprintf(buf, sizeof(buf), "session: %s", sess.peerName[0] ? sess.peerName : "(peer)");
    } else {
        snprintf(buf, sizeof(buf), "session: (none)");
    }
    display->drawString(x + 4, y + 32, buf);
    snprintf(buf, sizeof(buf), "treasures: %u", static_cast<unsigned>(st->treasureCount()));
    display->drawString(x + 4, y + 48, buf);
    display->drawString(x + 4, y + 64, "port: 300");
    display->drawString(x + 4, y + 96, "(any input dismisses)");
}

// --- Arrow ------------------------------------------------------------

void CompassUI::drawArrow(OLEDDisplay *display, int16_t cx, int16_t cy, float headingErrorDeg, bool dim) {
    const float rad = headingErrorDeg * 3.14159265358979323846f / 180.0f;
    const float c = cosf(rad);
    const float s = sinf(rad);

    int16_t tx = ARROW_TIP_X,  ty = ARROW_TIP_Y;
    int16_t lx = -ARROW_WING_X, ly = ARROW_WING_Y;
    int16_t rx = ARROW_WING_X,  ry = ARROW_WING_Y;
    int16_t bx = ARROW_TAIL_X,  by = ARROW_TAIL_Y;
    rotate(tx, ty, c, s);
    rotate(lx, ly, c, s);
    rotate(rx, ry, c, s);
    rotate(bx, by, c, s);

    if (dim) {
        // No filled triangle in dim mode — outline only.
        display->drawLine(cx + tx, cy + ty, cx + lx, cy + ly);
        display->drawLine(cx + lx, cy + ly, cx + rx, cy + ry);
        display->drawLine(cx + rx, cy + ry, cx + tx, cy + ty);
    } else {
        display->fillTriangle(cx + tx, cy + ty, cx + lx, cy + ly, cx + rx, cy + ry);
        display->drawLine(cx, cy, cx + bx, cy + by);
    }
}

// --- Input handling ---------------------------------------------------

int CompassUI::handleInputEvent(const InputEvent *e) {
    if (!_owner || !e) return 0;
    auto *st = _owner->state();

    switch (st->getState()) {
        case State::DISCOVERING: {
            const uint8_t count = st->discoveredNodeCount();
            if (count == 0) {
                if (e->inputEvent == INPUT_BROKER_SELECT_LONG) {
                    st->endSession();
                    _owner->notifyUIChanged();
                }
                return 0;
            }
            if (e->inputEvent == INPUT_BROKER_UP) {
                _selectedDiscovered = (_selectedDiscovered == 0) ? count - 1 : _selectedDiscovered - 1;
            } else if (e->inputEvent == INPUT_BROKER_DOWN) {
                _selectedDiscovered = (_selectedDiscovered + 1) % count;
            } else if (e->inputEvent == INPUT_BROKER_SELECT) {
                _owner->sendPairRequest(st->discoveredNode(_selectedDiscovered).nodeNum);
            } else if (e->inputEvent == INPUT_BROKER_SELECT_LONG ||
                       e->inputEvent == INPUT_BROKER_BACK) {
                st->endSession();
                _owner->notifyUIChanged();
            }
            return 1;
        }
        case State::PAIR_INCOMING: {
            if (e->inputEvent == INPUT_BROKER_LEFT || e->inputEvent == INPUT_BROKER_UP) {
                _pairIncomingSelection = 0;
            } else if (e->inputEvent == INPUT_BROKER_RIGHT || e->inputEvent == INPUT_BROKER_DOWN) {
                _pairIncomingSelection = 1;
            } else if (e->inputEvent == INPUT_BROKER_SELECT) {
                if (_pairIncomingSelection == 0) {
                    const uint32_t initiator = st->pendingPairNodeNum();
                    st->acceptPair();
                    _owner->sendPairAccept(initiator);
                } else {
                    const uint32_t initiator = st->pendingPairNodeNum();
                    st->rejectPair();
                    _owner->sendPairReject(initiator);
                }
                _owner->notifyUIChanged();
            }
            return 1;
        }
        case State::CALIBRATING: {
            if (e->inputEvent == INPUT_BROKER_SELECT) {
                st->finishCalibration();
                _owner->notifyUIChanged();
            } else if (e->inputEvent == INPUT_BROKER_SELECT_LONG ||
                       e->inputEvent == INPUT_BROKER_BACK) {
                st->endSession();
                _owner->notifyUIChanged();
            }
            return 1;
        }
        case State::TRACKING:
        case State::TRACKING_TREASURE:
        case State::SESSION_PAUSED: {
            if (e->inputEvent == INPUT_BROKER_SELECT_LONG ||
                e->inputEvent == INPUT_BROKER_BACK) {
                // End session and refresh frame list so screen rotation
                // returns to its normal behavior. The "minimize but keep
                // tracking" UX is a v2 follow-up (bead -dyg).
                st->endSession();
                _owner->notifyUIChanged();
                return 1;
            }
            return 0;
        }
        case State::STATUS: {
            // Any input dismisses the read-only status screen back to home.
            // endSession() with no active session is just a setState(IDLE).
            if (e->inputEvent != 0) {
                st->endSession();
                _owner->notifyUIChanged();
                return 1;
            }
            return 0;
        }
        default:
            return 0;
    }
}
