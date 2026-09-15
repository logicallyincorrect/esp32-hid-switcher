#!/bin/sh
set -eu
cd "$(dirname "$0")"
mkdir -p build
cp Info.plist build/Info.plist
if [ -n "${CLI_VERSION:-}" ]; then
  case "$CLI_VERSION" in *[!A-Za-z0-9._+-]*) echo "Invalid CLI_VERSION" >&2; exit 2;; esac
  /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $CLI_VERSION" build/Info.plist
fi
swiftc -O hid-switcher.swift -o build/hid-switcher -framework CoreBluetooth \
  -Xlinker -sectcreate -Xlinker __TEXT -Xlinker __info_plist -Xlinker "$PWD/build/Info.plist"
codesign --force --sign - --identifier local.hid-switcher.cli build/hid-switcher
codesign --verify --strict build/hid-switcher
