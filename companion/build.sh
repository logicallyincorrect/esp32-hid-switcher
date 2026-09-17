#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
app="companion/build/HID Switcher Companion.app"
mkdir -p "$app/Contents/MacOS"
cp companion/Info.plist "$app/Contents/Info.plist"
for arch in arm64 x86_64; do
  swiftc -swift-version 5 -target "$arch-apple-macosx13.0" -O -framework AppKit -framework CoreBluetooth -framework ApplicationServices \
    companion/Sources/Geometry.swift companion/Sources/MenuBarApp.swift companion/Sources/main.swift \
    -o "companion/build/hid-switcher-companion-$arch"
done
lipo -create companion/build/hid-switcher-companion-arm64 companion/build/hid-switcher-companion-x86_64 \
  -output "$app/Contents/MacOS/hid-switcher-companion"
chmod 755 "$app/Contents/MacOS/hid-switcher-companion"
codesign --force --sign - "$app"
printf '%s\n' "$app/Contents/MacOS/hid-switcher-companion"
archive="companion/build/HID-Switcher-Companion-macOS.zip"
ditto -c -k --sequesterRsrc --keepParent "$app" "$archive"
printf '%s\n' "$archive"
