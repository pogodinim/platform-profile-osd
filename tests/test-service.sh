#!/usr/bin/env bash
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
binary=$project_dir/build/platform-profile-osd
config=$project_dir/tests/fixtures/silent.ini

test -x "$binary"
test -r "$config"
for command in systemctl systemd-run flock timeout; do
    command -v "$command" >/dev/null
done
# Fail before creating anything if the user service manager is unavailable.
systemctl --user list-units --no-pager >/dev/null

test_root=$(mktemp -d "${TMPDIR:-/tmp}/platform-profile-osd-service.XXXXXXXX")
controller=platform-profile-osd-test-${test_root##*.}-session.service
daemon=platform-profile-osd-test-${test_root##*.}-daemon.service
created_units=()
stage=setup

cleanup() {
    local status=$?
    if (( status != 0 )); then
        printf 'Service test failed during %s (exit %s).\n' "$stage" "$status" >&2
        journalctl --user -u "$daemon" -n 30 --no-pager >&2 || true
    fi
    if (( ${#created_units[@]} > 0 )); then
        # --collect may already have unloaded a stopped unit.
        if ! systemctl --user stop "${created_units[@]}" >/dev/null 2>&1; then
            local unit state
            for unit in "${created_units[@]}"; do
                state=$(systemctl --user show "$unit" --property=ActiveState --value) || return 1
                if [[ "$state" != inactive && "$state" != failed ]]; then
                    printf 'Could not stop %s; preserving %s.\n' "$unit" "$test_root" >&2
                    return 1
                fi
            done
        fi
        systemctl --user reset-failed "${created_units[@]}" >/dev/null 2>&1 || true
    fi
    rm -rf -- "$test_root"
    return "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

mkdir -m 700 "$test_root/runtime"
lock=$test_root/runtime/platform-profile-osd.lock

wait_for_daemon() {
    local previous_pid=${1:-0} pid lock_status
    for _ in {1..50}; do
        pid=$(systemctl --user show "$daemon" --property=MainPID --value)
        if [[ "$pid" =~ ^[0-9]+$ && "$pid" -gt 1 && "$pid" != "$previous_pid" ]] &&
           systemctl --user is-active --quiet "$daemon" && [[ -f "$lock" ]]; then
            # Type=simple can be active before the process has taken its lock.
            if flock --nonblock --conflict-exit-code=75 "$lock" true; then
                :
            else
                lock_status=$?
                if (( lock_status == 75 )); then
                    printf '%s\n' "$pid"
                    return 0
                fi
                return "$lock_status"
            fi
        fi
        sleep 0.1
    done
    echo 'Timed out waiting for an active daemon holding its test lock.' >&2
    return 1
}

stage=start
systemd-run --user --unit="$controller" --collect /usr/bin/sleep infinity >/dev/null
created_units+=("$controller")
systemd-run --user --unit="$daemon" --collect \
    --property="PartOf=$controller" \
    --property="After=$controller" \
    --property=Restart=on-failure \
    --property=RestartSec=100ms \
    --setenv="XDG_RUNTIME_DIR=$test_root/runtime" \
    "$binary" --config "$config" >/dev/null
created_units+=("$daemon")

first_pid=$(wait_for_daemon)
printf 'Start passed (PID %s, isolated lock).\n' "$first_pid"

stage=duplicate-rejection
# Share only the test daemon's lock, and bound a broken duplicate check.
duplicate_output=$(XDG_RUNTIME_DIR="$test_root/runtime" \
    timeout 5s "$binary" --config "$config" 2>&1)
printf '%s\n' "$duplicate_output" | grep -q \
    'another daemon instance is already running; exiting'
test "$(systemctl --user show "$daemon" --property=MainPID --value)" = \
    "$first_pid"
echo 'Duplicate daemon rejection passed.'

stage=restart
systemctl --user kill --kill-whom=main --signal=KILL "$daemon"
second_pid=$(wait_for_daemon "$first_pid")
test "$(systemctl --user show "$daemon" --property=NRestarts --value)" -ge 1
printf 'Restart after SIGKILL passed (PID %s -> %s).\n' "$first_pid" "$second_pid"

# PartOf models the stop relationship used with graphical-session.target.
stage=PartOf-stop
systemctl --user stop "$controller"
for _ in {1..30}; do
    state=$(systemctl --user show "$daemon" --property=ActiveState --value)
    [[ "$state" = inactive ]] && break
    sleep 0.1
done
if [[ "$state" != inactive ]]; then
    echo 'Daemon did not stop with its session controller.' >&2
    exit 1
fi
flock --nonblock "$lock" true
echo 'PartOf stop and lock release passed.'

echo 'Transient user-service start, restart, and PartOf stop tests passed.'
