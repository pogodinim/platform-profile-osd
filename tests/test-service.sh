#!/usr/bin/env bash
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
binary=$project_dir/build/platform-profile-osd
config=$project_dir/tests/fixtures/silent.ini
controller=platform-profile-osd-test-session.service
daemon=platform-profile-osd-integration-test.service

cleanup() {
    systemctl --user stop "$daemon" "$controller" >/dev/null 2>&1 || true
    systemctl --user reset-failed "$daemon" "$controller" >/dev/null 2>&1 || true
}
trap cleanup EXIT HUP INT TERM

if systemctl --user is-active --quiet "$daemon" "$controller"; then
    echo 'Refusing to replace an active integration-test unit.' >&2
    exit 1
fi

systemd-run --user --unit="$controller" --collect /usr/bin/sleep infinity >/dev/null
systemd-run --user --unit="$daemon" --collect \
    --property="PartOf=$controller" \
    --property="After=$controller" \
    --property=Restart=on-failure \
    --property=RestartSec=100ms \
    "$binary" --config "$config" >/dev/null

for _ in {1..30}; do
    systemctl --user is-active --quiet "$daemon" && break
    sleep 0.1
done
systemctl --user is-active --quiet "$daemon"
first_pid=$(systemctl --user show "$daemon" --property=MainPID --value)
test "$first_pid" -gt 1

duplicate_output=$("$binary" --config "$config" 2>&1)
printf '%s\n' "$duplicate_output" | grep -q \
    'another daemon instance is already running; exiting'
test "$(systemctl --user show "$daemon" --property=MainPID --value)" = \
    "$first_pid"

systemctl --user kill --kill-whom=main --signal=KILL "$daemon"
second_pid=$first_pid
for _ in {1..50}; do
    sleep 0.1
    second_pid=$(systemctl --user show "$daemon" --property=MainPID --value)
    if [[ "$second_pid" -gt 1 && "$second_pid" != "$first_pid" ]] &&
       systemctl --user is-active --quiet "$daemon"; then
        break
    fi
done
test "$second_pid" -gt 1
test "$second_pid" != "$first_pid"
test "$(systemctl --user show "$daemon" --property=NRestarts --value)" -ge 1

# PartOf models the stop relationship used with graphical-session.target.
systemctl --user stop "$controller"
for _ in {1..30}; do
    systemctl --user is-active --quiet "$daemon" || break
    sleep 0.1
done
if systemctl --user is-active --quiet "$daemon"; then
    echo 'Daemon did not stop with its session controller.' >&2
    exit 1
fi

echo 'Transient user-service start, restart, and PartOf stop tests passed.'
