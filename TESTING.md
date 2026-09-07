# Testing

This file separates repeatable automated checks from tests that require a real
graphical session, user service manager, audio server, or physical profile key.

## Automated

Run:

```bash
make clean
make
make test
```

Coverage includes:

- readable labels for standard and unknown profile names;
- default, custom, empty, and unknown sound mappings;
- valid and invalid configuration values;
- filenames containing spaces;
- literal audio arguments containing spaces and shell metacharacters;
- missing profile files;
- `--version`, `--help`, `--print-profile`, and `--print-choices`;
- rejection of conflicting CLI actions;
- startup-notification and audio defaults;
- daemon-only single-instance locking while read-only CLI actions remain usable;
- missing `pw-play` and missing mapped-sound CLI behavior;
- shell syntax checks for every maintained script; and
- validation of the maintained XDG desktop entry and systemd user unit.

The CLI tests use a temporary synthetic sysfs directory only for read commands.
They do not claim that regular files reproduce kernfs `EPOLLPRI` behavior.

## Staged installation

Installer/uninstaller tests use temporary `HOME`, `XDG_CONFIG_HOME`, and
`XDG_DATA_HOME` directories. They verify a first install, an idempotent second
install, exact-file removal, preservation of user configuration, and complete
configuration removal. They never target the live installation during project
development.

Run this test from a compatible graphical session:

```bash
./tests/test-install.sh
```

The service lifecycle test uses uniquely named transient user units. It tests
start, `Restart=on-failure`, stop, and the same `PartOf=` relationship used for
the graphical session without installing a service or stopping the real
desktop target:

```bash
./tests/test-service.sh
```

## Real hardware checklist

Record the kernel, desktop, session type, and exposed choices before testing.

- [x] optional initial current-profile notification
- [x] repeated physical profile-key changes
- [x] immediate `EPOLLPRI` detection without polling
- [x] rapid notification behavior (replacement failed; non-replacement passed)
- [x] visual-only mode (`--no-sound`)
- [x] visual plus bundled audio
- [x] `sound_enabled = false`
- [x] missing `pw-play`
- [x] missing individual sound file
- [x] valid custom sound mapping, including a path with spaces
- [x] graceful synthetic unknown-profile presentation
- [ ] notification daemon restart/recovery
- [ ] PipeWire restart/recovery
- [x] user-service start, unexpected-failure restart, and stop
- [ ] graphical-session target start/stop relationship
- [x] idle CPU and wakeup observation
- [ ] migration and rollback rehearsal

Results for the initial development machine are filled in before publication.

## Initial development results — 2026-09-02

Environment: CachyOS, KDE Plasma 6.7.4 on Wayland, Linux 7.2.2, ASUS ROG
Strix SCAR 17 G733PZ. Exposed choices were `quiet balanced performance`.

| Check | Result |
| --- | --- |
| GCC warning-clean build | Pass |
| Unit and CLI tests | Pass |
| Address/undefined-behavior sanitizers | Pass; leak detection disabled because the test sandbox uses ptrace |
| GCC static analyzer | Pass |
| Real current profile and choices reads | Pass |
| Real event-driven profile changes | Pass: `performance → quiet → balanced → performance` captured from `EPOLLPRI` |
| Direct freedesktop notification | Pass against Plasma 6.7.4; one `Notify` call observed for each physical profile change from one daemon |
| Duplicate-process diagnosis | Pass: the apparent intermittent behavior was traced to two daemon processes and two D-Bus senders; one daemon was consistent |
| Rapid replacement call path | Fail on Plasma: all calls reached the server, but reusing an expired notification ID updated it silently without a new popup; replacement is now opt-in |
| Rapid non-replacement stress | Pass: 27 consecutive calls preserved the exact `quiet → balanced → performance` sequence, popups and audio remained normal, and the daemon had zero restarts |
| Bundled quiet/balanced/performance playback | Pass with physical M4 changes: Quiet, Balanced, and Performance each played the matching WAV |
| Custom WAV filename containing spaces | Pass |
| `--no-sound` and config-disabled audio diagnostics | Pass |
| Missing `pw-play` and missing mapped file | Pass; non-fatal status and one warning |
| Simulated unavailable PipeWire remote | Pass; daemon stayed active and logged one asynchronous warning |
| Unknown profile label/mapping/startup handling | Pass in automated tests; startup notification is now configurable and defaults off |
| Transient systemd start/restart/stop | Pass |
| Session-style `PartOf=` stop relationship | Pass; actual `graphical-session.target` also confirmed active |
| Installer first and second run | Pass under a temporary home with XDG desktop autostart |
| Uninstaller and config preservation/removal | Pass under a temporary home |
| Live systemd install and idempotent reinstall | Pass; enabled and active with exactly one process |
| Live service lifecycle | Pass; recovered from `SIGKILL`, and ordinary stop/start/restart all succeeded |
| Duplicate daemon protection | Pass in CLI, transient-service, and live-service tests; second daemon exited successfully and CLI diagnostics remained usable |
| Live uninstaller and config preservation | Pass; service disabled/stopped, exact installed files removed, user config hash unchanged, then systemd reinstall succeeded |
| Installer audio summary | Pass; currently installed runtime sounds and bundled-to-install sounds are reported separately |
| Idle observation | 0 CPU ticks and 0 context switches over 10 seconds; 0.0% CPU, 2796 KiB RSS, one thread |
| Original working installation integrity | Pass; paths, timestamps, and SHA-256 hashes remained unchanged |

Not performed against live desktop infrastructure: deliberately restarting
Plasma's notification service or PipeWire, stopping the real
`graphical-session.target`, and a reboot/login cycle. Those actions would
disrupt the working session. Recovery behavior was instead exercised with
transient and installed units, missing backends, and retry-safe code paths.
Combined notification-plus-audio physical changes passed under the installed
systemd service in both slow and rapid sequences. The replacement timing test
was completed and failed only for expired notification IDs, so replacement is
now disabled by default.

Handoff state: the new service file, binary, sounds, and sound-enabled live-test
configuration are installed. The service is `enabled` and `active`, startup
notifications and notification replacement are disabled, and the legacy ASUS
process is not running. The legacy files remain present and unchanged pending
the controlled migration/reboot step. Do not log out or reboot in this state:
either reversibly disable the legacy autostart entry first or disable the new
service so both notifiers cannot start at the next login.

## Controlled notification-replacement test

Run exactly one daemon with `replace_notifications = true`. Confirm first that
both `systemctl --user is-active platform-profile-osd.service` and
`pgrep -af platform-profile-osd` show no daemon. Then start a verbose manual
instance and exercise all of these sequences with M4:

1. Slow changes, waiting for each popup to appear.
2. Rapid changes through all profiles while the popup is visible.
3. A pause longer than `notification_timeout_ms`, followed by another change.

In parallel, monitor the session bus and confirm one `Notify` call per real
change from one sender. Record whether a new Plasma popup appears after the old
notification has expired. Keep replacement enabled only if all three sequences
pass.

Result on Plasma 6.7.4: the daemon emitted the correct call for every slow and
rapid profile change, but Plasma did not show a new popup when an expired
notification ID was reused. With replacement disabled, slow changes and a
27-change rapid stress sequence both produced correct notifications and audio,
including a normal post-stress change. The service stayed active with zero
restarts.

## Live systemd installation test

This is a controlled migration step, not part of the temporary-home test. Keep
the legacy ASUS files and their baseline hashes intact. Before installation,
inspect `pgrep -af 'platform-profile-osd|asus-profile-notify'`, stop any manual
new-project daemon, and temporarily stop the legacy process so only one notifier
is active during profile-key testing.

Run:

```bash
./install.sh --method=systemd
systemctl --user daemon-reload
systemctl --user status platform-profile-osd.service
systemctl --user is-enabled platform-profile-osd.service
systemctl --user is-active platform-profile-osd.service
pgrep -af platform-profile-osd
journalctl --user -u platform-profile-osd.service
```

Verify physical M4 profile detection, notifications, explicitly enabled audio,
one process, one notification per change, clean journal output, restart after a
forced unexpected failure, and ordinary stop/start behavior. Repeat the
installer once to verify live-home idempotency. Do not remove the legacy files.

## Reboot/login test

Perform this only after the live service test passes and the legacy autostart
has been intentionally, reversibly disabled so both notifiers cannot start at
login. Reboot and log in normally without manually launching the daemon, then:

1. Confirm the user service started automatically and exactly one daemon exists.
2. Cycle M4 through Quiet, Balanced, and Performance.
3. Confirm notifications and the configured optional audio work immediately.
4. Run the following diagnostics and retain their output:

```bash
~/.local/bin/platform-profile-osd --check
systemctl --user status platform-profile-osd.service
pgrep -af platform-profile-osd
journalctl --user -u platform-profile-osd.service
```

After the final install/uninstall/reinstall cycle, compare the saved SHA-256
baseline for the legacy binary, desktop file, and WAV files. Do not mark the
release candidate ready until the hashes match and the reboot results are
recorded here.
