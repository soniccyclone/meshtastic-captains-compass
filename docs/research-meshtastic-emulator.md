# Research: Meshtastic Firmware Emulator + MCP / E2E Harness — Feasibility Notes
**Date:** 2026-05-02
**Status:** Validation passed; project not yet started.
**Audience:** Whoever (human or agent) starts the separate `meshtastic-emu` project.

---

## 0. TL;DR

- **Premise:** "Can we emulate a Meshtastic device with the actual UI visible, then drive it via an MCP server and/or a Python E2E harness — like Playwright for Meshtastic firmware?"
- **Answer:** **Yes.** Upstream Meshtastic already ships a Portduino-based native build (`pio run -e native`) that links SDL2 + X11 and renders the OLED/TFT display in a real desktop window. We confirmed it builds, links, and boots a fully-functional simulated Meshtastic node from a Linux Docker container.
- **Not viable:** building it directly on macOS. Portduino + LovyanGFX assume glibc (`argp.h`, `malloc.h`, …); patching that is multi-day and pointless when Linux Docker works in minutes.
- **Recommended path:** new repo `meshtastic-emu`. Linux Docker base, X11 forwarding to macOS via XQuartz for development, Xvfb-headless for CI. ~1 week to v0.1 (Python harness + MCP server skeleton).

---

## 1. What We Wanted to Validate

The Captain's Compass project hit a UI bug ([PR #8](https://github.com/soniccyclone/meshtastic-captains-compass/pull/8)) that was diagnosed by code-reading because no E2E test rig exists. The question was whether we could build one.

Validation goals, in order:

1. Does upstream Meshtastic have a way to render the UI on a non-embedded host? (Asked because earlier I had hedged "the display backend is abstract; stdout-only by default" without verifying.)
2. If so, can we actually build a binary that does it?
3. If so, does the binary boot a real Meshtastic node?
4. If so, what's the realistic effort to wrap it into an MCP server + Python E2E harness?

---

## 2. What We Found

### 2.1 Upstream has SDL/X11 native rendering, in-tree

`vendor/meshtastic-firmware/variants/native/portduino/platformio.ini` defines four native build envs. The interesting two:

| Env | Display | Resolution | Use |
|---|---|---|---|
| `env:native` | SDL2 → X11 backend | OLED-style | matches our T114 scale |
| `env:native-tft` | X11 + libinput + LVGL | 320×240 | TFT-style |

`env:native` uses SDL2 for windowing (cross-platform in theory; X11 in practice on Linux). The display backend implementation is `vendor/meshtastic-firmware/src/graphics/Panel_sdl.cpp`.

The Display.Panel config string that selects it is `"X11"` — confusingly, since the implementation is in `Panel_sdl.cpp`. The mapping is in `src/platform/portduino/PortduinoGlue.h:64`:

```cpp
std::map<screen_modules, std::string> screen_names = {
    {x11, "X11"}, {fb, "FB"},
    {st7789, "ST7789"}, {st7735, "ST7735"}, {st7735s, "ST7735S"},
    {st7796, "ST7796"}, {ili9341, "ILI9341"}, {ili9342, "ILI9342"},
    {ili9486, "ILI9486"}, {ili9488, "ILI9488"}, {hx8357d, "HX8357D"}};
```

So `Display.Panel: X11` in `config.yaml` selects the SDL/X11-windowed display. On Linux that opens an X11 window; in theory on macOS SDL2 could open a Cocoa window, but Portduino's bootstrap pulls in too much glibc for that to compile cleanly — see §3.

### 2.2 Apt deps required for the build

Confirmed via iteration. Our existing `cc-builder` Docker base is missing all of these for native builds (it only had the ARM cross-compile chain):

**Build-time:**
```
build-essential       # gcc / g++ / make
pkg-config
libsdl2-dev
libulfius-dev
libssl-dev
libyaml-cpp-dev
libi2c-dev
libbluetooth-dev
libgpiod-dev
libusb-1.0-0-dev
libuv1-dev
```

**Runtime (in addition to libc):**
```
libsdl2-2.0-0
libulfius2.7
libgpiod2
libi2c0
libuv1
libyaml-cpp0.8
```

Both lists are derived empirically from compilation/link failures. soniccyclone's `Dockerfile.native` had a similar list, but a couple of items were incomplete; the ones above are what worked.

### 2.3 Build commands that work

From inside the existing `cc-builder` Docker container:

```bash
apt-get update -qq
apt-get install -y -qq \
  build-essential pkg-config \
  libsdl2-dev libulfius-dev libssl-dev libyaml-cpp-dev \
  libi2c-dev libbluetooth-dev libgpiod-dev libusb-1.0-0-dev libuv1-dev

cd /firmware-src
pio pkg install -e native    # ~5 min first run; downloads framework-portduino, LovyanGFX, etc.
pio run -e native            # ~2:30 first run; faster on incremental
# Output: .pio/build/native/program  (86 MB ARM64 ELF)
```

The binary's resolved deps include:

```
libulfius.so.2.7    => /lib/aarch64-linux-gnu/libulfius.so.2.7
libSDL2-2.0.so.0    => /lib/aarch64-linux-gnu/libSDL2-2.0.so.0
libX11.so.6         => /lib/aarch64-linux-gnu/libX11.so.6
libX11-xcb.so.1     => /lib/aarch64-linux-gnu/libX11-xcb.so.1
```

### 2.4 Where Portduino looks for `config.yaml`

`src/platform/portduino/PortduinoGlue.cpp` checks, in order:

1. Path passed via `--config <path>` flag
2. `./config.yaml` (cwd)
3. `/etc/meshtasticd/config.yaml`

If none found, prints `"No 'config.yaml' found..."` and refuses to start with the message:

```
*** Blank MAC Address not allowed!
Please set a MAC Address in config.yaml using either MACAddress or MACAddressSource.
```

### 2.5 Minimal `config.yaml` that boots the node

```yaml
Lora:
  Module: sim          # use upstream's SimRadio for the LoRa transceiver

Display:
  Panel: X11           # selects Panel_sdl.cpp's SDL2-over-X11 backend
  Width: 240
  Height: 135          # match T114 ST7789 dimensions

General:
  MACAddress: 00:00:00:00:00:42   # any unique MAC works; required field
```

A reference config with all the documented options is at `vendor/meshtastic-firmware/bin/config-dist.yaml` — it's mostly Raspberry Pi SPI display configurations, none of which use `X11`, but the `General.MACAddress` block at the bottom is documented.

### 2.6 The boot succeeded

With the config in cwd, the binary produced this real boot output:

```
Portduino is starting, VFS root at /root/.portduino/default
Using local config.yaml as config file
MAC ADDRESS: 00:00:00:00:00:42
Portduino critical error: Failed to open posix file /dev/spidev0.0, errno=2
No hardware spi chip found...
DEBUG | ??:??:?? 0 Upgrade time to quality Device
INFO  | 16:27:12 0

//\ E S H T /\ S T / C

DEBUG | 16:27:12 0 Filesystem files:
DEBUG | 16:27:12 0  /.. (0 Bytes)
DEBUG | 16:27:12 0  /. (0 Bytes)
INFO  | 16:27:12 0 No I2C device configured, Skip
INFO  | 16:27:12 0 No I2C devices found
DEBUG | 16:27:12 0 acc_info = 0
INFO  | 16:27:12 0 S:B:37,2.7.15.d18f3f7,native,meshtastic/firmware
INFO  | 16:27:12 0 Build timestamp: 1777680000
INFO  | 16:27:12 0 Init NodeDB
/root/.portduino/default/prefs/nodes.proto does not exist
ERROR | 16:27:12 0 Could not open / read /prefs/nodes.proto
WARN  | 16:27:12 0 NodeDatabase 0 is old, discard
DEBUG | 16:27:12 0 Install default NodeDatabase
ERROR | 16:27:12 0 Could not open / read /prefs/device.proto
WARN  | 16:27:12 0 Devicestate 0 is old or invalid, discard
INFO  | 16:27:12 0 Install default DeviceState
DEBUG | 16:27:12 0 Initial packet id 1452361721
DEBUG | 16:27:12 0 Partially randomized packet id 2731539450
DEBUG | 16:27:12 0 Use nodenum 0x42
```

That's a complete Meshtastic node init. The `spidev0.0` and `I2C devices found` lines are non-fatal and expected (no real hardware buses inside Docker). Once init completes, the runtime enters the main loop — at which point the SDL window would render *if* a display were attached.

We didn't reach the SDL-window step because XQuartz isn't installed on the test host. That's the only piece left.

---

## 3. What Doesn't Work: Direct macOS Build

We tried `pio run -e native` on macOS host (with `pip install platformio` in a venv and `brew install sdl2 argp-standalone`). It failed with:

1. `fatal error: 'argp.h' file not found` — fixable with `brew install argp-standalone` and adding `-I/opt/homebrew/include`
2. `fatal error: 'malloc.h' file not found` — `<malloc.h>` is glibc-specific (deprecated even on Linux but Portduino's `Arduino.h` includes it). On macOS `<stdlib.h>` provides malloc; no shim package exists.

These are the first two; there are likely more `linux/`-prefixed headers, epoll/inotify references, and Linux-specific libusb and libgpiod paths deeper in the build. Patching them would mean shipping a maintained "macOS Portduino" fork.

**Verdict:** out of scope. The reasonable architecture is Linux-Docker for compilation + X11/SDL forwarding to macOS for the display.

---

## 4. The Path to Seeing the SDL Window

Two pieces, both standard:

### 4.1 XQuartz on macOS (one-time setup, ~5 min)

```bash
brew install --cask xquartz
open -a XQuartz
# In XQuartz Preferences → Security: tick "Allow connections from network clients"
# Quit and reopen XQuartz (the toggle takes effect on restart)
xhost +localhost              # allow loopback X11 connections
echo $DISPLAY                  # should print :0 after fresh shell
```

### 4.2 Run with display forwarding

```bash
docker run --rm -it \
  -v /tmp/native-out:/host \
  -e DISPLAY=host.docker.internal:0 \
  --entrypoint /bin/bash \
  cc-builder -c '
    apt-get update -qq && apt-get install -y -qq \
      libsdl2-2.0-0 libulfius2.7 libgpiod2 libi2c0 libuv1 libyaml-cpp0.8 > /dev/null
    mkdir -p /work && cp /host/portduino-default/config.yaml /work/
    cd /work && /host/meshtastic-native
  '
```

`host.docker.internal` is the Docker Desktop on macOS magic hostname that resolves to the host. SDL on Linux opens an X11 connection to that hostname's `:0` display, which XQuartz on macOS receives.

(For CI, swap XQuartz for Xvfb running inside a sidecar container; see §5.)

---

## 5. Proposed Project: `meshtastic-emu`

Separate repo. Naming open: `meshtastic-emu`, `meshtastic-virtual-device`, `meshtastic-mcp`. The first is most accurate.

### 5.1 What's in the box

```
meshtastic-emu/
├── meshtastic-firmware-pin           # tracks upstream releases (same pattern as captains-compass)
├── docker/
│   ├── Dockerfile                    # Ubuntu + SDL2 + Portduino deps + pio
│   ├── Dockerfile.xvfb               # CI variant: Xvfb-in-container, no host X server needed
│   └── entrypoint.sh                 # builds env:native, runs binary, optionally with --config
├── patches/
│   └── apply.py                      # if needed: input-injection RPC + framebuffer-export hooks
├── meshtastic_emu/                   # Python package
│   ├── __init__.py
│   ├── device.py                     # high-level Device wrapper (start/stop/inject/screenshot)
│   ├── network.py                    # multi-device mesh (uses upstream's SimRadio over TCP loopback)
│   ├── matchers.py                   # OCR / pixel-pattern / element-text assertions
│   └── transport.py                  # IPC channel to firmware (input + framebuffer)
├── mcp/                              # MCP server (separate process, depends on meshtastic_emu)
│   └── server.py                     # tools: long_press, read_screen, wait_for_state, …
├── examples/
│   ├── run-with-xquartz.sh           # what we documented in §4 above
│   ├── test_pairing.py               # Pytest example
│   └── interactive.py                # ad-hoc REPL session
├── tests/
│   ├── test_boot.py                  # device boots, idle screen renders
│   └── test_pairing.py               # two devices pair end-to-end
├── README.md
└── pyproject.toml                    # ruff + pytest + the package
```

### 5.2 Reuse from `meshtastic-captains-compass`

The build infrastructure pattern transfers directly:
- **Pin file** for upstream firmware version
- **Multi-stage Dockerfile** (toolchain → pinned firmware → builder)
- **Anchor-substitution patcher** if we need to inject IPC hooks into the firmware
- **GitHub Actions** for release / per-PR / drift detection

Don't re-invent these. Copy the `Makefile` + `docker/Dockerfile` shape and adjust the env from `heltec-mesh-node-t114` to `native`.

### 5.3 Effort breakdown

| Phase | Work | Estimate |
|---|---|---|
| **0** | Repo scaffold + pin file + Makefile + multi-stage Dockerfile (cribbed from captains-compass) | 0.5 day |
| **1** | Base image with `env:native` baked in (apt deps + pio platform install + pre-built program). Verifies green-field via `make build` | 0.5 day |
| **2** | XQuartz forwarding example: `examples/run-with-xquartz.sh` that we tested manually | 0.5 day |
| **3** | Xvfb-in-container variant for CI (no host X server). Renders to a virtual framebuffer, exits cleanly | 1 day |
| **4** | IPC: input injection. Firmware patch (or stdin protocol) so the host can send `INPUT_BROKER_*` events. The hook point is `inputBroker->injectInputEvent()`; need to plumb stdin → that call. | 1–2 days |
| **5** | IPC: framebuffer readback. Either hook the SDL surface flip or read the X11 window. Latter is easier (use Pillow/`xwd` to grab the window) | 1 day |
| **6** | Python package `meshtastic_emu` with high-level Device class | 1–2 days |
| **7** | MCP server wrapping `meshtastic_emu` with tools `long_press`, `read_screen`, `select_menu_item`, `wait_for_state` | 1 day |
| **8** | First end-to-end test: `test_pairing.py` (two devices, full pair handshake, assert TRACKING state) | 1 day |
| **9** | README, install docs, GitHub Actions, first 0.1 release | 0.5 day |
| | **Total** | **~7–10 days** |

### 5.4 What this enables

- **Run `meshtastic-captains-compass`'s manual QA Tests 4–8 in CI** instead of by hand on real T114s. (The current bottleneck for shipping the compass: every change requires a tester with two physical T114s + magnetometer.)
- **MCP-driven debugging:** Claude Code can drive the simulated device while the user watches the SDL window. Click around, verify menu navigation, file bugs from the same session.
- **Multi-node mesh tests** via `SimRadio`: spawn N native binaries on TCP loopback radios and exercise routing / pairing / capability queries between them without physical hardware.
- **General firmware-development utility** beyond Captain's Compass — anyone modifying Meshtastic upstream can use it.

---

## 6. Cul-de-sacs and Lessons Learned

In rough order of how much time they cost.

### 6.1 Direct macOS native build

Tried first because "macOS-native is the fastest dev loop." Hit `argp.h`, then `malloc.h`. Each fix unblocks one file then exposes the next Linux-ism. This rabbit hole has no end inside a reasonable horizon. Don't repeat.

**Lesson:** If upstream supports Linux-only and ships only Linux Docker images, just use Linux Docker. Multi-platform parity is upstream's problem, not ours.

### 6.2 `set -e` + `timeout` interaction

Wrote a probe script that did:
```bash
set -e
timeout 8 /host/meshtastic-native > /tmp/log 2>&1
echo "exit: $?"
```
The `timeout` returns non-zero when it kills the child via signal. `set -e` triggered abort BEFORE the `echo` line ran, so the diagnostic `exit: $?` and the log dump never printed. Looked like the binary hung; actually the script aborted.

**Fix:** drop `set -e` for sections that intentionally use `timeout`, or `|| true` after the timeout.

### 6.3 bash backgrounding + signal forwarding

```bash
( /host/meshtastic-native 2>&1 ) > /tmp/log 2>&1 &
PID=$!
sleep 8
kill -INT $PID 2>/dev/null || true
wait $PID
```
The subshell PID is not the binary's PID; `kill -INT $PID` killed the subshell wrapper and the binary kept running. `wait` blocked indefinitely.

**Fix:** use `timeout` directly (with `|| true`) — handles signal forwarding correctly. Don't roll your own.

### 6.4 `docker ps --filter ancestor=cc-builder` misses containers

After `docker build`, the image's SHA can change while the tag stays `cc-builder:latest`. Containers running from the *old* SHA don't match `--filter ancestor=cc-builder` even though they have the right tag at the time of run. The result was three concurrent stuck containers we didn't notice because the filter showed nothing.

**Fix:** use `docker ps -a --format '{{.Names}} {{.Image}} {{.Status}}'` to enumerate everything when debugging.

### 6.5 Meshtastic firmware traps SIGTERM and shuts down slowly

`timeout 8` sends SIGTERM after 8s, which the firmware handles with a graceful shutdown sequence (flush NVS, etc.). On Portduino native this can take many seconds — or hang entirely if FSCom is mid-flush. Containers stayed alive long after the 8s deadline.

**Fix for probes:** use `timeout --signal=KILL 8 …` for hard kill, OR `timeout -k 5 8 …` to escalate to KILL after 5s grace. For real e2e tests the firmware should expose a clean-shutdown command via its API.

### 6.6 `apt-get install` requires `apt-get update` first in our base image

The `cc-builder` Dockerfile cleans `/var/lib/apt/lists/*` after install (good for image size). Fresh containers from that image have no apt index; install fails with `Unable to locate package`. Always prefix with `apt-get update -qq`.

---

## 7. Open Questions / Risks

### 7.1 SDL window via XQuartz: works in theory, untested here

The validation stopped at "binary boots and would open an SDL window if a display were attached." We didn't actually install XQuartz and verify that step. It's a well-trodden path (many Linux GUI apps run via `docker + XQuartz` on macOS) but we should run it before claiming it works.

**Action:** as part of `meshtastic-emu` Phase 2, do this for real. If it doesn't work, fall back to Xvfb-in-container + VNC viewer on the host.

### 7.2 X11 forwarding latency

SDL opens an X11 connection per render. Across `host.docker.internal` to XQuartz, each frame goes over a local TCP socket. For a 240×135 OLED display refreshing at low rates this should be fine, but if the firmware does heavy redraws (TFT, animations) latency may show. Worth measuring during Phase 2.

### 7.3 Input injection plumbing: which way?

Three options, in increasing complexity:

1. **stdin protocol:** firmware reads JSON commands from stdin in a side thread, calls `inputBroker->injectInputEvent()`. Simplest. Patches needed: ~30 lines in main.cpp.
2. **TCP/UDP socket:** firmware listens on a port, accepts commands. More flexible (multi-host). Patches: ~100 lines + threading.
3. **Reuse the existing Meshtastic Phone API:** It already accepts simulated input under `SIMULATOR_APP` portnum. Repurpose for keyboard events. Most "upstream-native" but I'm not sure the Phone API covers all `INPUT_BROKER_*` events.

**Recommendation:** start with (1). It's the smallest diff, easiest to upstream eventually, and works under any harness.

### 7.4 Framebuffer readback: SDL flip hook vs. X11 window grab

Either:

1. **Hook `SDL_RenderPresent` / `SDL_UpdateWindowSurface`:** patch `Panel_sdl.cpp` to call a callback that writes the frame to a memory-mapped file or socket. Pixel-perfect, no race conditions.
2. **Read the X11 window from the host:** `xwd -id <window-id> | xwdtopnm | …`, or PIL/Wayland equivalent. No firmware patches needed but timing-dependent (you might catch a half-rendered frame).

(1) is more reliable but costs a firmware patch. (2) is faster to ship.

**Recommendation:** start with (2) for v0.1; switch to (1) if flakiness shows.

### 7.5 `SimRadio` for multi-node mesh — capability unknown

Upstream has `src/platform/portduino/SimRadio.{h,cpp}` for simulating LoRa packets between native instances. We didn't test it. Some questions: does it use TCP loopback or shared memory? Is it bidirectional out of the box? Can two native binaries on the same host see each other automatically, or do they need explicit peer config?

**Action:** during Phase 8 (multi-node test), spend an hour reading `SimRadio` first. If it Just Works, great. If not, may need a small "radio bus" coordinator process.

### 7.6 macOS arm64 vs x86_64 — does it matter?

Our Docker is arm64 (matching M-series Macs). Linux ARM64 packages exist for everything we need. GitHub Actions runners are amd64. So the firmware binary in our local Docker is arm64; the one in CI is amd64. Different binaries, but both built from the same source against the same Portduino. Should be functionally identical.

Caveat: SDL/X11/libulfius runtime versions may differ slightly between Ubuntu's arm64 and amd64 packages (different upstream repos). Probably noise, but worth flagging if a test ever passes locally and fails in CI or vice versa.

---

## 8. References

### 8.1 Files in upstream Meshtastic firmware (paths relative to `meshtastic/firmware`)

| File | Why |
|---|---|
| `variants/native/portduino/platformio.ini` | The four `env:native*` build configs and their flags |
| `src/graphics/Panel_sdl.cpp` | The SDL2 display backend implementation |
| `src/graphics/Panel_sdl.hpp` | Display interface |
| `src/platform/portduino/PortduinoGlue.cpp` | Config file lookup, MAC address handling, display panel selection |
| `src/platform/portduino/PortduinoGlue.h` | `screen_modules` enum + `screen_names` map |
| `src/platform/portduino/SimRadio.{h,cpp}` | Simulated LoRa radio for multi-node mesh testing |
| `bin/config-dist.yaml` | Reference config showing all options |
| `bin/build-native.sh`, `bin/native-install.sh`, `bin/native-run.sh` | Upstream's helper scripts (mostly for buildroot, but instructive) |
| `.github/workflows/test_native.yml` | Upstream CI for the native build |

### 8.2 External docs / projects

- [meshTestic](https://github.com/meshtastic/meshTestic) — TS+mocha framework that drives **physical USB-connected devices**. Different problem.
- [Meshtasticator](https://github.com/GUVWAF/Meshtasticator) — Python LoRa simulator. Models radio behavior including collisions/loss. Could be a complement for network-layer testing.
- [soniccyclone/Meshtastic-Firmware-Friend-Finder-Edition `Dockerfile.native`](https://github.com/soniccyclone/Meshtastic-Firmware-Friend-Finder-Edition/blob/main/Dockerfile.native) — prior art for an apt + pio + Portduino Docker setup. Their dep list is mostly correct but missing a couple of items.
- [Docker + XQuartz on macOS](https://gist.github.com/sorny/969fe55d85c9b0035b0109a31cbcb088) — standard recipe for X11 forwarding.

### 8.3 This repo's relevant files

| File | Why it's relevant |
|---|---|
| `docker/Dockerfile` | Multi-stage pattern to copy |
| `Makefile` | `setup` / `build` / `bump-pin` pattern to copy |
| `meshtastic-firmware-pin` | Pin format to copy |
| `patches/apply.py` | Anchor-substitution patcher (if firmware patches needed for IPC hooks) |
| `.github/workflows/release.yml` | CalVer release pattern to copy |
| `.github/workflows/upstream-drift.yml` | `gh release list --exclude-pre-releases` drift detection |

---

## 9. Decision Point

**Before starting `meshtastic-emu`:** complete the XQuartz step (§4) once on a real Mac and confirm a window opens with the Meshtastic boot screen. That's a 30-minute experiment that converts "very likely possible" into "demonstrated possible."

If it works: scaffold the new repo, copy the build pattern from this one, ship phases 0–4 in the first week.

If XQuartz forwarding doesn't work for some reason we haven't seen: pivot to Xvfb-in-container immediately and use VNC for development viewing. Slightly less ergonomic but bulletproof.

Either way, this repo's PRD/TDD are good prior art for how to do the docs.

---

*This document is the canonical write-up of what we learned from the 2026-05-02 feasibility spike. If you start `meshtastic-emu`, link back to it from that repo's README.*
