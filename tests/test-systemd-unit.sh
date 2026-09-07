#!/bin/sh
set -eu

binary=${1:?binary path required}
unit=${2:?systemd unit path required}
binary_dir=$(CDPATH= cd -- "$(dirname -- "$binary")" && pwd -P)
unit_dir=$(CDPATH= cd -- "$(dirname -- "$unit")" && pwd -P)
binary=$binary_dir/$(basename -- "$binary")
unit=$unit_dir/$(basename -- "$unit")
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT HUP INT TERM
test_binary=$test_root/platform-profile-osd
test_unit=$test_root/platform-profile-osd.service

test -x "$binary"
grep -q '^ExecStart=%h/.local/bin/platform-profile-osd$' "$unit"
ln -s "$binary" "$test_binary"
sed "s|^ExecStart=.*$|ExecStart=$test_binary|" "$unit" >"$test_unit"
systemd-analyze verify "$test_unit"
