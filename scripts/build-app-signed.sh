#!/bin/zsh
# Personal alternative to scripts/build-app.sh: builds and signs Window
# Sweaters with a real Apple Developer ID identity (hardened runtime
# enabled), and optionally notarizes + staples it. Use this instead of
# clicking through Gatekeeper's "Open Anyway" for a build you'll actually
# keep running.
#
# Do NOT hardcode your identity or Apple ID credentials here or in any
# committed file. Pass them as environment variables per-invocation, and
# store notarization credentials in the Keychain (see below), never in a
# plaintext file, .env, or shell history you might commit.
#
# Usage:
#   CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
#     ./scripts/build-app-signed.sh
#
#   # Also notarize + staple (requires a keychain profile stored once, see below):
#   CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)" \
#   NOTARY_PROFILE="window-sweaters-notary" \
#     ./scripts/build-app-signed.sh
#
# Find your identity:
#   security find-identity -v -p codesigning
#
# Store notarization credentials once (goes to Keychain, not a file):
#   xcrun notarytool store-credentials "window-sweaters-notary" \
#     --apple-id "you@example.com" --team-id "TEAMID" --password "app-specific-password"
set -euo pipefail

if [[ -z "${CODESIGN_IDENTITY:-}" ]]; then
  echo "error: set CODESIGN_IDENTITY to a 'Developer ID Application: ...' identity." >&2
  echo "       list yours with: security find-identity -v -p codesigning" >&2
  exit 1
fi
if [[ "$CODESIGN_IDENTITY" != "Developer ID Application: "* ]]; then
  echo "error: CODESIGN_IDENTITY must start with 'Developer ID Application: '." >&2
  echo "       got: '$CODESIGN_IDENTITY'" >&2
  echo "       list yours with: security find-identity -v -p codesigning" >&2
  exit 1
fi

# No .env file here on purpose: CODESIGN_IDENTITY is not a secret (it is a
# public certificate name), but a checked-in or sourced .env would encourage
# putting the *actual* secret — an Apple ID app-specific password — in a
# plaintext file too. Pass these as one-off environment variables instead;
# put NOTARY_PROFILE's credentials in the Keychain via `notarytool
# store-credentials`, never in a file.

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/outputs/Window Sweaters.app"
STAGE="$(mktemp -d /tmp/window-sweaters-signed-build.XXXXXX)"
trap 'rm -rf "$STAGE"' EXIT

make -C "$ROOT" >/dev/null

STAGED="$STAGE/Window Sweaters.app/Contents"
mkdir -p "$STAGED/MacOS"
cp -f "$ROOT/bin/borders"     "$STAGED/MacOS/WindowSweaters"
cp -f "$ROOT/AppInfo.plist"   "$STAGED/Info.plist"
mkdir -p "$STAGED/Resources" "$STAGE/AppIcon.iconset"
clang -O2 -fobjc-arc -I"$ROOT/src" "$ROOT/scripts/build-icon.m" -framework Cocoa -o "$STAGE/build-icon"
"$STAGE/build-icon" "$STAGE/AppIcon.iconset"
iconutil -c icns "$STAGE/AppIcon.iconset" -o "$STAGED/Resources/AppIcon.icns"
chmod +x "$STAGED/MacOS/WindowSweaters"

# Clear inherited extended attributes from the freshly staged build (build
# hygiene, not a Gatekeeper bypass) before applying the real signature.
xattr -cr "$STAGE/Window Sweaters.app"
codesign --force --deep --options runtime --timestamp \
  --sign "$CODESIGN_IDENTITY" "$STAGE/Window Sweaters.app"
codesign --verify --deep --strict "$STAGE/Window Sweaters.app"
echo "--- Gatekeeper assessment (expected to fail until notarized) ---"
spctl --assess --type execute -v "$STAGE/Window Sweaters.app" || true

mkdir -p "$ROOT/outputs"
rm -rf "$APP"
ditto "$STAGE/Window Sweaters.app" "$APP"
echo "Signed with Developer ID: $APP"

if [[ -n "${NOTARY_PROFILE:-}" ]]; then
  ZIP="$STAGE/Window Sweaters.zip"
  ditto -c -k --keepParent "$APP" "$ZIP"
  echo "Submitting for notarization (this can take a few minutes)..."
  xcrun notarytool submit "$ZIP" --keychain-profile "$NOTARY_PROFILE" --wait
  xcrun stapler staple "$APP"
  xcrun stapler validate "$APP"
  echo "Notarized and stapled: $APP"
else
  echo "NOTARY_PROFILE not set — signed with your Developer ID but not notarized."
  echo "Gatekeeper will still warn until this is notarized and stapled."
fi
