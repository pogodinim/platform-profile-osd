#!/usr/bin/env bash
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

# Never contact the real service manager, even when checking broken code.
mkdir -p "$test_root/bin" "$test_root/live"
touch "$test_root/live/platform-profile-osd.service"
cat > "$test_root/bin/systemctl" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$PPO_TEST_SYSTEMCTL_LOG"
case "$*" in
    '--user show platform-profile-osd.service --property=FragmentPath --value')
        [[ "${PPO_TEST_MANAGER_UNAVAILABLE:-0}" = 0 ]] || exit 1
        printf '%s\n' "$PPO_TEST_FRAGMENT_PATH"
        ;;
    '--user disable --now platform-profile-osd.service')
        [[ "${PPO_TEST_STOP_FAILS:-0}" = 0 ]] || exit 42
        ;;
    '--user daemon-reload') ;;
    *) exit 99 ;;
esac
EOF
chmod 0755 "$test_root/bin/systemctl"

new_case() {
    case_dir=$test_root/$1
    service=$case_dir/config/systemd/user/platform-profile-osd.service
    mkdir -p "$case_dir/home/.local/bin" "$case_dir/config/systemd/user" \
        "$case_dir/config/platform-profile-osd" \
        "$case_dir/config/autostart" "$case_dir/data/platform-profile-osd/sounds"
    touch "$case_dir/home/.local/bin/platform-profile-osd" \
        "$case_dir/config/platform-profile-osd/config.ini" \
        "$case_dir/config/autostart/platform-profile-osd.desktop" \
        "$case_dir/data/platform-profile-osd/sounds/quiet.wav" \
        "$case_dir/data/platform-profile-osd/user-file"
    : > "$case_dir/systemctl.log"
    fragment=$test_root/live/platform-profile-osd.service
    stop_fails=0
    manager_unavailable=0
}

uninstall() {
    env HOME="$case_dir/home" XDG_CONFIG_HOME="$case_dir/config" \
        XDG_DATA_HOME="$case_dir/data" PATH="$test_root/bin:$PATH" \
        PPO_TEST_SYSTEMCTL_LOG="$case_dir/systemctl.log" \
        PPO_TEST_FRAGMENT_PATH="$fragment" PPO_TEST_STOP_FAILS="$stop_fails" \
        PPO_TEST_MANAGER_UNAVAILABLE="$manager_unavailable" \
        bash "$project_dir/uninstall.sh" "$@" >/dev/null
}

expect_removed() {
    test ! -e "$case_dir/home/.local/bin/platform-profile-osd"
    test ! -e "$service"
    test ! -e "$case_dir/config/autostart/platform-profile-osd.desktop"
    test ! -e "$case_dir/data/platform-profile-osd/sounds/quiet.wav"
    test -e "$case_dir/config/platform-profile-osd/config.ini"
    test -e "$case_dir/data/platform-profile-osd/user-file"
}

show_call='--user show platform-profile-osd.service --property=FragmentPath --value'

new_case desktop
uninstall
expect_removed
if [[ -s "$case_dir/systemctl.log" ]]; then
    echo 'FAIL: uninstall without a service file contacted the service manager.' >&2
    exit 1
fi
uninstall --remove-config
test ! -e "$case_dir/config/platform-profile-osd/config.ini"
test -e "$case_dir/data/platform-profile-osd/user-file"

new_case unrelated-unit
touch "$service"
uninstall
expect_removed
printf '%s\n' "$show_call" | cmp - "$case_dir/systemctl.log"

new_case owned-unit
touch "$service"
fragment=$service
uninstall
expect_removed
printf '%s\n' "$show_call" \
    '--user disable --now platform-profile-osd.service' \
    '--user daemon-reload' | cmp - "$case_dir/systemctl.log"

new_case linked-unit
touch "$service"
ln -s "$service" "$case_dir/loaded.service"
fragment=$case_dir/loaded.service
uninstall
expect_removed
printf '%s\n' "$show_call" \
    '--user disable --now platform-profile-osd.service' \
    '--user daemon-reload' | cmp - "$case_dir/systemctl.log"

new_case unavailable-manager
touch "$service"
manager_unavailable=1
uninstall
expect_removed
printf '%s\n' "$show_call" | cmp - "$case_dir/systemctl.log"

new_case failed-stop
touch "$service"
fragment=$service
stop_fails=1
if uninstall; then
    echo 'FAIL: uninstall succeeded after its service could not be stopped.' >&2
    exit 1
fi
test -e "$service"
test -e "$case_dir/home/.local/bin/platform-profile-osd"
test -e "$case_dir/data/platform-profile-osd/sounds/quiet.wav"
printf '%s\n' "$show_call" \
    '--user disable --now platform-profile-osd.service' | cmp - "$case_dir/systemctl.log"

echo 'Uninstaller service ownership and preservation tests passed.'
