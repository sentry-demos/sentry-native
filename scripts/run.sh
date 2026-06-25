#!/usr/bin/env bash
# Run the Empower Plant Fleet Control Center GUI.
# Requires SENTRY_DSN in the environment for events to reach Sentry.
# Usage: scripts/run.sh [extra args passed to empower-fleet]
set -euo pipefail
cd "$(dirname "$0")/.."

if [ -z "${SENTRY_DSN:-}" ]; then
  echo "warning: SENTRY_DSN is not set - events will not be sent to Sentry" >&2
fi

: "${SENTRY_ENVIRONMENT:=development}"
export SENTRY_ENVIRONMENT

exec ./build/empower-fleet "$@"
