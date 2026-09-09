#!/usr/bin/env bash
set -euo pipefail

remove_config=false

for argument in "$@"; do
    case "$argument" in
        --remove-config)
            remove_config=true
            ;;
        --help|-h)
            cat <<'EOF'
Usage: ./uninstall.sh [--remove-config]

Remove files installed by platform-profile-osd for the current user. User
configuration is preserved unless --remove-config is explicitly supplied.
EOF
            exit 0
            ;;
        *)
            printf 'Unknown option: %s\n' "$argument" >&2
            exit 2
            ;;
    esac
done

home_dir=${HOME:?HOME is not set}
config_home=${XDG_CONFIG_HOME:-$home_dir/.config}
data_home=${XDG_DATA_HOME:-$home_dir/.local/share}
bin_file=$home_dir/.local/bin/platform-profile-osd
data_dir=$data_home/platform-profile-osd
sound_dir=$data_dir/sounds
service_file=$config_home/systemd/user/platform-profile-osd.service
autostart_file=$config_home/autostart/platform-profile-osd.desktop
config_dir=$config_home/platform-profile-osd
config_file=$config_dir/config.ini

manage_service=false
if [[ -e "$service_file" ]] && command -v systemctl >/dev/null 2>&1; then
    # A temporary HOME/XDG_CONFIG_HOME does not isolate the session bus.
    # Only stop a unit loaded from this installation's actual service file.
    loaded_service=$(systemctl --user show platform-profile-osd.service \
        --property=FragmentPath --value 2>/dev/null) || loaded_service=
    if [[ -n "$loaded_service" && "$service_file" -ef "$loaded_service" ]]; then
        systemctl --user disable --now platform-profile-osd.service
        manage_service=true
    fi
fi

rm -f -- "$service_file"
rm -f -- "$autostart_file"
rm -f -- "$bin_file"
rm -f -- "$sound_dir/quiet.wav"
rm -f -- "$sound_dir/balanced.wav"
rm -f -- "$sound_dir/performance.wav"
rm -f -- "$data_dir/config.example.ini"

# rmdir only removes empty directories, so unrelated/user files are preserved.
rmdir -- "$sound_dir" 2>/dev/null || true
rmdir -- "$data_dir" 2>/dev/null || true

if [[ "$remove_config" == true ]]; then
    rm -f -- "$config_file"
    rmdir -- "$config_dir" 2>/dev/null || true
    printf 'Removed configuration: %s\n' "$config_file"
else
    printf 'Preserved user configuration (if present): %s\n' "$config_file"
fi

if [[ "$manage_service" == true ]]; then
    systemctl --user daemon-reload >/dev/null 2>&1 || true
fi

printf 'Removed platform-profile-osd installed files.\n'
