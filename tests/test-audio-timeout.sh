#!/bin/sh
set -eu
binary=${1:?test binary required}
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
"$binary" "$test_root"
