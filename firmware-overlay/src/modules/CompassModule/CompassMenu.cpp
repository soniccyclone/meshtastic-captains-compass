// Captain's Compass — submenu implementation.
// See docs/tdd-issue-009-compass-submenu.md §3.

#include "CompassMenu.h"

#include "CompassModule.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "graphics/draw/MenuHandler.h"

using compass::CompassMenu;
using graphics::BannerOverlayOptions;

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
        Treasures,
        SaveTreasure,
        Calibrate,
        EndSession,
        Status,
        Settings,
        enumEnd,
    };

    static const char *labels[enumEnd] = {
        "Back", "Find Desire", "Treasures", "Save Treasure",
        "Calibrate", "End Session", "Status", "Settings",
    };
    static int enums[enumEnd] = {
        Back, FindDesire, Treasures, SaveTreasure,
        Calibrate, EndSession, Status, Settings,
    };

    BannerOverlayOptions opts;
    opts.message = "Compass";
    opts.optionsArrayPtr = labels;
    opts.optionsEnumPtr = enums;
    opts.optionsCount = enumEnd;
    opts.bannerCallback = [](int selected) -> void {
        switch (selected) {
            case FindDesire:
                LOG_INFO("Compass menu: Find Desire (stub — bd-9-2)");
                break;
            case Treasures:
                graphics::menuHandler::menuQueue = graphics::menuHandler::compass_treasure_picker;
                break;
            case SaveTreasure:
                LOG_INFO("Compass menu: Save Treasure (stub — bd-9-3)");
                break;
            case Calibrate:
                LOG_INFO("Compass menu: Calibrate (stub — bd-9-2)");
                break;
            case EndSession:
                LOG_INFO("Compass menu: End Session (stub — bd-9-2)");
                break;
            case Status:
                LOG_INFO("Compass menu: Status (stub — bd-9-5)");
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
    // bd-9-4 replaces this stub with a list dynamically built from
    // CompassModule::instance->state()->treasureCount(). For the
    // skeleton we just show a placeholder so QA can verify the dispatch
    // path lands here.
    static const char *labels[] = {"Back", "(populated in bd-9-4)"};
    static int enums[] = {0, 1};

    BannerOverlayOptions opts;
    opts.message = "Treasures";
    opts.optionsArrayPtr = labels;
    opts.optionsEnumPtr = enums;
    opts.optionsCount = 2;
    opts.bannerCallback = [](int /*selected*/) -> void {
        LOG_INFO("Compass menu: Treasures pick (stub — bd-9-4)");
    };
    if (screen) screen->showOverlayBanner(opts);
}

void CompassMenu::showPendingToast() {
    const char *msg = CompassMenu::pendingToast ? CompassMenu::pendingToast : "";
    CompassMenu::pendingToast = nullptr;
    if (screen) screen->showSimpleBanner(msg, 2000);
}
