# platform-profile-osd

**Cross-vendor Linux platform-profile notifications with optional
profile-specific audio feedback.**

`platform-profile-osd` watches the standard Linux
`/sys/firmware/acpi/platform_profile` interface. It shows the active profile
through a freedesktop desktop notification and can play a short sound selected
for that profile.

The daemon is small, read-only, event-driven, and runs as the logged-in user.
By default it reports only actual profile changes; an optional setting can also
show the current profile once at startup. It does not poll sysfs while idle.

## Example behavior

- Quiet → `Quiet` notification + optional quiet sound
- Balanced → `Balanced` notification + optional balanced sound
- Performance → `Performance` notification + optional performance sound
- A future profile such as `ultra-performance` → `Ultra Performance`
  notification + optional default sound

Unknown profiles are treated as data, converted to a readable label, and never
executed. They are silent by default and monitoring continues normally.

## What it does not do

`platform-profile-osd`:

- does not change performance profiles;
- does not control fans or fan curves;
- does not alter CPU or GPU power settings;
- does not replace `asusctl` or any vendor utility;
- does not depend on ASUS hardware or ASUS-specific profile names; and
- does not require root during normal operation.

## Compatibility test

Run:

```bash
cat /sys/firmware/acpi/platform_profile
cat /sys/firmware/acpi/platform_profile_choices
```

The first command should print the current raw profile. The second should print
the choices offered by the kernel/vendor driver. For example:

```text
balanced
quiet balanced performance
```

The number, names, and order of choices vary by machine. Compatibility depends
on the kernel and vendor driver exposing this standard interface and emitting
its sysfs change notification when the active profile changes.

Tested on ASUS ROG Strix SCAR 17 G733PZ running CachyOS and KDE Plasma 6.7.4.
The tested session used Wayland and Linux 7.2.2, with the choices
`quiet balanced performance`.

Other machines are potentially compatible when their Linux kernel exposes both
sysfs paths shown above and emits a change notification when the profile changes.
No broader hardware compatibility is claimed without testing.

## Dependencies

Required at runtime:

- Linux with readable `platform_profile` sysfs support;
- `libsystemd` (used for its small `sd-bus` D-Bus client API);
- a graphical user-session D-Bus; and
- a freedesktop-compatible notification service; and
- a writable per-user `$XDG_RUNTIME_DIR` for the daemon instance lock.

KDE Plasma and GNOME implement the freedesktop notification API. Wayland
compositors such as Sway and Hyprland work when a compatible notification
daemon is running.

Optional audio dependency:

- PipeWire's `pw-play` command.

The daemon, installer, and notifications continue to work when `pw-play` is
absent. Missing sounds and playback failures are also non-fatal.

Build dependencies:

- a C11 compiler;
- GNU Make;
- `pkg-config`; and
- libsystemd development headers (`libsystemd-dev` on Debian/Ubuntu,
  `systemd-libs` plus headers on Arch-derived systems).

Additional test dependencies:

- Bash, for syntax-checking the Bash installer and integration scripts;
- Python 3 and `dbus-daemon`, for private-bus notification recovery tests;
- `desktop-file-validate` from `desktop-file-utils`; and
- `systemd-analyze` from systemd.

## Installation

From a checked-out source tree:

```bash
make
make test
./install.sh
```

The installer performs a compatibility check, builds the binary, and installs:

```text
~/.local/bin/platform-profile-osd
~/.local/share/platform-profile-osd/sounds/*.wav
~/.local/share/platform-profile-osd/config.example.ini
~/.config/systemd/user/platform-profile-osd.service
```

It then enables the systemd user service. Installation is idempotent and does
not fail merely because optional audio is unavailable.

The startup method can be selected explicitly:

```bash
./install.sh --method=systemd
./install.sh --method=desktop
./install.sh --method=none
```

`auto` is the default. It prefers a systemd user manager and falls back to an
XDG `.desktop` autostart entry. `none` installs files without session startup,
which is useful for packaging and staged testing.

### systemd and graphical sessions

The user service is wanted by and is part of `graphical-session.target`. It
starts as the normal user, has the session D-Bus/PipeWire environment, stops
with the graphical session, and restarts only after unexpected failure.

Some desktop environments do not activate `graphical-session.target`
consistently. Use `--method=desktop` on those systems. Do not enable both startup
methods at once; the installer keeps only the selected project startup file.

## Configuration

No configuration file is required. To customize the defaults:

```bash
mkdir -p ~/.config/platform-profile-osd
cp ~/.local/share/platform-profile-osd/config.example.ini \
   ~/.config/platform-profile-osd/config.ini
```

The format is a small INI file:

```ini
[general]
notifications_enabled = true
notify_on_startup = false
sound_enabled = false
replace_notifications = false
notification_timeout_ms = 1800
# sound_directory = ~/.local/share/platform-profile-osd/sounds

[sounds]
low-power = quiet.wav
cool = quiet.wav
quiet = quiet.wav
balanced = balanced.wav
balanced-performance = performance.wav
performance = performance.wav
default =
```

Supported settings:

| Setting | Default | Meaning |
| --- | --- | --- |
| `notifications_enabled` | `true` | Show desktop notifications. |
| `notify_on_startup` | `false` | Show the current profile once when the daemon starts. |
| `sound_enabled` | `false` | Enable optional sound feedback globally. |
| `replace_notifications` | `false` | Update the previous notification during rapid changes. |
| `notification_timeout_ms` | `1800` | Requested timeout; `-1` uses the notification server default. |
| `sound_directory` | XDG data directory | Base directory for relative sound mappings. |
| `[sounds] PROFILE` | See example | WAV path for an exact raw kernel profile. Empty disables that mapping. |
| `[sounds] default` | empty | Fallback for custom or unknown profiles; empty means silence. |

Relative `sound_directory` values are resolved from the configuration file's
directory. Relative sound mappings are resolved from `sound_directory`.
Absolute paths and leading `~/` are supported, including filenames with spaces.
Shell syntax and environment variables are not evaluated.

Notification replacement is opt-in because notification servers differ in how
they handle an expired replacement ID. In particular, Plasma may update an old
notification in its history without showing a new popup. Leave replacement
disabled when every profile change should produce a visible popup.

### Audio

Visual notifications are the core feature, so audio is disabled by default.
Enable it permanently with:

```ini
[general]
sound_enabled = true
```

Or enable it for one invocation:

```bash
platform-profile-osd --sound
```

`--no-sound` still overrides an enabled configuration. To use your own WAV file,
set a mapping without editing the source:

```ini
[sounds]
balanced = /home/me/Audio/My balanced sound.wav
```

Custom and unknown profiles have no sound unless mapped explicitly or through
`default`.

Playback is limited to ten seconds, including custom WAV files. A stalled audio
backend cannot leave a player running indefinitely; visual monitoring continues.

## CLI and diagnostics

```text
platform-profile-osd --check
platform-profile-osd --print-profile
platform-profile-osd --print-choices
platform-profile-osd --test-notification
platform-profile-osd --test-sound quiet
platform-profile-osd --no-sound
platform-profile-osd --verbose
```

`--check` reports the kernel interface, raw current profile, discovered choices,
session notification support, optional audio state, bundled sounds, startup
state, and XDG paths. Home-directory paths are abbreviated with `~` so the
output is suitable for a GitHub issue.

With replacement enabled, `--test-notification` immediately updates its first
test message. Seeing one final “Notifications are working” item confirms the
freedesktop replacement path rather than leaving two stale notifications.

Useful service commands:

```bash
systemctl --user status platform-profile-osd.service
journalctl --user -u platform-profile-osd.service
```

Normal logging is intentionally quiet: one startup line plus failures. Add
`--verbose` to a manual invocation when diagnosing profile changes.

Only daemon mode takes the per-user-session lock at
`$XDG_RUNTIME_DIR/platform-profile-osd.lock`. A second daemon prints a useful
message and exits successfully, while `--check`, print, and test actions remain
available. The lock is released automatically by the kernel when the daemon
exits; the small lock file may remain in the runtime directory until logout.

Failure policy is intentionally simple:

- an unreadable/missing active-profile node or a failed `epoll` setup is fatal,
  because there is nothing reliable to monitor;
- configuration mistakes, notification failures, a missing audio backend, and
  missing/playback-failed sounds are logged and remain non-fatal;
- a broken D-Bus connection is reopened and the current notification retried
  once; timeouts and notification-server errors are not retried;
- repeated notification/audio failures produce a single warning per outage or
  process lifetime rather than flooding the journal; and
- a profile with an intentionally empty mapping is normal and silent.

## How change detection works

The daemon reads the current value once, registers the open sysfs file with
`epoll`, and blocks for `EPOLLPRI`/`EPOLLERR`. On notification it seeks back to
offset zero, rereads the value, and acts only when the raw profile changed.

This follows the kernel's kernfs/sysfs poll contract and consumes essentially
no CPU while idle. There is no 0.2-second loop, timer, or inotify dependency.
The daemon never opens the profile file for writing.

## Uninstallation

From the source tree:

```bash
./uninstall.sh
```

The script stops/disables the project user service only when the manager's
loaded unit belongs to the installation being removed. An installation under
another home/configuration directory does not control that service. If stopping
an owned service fails, removal stops before deleting installed files.

It removes only the exact binary, service, autostart file, example
configuration, and bundled sound files installed by this project. It uses
`rmdir` only for directories that are empty.

User configuration is preserved. Remove it explicitly with:

```bash
./uninstall.sh --remove-config
```

or manually:

```bash
rm ~/.config/platform-profile-osd/config.ini
rmdir ~/.config/platform-profile-osd 2>/dev/null || true
```

## Building and testing

```bash
make
make test
```

Automated tests cover profile-label normalization, standard/default/unknown
mapping behavior, configuration parsing, invalid mappings, raw profile reads,
and core CLI behavior. Hardware event, notification, audio, systemd, idle CPU,
and staged installer results are tracked in [TESTING.md](TESTING.md).

`make test-recovery` runs just the isolated notification/D-Bus recovery tests.
It creates a private bus and mock notification server, drives the production
notification callback, and cleans up its own processes. It does not connect to
the desktop bus, watch real sysfs, or play audio. These tests also run as part
of `make test`.

## Prior art

[`t-8ch/profilesalertd`](https://github.com/t-8ch/profilesalertd) is relevant
prior art from 2021. It monitors the same Linux `platform_profile` node with an
`EPOLLPRI`-based C daemon, sends freedesktop notifications, and supplies a
graphical-session systemd service. It is GPL-3.0 licensed.

`platform-profile-osd` is an independent implementation. It does not claim that
platform-profile notifications are new and does not copy `profilesalertd`
source. Its focus is cross-vendor presentation, optional polished audio,
configurable mappings, automatic choice discovery, graceful unknown profiles,
diagnostics, and simple user-local installation.

## Known limitations

- A machine can expose the sysfs files while a buggy/old driver fails to emit a
  change notification. The current profile remains readable through the CLI,
  but changes cannot be detected without polling; this project intentionally
  has no polling fallback.
- The default service integration depends on the desktop activating
  `graphical-session.target`; XDG autostart is the fallback.
- Notification appearance and timeout policy are ultimately controlled by the
  user's notification server.
- Plasma notification-server and PipeWire recovery passed on the test host.
  Suspend/resume preserved notifications, but audio failed with kernel HDA
  controller/codec errors. Live session-bus restart, graphical-session target
  stop/start, and kernel profile-interface removal remain unvalidated.
  See [untested recovery scenarios](TESTING.md#recovery-scenarios-not-yet-validated).
- Only the hardware listed above has been tested by this project so far.

## License

MIT. See [LICENSE](LICENSE). The license covers the independently written source
and the three bundled project-created WAV sound assets. External dependencies
retain their own licenses and are not vendored here.
