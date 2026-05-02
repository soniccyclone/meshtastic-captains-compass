#!/usr/bin/env python3
"""Apply Captain's Compass patches to a pristine meshtastic/firmware checkout.

Run from the firmware source root (cwd). Idempotent — re-running on an
already-patched tree is a no-op. Each patch fails loudly if its upstream
anchor has moved; that is the signal to update the anchor in this file.

Usage:
  python3 apply.py             # apply all patches
  python3 apply.py --dry-run   # check anchors only, exit 0 if all match

See docs/tdd-issue-002-captains-compass.md §6 for the full patch contract.
"""
import sys
from pathlib import Path

MARKER = "captains-compass:"


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _read(path):
    """Read a file, exiting cleanly if it's missing (helps debug wrong cwd)."""
    p = Path(path)
    if not p.exists():
        sys.exit(f"ERROR: {path} not found. Run from the firmware source root.")
    return p.read_text()


def _apply(path, anchor, replacement, dry_run, *, position="after"):
    """Idempotent anchor-substitution patch.

    - If MARKER is already in the file: skip.
    - If anchor is missing: sys.exit with a (file, anchor) message — this is
      the upstream-drift detector.
    - Otherwise: in dry-run, print OK; in apply, insert replacement
      at the given position relative to the anchor and write the file.

    `position` is "after" (default) or "before"; the anchor itself is
    preserved. Use "replace" if the anchor itself is replaced.
    """
    text = _read(path)
    if MARKER in text:
        print(f"  Skipped {path}: already patched")
        return
    if anchor not in text:
        sys.exit(f"ERROR: anchor missing in {path}: {anchor!r}")
    if dry_run:
        print(f"  OK {path}: anchor present")
        return
    if position == "after":
        new_text = text.replace(anchor, anchor + replacement, 1)
    elif position == "before":
        new_text = text.replace(anchor, replacement + anchor, 1)
    elif position == "replace":
        new_text = text.replace(anchor, replacement, 1)
    else:
        sys.exit(f"ERROR: bad position {position!r}")
    Path(path).write_text(new_text)
    print(f"  Patched {path}")


def _todo(bead, name):
    """Placeholder for a not-yet-implemented patch.

    Prints a TODO line and returns. This makes BEAD-3 (framework) succeed
    in both dry-run and apply modes while signalling which beads still
    need to fill in their anchor + replacement. BEAD-5, BEAD-6, BEAD-12
    each replace one of these calls with a real `_apply(...)` invocation.
    """
    print(f"  TODO {bead}: {name} not yet implemented (BEAD-3 placeholder)")


# ---------------------------------------------------------------------------
# Per-file patches
# ---------------------------------------------------------------------------

def patch_variant_h(dry_run=False):
    """No-op: T114 variant.h is already complete for QMC5883L use.

    BEAD-5 finding: upstream variants/nrf52840/heltec_mesh_node_t114/variant.h
    already defines WIRE_INTERFACES_COUNT=2, PIN_WIRE1_SDA=P0.16, and
    PIN_WIRE1_SCL=P0.13 (lines 99, 109-110). The Magnetometer driver
    uses Wire1 directly; no MAG_SDA/MAG_SCL aliases are needed.
    """
    print("  Skipped variant.h: upstream already defines PIN_WIRE1_SDA/SCL")


def patch_variant_cpp(dry_run=False):
    """No-op: Adafruit nRF52 BSP auto-creates Wire1 from PIN_WIRE1_* defines.

    BEAD-5 finding: no nRF52 variant in upstream explicitly instantiates
    Wire1 in variant.cpp despite multiple variants having
    WIRE_INTERFACES_COUNT=2 (heltec_mesh_node_t114, t096, t114-inkhud).
    The framework (FrameworkArduino/Wire.cpp) constructs Wire1 from the
    PIN_WIRE1_* macros at static initialization time.
    """
    print("  Skipped variant.cpp: BSP auto-creates Wire1")


def patch_portnums_proto(dry_run=False):
    """Add COMPASS_APP = 300 to meshtastic_PortNum enum.

    Anchor: 'MAX = 511;' — the enum's max marker, last entry before the
    closing brace. Stable: this line is the conventional end-of-enum
    marker that upstream maintains for range-checking, unlikely to move.
    """
    _apply(
        "protobufs/meshtastic/portnums.proto",
        anchor="MAX = 511;",
        replacement=(
            "/*\n"
            "   * Captain's Compass — direction-finding module.\n"
            "   * See https://github.com/soniccyclone/meshtastic-captains-compass\n"
            f"   * {MARKER} private-app range; not registered upstream.\n"
            "   */\n"
            "  COMPASS_APP = 300;\n\n"
            "  /*\n"
            "   * Currently we limit port nums to no higher than this value\n"
            "   */\n"
            "  MAX = 511;"
        ),
        dry_run=dry_run,
        position="replace",
    )


def patch_modules_cpp(dry_run=False):
    """Add CompassModule include + new CompassModule() in setupModules()."""
    path = "src/modules/Modules.cpp"
    text = _read(path)
    if MARKER in text:
        print(f"  Skipped {path}: already patched"); return

    include_anchor = '#include "modules/RoutingModule.h"'
    setup_anchor = "void setupModules()\n{"
    if include_anchor not in text:
        sys.exit(f"ERROR: anchor missing in {path}: {include_anchor!r}")
    if setup_anchor not in text:
        sys.exit(f"ERROR: anchor missing in {path}: {setup_anchor!r}")
    if dry_run:
        print(f"  OK {path}: anchors present"); return

    text = text.replace(
        include_anchor,
        include_anchor + f'\n// {MARKER}\n#include "modules/CompassModule/CompassModule.h"',
        1,
    )
    # Insert the instantiation as the first statement in setupModules() so
    # CompassModule::instance is set before any other module that might use it.
    text = text.replace(
        setup_anchor,
        setup_anchor + f"\n    new CompassModule();  // {MARKER}",
        1,
    )
    open(path, "w").write(text)
    print(f"  Patched {path}")


def patch_screen_cpp(dry_run=False):
    """Add 'Compass' entry to the home banner menu in MenuHandler.cpp.

    Three coordinated substitutions in src/graphics/draw/MenuHandler.cpp:
      1. enum optionsNumbers — add Compass before enumEnd
      2. options array — append Compass entry after the Position branch
      3. bannerCallback lambda — add 'else if (selected == Compass)' case
    Plus an #include for CompassModule.h.

    All four are idempotency-checked via the MARKER scan at function entry;
    once patched, every subsequent call is a no-op.
    """
    path = "src/graphics/draw/MenuHandler.cpp"
    text = _read(path)
    if MARKER in text:
        print(f"  Skipped {path}: already patched"); return

    enum_anchor   = "enum optionsNumbers { Back, Mute, Backlight, Position, Preset, Freetext, Sleep, enumEnd };"
    array_anchor  = "    optionsEnumArray[options++] = Position;"
    cb_anchor     = ("        } else if (selected == Freetext) {\n"
                     "            cannedMessageModule->LaunchFreetextWithDestination(NODENUM_BROADCAST);\n"
                     "        }")
    include_anchor = '#include "MenuHandler.h"'

    for a in (enum_anchor, array_anchor, cb_anchor, include_anchor):
        if a not in text:
            sys.exit(f"ERROR: anchor missing in {path}: {a!r}")
    if dry_run:
        print(f"  OK {path}: anchors present"); return

    # 1. enum: insert Compass before enumEnd
    enum_replacement = enum_anchor.replace(
        "Sleep, enumEnd",
        f"Sleep, Compass, enumEnd  /* {MARKER} */",
    )
    text = text.replace(enum_anchor, enum_replacement, 1)

    # 2. options array: append after Position branch
    array_replacement = (
        array_anchor + "\n"
        f"    // {MARKER}\n"
        '    optionsArray[options] = "Compass";\n'
        "    optionsEnumArray[options++] = Compass;"
    )
    text = text.replace(array_anchor, array_replacement, 1)

    # 3. bannerCallback lambda: add Compass case after Freetext
    cb_replacement = (
        cb_anchor + " "
        f"/* {MARKER} */ else if (selected == Compass) {{\n"
        "            if (CompassModule::instance) {\n"
        "                CompassModule::instance->sendCapabilityQuery();\n"
        "            }\n"
        "        }"
    )
    text = text.replace(cb_anchor, cb_replacement, 1)

    # 4. include
    include_replacement = (
        include_anchor + f'\n// {MARKER}\n'
        '#include "modules/CompassModule/CompassModule.h"'
    )
    text = text.replace(include_anchor, include_replacement, 1)

    open(path, "w").write(text)
    print(f"  Patched {path}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

PATCHES = [
    patch_variant_h,
    patch_variant_cpp,
    patch_portnums_proto,
    patch_modules_cpp,
    patch_screen_cpp,
]


def main(argv):
    dry_run = "--dry-run" in argv
    print(f"Captain's Compass patcher ({'dry-run' if dry_run else 'apply'})")
    for fn in PATCHES:
        fn(dry_run=dry_run)
    print("Done.")


if __name__ == "__main__":
    main(sys.argv[1:])
