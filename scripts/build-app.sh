#!/bin/zsh
# Package the knit border daemon as a proper menu bar app bundle.
# LSUIElement keeps it out of the Dock. Ad-hoc signing verifies the local
# bundle; it is not Developer ID signing or notarization for distribution.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/outputs/Window Sweaters.app"
STAGE="$(mktemp -d /tmp/window-sweaters-build.XXXXXX)"
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

xattr -cr "$STAGE/Window Sweaters.app"
# Ad-hoc by default. Set SIGN_IDENTITY to a local certificate (see
# `security find-identity -p codesigning`) so Accessibility permission
# survives rebuilds: ad-hoc signatures change with every build.
codesign --force --deep --sign "${SIGN_IDENTITY:--}" "$STAGE/Window Sweaters.app"
codesign --verify --deep --strict "$STAGE/Window Sweaters.app"

mkdir -p "$ROOT/outputs"
rm -rf "$APP"
ditto "$STAGE/Window Sweaters.app" "$APP"
echo "$APP"
