#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
app="companion/build/HID Switcher Companion.app"
mkdir -p "$app/Contents/MacOS"
cp companion/Info.plist "$app/Contents/Info.plist"
swiftc -swift-version 5 -target "$(uname -m)-apple-macosx13.0" -O -framework AppKit -framework CoreBluetooth -framework ApplicationServices \
  companion/Sources/Geometry.swift companion/Sources/MenuBarApp.swift companion/Sources/main.swift \
  -o "$app/Contents/MacOS/hid-switcher-companion"
codesign --force --sign - "$app"
printf '%s\n' "$app/Contents/MacOS/hid-switcher-companion"
