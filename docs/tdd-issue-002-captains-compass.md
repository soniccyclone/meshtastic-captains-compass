# Captain's Compass — Technical Design Document
**Issue:** #2
**Version:** 0.2
**PRD:** [prd-issue-002-captains-compass.md](prd-issue-002-captains-compass.md)
**Target:** Meshtastic firmware pinned at the SHA in `meshtastic-firmware-pin`, variant `heltec_mesh_node_t114`.

---

## 0. v0.2 Changelog

v0.1 of this doc described files at upstream firmware paths (`src/modules/CompassModule/...`, `variants/nrf52840/heltec_mesh_node_t114/...`, `protobufs/meshtastic/...`) without specifying how this repo physically relates to the upstream firmware tree. An agent implementing v0.1 sensibly inferred that the work happens *inside* the firmware checkout, cloned `meshtastic/firmware` somewhere, edited it in place, and reported success — at which point the work evaporated when the clone was discarded. Eight beads closed; zero source files in this repo.

v0.2 reframes the project as a **patch overlay against a pinned upstream SHA**, with a containerized build pipeline that turns this repo plus a pinned firmware checkout into a flashable UF2. The functional design (the protocol, the FSM, the math, the UI) is unchanged from v0.1 and lives in sections 8–14 below; the new sections 2–7 describe the delivery mechanism that v0.1 was missing.

Prior art for this approach: [soniccyclone/Meshtastic-Firmware-Friend-Finder-Edition](https://github.com/soniccyclone/Meshtastic-Firmware-Friend-Finder-Edition). We borrow its Python anchor-substitution patcher, multi-stage Docker layout, and three-workflow CI shape, with two changes: (1) a pinned upstream SHA instead of an unpinned `--depth=1` clone, and (2) a `firmware-overlay/` tree for whole new files — soniccyclone only needed in-place edits.

---

## 1. Architecture Overview

### Module-level (unchanged from v0.1)

```
┌─────────────────────────────────────────────────────────────┐
│                    Meshtastic Firmware                      │
│                                                             │
│  ┌──────────────┐   ┌──────────────┐   ┌────────────────┐  │
│  │  Screen.cpp  │   │  Modules.cpp │   │   Router /     │  │
│  │  (1 menu     │   │  (register   │   │   Service      │  │
│  │   entry)     │   │   module)    │   │   (send/recv)  │  │
│  └──────┬───────┘   └──────┬───────┘   └───────┬────────┘  │
│         │                  │                   │           │
│  ┌──────▼───────────────────▼───────────────────▼────────┐  │
│  │                    CompassModule                      │  │
│  │         (SinglePortModule + OSThread)                 │  │
│  │                                                       │  │
│  │  ┌──────────────┐  ┌──────────────┐  ┌────────────┐  │  │
│  │  │ CompassState │  │  CompassUI   │  │CompassMath │  │  │
│  │  │ (FSM + NVS)  │  │(drawFrame,   │  │(bearing,   │  │  │
│  │  │              │  │ input)       │  │ distance,  │  │  │
│  │  └──────────────┘  └──────────────┘  │ heading)   │  │  │
│  │                                      └────────────┘  │  │
│  │  ┌──────────────────────────────────────────────────┐ │  │
│  │  │              Magnetometer                        │ │  │
│  │  │         (QMC5883L driver on Wire1)               │ │  │
│  │  └──────────────────────────────────────────────────┘ │  │
│  └───────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
         │
    ┌────▼─────┐
    │  Wire1   │  TWI1 — P0.13 SDA, P0.16 SCL
    └────┬─────┘
         │
    ┌────▼──────────┐
    │  QMC5883L     │  I2C addr 0x0D
    └───────────────┘
```

### Repo-level

Two repos in play, related by overlay-and-patch:

```
meshtastic-captains-compass/   (this repo)         meshtastic/firmware     (upstream)
├── firmware-overlay/          ───  rsync  ───►   src/modules/CompassModule/...
│                                                  protobufs/meshtastic/compass.proto
├── patches/apply.py           ───  edits  ───►   variants/.../variant.{h,cpp}
│                                                  protobufs/meshtastic/portnums.proto
│                                                  src/modules/Modules.cpp
│                                                  src/graphics/Screen.cpp
└── meshtastic-firmware-pin    ───  pins  ────►   <full SHA on develop>
```

The build pipeline (section 4) shallow-clones upstream at the pinned SHA into a working copy, rsyncs the overlay over it, runs the patcher, and invokes PlatformIO. The output is a `firmware.uf2` for `heltec_mesh_node_t114` containing all stock Meshtastic functionality plus the Captain's Compass module.

**Read these upstream Meshtastic source files before implementing any module-level bead.** All paths are relative to `vendor/meshtastic-firmware/` after `make setup` (section 4.1):
- `src/mesh/MeshModule.h` — base class interface, `wantUIFrame`, `drawFrame`, `interceptingKeyboardInput`, `handleInputEvent`
- `src/mesh/SinglePortModule.h` — packet handling base, `handleReceived`, `allocDataPacket`
- `src/concurrency/OSThread.h` — cooperative scheduler, `runOnce` return semantics
- `src/graphics/Screen.h` / `Screen.cpp` — frame registration, `setFrames`, `showOverlayBanner`
- `src/mesh/Router.h` — `sendToMesh`, `allocForSending`, `NODENUM_BROADCAST`
- `src/NodeDB.h` — `nodeDB->getNodeByNum`, `getNode`, `getMyNodeNum`
- `src/mesh/generated/meshtastic/portnum.pb.h` — existing PortNum enum, find the private app range
- `variants/nrf52840/heltec_mesh_node_t114/variant.h` — current GPIO assignments, WIRE defines
- `variants/nrf52840/heltec_mesh_node_t114/variant.cpp` — Wire0 instantiation pattern; check whether the BSP auto-creates Wire1 from `PIN_WIRE1_*` defines
- An existing module that uses NVS (e.g., `src/modules/StoreForwardModule.cpp`) — `Preferences` usage pattern
- An existing module that adds a Screen menu entry — find the most recent example and copy its registration shape

---

## 2. Repository Layout

```
meshtastic-captains-compass/
├── meshtastic-firmware-pin              # one line: 40-char upstream SHA
├── firmware-overlay/                    # NEW files; mirrored at firmware paths
│   ├── protobufs/meshtastic/
│   │   └── compass.proto
│   └── src/modules/CompassModule/
│       ├── CompassMath.{h,cpp}
│       ├── CompassModule.{h,cpp}
│       ├── CompassState.{h,cpp}
│       ├── CompassUI.{h,cpp}
│       └── Magnetometer.{h,cpp}
├── patches/
│   └── apply.py                         # anchor-substitution patcher
├── docker/
│   ├── Dockerfile                       # multi-stage T114 builder
│   └── entrypoint.sh
├── .github/workflows/
│   ├── pr-build-t114.yml
│   ├── release.yml
│   └── upstream-drift.yml
├── vendor/                              # gitignored — populated by `make setup`
│   └── meshtastic-firmware/             # shallow clone at pinned SHA
├── output/                              # gitignored — build artifacts
├── docs/
│   ├── prd-issue-002-captains-compass.md
│   └── tdd-issue-002-captains-compass.md
├── Makefile
├── build.sh
├── .gitignore                           # adds vendor/, output/
├── .beads/
├── .claude/
├── AGENTS.md
├── CLAUDE.md
└── LICENSE
```

`vendor/` exists for one purpose: so Claude Code, IDEs, language servers, and humans can read the pinned upstream source while developing patches. It is never committed (`.gitignore` excludes the whole directory). The CI build pipeline does *not* depend on `vendor/`; it shallow-fetches inside the Docker image at the same pinned SHA. The host `vendor/` and the container `/firmware-src` are two materializations of the same content.

---

## 3. Pin File

A single file at the repo root, `meshtastic-firmware-pin`, contains exactly one line: the 40-character upstream SHA. No surrounding whitespace, no trailing newline beyond the standard.

Every component reads from this file:

- `make setup` checks out this SHA in `vendor/meshtastic-firmware/`
- `make build` passes it as `--build-arg FIRMWARE_SHA=...` to docker
- `release.yml` and `pr-build-t114.yml` do the same
- `upstream-drift.yml` compares it to upstream `develop` HEAD weekly and proposes a bump

Bumping the pin is a one-line commit. The pin tracks `meshtastic/firmware` `develop` HEAD at known-tested points; bumps go through the drift workflow (section 5.3) so each bump is gated on the patcher still applying cleanly and the firmware still building.

The pin is the single source of truth for "which upstream did this code work against." When something breaks in the field, the SHA in this file is what reproduces the build that produced the failing UF2.

---

## 4. Build Pipeline

### 4.1 Makefile

```make
FIRMWARE_SHA := $(shell cat meshtastic-firmware-pin 2>/dev/null)
FIRMWARE_DIR := vendor/meshtastic-firmware
IMAGE        := cc-builder
OUTPUT_DIR   := $(CURDIR)/output

.PHONY: help setup build shell clean bump-pin

help:
	@echo "Captain's Compass — build pipeline"
	@echo
	@echo "Targets:"
	@echo "  setup     — shallow-clone meshtastic/firmware into $(FIRMWARE_DIR) at pinned SHA"
	@echo "  build     — build $(IMAGE) docker image and emit output/firmware.uf2"
	@echo "  shell     — drop into a shell inside the builder image"
	@echo "  clean     — remove output/"
	@echo "  bump-pin  — write SHA=<sha> to meshtastic-firmware-pin and re-setup"
	@echo
	@echo "FIRMWARE_SHA = $(FIRMWARE_SHA)"

setup:
	@test -n "$(FIRMWARE_SHA)" || ( echo "meshtastic-firmware-pin missing or empty" && exit 1 )
	@if [ ! -d $(FIRMWARE_DIR)/.git ]; then \
	  mkdir -p $(FIRMWARE_DIR); \
	  cd $(FIRMWARE_DIR) && \
	    git init -q && \
	    git remote add origin https://github.com/meshtastic/firmware.git; \
	fi
	cd $(FIRMWARE_DIR) && \
	  git fetch --depth=1 origin $(FIRMWARE_SHA) && \
	  git checkout -q FETCH_HEAD && \
	  git submodule update --init --recursive --depth=1

build:
	@test -n "$(FIRMWARE_SHA)" || ( echo "meshtastic-firmware-pin missing or empty" && exit 1 )
	docker build --build-arg FIRMWARE_SHA=$(FIRMWARE_SHA) -t $(IMAGE) -f docker/Dockerfile .
	@mkdir -p $(OUTPUT_DIR)
	docker run --rm -v "$(OUTPUT_DIR):/output" $(IMAGE)
	@echo "Artifact: $(OUTPUT_DIR)/firmware.uf2"

shell:
	docker run --rm -it --entrypoint /bin/bash -v "$(OUTPUT_DIR):/output" $(IMAGE)

clean:
	rm -rf $(OUTPUT_DIR)

bump-pin:
	@test -n "$(SHA)" || ( echo "usage: make bump-pin SHA=<full-sha>" && exit 1 )
	@printf "%s\n" "$(SHA)" > meshtastic-firmware-pin
	@$(MAKE) setup
```

GitHub's `uploadpack.allowReachableSHA1InWant` is enabled, so `git fetch --depth=1 origin <sha>` works against any reachable commit without first knowing the branch. That keeps `setup` independent of upstream branch renames.

### 4.2 Dockerfile (multi-stage)

**Host architecture is intentionally not pinned.** No `--platform` override anywhere in the Dockerfile, the Makefile, or the CI workflows. Docker resolves the base image to the host arch: arm64 on Apple Silicon dev machines, amd64 on `ubuntu-latest` GitHub Actions runners. Both hosts cross-compile to ARM Cortex-M4F via `arm-none-eabi-gcc`, so the resulting `firmware.uf2` is the same regardless of where it was built. This trades local↔CI image identity for native build performance on each side; layer caches are naturally separate (cache keys include arch). Do not add `--platform=linux/amd64` "for consistency" — it would silently force Rosetta translation on Apple Silicon and erase the speedup.

`docker/Dockerfile`:

```dockerfile
# Stage 1: toolchain — apt + python venv + platformio. Invalidates rarely.
FROM ubuntu:24.04 AS toolchain
ENV DEBIAN_FRONTEND=noninteractive
ENV PATH="/opt/pio-venv/bin:${PATH}"
RUN apt-get update && apt-get install -y --no-install-recommends \
      git python3 python3-pip python3-venv ca-certificates curl \
      libusb-1.0-0 rsync \
 && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/pio-venv \
 && /opt/pio-venv/bin/pip install --upgrade pip platformio

# Stage 2: pinned firmware — clone + checkout SHA + install pio platform.
# Invalidates ONLY when FIRMWARE_SHA changes.
FROM toolchain AS firmware
ARG FIRMWARE_SHA
RUN test -n "${FIRMWARE_SHA}" || ( echo "FIRMWARE_SHA build-arg required" && exit 1 )
RUN mkdir -p /firmware-src \
 && cd /firmware-src \
 && git init -q \
 && git remote add origin https://github.com/meshtastic/firmware.git \
 && git fetch --depth=1 origin ${FIRMWARE_SHA} \
 && git checkout -q FETCH_HEAD \
 && git submodule update --init --recursive --depth=1 \
 && pio pkg install --environment heltec-mesh-node-t114

# Stage 3: builder — overlay + patcher + entrypoint.
# Invalidates on every iteration of our patches.
FROM firmware AS builder
COPY firmware-overlay/         /overlay/
COPY patches/                  /patches/
COPY docker/entrypoint.sh      /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh
WORKDIR /firmware-src
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
```

Layer-cache cost model:

| Change                                       | Stages invalidated | Cold time  |
|----------------------------------------------|--------------------|------------|
| Edit a file in `firmware-overlay/` or `patches/` | 3 only         | ~5 s + pio rebuild |
| Bump `meshtastic-firmware-pin`               | 2 + 3              | ~5 min     |
| Edit `docker/Dockerfile` toolchain stage     | 1 + 2 + 3          | ~10 min    |

In CI, `docker/build-push-action@v6` with `cache-from: type=gha, cache-to: type=gha, mode=max` makes stage 2 free across runs that don't bump the pin.

### 4.3 entrypoint.sh

`docker/entrypoint.sh`:

```bash
#!/bin/bash
# Build entrypoint: reset the firmware tree, apply our overlay + patches,
# regenerate nanopb, run pio, emit firmware.uf2.
set -euo pipefail
cd /firmware-src

echo "=== Reset firmware tree ==="
git reset --hard HEAD
git clean -fdx -e .pio   # keep pio build cache; drop everything else

echo "=== Apply overlay (new files) ==="
rsync -a /overlay/ /firmware-src/

echo "=== Apply patches (modifications) ==="
python3 /patches/apply.py

# nanopb regen is conditional on overlay containing .proto files.
# Stock upstream commits its generated .pb.h/.pb.cpp; only when we add
# compass.proto (BEAD-6) do we need to regenerate. Logic added in BEAD-6.

echo "=== Build heltec-mesh-node-t114 ==="
pio run --environment heltec-mesh-node-t114

echo "=== Emit artifact ==="
mkdir -p /output
cp .pio/build/heltec-mesh-node-t114/firmware.uf2 /output/firmware.uf2
echo "Done: /output/firmware.uf2"
```

The `git reset --hard HEAD` + `git clean -fdx -e .pio` combination is what makes builds idempotent. Any partial overlay or patcher state from a previous run is discarded before re-applying. The `.pio` directory is preserved so the build cache survives.

### 4.4 build.sh (local convenience)

`build.sh` is a thin wrapper for users who want to bypass `make`:

```bash
#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
exec make build
```

---

## 5. CI Workflows

Three workflows, each with a single job. All run on `ubuntu-latest`. Verification criterion in all cases: `pio run` exits 0 and a `firmware.uf2` lands in `/output`. No checksum or reproducibility checks beyond that.

### 5.1 release.yml — push to main

`.github/workflows/release.yml`:

- Triggers: `push` to `main`.
- Concurrency group: `release`, `cancel-in-progress: false` (don't drop in-flight releases).
- Permissions: `contents: write`.
- Steps:
  1. `actions/checkout@v4`.
  2. Read `meshtastic-firmware-pin` into `FIRMWARE_SHA` env var.
  3. Compute CalVer tag: `v$(date -u +%Y.%m.%d)-${GITHUB_RUN_NUMBER}`.
  4. `docker/build-push-action@v6` with `--build-arg FIRMWARE_SHA=$FIRMWARE_SHA`, `cache-from: type=gha`, `cache-to: type=gha,mode=max`, `load: true`, `tags: cc-builder:release`.
  5. `docker run --rm -v "$PWD/output:/output" cc-builder:release`.
  6. `softprops/action-gh-release@v2` with `tag_name`, `prerelease: true`, `generate_release_notes: true`, `files: output/firmware.uf2`. Body template includes UF2 flash instructions for the T114 (double-press reset → drag-drop onto `T114_BOOT`).

### 5.2 pr-build-t114.yml — per-PR builds

- Triggers: `pull_request` `opened`, `synchronize`, `reopened`, with `paths` filter:
  ```yaml
  - 'firmware-overlay/**'
  - 'patches/**'
  - 'docker/**'
  - 'meshtastic-firmware-pin'
  - '.github/workflows/pr-build-t114.yml'
  ```
- Concurrency group: `pr-build-t114-${{ github.event.pull_request.number }}`, `cancel-in-progress: true`.
- Permissions: `contents: write`, `pull-requests: write`.
- Steps 1–5: same as release.yml.
- Step 6: `softprops/action-gh-release@v2` with `tag_name: v<date>-pr<num>.<run>`, `prerelease: true`, `target_commitish: ${{ github.event.pull_request.head.sha }}`.
- Step 7: `actions/github-script@v7` posts a PR comment with the release URL and flash instructions.

### 5.3 upstream-drift.yml — weekly drift detection

- Triggers: `schedule: cron: '0 6 * * 1'` (Monday 06:00 UTC) + `workflow_dispatch`.
- Permissions: `contents: write`, `pull-requests: write`, `issues: write`.
- Steps:
  1. `actions/checkout@v4`.
  2. Read pinned SHA.
  3. `git ls-remote https://github.com/meshtastic/firmware.git refs/heads/develop` → `LATEST_SHA`.
  4. If `LATEST_SHA == FIRMWARE_SHA`: exit 0, no work.
  5. `docker build --build-arg FIRMWARE_SHA=$LATEST_SHA -t cc-builder-drift -f docker/Dockerfile .`.
  6. `docker run --rm cc-builder-drift python3 /patches/apply.py --dry-run` (the `--dry-run` mode of `apply.py`, section 6.2, checks anchor presence without writing).
  7. If dry-run succeeds and a full `pio run` also succeeds: write `LATEST_SHA` to `meshtastic-firmware-pin`, commit on a branch, push, open PR titled `chore: bump firmware pin to <sha7>`.
  8. If dry-run fails: parse `apply.py` stderr for missing-anchor lines, open issue tagged `upstream-drift` listing each failed `(file, anchor)` pair.

The drift workflow is the load-bearing safety net for "upstream moved under us." Without it, drift is discovered when a human flashes a build and something breaks.

---

## 6. Patch System

### 6.1 Two patch shapes

Our changes against upstream split cleanly into two categories:

**Overlay (10 new files)** — files that don't exist upstream and are entirely ours. They live at their final firmware paths under `firmware-overlay/`. The entrypoint runs `rsync -a /overlay/ /firmware-src/`, which copies them in place.

```
firmware-overlay/protobufs/meshtastic/compass.proto
firmware-overlay/src/modules/CompassModule/CompassMath.h
firmware-overlay/src/modules/CompassModule/CompassMath.cpp
firmware-overlay/src/modules/CompassModule/CompassModule.h
firmware-overlay/src/modules/CompassModule/CompassModule.cpp
firmware-overlay/src/modules/CompassModule/CompassState.h
firmware-overlay/src/modules/CompassModule/CompassState.cpp
firmware-overlay/src/modules/CompassModule/CompassUI.h
firmware-overlay/src/modules/CompassModule/CompassUI.cpp
firmware-overlay/src/modules/CompassModule/Magnetometer.h
firmware-overlay/src/modules/CompassModule/Magnetometer.cpp
```

**Patches (5 modified files)** — small textual edits to existing upstream files, applied by `patches/apply.py` as anchor-string substitutions:

| Upstream path                                              | Edit                                  |
|------------------------------------------------------------|---------------------------------------|
| `variants/nrf52840/heltec_mesh_node_t114/variant.h`        | Add 4 `#define`s for MAG pins + WIRE1 |
| `variants/nrf52840/heltec_mesh_node_t114/variant.cpp`      | Add `TwoWire Wire1(...)` *if BSP doesn't auto-create from `PIN_WIRE1_*`* — verify in BEAD-5 |
| `protobufs/meshtastic/portnums.proto`                      | Add `COMPASS_APP = 300;`              |
| `src/modules/Modules.cpp`                                  | `#include` + `new CompassModule();`   |
| `src/graphics/Screen.cpp`                                  | One menu entry                        |

### 6.2 apply.py contract

Single file at `patches/apply.py`. One function per patched upstream file. Each function obeys the same contract:

1. Open the file at its upstream path (cwd is `/firmware-src` in container, `vendor/meshtastic-firmware/` for local dry-runs).
2. Check for the idempotency `MARKER` string (`captains-compass:`). If present, print `Skipped <path>: already patched` and return. Re-running `apply.py` on an already-patched tree is a no-op.
3. Search for the anchor string. If absent, call `sys.exit(f"ERROR: anchor missing in {path}: {anchor!r}")`. This loud failure is the upstream-drift detection mechanism.
4. Construct the replacement string. Every replacement contains the `MARKER` so the next run is a no-op.
5. Write the file.

Skeleton:

```python
#!/usr/bin/env python3
"""Apply Captain's Compass patches to a pristine meshtastic/firmware checkout.

Run from the firmware source root (cwd). Idempotent — re-running on an
already-patched tree is a no-op. Each patch fails loudly if its upstream
anchor has moved; that is the signal to update the anchor in this file.

Usage:
  python3 apply.py            # apply all patches; non-zero exit on missing anchor
  python3 apply.py --dry-run  # check anchors only, exit 0 if all match
"""
import sys

MARKER = "captains-compass:"

# --- One function per patched file ---------------------------------------

def patch_variant_h(dry_run=False):
    path = "variants/nrf52840/heltec_mesh_node_t114/variant.h"
    text = open(path).read()
    if MARKER in text:
        print(f"Skipped {path}: already patched"); return
    anchor = "<TBD: choose during BEAD-5>"
    if anchor not in text:
        sys.exit(f"ERROR: anchor missing in {path}: {anchor!r}")
    if dry_run:
        print(f"OK {path}"); return
    insertion = (
        f"// {MARKER} external QMC5883L on TWI1\n"
        "#define MAG_SDA              (13)\n"
        "#define MAG_SCL              (16)\n"
        "#define WIRE_INTERFACES_COUNT  2\n"
        "#define WIRE1_INTERFACES_COUNT 1\n"
        "#define PIN_WIRE1_SDA        MAG_SDA\n"
        "#define PIN_WIRE1_SCL        MAG_SCL\n\n"
    )
    text = text.replace(anchor, insertion + anchor, 1)
    open(path, "w").write(text)
    print(f"Patched {path}")

# patch_variant_cpp, patch_portnums_proto, patch_modules_cpp, patch_screen_cpp
# follow the same shape.

# --- Main ----------------------------------------------------------------

PATCHES = [
    patch_variant_h,
    patch_variant_cpp,
    patch_portnums_proto,
    patch_modules_cpp,
    patch_screen_cpp,
]

def main():
    dry_run = "--dry-run" in sys.argv
    for p in PATCHES:
        p(dry_run=dry_run)

if __name__ == "__main__":
    main()
```

### 6.3 Anchor strings (finalized during implementation)

Anchor strings cannot be finalized in this doc; they require reading the upstream files in `vendor/meshtastic-firmware/`. Each implementation bead for a patched file (BEAD-5 through BEAD-12 below) instructs the implementer to:

1. Run `make setup` to populate `vendor/`.
2. Read the target upstream file.
3. Choose an anchor string that is (a) **unique** in the file, (b) **stable** across plausible upstream churn (prefer a function signature, a `// region` comment, or a section header over a single line of code).
4. Write the patch function in `apply.py`.
5. Run `make build` end-to-end and verify the resulting UF2 boots.

For each patched file, the bead description lists the *expected* anchor location and the *expected* insertion content; the implementer's job is to find the exact string in the upstream file and confirm it survives the next two upstream commits.

### 6.4 Why anchor substitution rather than `git apply`

Anchor substitution tolerates cosmetic upstream churn that breaks unified diffs:
- A neighbouring comment edit doesn't affect a function-signature anchor.
- Whitespace normalization (tabs↔spaces, trailing space) can be folded into the anchor match.
- When an anchor *does* move, the failure mode is `sys.exit("ERROR: anchor missing in X: 'foo'")` — the implementer immediately knows what to look for.

`git apply` either fuzzes silently (succeeds with a wrong placement) or rejects with diff context that takes minutes to decode. For a long-lived patch series against a moving upstream, that's the wrong failure mode.

---

## 7. Build System Notes (PlatformIO + nanopb)

No new PlatformIO `lib_deps`. The QMC5883L is driven directly over `Wire1` — no third-party driver library. nanopb is already in the build; `compass.proto` follows the existing pattern.

The build system auto-generates nanopb from `.proto` files via the upstream regeneration script (confirm exact filename in BEAD-4: `bin/regen-protos.sh` is the candidate from prior reading; alternates are `bin/build-all.sh` or `bin/generate-proto.sh`). After the overlay copies `compass.proto` into place and the patcher adds `COMPASS_APP = 300;` to `portnums.proto`, the entrypoint runs the regeneration script to produce:

```
src/mesh/generated/meshtastic/compass.pb.h
src/mesh/generated/meshtastic/compass.pb.c
```

These generated files are produced fresh in every container run; they are *not* checked into our overlay. (Upstream commits its own generated proto files; ours are produced by the same script the upstream build expects to run during dev.)

---

## 8. Variant Patch

**BEAD-5 finding (2026-05-02): no patch required.**

The upstream T114 variant.h is already correctly configured for an external QMC5883L on TWI1:

```c
// variants/nrf52840/heltec_mesh_node_t114/variant.h
#define WIRE_INTERFACES_COUNT 2          // line 99
#define PIN_WIRE1_SDA (0 + 16)            // line 109 — P0.16
#define PIN_WIRE1_SCL (0 + 13)            // line 110 — P0.13
```

And the Adafruit nRF52 BSP auto-creates `Wire1` from those defines at static init time. Verified by `grep TwoWire Wire1` across all `variants/nrf52840/*/variant.cpp` — no nRF52 variant explicitly instantiates Wire1, including the three T114-family variants with `WIRE_INTERFACES_COUNT=2` (heltec_mesh_node_t114, t096, t114-inkhud).

`patches/apply.py` keeps `patch_variant_h` and `patch_variant_cpp` as documented no-ops so that future readers understand we deliberately don't patch these files; if a future upstream change ever drops `PIN_WIRE1_*` from variant.h, the Magnetometer driver's `Wire1.begin()` will fail at runtime, prompting us to add a real patch then.

**Wiring:** QMC5883L SDA → P0.16, SCL → P0.13 (per upstream variant.h). The original PRD said the opposite; trust upstream.

**Verification on real hardware:** firmware compiles for `heltec-mesh-node-t114` (already confirmed by BEAD-2). `Wire1.beginTransmission(0x0D)` / `Wire1.endTransmission()` returns 0 (ACK) when QMC5883L is wired — verified at BEAD-7.

---

## 9. Protobuf Definition

**Overlay file:** `firmware-overlay/protobufs/meshtastic/compass.proto`

```protobuf
syntax = "proto3";
package meshtastic;
option java_package = "com.geeksville.mesh";
option optimize_for = LITE_RUNTIME;

enum CompassMsgType {
  POSITION_UPDATE  = 0;
  PAIR_REQUEST     = 1;
  PAIR_ACCEPT      = 2;
  PAIR_CONFIRM     = 3;
  PAIR_REJECT      = 4;
  SESSION_END      = 5;
  CAPABILITY_QUERY = 6;
  CAPABILITY_ADV   = 7;
}

message CompassPacket {
  CompassMsgType type = 1;
  sint32 latitude_i   = 2;
  sint32 longitude_i  = 3;
  uint32 time         = 4;
  uint32 battery_pct  = 5;
}
```

**Patch:** add `COMPASS_APP = 300;` to `meshtastic_PortNum` in `protobufs/meshtastic/portnums.proto`. Value 300 is in the private-app reserved range (256–511); verify it is not already taken before committing.

The nanopb-generated `compass.pb.h` / `compass.pb.c` are produced by the entrypoint's regeneration step (section 7), not committed to the overlay. Include `compass.pb.h` in `CompassModule.h`. The encode/decode calls follow the exact pattern used in any existing `SinglePortModule` — read one for reference before writing any encode/decode code.

---

## 10. Magnetometer Driver

**Overlay files:** `firmware-overlay/src/modules/CompassModule/Magnetometer.{h,cpp}`

### QMC5883L Register Map

| Addr | Name       | R/W | Notes |
|------|------------|-----|-------|
| 0x00 | XOUT_LSB   | R   | |
| 0x01 | XOUT_MSB   | R   | |
| 0x02 | YOUT_LSB   | R   | |
| 0x03 | YOUT_MSB   | R   | |
| 0x04 | ZOUT_LSB   | R   | |
| 0x05 | ZOUT_MSB   | R   | |
| 0x06 | STATUS     | R   | Bit0=DRDY (data ready), Bit1=OVL (overflow), Bit2=DOR (skip) |
| 0x09 | CTRL1      | R/W | Mode[1:0], ODR[3:2], RNG[5:4], OSR[7:6] |
| 0x0A | CTRL2      | R/W | Bit6=ROL_PTR, Bit7=SOFT_RST |
| 0x0B | SETPERIOD  | R/W | Write 0x01 on init |
| 0x0D | CHIP_ID    | R   | Reads 0xFF on valid QMC5883L |

**CTRL1 init value:** `0x1D` — continuous mode (01), 200 Hz ODR (11), 8 G range (01), OSR 512 (00).
**CTRL2 init value:** `0x40` — ROL_PTR enabled.

### Class Interface

```cpp
class Magnetometer {
public:
    enum InitResult { OK, NOT_FOUND, INIT_FAILED };

    InitResult begin();          // call once; initializes Wire1 and chip
    bool       read(int16_t &x, int16_t &y, int16_t &z); // raw, blocking wait for DRDY
    float      heading();        // calibrated heading, degrees [0, 360)
    bool       isReady() const;  // true if begin() returned OK

    // Calibration offsets (hard-iron only)
    void  setCalibration(int16_t ox, int16_t oy, int16_t oz);
    void  getCalibration(int16_t &ox, int16_t &oy, int16_t &oz) const;

private:
    static constexpr uint8_t  I2C_ADDR    = 0x0D;
    static constexpr uint8_t  CHIP_ID_VAL = 0xFF;
    static constexpr uint16_t DRDY_TIMEOUT_MS = 10;

    bool    _ready = false;
    int16_t _calX = 0, _calY = 0, _calZ = 0;

    bool    writeReg(uint8_t reg, uint8_t val);
    uint8_t readReg(uint8_t reg);
    bool    burstRead(uint8_t startReg, uint8_t *buf, uint8_t len);
};
```

### Init Sequence

```
begin():
  Wire1.begin()
  Wire1.beginTransmission(I2C_ADDR)
  if endTransmission() != 0 → return NOT_FOUND
  if readReg(0x0D) != 0xFF  → return INIT_FAILED
  writeReg(0x0B, 0x01)       // SET/RESET period
  writeReg(0x09, 0x1D)       // CTRL1: continuous, 200Hz, 8G, OSR512
  writeReg(0x0A, 0x40)       // CTRL2: ROL_PTR
  _ready = true
  return OK
```

### Read Sequence

```
read(x, y, z):
  wait for DRDY bit in STATUS (0x06), max DRDY_TIMEOUT_MS
  if timeout → return false
  burstRead(0x00, buf, 6)
  x = (int16_t)((buf[1] << 8) | buf[0])
  y = (int16_t)((buf[3] << 8) | buf[2])
  z = (int16_t)((buf[5] << 8) | buf[4])
  return true

heading():
  read(rx, ry, rz)
  cx = rx - _calX
  cy = ry - _calY
  h = atan2f((float)cy, (float)cx) * 180.0f / M_PI
  if h < 0: h += 360.0f
  return h
```

**Magnetic declination:** Not corrected in v0.1. The heading is magnetic north, not true north. The bearing-to-target is computed from GPS coordinates (true north). For short-range tracking (< 1 km) at most latitudes the error is small enough to be acceptable. Add declination correction in a future bead.

---

## 11. CompassMath

**Overlay files:** `firmware-overlay/src/modules/CompassModule/CompassMath.{h,cpp}`

Pure functions, no state, no dependencies on Meshtastic types. Takes integer lat/lon (WGS84 × 1e7) to match the wire protocol.

```cpp
namespace CompassMath {
    // Bearing from (fromLat, fromLon) to (toLat, toLon), degrees [0, 360).
    // Inputs are integer lat/lon × 1e7 (matching GPS and protobuf convention).
    float bearing(int32_t fromLat, int32_t fromLon,
                  int32_t toLat,   int32_t toLon);

    // Great-circle distance in meters.
    float distanceMeters(int32_t fromLat, int32_t fromLon,
                         int32_t toLat,   int32_t toLon);

    // Signed heading error in degrees [-180, 180].
    // Positive = target is clockwise from current heading (turn right).
    float headingError(float currentHeadingDeg, float bearingDeg);
}
```

### Algorithms

**Bearing (forward azimuth):**
```
dLon = (toLon - fromLon) / 1e7 * π/180
lat1 = fromLat / 1e7 * π/180
lat2 = toLat   / 1e7 * π/180

x = sin(dLon) * cos(lat2)
y = cos(lat1)*sin(lat2) - sin(lat1)*cos(lat2)*cos(dLon)
θ = atan2(x, y) * 180/π
return fmod(θ + 360, 360)
```

**Distance (Haversine):**
```
dLat = (toLat - fromLat) / 1e7 * π/180
dLon = (toLon - fromLon) / 1e7 * π/180
lat1 = fromLat / 1e7 * π/180
lat2 = toLat   / 1e7 * π/180

a = sin²(dLat/2) + cos(lat1)*cos(lat2)*sin²(dLon/2)
c = 2 * atan2(√a, √(1-a))
return 6371000 * c    // meters
```

**Heading error:**
```
err = fmod(bearingDeg - currentHeadingDeg + 360, 360)
if err > 180: err -= 360
return err   // [-180, 180]
```

---

## 12. CompassState

**Overlay files:** `firmware-overlay/src/modules/CompassModule/CompassState.{h,cpp}`

Owns the FSM, NVS persistence, Treasure store, and all session data. No I/O — no Wire, no radio, no display. Called by CompassModule.

### State Machine

```cpp
enum class State {
    IDLE,
    DISCOVERING,         // CAPABILITY_QUERY sent, collecting ADVs (3s window)
    AWAITING_PAIR_ACCEPT,// PAIR_REQUEST sent, 60s timeout
    PAIR_INCOMING,       // PAIR_REQUEST received, showing prompt
    TRACKING,            // Active as Compass (pointing at Desire)
    TRACKED,             // Active as Desire (sending position updates)
    TRACKING_TREASURE,   // Navigating to a saved waypoint
    SESSION_PAUSED,      // No Desire updates for 5min; backgrounded
    CALIBRATING,         // Magnetometer calibration loop
};
```

### Key Structs

```cpp
struct DiscoveredNode {
    uint32_t nodeNum;
    char     shortName[5];  // from NodeDB
    int8_t   rssi;          // from last CAPABILITY_ADV
};

struct SessionData {
    uint32_t peerNodeNum;
    char     peerName[12];
    int32_t  peerLatI;
    int32_t  peerLonI;
    uint32_t peerLastUpdateSec;   // Unix time of last POSITION_UPDATE
    uint32_t peerBatteryPct;
    bool     peerHasGPS;
};

struct Treasure {
    char     label[13];    // 12 chars + null
    int32_t  latI;
    int32_t  lonI;
    uint32_t savedAt;      // Unix timestamp
};
```

### Class Interface

```cpp
class CompassState {
public:
    static constexpr uint8_t  MAX_TREASURES        = 5;
    static constexpr uint8_t  MAX_DISCOVERED_NODES = 8;
    static constexpr uint32_t DISCOVERY_WINDOW_MS  = 3000;
    static constexpr uint32_t PAIR_TIMEOUT_MS      = 60000;
    static constexpr uint32_t SIGNAL_LOST_MS       = 90000;
    static constexpr uint32_t SESSION_PAUSE_MS     = 300000; // 5 min
    static constexpr uint32_t STATIONARY_SUPPRESS_MS = 300000; // 5 min keepalive
    static constexpr float    ARRIVAL_THRESHOLD_M  = 15.0f;
    static constexpr float    GPS_NOISE_FLOOR_M    = 5.0f;

    void begin();  // load NVS, set state=IDLE

    State getState() const;

    // Discovery
    void     startDiscovery();
    void     addDiscoveredNode(uint32_t nodeNum, int8_t rssi);
    bool     isDiscoveryWindowOpen() const;  // true if within 3s of startDiscovery()
    uint8_t  discoveredNodeCount() const;
    const DiscoveredNode& discoveredNode(uint8_t i) const;

    // Pairing (Compass side)
    void startPairRequest(uint32_t targetNodeNum);
    void onPairAccepted(uint32_t peerNodeNum);
    void onPairRejected();
    bool isPairTimedOut() const;

    // Pairing (Desire side)
    void onPairRequestReceived(uint32_t initiatorNodeNum);
    void acceptPair();
    void rejectPair();
    uint32_t pendingPairNodeNum() const;  // valid in PAIR_INCOMING

    // Session management
    void onPositionUpdate(int32_t latI, int32_t lonI, uint32_t time, uint32_t battPct, bool hasGPS);
    void onSessionEnd();
    void endSession();    // local user-initiated

    // Session queries (valid in TRACKING / SESSION_PAUSED)
    const SessionData& session() const;
    bool  isSignalLost() const;    // >90s since last update
    bool  isSessionPaused() const; // >5min since last update

    // Treasure navigation
    void startTreasureNav(uint8_t index);
    const Treasure& activeTreasure() const;  // valid in TRACKING_TREASURE

    // Treasure CRUD
    bool saveTreasure(const char *label, int32_t latI, int32_t lonI, uint32_t ts);
    uint8_t treasureCount() const;
    const Treasure& treasure(uint8_t i) const;
    void deleteTreasure(uint8_t i);

    // Calibration
    void startCalibration();
    void feedCalSample(int16_t x, int16_t y, int16_t z);
    bool finishCalibration();  // false if < MIN_CAL_SAMPLES; saves to NVS on true
    uint32_t calSampleCount() const;
    void     loadCalibration(int16_t &ox, int16_t &oy, int16_t &oz) const;

    // Desire-side update suppression
    bool shouldSendUpdate(int32_t currentLatI, int32_t currentLonI) const;
    void onUpdateSent(int32_t latI, int32_t lonI);

private:
    static constexpr uint16_t MIN_CAL_SAMPLES = 50; // ~5s at typical rotation speed
    static constexpr char NVS_NS[] = "compass";

    State    _state = State::IDLE;
    uint32_t _stateEnteredMs = 0;

    DiscoveredNode _discovered[MAX_DISCOVERED_NODES];
    uint8_t        _discoveredCount = 0;
    uint32_t       _discoveryStartMs = 0;

    uint32_t _pendingPairNodeNum = 0;
    uint32_t _pairRequestSentMs  = 0;

    SessionData _session = {};

    Treasure _treasures[MAX_TREASURES];
    uint8_t  _treasureCount = 0;
    uint8_t  _activeTreasureIdx = 0xFF;

    // Calibration state
    int16_t _calMinX, _calMaxX;
    int16_t _calMinY, _calMaxY;
    int16_t _calMinZ, _calMaxZ;
    uint32_t _calSamples = 0;

    // Stored calibration offsets
    int16_t _calOX = 0, _calOY = 0, _calOZ = 0;

    // Desire-side suppression
    int32_t  _lastSentLatI   = 0;
    int32_t  _lastSentLonI   = 0;
    uint32_t _lastSentMs     = 0;

    void     setState(State s);
    void     loadNVS();
    void     saveTreasuresToNVS();
    void     saveCalToNVS();
};
```

### NVS Layout

Namespace: `"compass"`

| Key        | Type    | Description |
|------------|---------|-------------|
| `cal_ver`  | uint8   | Schema version; current = 1. If mismatch on load, discard and prompt re-cal. |
| `cal_x`    | int16   | Hard-iron X offset |
| `cal_y`    | int16   | Hard-iron Y offset |
| `cal_z`    | int16   | Hard-iron Z offset |
| `upd_int`  | uint16  | Position update interval, seconds (10/30/60/120; default 30) |
| `tr_cnt`   | uint8   | Number of saved Treasures (0–5) |
| `tr0_lat` … `tr4_lat` | int32 | Treasure latitude_i |
| `tr0_lon` … `tr4_lon` | int32 | Treasure longitude_i |
| `tr0_ts`  … `tr4_ts`  | uint32 | Treasure save timestamp |
| `tr0_lbl` … `tr4_lbl` | string | Treasure label (max 12 chars) |

Use `Preferences` class (`#include <Preferences.h>`). Open read-write for writes, read-only for loads. Always call `prefs.end()` after each operation block.

---

## 13. CompassModule

**Overlay files:** `firmware-overlay/src/modules/CompassModule/CompassModule.{h,cpp}`

The central coordinator. Extends `SinglePortModule` for packet handling and `OSThread` for the periodic position-update timer.

### Class Interface

```cpp
class CompassModule : public SinglePortModule, private OSThread {
public:
    CompassModule();

    // MeshModule overrides
    bool wantUIFrame()  override;
    bool interceptingKeyboardInput() override;

    // SinglePortModule override
    ProcessMessage handleReceived(const MeshPacket &mp) override;

    // OSThread override
    int32_t runOnce() override;

    // Called by CompassUI when user takes UI actions
    void sendCapabilityQuery();
    void sendPairRequest(uint32_t targetNodeNum);
    void sendPairAccept(uint32_t toNodeNum);
    void sendPairReject(uint32_t toNodeNum);
    void sendSessionEnd();

    CompassState   *state();
    Magnetometer   *mag();

    static CompassModule *instance;

private:
    CompassState  _state;
    Magnetometer  _mag;

    void sendPacket(uint32_t to, uint8_t hopLimit, const meshtastic_CompassPacket &pkt);
    void handleCapabilityQuery(const MeshPacket &mp);
    void handleCapabilityAdv(const MeshPacket &mp);
    void handlePairRequest(const MeshPacket &mp);
    void handlePairAccept(const MeshPacket &mp);
    void handlePairConfirm(const MeshPacket &mp);
    void handlePairReject(const MeshPacket &mp);
    void handlePositionUpdate(const MeshPacket &mp);
    void handleSessionEnd(const MeshPacket &mp);

    int32_t tickTracked();   // called from runOnce() when state=TRACKED
    int32_t tickTracking();  // called from runOnce() when state=TRACKING/SESSION_PAUSED
};
```

### Packet Dispatch

`handleReceived` decodes the nanopb payload, then dispatches to the appropriate private handler. Unrecognized message types return `ProcessMessage::CONTINUE` (let other modules see it). All recognized types return `ProcessMessage::STOP`.

### OSThread::runOnce Behavior

`runOnce` is the heartbeat. Return value is milliseconds until next call.

```
switch state:
  IDLE, DISCOVERING, AWAITING_PAIR_ACCEPT, PAIR_INCOMING, CALIBRATING:
    return 500  // fast tick to detect timeouts
  TRACKED:
    return tickTracked()
  TRACKING, SESSION_PAUSED:
    return tickTracking()
  TRACKING_TREASURE:
    return 1000  // update distance display
```

**tickTracked():**
```
if _state.shouldSendUpdate(myLatI, myLonI):
    build POSITION_UPDATE packet with current GPS + battery
    sendPacket(peerNodeNum, 3, pkt)
    _state.onUpdateSent(myLatI, myLonI)
return updateIntervalMs  // from NVS setting
```

**tickTracking():**
```
if _state.isSessionPaused() and was not paused last tick:
    screen->setFrames()  // release UI frame, return to normal rotation
if _state.isDiscoveryWindowOpen():
    return 100  // fast tick during 3s discovery window
return 1000
```

**CAPABILITY_QUERY / discovery:**
When `sendCapabilityQuery()` is called, it broadcasts the packet and calls `_state.startDiscovery()`. Incoming `CAPABILITY_ADV` packets are fed to `_state.addDiscoveredNode()` for 3 seconds, then the UI renders the sorted list.

### Packet Construction

Hop limits per message type:
```
hop_limit=0: CAPABILITY_QUERY, CAPABILITY_ADV, PAIR_REQUEST,
             PAIR_ACCEPT, PAIR_CONFIRM, PAIR_REJECT
hop_limit=3: POSITION_UPDATE, SESSION_END
```

Broadcast (to=NODENUM_BROADCAST): CAPABILITY_QUERY, CAPABILITY_ADV, PAIR_REQUEST
Unicast (to=peerNodeNum): everything else

---

## 14. CompassUI

**Overlay files:** `firmware-overlay/src/modules/CompassModule/CompassUI.{h,cpp}`

Handles all rendering and input. No radio calls — delegates to `CompassModule` for any packet sends.

### wantUIFrame

Returns `true` when any of these states are active:
- DISCOVERING, AWAITING_PAIR_ACCEPT, PAIR_INCOMING
- TRACKING, SESSION_PAUSED
- TRACKING_TREASURE
- CALIBRATING

Returns `false` in IDLE and TRACKED (Desire role has no UI takeover).

### interceptingKeyboardInput

Returns `true` whenever `wantUIFrame()` returns `true`.

### Screen Layout (128×64 px)

**DISCOVERING (node list):**
```
┌────────────────────────────┐
│ Find Desire         [3s..]  │  row 0 (font 10px)
│ > Alice  ████               │  row 12 (RSSI bar)
│   Bob    ███                │  row 24
│   Carol  █                  │  row 36
│ [refresh]                   │  row 56
└────────────────────────────┘
```

**TRACKING (compass arrow, full screen):**
```
┌────────────────────────────┐
│        /\                   │  arrow tip (rotated)
│       /  \                  │
│      / ↑  \  BOB            │  name right side
│      \    /  1.2 km         │  distance
│       \  /   23s ago  [▮▮▯] │  time + battery
│        \/                   │
└────────────────────────────┘
```

Arrow is drawn in a 48×48 px bounding box centered at (40, 28) relative to frame origin.
Right column (x=82 to x=128) shows peer name, distance, time-since-update, battery.

**Arrow geometry (before rotation, pointing up = 0° heading error):**
```
tip:        ( 0, -18)   relative to arrow center
left wing:  (-9,  -4)
right wing: ( 9,  -4)
tail:        ( 0,  12)
```
Draw as: `fillTriangle(tip, leftWing, rightWing)` + `drawLine(center, tail)`

**Rotation (heading error θ in radians):**
```
x' = x * cos(θ) - y * sin(θ)
y' = x * sin(θ) + y * cos(θ)
```
nRF52840 has hardware FPU; use `sinf`/`cosf` directly.

**Blink behavior (signal lost >90s):** Toggle arrow visibility every 500ms. Track with a `millis()` timestamp, flip a bool on each `drawFrame` call when the toggle interval has elapsed.

**PAIR_INCOMING:**
```
┌────────────────────────────┐
│ Pair Request               │
│                            │
│ Alice wants to             │
│ track you.                 │
│ [ACCEPT]    [REJECT]       │
└────────────────────────────┘
```

**CALIBRATING:**
```
┌────────────────────────────┐
│ Calibrating...             │
│                            │
│ Rotate device slowly       │
│ in all directions.         │
│ [NNN samples] [DONE]       │
└────────────────────────────┘
```

**TRACKING_TREASURE:**
Same layout as TRACKING but label shows Treasure name and no battery indicator. When within 15m, replace the arrow area with large text `YOU'RE HERE` and hold the display on.

### Input Handling

Button events come from `InputBroker` via `handleInputEvent(const InputEvent *e)`.

Map events to UI actions:
```
State::DISCOVERING:
  UP/DOWN           → scroll node list
  SELECT (short)    → confirm selected node → module->sendPairRequest()
  SELECT (long)     → exit to IDLE

State::PAIR_INCOMING:
  LEFT / UP         → highlight ACCEPT
  RIGHT / DOWN      → highlight REJECT
  SELECT            → confirm highlighted choice

State::TRACKING:
  SELECT (long)     → release UI frame (module keeps tracking in background)
  any other         → pass through to Screen normally

State::CALIBRATING:
  SELECT (short)    → _state.finishCalibration()
  SELECT (long)     → abort, return to IDLE

State::TRACKING_TREASURE:
  SELECT (long)     → release UI frame
```

Long-press threshold: 800ms. Track `pressStartMs` on key-down event; evaluate duration on key-up.

---

## 15. Integration

Two upstream files. Both edits are applied by `apply.py`.

**File 1:** `src/modules/Modules.cpp` — two insertions, both in `patch_modules_cpp`:

- `#include "modules/CompassModule/CompassModule.h"` — anchored after `#include "modules/RoutingModule.h"` (a stable, unconditional include).
- `new CompassModule();` — anchored at the opening of `void setupModules() {`, inserted as the first statement so `CompassModule::instance` is set before any other module that might use it.

The constructor registers the module with the `SinglePortModule` dispatcher and the `OSThread` scheduler. No other changes needed.

**File 2:** `src/graphics/draw/MenuHandler.cpp` — four coordinated insertions, all in `patch_screen_cpp` (kept named `screen_cpp` for historical continuity with the v0.1 TDD; the actual file is `MenuHandler.cpp`).

The entry attaches to the existing **home banner menu** (`menuHandler::homeBaseMenu()`), which is the popup overlay triggered by the home button. Adding "Compass" requires:

1. **Enum:** `enum optionsNumbers { Back, Mute, Backlight, Position, Preset, Freetext, Sleep, enumEnd };` → insert `Compass,` before `enumEnd`.
2. **Options array:** after the `Position` branch's `optionsEnumArray[options++] = Position;` line, append `optionsArray[options] = "Compass"; optionsEnumArray[options++] = Compass;`.
3. **Banner callback:** after the `} else if (selected == Freetext) { ... }` arm, add `else if (selected == Compass) { CompassModule::instance->sendCapabilityQuery(); }`.
4. **Include:** after `#include "MenuHandler.h"`, add `#include "modules/CompassModule/CompassModule.h"`.

`sendCapabilityQuery()` calls `_state.startDiscovery()` internally and broadcasts the query. The CompassModule's `wantUIFrame()` returns true while in DISCOVERING, which causes the screen rotation to render the discovery frame on the next refresh. No explicit `setFrames()` call is needed — the screen framework polls `wantUIFrame()` per frame.

The rest of the Compass sub-menu (Save Treasure, Treasures list, End Session, Settings, Status/calibrate) lives entirely within `CompassUI::drawFrame` state transitions — not as separate menu entries.

---

## 16. v2 Roadmap (Deferred)

These items were considered for v0.2 and deliberately deferred. Implement after the T114 build pipeline is shipping reliably and the protocol has stabilized.

### 16.1 Native/Portduino build + smoke tests

soniccyclone's prior-art repo includes a `Dockerfile.native` that builds the Meshtastic `native` (Portduino) PlatformIO environment with the patches applied, plus a smoke-test framework that spins up two simulated nodes and exercises the pairing protocol end-to-end on every PR. This catches protocol regressions without needing physical T114s.

Add as:
- `docker/Dockerfile.native` — analogous structure, env `native` instead of `heltec_mesh_node_t114`.
- `docker/entrypoint-native.sh` — runs `pio run -e native` and writes `build.log`.
- `tests/smoke/two_node.py` — Python harness that drives two `native` binaries via stdio, exchanges `CAPABILITY_QUERY` / `PAIR_REQUEST` / `POSITION_UPDATE`, asserts state machine transitions.
- `tests/smoke/pairing.py` — full pairing-handshake assertion.
- `.github/workflows/ci-native.yml` — runs both compile and smoke on every PR touching the patch surface or smoke tests.

### 16.2 Reproducible-binary verification

`SOURCE_DATE_EPOCH` plus auditing the firmware for `__DATE__` / `__TIME__` uses, so two builds at the same pin produce byte-identical UF2s. Cheap once the rest works; valuable for supply-chain auditing of release binaries.

### 16.3 Magnetic declination correction

WMM lookup table or fixed per-region offset. The v0.1 heading is magnetic; the bearing-to-target is true. For sub-1km tracking the error is small enough to ignore. Add when targeting longer ranges.

### 16.4 Multi-target tracking

PRD §6 explicitly excludes this. Add when there's a real CUJ for it (e.g., search-and-rescue with multiple Desires).

---

## 17. Bead Breakdown

Each bead is independently committable. Implement in dependency order; parallel tracks are marked.

The build infrastructure (BEAD-1 through BEAD-4) ships *before any C++ is written*. This is the change from v0.1: in v0.1, the build infrastructure was implicit and nonexistent, so the previous agent had nowhere to put its work and lost it all. v0.2 makes the build infrastructure the load-bearing foundation that everything else stacks onto.

---

### BEAD-1: Repo scaffold + pin file
**Depends on:** nothing
**Files:** `meshtastic-firmware-pin`, `Makefile`, `build.sh`, `.gitignore`, empty directories `firmware-overlay/`, `patches/`, `docker/`, `output/`
**What to do:**
- Pick a pinned SHA: latest commit on `meshtastic/firmware` `develop` at the time of starting work. Document in commit message which SHA and why.
- Write `meshtastic-firmware-pin` with that SHA.
- Write `Makefile` per section 4.1 (setup, build, shell, clean, bump-pin).
- Write `build.sh` thin wrapper.
- Add `vendor/`, `output/` to `.gitignore`.
**Acceptance:**
- `make setup` clones `meshtastic/firmware` into `vendor/meshtastic-firmware/` at the pinned SHA.
- Re-running `make setup` is a no-op (idempotent).
- `make help` prints usage.
- `vendor/` and `output/` are gitignored.

---

### BEAD-2: Docker pipeline
**Depends on:** BEAD-1
**Files:** `docker/Dockerfile`, `docker/entrypoint.sh`
**What to do:**
- Implement the multi-stage Dockerfile per section 4.2.
- Implement `entrypoint.sh` per section 4.3.
- At this stage, `firmware-overlay/` and `patches/apply.py` are still empty — the pipeline must still produce a stock `firmware.uf2` from the unmodified upstream.
**Acceptance:**
- `make build` produces `output/firmware.uf2` for the unmodified upstream firmware. (This is the "baseline" build: stock Meshtastic, no Compass code.)
- Layer cache works: editing nothing and re-running `make build` re-uses all stages, takes < 30 seconds.
- `make shell` drops into a shell at `/firmware-src` inside the image with `pio` on PATH.

---

### BEAD-3: Patcher framework
**Depends on:** BEAD-1
**Files:** `patches/apply.py`
**What to do:**
- Implement the `apply.py` skeleton per section 6.2 with the `MARKER`, the per-file function shape, the `--dry-run` mode, and the loud-on-anchor-missing exit. Include placeholder functions for all 5 patches that immediately `sys.exit("not yet implemented")` — they get filled in by BEAD-5, 6, 11, 12.
- `apply.py` must be runnable from anywhere as long as cwd is the firmware source root.
**Acceptance:**
- `python3 apply.py --dry-run` from a clean firmware checkout succeeds (passes through the placeholder no-ops; documents that no patches are wired up yet).
- `python3 apply.py` from an already-fully-patched checkout is a no-op (every function hits its `MARKER` skip path).
- Removing any anchor from the source files makes `apply.py` exit non-zero with the `(file, anchor)` named.

---

### BEAD-4: GitHub Actions
**Depends on:** BEAD-2
**Files:** `.github/workflows/release.yml`, `.github/workflows/pr-build-t114.yml`, `.github/workflows/upstream-drift.yml`
**What to do:** Implement all three workflows per section 5. Use `docker/build-push-action@v6` with GHA cache.
**Acceptance:**
- A push to `main` produces a tagged prerelease with `firmware.uf2` attached.
- A PR touching `firmware-overlay/`, `patches/`, `docker/`, or `meshtastic-firmware-pin` produces a per-PR prerelease and posts a comment on the PR with the link.
- Manually dispatching `upstream-drift.yml` with the pinned SHA equal to upstream develop HEAD exits 0 with no work.
- Manually dispatching `upstream-drift.yml` with an artificially-stale pin opens a PR bumping the pin (assuming patcher anchors still apply).

---

### BEAD-5: Variant patch
**Depends on:** BEAD-3
**Files:** `patches/apply.py` (`patch_variant_h`, optionally `patch_variant_cpp`)
**What to do:**
- Read `vendor/meshtastic-firmware/variants/nrf52840/heltec_mesh_node_t114/variant.h`.
- Choose anchor for variant.h (a unique stable line near the existing GPIO defines).
- Implement `patch_variant_h` per section 8 to inject the 4 `#define`s.
- Read `variant.cpp` and other nRF52 variants with `WIRE_INTERFACES_COUNT=2`. Determine if BSP auto-creates `Wire1`. If not, implement `patch_variant_cpp`.
**Acceptance:**
- `make build` succeeds (overlay still empty; this just confirms variant patch doesn't break the base build).
- Booting the resulting UF2 on a T114 with QMC5883L wired: a minimal probe (e.g., temporary diagnostic in `setup()`) shows `Wire1.beginTransmission(0x0D)` returns 0 (ACK).

---

### BEAD-6: Protobuf — overlay + portnums patch
**Depends on:** BEAD-3
**Files:** `firmware-overlay/protobufs/meshtastic/compass.proto`, `patches/apply.py` (`patch_portnums_proto`)
**What to do:**
- Write `compass.proto` per section 9.
- Read `vendor/meshtastic-firmware/protobufs/meshtastic/portnums.proto`.
- Confirm value 300 is unused.
- Choose anchor (the trailing `}` of the `meshtastic_PortNum` enum, or the last-defined enum member).
- Implement `patch_portnums_proto` to inject `COMPASS_APP = 300;`.
- Confirm exact upstream proto-regen script name (update `entrypoint.sh` if needed).
**Acceptance:**
- `make build` runs to completion. Generated `compass.pb.h` exists in the build output.
- `meshtastic_PortNum_COMPASS_APP` resolves to 300 in the generated header.
- `meshtastic_CompassMsgType` and `meshtastic_CompassPacket` are accessible.

---

### BEAD-7: Magnetometer driver
**Depends on:** BEAD-5
**Files:** `firmware-overlay/src/modules/CompassModule/Magnetometer.{h,cpp}`
**What to do:** Implement per section 10. Init sequence, DRDY-gated read, heading computation, calibration offset application.
**Acceptance:**
- `make build` produces a UF2 that includes the new files.
- On real hardware: `begin()` returns `OK` with QMC5883L connected, `NOT_FOUND` with nothing on bus, `INIT_FAILED` with bus present but chip ID wrong.
- `heading()` returns a value in [0, 360) that rotates smoothly as the device is rotated.
- No blocking delay in the hot path longer than `DRDY_TIMEOUT_MS`.

---

### BEAD-8: CompassMath
**Depends on:** BEAD-2 (parallel with BEAD-5, BEAD-6, BEAD-7)
**Files:** `firmware-overlay/src/modules/CompassModule/CompassMath.{h,cpp}`
**What to do:** Implement the three functions in section 11.
**Acceptance:**
- `make build` succeeds; the CompassMath translation unit compiles.
- Spot-check on host (small ad-hoc test program if convenient):
  - `bearing(0, 0, 1e7, 0)` ≈ 0° (due north)
  - `bearing(0, 0, 0, 1e7)` ≈ 90° (due east)
  - `distanceMeters(0, 0, 0, 1e7)` ≈ 111195m (1° longitude at equator)
  - `headingError(350, 10)` ≈ 20°
  - `headingError(10, 350)` ≈ -20°

(v2 native/Portduino smoke build (section 16.1) will turn these into proper unit tests.)

---

### BEAD-9: CompassState
**Depends on:** BEAD-8
**Files:** `firmware-overlay/src/modules/CompassModule/CompassState.{h,cpp}`
**What to do:** Implement FSM, NVS load/save, Treasure CRUD, calibration sample accumulation, and Desire-side suppression logic per section 12.
**Acceptance:**
- `make build` succeeds.
- On real hardware:
  - State transitions follow the diagram in section 12 exactly.
  - `saveTreasure` / `treasure` round-trip through NVS correctly across a reboot.
  - `shouldSendUpdate`: returns true initially, false on second call with same position within `STATIONARY_SUPPRESS_MS`, true again after `STATIONARY_SUPPRESS_MS` even with same position.
  - `finishCalibration()` returns false if `calSampleCount() < MIN_CAL_SAMPLES`.
  - NVS cal-version mismatch results in zero offsets returned, not a crash.

---

### BEAD-10: CompassModule
**Depends on:** BEAD-6 (proto types), BEAD-7 (Magnetometer), BEAD-9 (CompassState)
**Files:** `firmware-overlay/src/modules/CompassModule/CompassModule.{h,cpp}`
**What to do:** Implement packet dispatch, `runOnce` scheduler, send helpers per section 13.
**Acceptance:**
- `make build` succeeds.
- On real hardware (or using a paired second T114):
  - Module registers on `COMPASS_APP` port. Other-port packets are ignored.
  - `sendCapabilityQuery()` produces a broadcast packet with `hop_limit=0` and type `CAPABILITY_QUERY`.
  - Injected mock `PAIR_REQUEST` packet → `_state.getState() == State::PAIR_INCOMING`.
  - In `TRACKED` state, `runOnce` calls `sendPacket` at approximately the configured interval and calls `_state.onUpdateSent`.
  - `wantUIFrame()` returns `true` in DISCOVERING/TRACKING/PAIR_INCOMING/CALIBRATING; `false` in IDLE and TRACKED.

---

### BEAD-11: CompassUI
**Depends on:** BEAD-8 (CompassMath), BEAD-9 (CompassState), BEAD-10 (CompassModule)
**Files:** `firmware-overlay/src/modules/CompassModule/CompassUI.{h,cpp}`
**What to do:** Implement `drawFrame`, `interceptingKeyboardInput`, `handleInputEvent` per section 14.
**Acceptance:**
- `make build` succeeds.
- On real hardware:
  - Arrow tip points visually upward when heading error = 0.
  - Arrow rotates clockwise for positive heading error, counter-clockwise for negative. Verified at ±45°, ±90°, ±180°.
  - Arrow blinks (500ms period) when `_state.isSignalLost()` is true.
  - PAIR_INCOMING screen shows node name; ACCEPT calls `module->sendPairAccept`.
  - CALIBRATING screen increments sample count in real time.
  - Long-press (>800ms) SELECT in TRACKING releases the UI frame and calls `screen->setFrames()`.
  - TRACKING_TREASURE shows `YOU'RE HERE` when distance < 15m.

---

### BEAD-12: Integration patches
**Depends on:** BEAD-10, BEAD-11
**Files:** `patches/apply.py` (`patch_modules_cpp`, `patch_screen_cpp`)
**What to do:**
- Read `vendor/meshtastic-firmware/src/modules/Modules.cpp`. Choose anchors for the include and for the `setupModules()` body. Implement `patch_modules_cpp` per section 15.
- Read `vendor/meshtastic-firmware/src/graphics/Screen.cpp`. Find the most recent module's menu-entry registration. Choose an anchor on its line. Implement `patch_screen_cpp` to inject the Compass menu entry.
**Acceptance:**
- `make build` succeeds end-to-end.
- Booted UF2 on T114:
  - "Compass" entry appears in the Meshtastic main menu.
  - Selecting it triggers DISCOVERING state and the node list frame appears.
  - All existing Meshtastic functionality (text, position share, mesh routing) is unaffected when Compass is idle.
- A second, paired T114 successfully exchanges PAIR_REQUEST / PAIR_ACCEPT / PAIR_CONFIRM and enters TRACKING / TRACKED states. Position arrow updates as devices are moved relative to each other.

---

*This document is the authoritative implementation spec for issue #2. PRD owns the what and why; this doc owns the how. When these conflict, fix this doc — not the PRD.*
