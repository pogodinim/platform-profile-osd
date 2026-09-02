#!/bin/sh
set -eu

binary=${1:?binary path required}
script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
test_root=$(mktemp -d)
trap 'rm -rf -- "$test_root"' EXIT HUP INT TERM

mkdir -p "$test_root/sysfs" "$test_root/config/platform-profile-osd" \
    "$test_root/runtime" "$test_root/fake-bin" "$test_root/empty-bin" \
    "$test_root/audio dir"
printf '%s\n' 'future-ultra' >"$test_root/sysfs/platform_profile"
printf '%s\n' 'quiet balanced future-ultra' >"$test_root/sysfs/platform_profile_choices"

profile=$(PLATFORM_PROFILE_OSD_SYSFS_DIR="$test_root/sysfs" \
    XDG_CONFIG_HOME="$test_root/config" "$binary" --print-profile)
[ "$profile" = 'future-ultra' ]

choices=$(PLATFORM_PROFILE_OSD_SYSFS_DIR="$test_root/sysfs" \
    XDG_CONFIG_HOME="$test_root/config" "$binary" --print-choices)
[ "$choices" = 'quiet balanced future-ultra' ]

version=$("$binary" --version)
[ "$version" = 'platform-profile-osd 0.1.0' ]

"$binary" --help | grep -q -- '--no-sound'

if "$binary" --print-profile --print-choices >/dev/null 2>&1; then
    echo 'conflicting actions unexpectedly succeeded' >&2
    exit 1
fi

# The daemon lock must reject a second watcher without blocking CLI actions.
if command -v flock >/dev/null 2>&1; then
    (
        flock -n 9

        locked_profile=$(PLATFORM_PROFILE_OSD_SYSFS_DIR="$test_root/sysfs" \
            XDG_CONFIG_HOME="$test_root/config" \
            XDG_RUNTIME_DIR="$test_root/runtime" \
            "$binary" --print-profile)
        [ "$locked_profile" = 'future-ultra' ]

        duplicate_output=$(PLATFORM_PROFILE_OSD_SYSFS_DIR="$test_root/sysfs" \
            XDG_CONFIG_HOME="$test_root/config" \
            XDG_RUNTIME_DIR="$test_root/runtime" \
            "$binary" --no-sound 2>&1)
        printf '%s\n' "$duplicate_output" | grep -q \
            'another daemon instance is already running; exiting'
    ) 9>"$test_root/runtime/platform-profile-osd.lock"
fi

# Playback receives one literal path argument, including spaces and shell
# metacharacters. The fake backend avoids depending on a live audio server.
space_sound="$test_root/audio dir/quiet tone;not-a-command.wav"
space_config="$test_root/config with spaces.ini"
touch "$space_sound"
printf '%s\n' \
    '#!/bin/sh' \
    '[ "$#" -eq 2 ]' \
    '[ "$1" = -- ]' \
    '[ "$2" = "$EXPECTED_SOUND" ]' \
    >"$test_root/fake-bin/pw-play"
chmod 0755 "$test_root/fake-bin/pw-play"
printf '%s\n' \
    '[general]' \
    'notifications_enabled = false' \
    'sound_enabled = true' \
    "sound_directory = $test_root/audio dir" \
    '[sounds]' \
    'quiet = quiet tone;not-a-command.wav' \
    >"$space_config"

sound_output=$(EXPECTED_SOUND="$space_sound" PATH="$test_root/fake-bin" \
    "$binary" --config "$space_config" --test-sound quiet)
printf '%s\n' "$sound_output" | grep -q "Started sound mapped to 'quiet'."

if PATH="$test_root/empty-bin" \
    "$binary" --config "$space_config" --test-sound quiet >/dev/null 2>&1; then
    echo 'missing pw-play unexpectedly succeeded' >&2
    exit 1
fi

if "$binary" --config "$script_dir/fixtures/missing-sound.ini" \
    --test-sound quiet >/dev/null 2>&1; then
    echo 'missing mapped sound unexpectedly succeeded' >&2
    exit 1
fi

echo 'All CLI tests passed.'
