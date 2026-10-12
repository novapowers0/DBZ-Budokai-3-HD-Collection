#!/usr/bin/env bash
set -euo pipefail

# Packages a macOS build into a self-contained "DBZ Budokai 3.app":
#   Contents/MacOS/      dbz3 + librexruntime.dylib + librexgpu-xenos.dylib
#   Contents/Resources/  AppIcon.icns, gamecontrollerdb.txt, vulkan/ (MoltenVK)
# Inside a bundle the game keeps settings, saves, mods, logs and the game data
# (the .iso, or default.xex + us/ / eu/) in ~/Library/Application Support/DBZ
# Budokai 3 (src/portable_root.h), so the .app can live in /Applications.
# Usage: bash tools/macos/make_app.sh [build_dir] [output.app]
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
build="${1:-$repo/out/build/mac-arm64-release}"
app="${2:-$repo/out/DBZ Budokai 3.app}"

test -x "$build/dbz3" || { echo "missing $build/dbz3 (build mac-arm64-release first)" >&2; exit 1; }
test -d "$build/vulkan" || { echo "missing $build/vulkan (MoltenVK staged by the SDK)" >&2; exit 1; }

rm -rf "$app"
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
cp "$build/dbz3" "$build/librexruntime.dylib" "$build/librexgpu-xenos.dylib" "$app/Contents/MacOS/"
cp "$build/gamecontrollerdb.txt" "$app/Contents/Resources/"
cp -R "$build/vulkan" "$app/Contents/Resources/vulkan"

# Icon: drawn by make_icon.swift, then every size macOS asks for.
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
swift "$here/make_icon.swift" "$work/icon.png" >/dev/null
mkdir "$work/AppIcon.iconset"
for size in 16 32 128 256 512; do
  sips -z "$size" "$size" "$work/icon.png" --out "$work/AppIcon.iconset/icon_${size}x${size}.png" >/dev/null
  double=$((size * 2))
  sips -z "$double" "$double" "$work/icon.png" --out "$work/AppIcon.iconset/icon_${size}x${size}@2x.png" >/dev/null
done
iconutil -c icns "$work/AppIcon.iconset" -o "$app/Contents/Resources/AppIcon.icns"

# Single source of truth for the version: src/version.rc (DBZ3_VERSION_STR).
version=$(grep -oE 'DBZ3_VERSION_STR[[:space:]]+"[^"]+"' "$repo/src/version.rc" \
  | head -n1 | sed -E 's/.*"([^"]+)".*/\1/')
cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleExecutable</key><string>dbz3</string>
  <key>CFBundleIdentifier</key><string>io.github.novapowers0.dbz3</string>
  <key>CFBundleName</key><string>DBZ Budokai 3</string>
  <key>CFBundleDisplayName</key><string>DBZ Budokai 3</string>
  <key>CFBundleIconFile</key><string>AppIcon</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>${version}</string>
  <key>CFBundleVersion</key><string>${version}</string>
  <key>LSMinimumSystemVersion</key><string>13.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.action-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>GCSupportsControllerUserInteraction</key><true/>
</dict>
</plist>
PLIST

# Ad-hoc signature for the whole bundle (the binaries are already linker-signed).
codesign --force --deep --sign - "$app" 2>&1 | grep -v "replacing existing signature" || true
codesign --verify --deep "$app"
echo "$app"
