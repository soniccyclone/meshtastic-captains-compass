#!/bin/bash
# Captain's Compass build entrypoint.
# See docs/tdd-issue-002-captains-compass.md §4.3.
#
# Reset the firmware tree, apply our overlay + patches, run pio, emit
# firmware.uf2. Idempotent: any partial state from a previous run is
# nuked by the git reset before re-applying.

set -euo pipefail
cd /firmware-src

echo "=== Reset firmware tree ==="
git reset --hard HEAD
git clean -fdx -e .pio   # keep pio build cache; drop everything else

echo "=== Apply overlay (new files) ==="
rsync -a /overlay/ /firmware-src/

echo "=== Apply patches (modifications) ==="
python3 /patches/apply.py

echo "=== Regenerate nanopb protos ==="
# Equivalent to upstream's bin/regen-protos.sh, but uses system protoc +
# pip-installed nanopb (which runs natively on arm64) instead of upstream's
# x86-only nanopb-0.4.9 prebuilt binary. Regenerates ALL .pb.h/.pb.cpp from
# .proto sources so portnums.proto patch + compass.proto overlay both
# propagate; other generated files remain functionally identical.
# NANOPB_PROTO_INCLUDE is set in the Dockerfile and points at nanopb's
# bundled google/protobuf/*.proto well-known files.
( cd protobufs && \
    protoc --experimental_allow_proto3_optional \
           --nanopb_out="-S.cpp:../src/mesh/generated/" \
           -I=. \
           -I="${NANOPB_PROTO_INCLUDE}" \
           meshtastic/*.proto )

echo "=== Build heltec-mesh-node-t114 ==="
pio run --environment heltec-mesh-node-t114

echo "=== Emit artifact ==="
# Meshtastic's custom build emits firmware-heltec-mesh-node-t114-<ver>-<sha>.uf2.
# Glob it and copy to a stable name; also preserve the versioned name for traceability.
mkdir -p /output
shopt -s nullglob
UF2_FILES=(.pio/build/heltec-mesh-node-t114/firmware-heltec-mesh-node-t114-*.uf2)
if [[ ${#UF2_FILES[@]} -ne 1 ]]; then
  echo "ERROR: expected exactly 1 firmware UF2, found ${#UF2_FILES[@]}: ${UF2_FILES[*]}" >&2
  exit 1
fi
cp "${UF2_FILES[0]}" /output/firmware.uf2
cp "${UF2_FILES[0]}" "/output/$(basename "${UF2_FILES[0]}")"
echo "Done: /output/firmware.uf2"
echo "      /output/$(basename "${UF2_FILES[0]}")"
