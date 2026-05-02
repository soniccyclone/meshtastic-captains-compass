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
    """Inject the QMC5883L pin defines into the T114 variant.h.

    Implemented by BEAD-5. See TDD §8.
    """
    _todo("BEAD-5", "patch_variant_h")


def patch_variant_cpp(dry_run=False):
    """Add explicit Wire1 instantiation if BSP doesn't auto-create it.

    Conditional — implementer must first verify whether the Adafruit nRF52
    BSP auto-creates Wire1 from PIN_WIRE1_* defines (compare to other
    nRF52 variants with WIRE_INTERFACES_COUNT=2). If not, fill in.
    Implemented by BEAD-5. See TDD §8.
    """
    _todo("BEAD-5", "patch_variant_cpp")


def patch_portnums_proto(dry_run=False):
    """Add COMPASS_APP = 300 to meshtastic_PortNum enum.

    Implemented by BEAD-6. See TDD §9.
    """
    _todo("BEAD-6", "patch_portnums_proto")


def patch_modules_cpp(dry_run=False):
    """Add CompassModule include + new CompassModule() in setupModules().

    Implemented by BEAD-12. See TDD §15.
    """
    _todo("BEAD-12", "patch_modules_cpp")


def patch_screen_cpp(dry_run=False):
    """Add 'Compass' menu entry triggering startDiscovery + sendCapabilityQuery.

    Implemented by BEAD-12. See TDD §15.
    """
    _todo("BEAD-12", "patch_screen_cpp")


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
