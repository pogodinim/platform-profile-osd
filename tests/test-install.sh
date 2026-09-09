#!/usr/bin/env bash
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

# Temporary home directories do not isolate systemctl from the real user bus.
# Permit diagnostics, but record and reject any attempted manager mutation.
export PPO_TEST_REAL_SYSTEMCTL
PPO_TEST_REAL_SYSTEMCTL=$(command -v systemctl || true)
export PPO_TEST_MUTATIONS=$test_root/systemctl-mutations
mkdir -p "$test_root/bin"
: > "$PPO_TEST_MUTATIONS"
cat > "$test_root/bin/systemctl" <<'EOF'
#!/usr/bin/env bash
case "$*" in
    '--user is-enabled platform-profile-osd.service'|\
    '--user is-active platform-profile-osd.service'|\
    '--user show platform-profile-osd.service --property=FragmentPath --value')
        [[ -n "$PPO_TEST_REAL_SYSTEMCTL" ]] || exit 1
        exec "$PPO_TEST_REAL_SYSTEMCTL" "$@"
        ;;
    *)
        printf '%s\n' "$*" >> "$PPO_TEST_MUTATIONS"
        echo 'Staged test refused a service-manager mutation.' >&2
        exit 99
        ;;
esac
EOF
chmod 0755 "$test_root/bin/systemctl"
export PATH="$test_root/bin:$PATH"

export HOME=$test_root/home
export XDG_CONFIG_HOME=$test_root/config
export XDG_DATA_HOME=$test_root/data
mkdir -p "$HOME" "$XDG_CONFIG_HOME" "$XDG_DATA_HOME"
mkdir -p "$XDG_CONFIG_HOME/platform-profile-osd"
cp "$project_dir/tests/fixtures/sound-enabled.ini" \
    "$XDG_CONFIG_HOME/platform-profile-osd/config.ini"

install_output=$("$project_dir/install.sh" --method=desktop)
printf '%s\n' "$install_output" | grep -q 'Currently installed runtime audio:'
printf '%s\n' "$install_output" | grep -q 'Bundled sounds to install:'
printf '%s\n' "$install_output" | grep -q 'Audio after installation:'
printf '%s\n' "$install_output" | grep -Eq 'quiet\.wav: +missing'
if printf '%s\n' "$install_output" | grep -q 'Audio feedback:'; then
    echo 'pre-install summary included a confusing runtime audio verdict' >&2
    exit 1
fi
test "$(printf '%s\n' "$install_output" | grep -cE \
    '    (quiet|balanced|performance)\.wav: available')" -eq 3

# A staged unit must not let a desktop-method reinstall stop the live unit.
mkdir -p "$XDG_CONFIG_HOME/systemd/user"
cp "$project_dir/systemd/platform-profile-osd.service" \
    "$XDG_CONFIG_HOME/systemd/user/platform-profile-osd.service"
"$project_dir/install.sh" --method=desktop >/dev/null
test ! -e "$XDG_CONFIG_HOME/systemd/user/platform-profile-osd.service"
test ! -s "$PPO_TEST_MUTATIONS"

test -x "$HOME/.local/bin/platform-profile-osd"
test -f "$XDG_CONFIG_HOME/autostart/platform-profile-osd.desktop"
cmp "$project_dir/sounds/quiet.wav" \
    "$XDG_DATA_HOME/platform-profile-osd/sounds/quiet.wav"
cmp "$project_dir/sounds/balanced.wav" \
    "$XDG_DATA_HOME/platform-profile-osd/sounds/balanced.wav"
cmp "$project_dir/sounds/performance.wav" \
    "$XDG_DATA_HOME/platform-profile-osd/sounds/performance.wav"

touch "$XDG_DATA_HOME/platform-profile-osd/user-file"

"$project_dir/uninstall.sh"
test ! -s "$PPO_TEST_MUTATIONS"
test ! -e "$HOME/.local/bin/platform-profile-osd"
test ! -e "$XDG_DATA_HOME/platform-profile-osd/sounds/quiet.wav"
test -e "$XDG_DATA_HOME/platform-profile-osd/user-file"
test -e "$XDG_CONFIG_HOME/platform-profile-osd/config.ini"

"$project_dir/uninstall.sh" --remove-config
test ! -s "$PPO_TEST_MUTATIONS"
test ! -e "$XDG_CONFIG_HOME/platform-profile-osd/config.ini"
test -e "$XDG_DATA_HOME/platform-profile-osd/user-file"

echo 'Installer/uninstaller integration tests passed.'
