FROM mcr.microsoft.com/devcontainers/cpp:2-debian-13

# System deps for PlatformIO + nRF52 toolchain + USB flashing
RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 python3-pip python3-venv \
    git wget curl zip unzip \
    libusb-1.0-0-dev libudev-dev \
    udev \
    && rm -rf /var/lib/apt/lists/*

# PlatformIO and nRF52 tools in a venv to avoid Debian 13 pip conflicts
RUN python3 -m venv /opt/pio-venv && \
    /opt/pio-venv/bin/pip install --no-cache-dir \
        platformio==6.1.19 \
        adafruit-nrfutil \
        esptool \
        nanopb==0.4.9.1 \
        grpcio-tools

ENV PATH="/opt/pio-venv/bin:${PATH}"
ENV PLATFORMIO_CORE_DIR=/opt/pio/core
ENV PLATFORMIO_SETTING_ENABLE_TELEMETRY=No

# Pre-warm the nRF52 platform so first `pio run` doesn't download everything
RUN pio platform install nordicnrf52 && \
    pio platform install espressif32
