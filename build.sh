#!/bin/bash
# Thin wrapper for `make build` — see docs/tdd-issue-002-captains-compass.md §4.4.
set -euo pipefail
cd "$(dirname "$0")"
exec make build
