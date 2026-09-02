#!/usr/bin/env bash
set -euo pipefail

project_dir=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
method=auto

usage() {
    cat <<'EOF'
Usage: ./install.sh [--method=auto|systemd|desktop|none]

Install platform-profile-osd for the current user. The default selects a
systemd user service when available and otherwise uses XDG desktop autostart.
The "none" method installs files without configuring session startup.
EOF
}

for argument in "$@"; do
    case "$argument" in
        --method=auto|--method=systemd|--method=desktop|--method=none)
            method=${argument#--method=}
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$argument" >&2
            usage >&2
            exit 2
            ;;
    esac
done

profile_file=/sys/firmware/acpi/platform_profile
choices_file=/sys/firmware/acpi/platform_profile_choices

if [[ ! -e "$profile_file" ]]; then
    cat >&2 <<EOF
Unsupported system: $profile_file does not exist.

This utility requires a Linux kernel/vendor driver that exposes the standard
platform-profile interface. No files were installed.
EOF
    exit 1
fi
if [[ ! -r "$profile_file" ]]; then
    printf 'Unsupported system: %s is not readable by this user.\n' "$profile_file" >&2
    exit 1
fi

printf 'Platform profile interface: available\n'
printf 'Current profile: %s\n' "$(<"$profile_file")"
if [[ -r "$choices_file" ]]; then
    printf 'Available profiles: %s\n' "$(<"$choices_file")"
else
    printf 'Available profiles: unavailable (%s is not readable)\n' "$choices_file"
fi

for command in cc make pkg-config install; do
    if ! command -v "$command" >/dev/null 2>&1; then
        printf 'Missing build/install dependency: %s\n' "$command" >&2
        exit 1
    fi
done
if ! pkg-config --exists libsystemd; then
    printf 'Missing build dependency: libsystemd development files\n' >&2
    exit 1
fi
for sound in quiet.wav balanced.wav performance.wav; do
    if [[ ! -r "$project_dir/sounds/$sound" ]]; then
        printf 'Missing bundled project sound: %s\n' "$project_dir/sounds/$sound" >&2
        exit 1
    fi
done

make -C "$project_dir" all

printf '\nPre-install compatibility check:\n'
if ! compatibility_report=$("$project_dir/build/platform-profile-osd" --check); then
    printf '%s\n' "$compatibility_report"
    cat >&2 <<'EOF'

Required session compatibility is unavailable. Run the installer from the
graphical user session after a freedesktop-compatible notification service is
running and XDG_RUNTIME_DIR is available. No files were installed.
EOF
    exit 1
fi

printf '  Platform profile interface: available\n'
printf '  Session D-Bus: available\n'
printf '  Notification API: available\n'
printf '\nCurrently installed runtime audio:\n'
printf '%s\n' "$compatibility_report" | sed -n \
    '/^Audio (optional)$/,/^$/ {
        /^Audio (optional)$/d
        /^$/d
        /Audio feedback:/d
        p
    }'
printf '\nBundled sounds to install:\n'
for sound in quiet.wav balanced.wav performance.wav; do
    printf '    %s: available\n' "$sound"
done
if command -v pw-play >/dev/null 2>&1; then
    printf '  Audio after installation: available (optional)\n'
else
    printf '  Audio after installation: unavailable (pw-play not found; notifications still work)\n'
fi

home_dir=${HOME:?HOME is not set}
config_home=${XDG_CONFIG_HOME:-$home_dir/.config}
data_home=${XDG_DATA_HOME:-$home_dir/.local/share}
bin_dir=$home_dir/.local/bin
data_dir=$data_home/platform-profile-osd
sound_dir=$data_dir/sounds
service_dir=$config_home/systemd/user
autostart_dir=$config_home/autostart

if [[ "$method" == auto ]]; then
    if command -v systemctl >/dev/null 2>&1 &&
       systemctl --user show-environment >/dev/null 2>&1; then
        method=systemd
    else
        method=desktop
    fi
fi

if [[ "$method" == systemd ]]; then
    if ! command -v systemctl >/dev/null 2>&1; then
        printf 'Cannot use systemd startup: systemctl was not found.\n' >&2
        exit 1
    fi
    if ! systemctl --user show-environment >/dev/null 2>&1; then
        printf 'Cannot use systemd startup: the user manager is unavailable.\n' >&2
        exit 1
    fi
fi

install -d -m 0755 "$bin_dir" "$sound_dir" "$data_dir"
install -m 0755 "$project_dir/build/platform-profile-osd" \
    "$bin_dir/platform-profile-osd"
install -m 0644 "$project_dir/sounds/quiet.wav" "$sound_dir/quiet.wav"
install -m 0644 "$project_dir/sounds/balanced.wav" "$sound_dir/balanced.wav"
install -m 0644 "$project_dir/sounds/performance.wav" "$sound_dir/performance.wav"
install -m 0644 "$project_dir/config/config.example.ini" \
    "$data_dir/config.example.ini"

case "$method" in
    systemd)
        install -d -m 0755 "$service_dir"
        install -m 0644 "$project_dir/systemd/platform-profile-osd.service" \
            "$service_dir/platform-profile-osd.service"
        rm -f -- "$autostart_dir/platform-profile-osd.desktop"
        systemctl --user daemon-reload
        systemctl --user enable --now platform-profile-osd.service
        ;;
    desktop)
        install -d -m 0755 "$autostart_dir"
        install -m 0644 "$project_dir/autostart/platform-profile-osd.desktop" \
            "$autostart_dir/platform-profile-osd.desktop"
        if [[ -e "$service_dir/platform-profile-osd.service" ]] &&
           command -v systemctl >/dev/null 2>&1; then
            systemctl --user disable --now platform-profile-osd.service >/dev/null 2>&1 || true
            rm -f -- "$service_dir/platform-profile-osd.service"
            systemctl --user daemon-reload >/dev/null 2>&1 || true
        fi
        ;;
    none)
        ;;
esac

printf '\nInstalled platform-profile-osd:\n'
printf '  Executable: %s\n' "$bin_dir/platform-profile-osd"
printf '  Sounds:     %s\n' "$sound_dir"
printf '  Example:    %s\n' "$data_dir/config.example.ini"
printf '  Startup:    %s\n' "$method"
if command -v pw-play >/dev/null 2>&1; then
    printf '  Audio:      available (optional; pw-play found)\n'
else
    printf '  Audio:      unavailable (pw-play not found; notifications still work)\n'
fi
printf '\nConfiguration is optional. To customize, run:\n'
printf '  mkdir -p %q\n' "$config_home/platform-profile-osd"
printf '  cp %q %q\n' "$data_dir/config.example.ini" \
    "$config_home/platform-profile-osd/config.ini"
