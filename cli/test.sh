#!/bin/sh
set -eu
cd "$(dirname "$0")"
./build/hid-switcher --help
./build/hid-switcher --version
invalid() {
  set +e
  ./build/hid-switcher "$@" > /dev/null 2>&1
  result=$?
  set -e
  [ "$result" -eq 2 ] || { echo "Expected argument rejection: $*" >&2; exit 1; }
}
invalid select 0
invalid move 1 4
invalid name 1 ""
invalid wifi bad
invalid status extra
invalid --device invalid status
echo "CLI argument checks passed (no Bluetooth access required)"
