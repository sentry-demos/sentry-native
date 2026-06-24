#!/usr/bin/env bash
# Configure and build the Empower Plant native demo.
# Usage: scripts/build.sh [Debug|RelWithDebInfo]   (default: RelWithDebInfo)
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_TYPE="${1:-RelWithDebInfo}"
GEN=""
command -v ninja >/dev/null 2>&1 && GEN="-G Ninja"

cmake -B build -S . $GEN -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DEMPOWER_BUILD_GUI=ON
cmake --build build --parallel

echo
echo "Built:"
echo "  build/empower-fleet      - the GUI Fleet Control Center"
echo "  build/empower-headless   - headless autopilot / remote listener"
echo "  build/empower-smoke      - connectivity smoke test"
echo
echo "Set SENTRY_DSN, then run scripts/run.sh"
