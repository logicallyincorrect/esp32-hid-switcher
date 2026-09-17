#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
package_dir=$(mktemp -d)
trap 'rm -rf "$package_dir"' EXIT HUP INT TERM
ditto -x -k companion/build/HID-Switcher-Companion-macOS.zip "$package_dir"
app="$package_dir/HID Switcher Companion.app"
binary="$app/Contents/MacOS/hid-switcher-companion"
plutil -lint "$app/Contents/Info.plist"
test -x "$binary"
lipo "$binary" -verify_arch arm64 x86_64
codesign --verify --all-architectures --deep --strict "$app"
"$binary" check
printf '%s\n' 'Package verified. This does not establish Gatekeeper approval or live BLE behavior.'
