#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM
for source in tests/*.cpp; do
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Isrc "$source" -o "$test_dir/test"
  "$test_dir/test"
done
python3 tests/hid-device-lookup.py
echo "Host firmware tests passed"
