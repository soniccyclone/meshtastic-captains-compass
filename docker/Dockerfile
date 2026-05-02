# Captain's Compass — multi-stage T114 firmware builder.
# See docs/tdd-issue-002-captains-compass.md §4.2 for the design.
#
# Host architecture is intentionally not pinned: arm64 on Apple Silicon dev
# machines, amd64 on ubuntu-latest GHA runners. Both cross-compile to ARM
# Cortex-M4F via arm-none-eabi-gcc, so the resulting firmware.uf2 is the
# same regardless of where it was built. Do NOT add --platform=linux/amd64.

# ---------------------------------------------------------------------------
# Stage 1: toolchain — apt + python venv + platformio. Invalidates rarely.
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS toolchain
ENV DEBIAN_FRONTEND=noninteractive
ENV PATH="/opt/pio-venv/bin:${PATH}"
RUN apt-get update && apt-get install -y --no-install-recommends \
      git python3 python3-pip python3-venv ca-certificates curl \
      libusb-1.0-0 rsync \
 && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv /opt/pio-venv \
 && /opt/pio-venv/bin/pip install --upgrade pip \
 && /opt/pio-venv/bin/pip install platformio

# ---------------------------------------------------------------------------
# Stage 2: pinned firmware — clone + checkout SHA + install pio platform.
# Invalidates ONLY when FIRMWARE_SHA changes.
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# Stage 3: builder — overlay + patcher + entrypoint.
# Invalidates on every iteration of our patches.
# ---------------------------------------------------------------------------
FROM firmware AS builder
COPY firmware-overlay/         /overlay/
COPY patches/                  /patches/
COPY docker/entrypoint.sh      /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh
WORKDIR /firmware-src
ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
