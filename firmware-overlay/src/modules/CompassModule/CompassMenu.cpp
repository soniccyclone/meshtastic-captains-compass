// Captain's Compass — submenu implementation.
// See docs/tdd-issue-009-compass-submenu.md §3.

#include "CompassMenu.h"

#include "CompassModule.h"
#include "CompassState.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "graphics/draw/MenuHandler.h"
#include "mesh/NodeDB.h"   // localPosition

#include <stdio.h>

namespace compass {

const char *CompassMenu::pendingToast = nullptr;

void CompassMenu::open() {
    graphics::menuHandler::menuQueue = graphics::menuHandler::compass_menu;
}

void CompassMenu::buildRoot() {
    // Order and labels are the source of truth for PRD §5. If you change
    // either here, also update prd-issue-009-compass-submenu.md §5.
    enum opt {
        Back,
        FindDesire,
        PairNewDevice,
        Treasures,
        SaveTreasure,
        Calibrate,
        EndSession,
        Status,
        Settings,
        enumEnd,
    };

    static const char *labels[enumEnd] = {
        "Back", "Find Desire", "Pair New Device", "Treasures", "Save Treasure",
        "Calibrate", "End Session", "Status", "Settings",
    };
    static int enums[enumEnd] = {
        Back, FindDesire, PairNewDevice, Treasures, SaveTreasure,
        Calibrate, EndSession, Status, Settings,
    };

    graphics::BannerOverlayOptions opts;
    opts.message = "Compass";
    opts.optionsArrayPtr = labels;
    opts.optionsEnumPtr = enums;
    opts.optionsCount = enumEnd;
    opts.bannerCallback = [](int selected) -> void {
        switch (selected) {
            case FindDesire:
                graphics::menuHandler::menuQueue = graphics::menuHandler::compass_saved_desires;
                break;
            case PairNewDevice:
                if (CompassModule::instance) CompassModule::instance->sendCapabilityQuery();
                break;
            case Treasures:
                graphics::menuHandler::menuQueue = graphics::menuHandler::compass_treasure_picker;
                break;
            case SaveTreasure: {
                // Static buffer for the success-toast message; pointer outlives the
                // callback because the toast is shown on the next frame.
                static char savedMsg[24];
                if (!CompassModule::instance) {
                    CompassMenu::pendingToast = "Module not ready";
                } else if (localPosition.latitude_i == 0 && localPosition.longitude_i == 0) {
                    CompassMenu::pendingToast = "No GPS fix";
                } else {
                    char label[13];
                    const uint32_t now = localPosition.time;
                    snprintf(label, sizeof(label), "T-%u", static_cast<unsigned>(now % 10000));
                    const bool saved = CompassModule::instance->state()->saveTreasure(
                        label, localPosition.latitude_i, localPosition.longitude_i, now);
                    if (saved) {
                        snprintf(savedMsg, sizeof(savedMsg), "%s saved", label);
                        CompassMenu::pendingToast = savedMsg;
                    } else {
                        CompassMenu::pendingToast = "Treasures full";
                    }
                }
                graphics::menuHandler::menuQueue = graphics::menuHandler::compass_toast;
                break;
            }
            case Calibrate:
                if (CompassModule::instance) {
                    CompassModule::instance->state()->startCalibration();
                    CompassModule::instance->notifyUIChanged();
                }
                break;
            case EndSession:
                if (CompassModule::instance &&
                    CompassModule::instance->state()->session().peerNodeNum != 0) {
                    CompassModule::instance->sendSessionEnd();
                } else {
                    CompassMenu::pendingToast = "No active session";
                    graphics::menuHandler::menuQueue = graphics::menuHandler::compass_toast;
                }
                break;
            case Status:
                if (CompassModule::instance) {
                    CompassModule::instance->state()->startStatus();
                    CompassModule::instance->notifyUIChanged();
                }
                break;
            case Settings:
                CompassMenu::pendingToast = "Coming soon";
                graphics::menuHandler::menuQueue = graphics::menuHandler::compass_toast;
                break;
            case Back:
            default:
                break;
        }
    };
    if (screen) screen->showOverlayBanner(opts);
}

void CompassMenu::buildTreasures() {
    if (!CompassModule::instance) return;
    CompassState *st = CompassModule::instance->state();
    const uint8_t count = st->treasureCount();

    // Static so pointers / enums outlive the function call. Slot 0 is Back;
    // slots 1..count point at the live Treasure::label members in
    // CompassState (which lives forever, so the pointers stay valid).
    static const char *labels[CompassState::MAX_TREASURES + 1];
    static int enums[CompassState::MAX_TREASURES + 1];

    labels[0] = "Back";
    enums[0]  = 0;

    uint8_t entryCount;
    if (count == 0) {
        labels[1] = "(no treasures saved)";
        enums[1]  = -1;   // "empty-state" sentinel; ignored in the callback
        entryCount = 2;
    } else {
        for (uint8_t i = 0; i < count; ++i) {
            labels[i + 1] = st->treasure(i).label;
            // Carry the treasure index in the enum value (offset by 1 so 0 = Back).
            enums[i + 1] = static_cast<int>(i) + 1;
        }
        entryCount = static_cast<uint8_t>(count + 1);
    }

    graphics::BannerOverlayOptions opts;
    opts.message = "Treasures";
    opts.optionsArrayPtr = labels;
    opts.optionsEnumPtr = enums;
    opts.optionsCount = entryCount;
    opts.bannerCallback = [](int selected) -> void {
        if (selected <= 0) return;   // Back or empty-state sentinel
        if (!CompassModule::instance) return;
        const uint8_t idx = static_cast<uint8_t>(selected - 1);
        if (idx >= CompassModule::instance->state()->treasureCount()) return;
        CompassModule::instance->state()->startTreasureNav(idx);
        CompassModule::instance->notifyUIChanged();
    };
    if (screen) screen->showOverlayBanner(opts);
}

void CompassMenu::showPendingToast() {
    const char *msg = CompassMenu::pendingToast ? CompassMenu::pendingToast : "";
    CompassMenu::pendingToast = nullptr;
    if (screen) screen->showSimpleBanner(msg, 2000);
}

void CompassMenu::buildSavedDesires() {
    if (!CompassModule::instance) return;
    CompassState *st = CompassModule::instance->state();
    const uint8_t count = st->savedDesireCount();

    static const char *labels[CompassState::MAX_SAVED_DESIRES + 1];
    static int enums[CompassState::MAX_SAVED_DESIRES + 1];
    labels[0] = "Back"; enums[0] = 0;

    uint8_t entryCount;
    if (count == 0) {
        labels[1] = "(no saved desires — use Pair New Device)";
        enums[1] = -1;
        entryCount = 2;
    } else {
        for (uint8_t i = 0; i < count; ++i) {
            labels[i+1] = st->savedDesire(i).name;
            enums[i+1] = static_cast<int>(i) + 1;
        }
        entryCount = static_cast<uint8_t>(count + 1);
    }

    graphics::BannerOverlayOptions opts;
    opts.message = "Find Desire";
    opts.optionsArrayPtr = labels;
    opts.optionsEnumPtr = enums;
    opts.optionsCount = entryCount;
    opts.bannerCallback = [](int selected) -> void {
        if (!CompassModule::instance) return;
        if (selected <= 0) return;
        const uint8_t idx = static_cast<uint8_t>(selected - 1);
        CompassState *st = CompassModule::instance->state();
        if (idx >= st->savedDesireCount()) return;
        const SavedDesire &desire = st->savedDesire(idx);
        static char waitMsg[24];
        snprintf(waitMsg, sizeof(waitMsg), "Finding %s...",
                 desire.name[0] ? desire.name : "node");
        CompassModule::instance->sendPairRequest(desire.nodeNum);
        CompassMenu::pendingToast = waitMsg;
        graphics::menuHandler::menuQueue = graphics::menuHandler::compass_toast;
    };
    if (screen) screen->showOverlayBanner(opts);
}

void CompassMenu::buildDiscoveryResults() {
    if (!CompassModule::instance) return;
    CompassState *st = CompassModule::instance->state();
    const uint8_t count = st->discoveredNodeCount();
    static const char *labels[CompassState::MAX_DISCOVERED_NODES + 1];
    static int enums[CompassState::MAX_DISCOVERED_NODES + 1];
    labels[0] = "Back"; enums[0] = 0;
    uint8_t entryCount;
    if (count == 0) {
        labels[1] = "(no nodes found)"; enums[1] = -1; entryCount = 2;
    } else {
        for (uint8_t i = 0; i < count; ++i) {
            labels[i+1] = st->discoveredNode(i).shortName;
            enums[i+1] = static_cast<int>(i) + 1;
        }
        entryCount = static_cast<uint8_t>(count + 1);
    }
    graphics::BannerOverlayOptions opts;
    opts.message = "Pick Desire";
    opts.optionsArrayPtr = labels;
    opts.optionsEnumPtr = enums;
    opts.optionsCount = entryCount;
    opts.bannerCallback = [](int selected) -> void {
        if (!CompassModule::instance) return;
        if (selected <= 0) { CompassModule::instance->state()->endSession(); return; }
        const uint8_t idx = static_cast<uint8_t>(selected - 1);
        CompassState *st = CompassModule::instance->state();
        if (idx >= st->discoveredNodeCount()) return;
        static char waitMsg[24];
        const char *sn = st->discoveredNode(idx).shortName;
        snprintf(waitMsg, sizeof(waitMsg), "Waiting for %s...", sn[0] ? sn : "node");
        CompassModule::instance->sendPairRequest(st->discoveredNode(idx).nodeNum);
        CompassMenu::pendingToast = waitMsg;
        graphics::menuHandler::menuQueue = graphics::menuHandler::compass_toast;
    };
    if (screen) screen->showOverlayBanner(opts);
}

void CompassMenu::buildPairIncoming() {
    if (!CompassModule::instance) return;
    CompassState *st = CompassModule::instance->state();
    if (st->getState() != compass::State::PAIR_INCOMING) return;
    const meshtastic_NodeInfoLite *info =
        nodeDB ? nodeDB->getMeshNode(st->pendingPairNodeNum()) : nullptr;
    const char *name = (info && info->has_user) ? info->user.long_name : "??";
    static char bannerTitle[32];
    snprintf(bannerTitle, sizeof(bannerTitle), "Pair: %.20s", name);
    static const char *labels[] = {"Accept", "Reject"};
    static int enums[] = {1, 2};
    static uint32_t capturedNodeNum;
    static char capturedName[32];
    capturedNodeNum = st->pendingPairNodeNum();
    snprintf(capturedName, sizeof(capturedName), "%.31s", name);
    graphics::BannerOverlayOptions opts;
    opts.message = bannerTitle;
    opts.optionsArrayPtr = labels;
    opts.optionsEnumPtr = enums;
    opts.optionsCount = 2;
    opts.bannerCallback = [](int selected) -> void {
        if (!CompassModule::instance) return;
        CompassState *st = CompassModule::instance->state();
        if (st->getState() != compass::State::PAIR_INCOMING) return;
        if (selected == 1) {
            st->acceptPair();
            st->saveDesire(capturedNodeNum, capturedName);
            CompassModule::instance->sendPairAccept(capturedNodeNum);
            CompassMenu::pendingToast = "Accepted. Being tracked.";
        } else {
            st->rejectPair();
            CompassModule::instance->sendPairReject(capturedNodeNum);
            CompassMenu::pendingToast = "Request rejected.";
        }
        graphics::menuHandler::menuQueue = graphics::menuHandler::compass_toast;
    };
    if (screen) screen->showOverlayBanner(opts);
}

} // namespace compass
