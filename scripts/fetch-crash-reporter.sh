#!/usr/bin/env bash
# Downloads the official Sentry desktop crash reporter and places it next to the
# built binaries, together with the Empower Plant theming (appsettings.json).
#
# The Fleet Control Center auto-detects this reporter at startup and wires it up
# via sentry_options_set_external_crash_reporter_path().
#
# Usage: scripts/fetch-crash-reporter.sh <dest-dir> [version]
# Requires the GitHub CLI (gh) to be authenticated.
set -euo pipefail

DEST="${1:?usage: fetch-crash-reporter.sh <dest-dir> [version]}"
VERSION="${2:-0.3.2}"
REPO="getsentry/sentry-desktop-crash-reporter"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Map the host platform to the release-asset RID.
case "$(uname -s)" in
  Linux*)  RID="linux-x64";  ARCHIVE="tar.gz" ;;
  Darwin*) RID="osx-arm64";  ARCHIVE="tar.gz" ;;
  MINGW*|MSYS*|CYGWIN*) RID="win-x64"; ARCHIVE="zip" ;;
  *) echo "unsupported platform: $(uname -s)"; exit 1 ;;
esac

mkdir -p "$DEST"
echo "Downloading $REPO $VERSION ($RID)..."
gh release download "$VERSION" --repo "$REPO" --pattern "*${RID}*" --dir "$DEST" --clobber

# Extract whatever was downloaded.
shopt -s nullglob
for f in "$DEST"/*."$ARCHIVE"; do
  case "$f" in
    *.tar.gz) tar -xzf "$f" -C "$DEST" ;;
    *.zip)    unzip -o "$f" -d "$DEST" ;;
  esac
  rm -f "$f"
done

# Drop the Empower Plant theming next to the reporter.
cp "$ROOT/assets/crash-reporter/appsettings.json" "$DEST/appsettings.json"
echo "Crash reporter ready in $DEST"
