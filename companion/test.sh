#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT HUP INT TERM
swiftc -swift-version 5 companion/Sources/Geometry.swift companion/Tests/main.swift -o "$dir/tests"
"$dir/tests"
