#!/usr/bin/env bash
set -e

echo "[+] Initializing git submodules..."
git submodule update --init --recursive

# If meshtastic/firmware is checked out as a submodule or in-tree,
# install its Python requirements
if [ -f "firmware/requirements.txt" ]; then
    echo "[+] Installing firmware Python deps..."
    /opt/pio-venv/bin/pip install --no-cache-dir -r firmware/requirements.txt
fi

if [ -f "requirements.txt" ]; then
    echo "[+] Installing project Python deps..."
    /opt/pio-venv/bin/pip install --no-cache-dir -r requirements.txt
fi

echo "[DONE] Devcontainer setup complete."
