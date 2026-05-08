// Captain's Compass — submenu (the screen pushed by the home banner-menu's
// `Compass` entry). See docs/tdd-issue-009-compass-submenu.md §3.
//
// The class is a function bag: all members are static. Lifetime is "forever";
// no instance, no allocation. Two reasons:
//   1. The banner-menu API (BannerOverlayOptions::bannerCallback) is a
//      std::function captured at showOverlayBanner() time, so callbacks must
//      either be capture-less lambdas or reference globals. Statics fit.
//   2. There is exactly one Compass submenu surface; modeling it as a class
//      with state would just be ceremony.
//
// Re-entrance: never call `screen->showOverlayBanner(...)` from inside another
// banner's callback — upstream's NotificationRenderer assumes the previous
// banner is closing when a callback fires. To open another submenu, set
// `graphics::menuHandler::menuQueue` and let the next frame's
// `handleMenuSwitch` dispatch the build function. This is the same pattern
// `systemBaseMenu`, `loraMenu`, etc. use upstream.

#pragma once

#include "configuration.h"

namespace compass {

class CompassMenu {
public:
    // Called from the home banner-menu's `Compass` callback. Queues the
    // root submenu for the next frame.
    static void open();

    // Dispatch entry points called from `menuHandler::handleMenuSwitch`
    // patched cases. See patches/apply.py::patch_screen_cpp.
    static void buildRoot();
    static void buildTreasures();
    static void showPendingToast();
    static void buildSavedDesires();
    static void buildDiscoveryResults();
    static void buildPairIncoming();

    // Set by callbacks before queueing `compass_toast`. Lives across one
    // frame (set in callback, read in dispatch case, never both).
    static const char *pendingToast;
};

} // namespace compass
