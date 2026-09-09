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
- uninstaller service ownership, configuration/unrelated-file preservation,
  and refusal to remove files after an owned service fails to stop;
- isolated notification-service and D-Bus recovery using the production
  notification callback, including first-send recovery and timeout handling;
- missing `pw-play` and missing mapped-sound CLI behavior;
- shell syntax checks for every maintained script; and
- validation of the maintained XDG desktop entry and systemd user unit.

The CLI tests use a temporary synthetic sysfs directory only for read commands.
They do not claim that regular files reproduce kernfs `EPOLLPRI` behavior.

## Staged installation

Installer/uninstaller tests use temporary `HOME`, `XDG_CONFIG_HOME`, and
`XDG_DATA_HOME` directories. They verify a first install, an idempotent second
install, exact-file removal, preservation of user configuration, and complete
configuration removal. A temporary `systemctl` wrapper permits diagnostics but
rejects and records any attempted service-manager mutation. The tests also
check that a staged service file cannot make a desktop-method reinstall stop
another installation's service. They never target the live installation during
project development.

Run this test from a compatible graphical session:

```bash
./tests/test-install.sh
```

The service lifecycle test requires a running user service manager, the real
platform-profile sysfs interface, `flock`, and `timeout`. Build the binary first.
Each run uses uniquely named transient user units and a private temporary
`XDG_RUNTIME_DIR` for the daemon lock. The silent fixture disables notifications
and audio, so the installed notifier can keep running during the test.

It waits for the test daemon to hold its lock, checks duplicate rejection,
`Restart=on-failure` after `SIGKILL`, and the same `PartOf=` stop relationship
used for the graphical session. It also checks lock release on stop. Cleanup
removes only that run's units and temporary directory; failures report the
current stage and recent test-daemon journal entries. It does not install a
service or stop the real desktop target:

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
- [x] reboot/login automatic startup and physical profile-key acceptance
- [ ] graphical-session target start/stop relationship
- [x] idle CPU and wakeup observation
- [x] migration and rollback rehearsal

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

At the end of the September 2 testing, the following had not been performed:
deliberately restarting
Plasma's notification service or PipeWire, stopping the real
`graphical-session.target`, and a reboot/login cycle. Those actions would
disrupt the working session. Recovery behavior was instead exercised with
transient and installed units, missing backends, and retry-safe code paths.
The later reboot/login and rollback results are recorded separately below.
Combined notification-plus-audio physical changes passed under the installed
systemd service in both slow and rapid sequences. The replacement timing test
was completed and failed only for expired notification IDs, so replacement is
now disabled by default.

## Pre-reboot checkpoint — 2026-09-07

Ran `./install.sh --method=systemd` from commit `4fb2441`. The installed binary
and service file match the source checkout. The service is `enabled` and
`active`, with exactly one daemon, zero restarts, and no errors in the
current-boot service journal. `graphical-session.target` is active and
`--check` reports the kernel interface, notification API, and optional audio
available. Existing configuration is unchanged: audio is enabled, while startup
notifications and notification replacement are disabled.

The earlier rename to `asus-profile-notify.desktop.disabled` inside the
autostart directory did not prevent startup on this host: the autostart
generator created a service for that file and launched the legacy notifier.
Stopped that specific legacy service and moved its entry to
`~/.config/autostart-disabled/asus-profile-notify.desktop`. After reloading the
user manager, the old generated unit is `not-found` and no legacy notifier is
running. The legacy executable, moved desktop entry, and three legacy WAV files
all match their saved SHA-256 hashes; the user configuration hash also matches.

Reboot/login and physical profile-switching acceptance checks were deferred
at this checkpoint; their later results are recorded below.
No reboot, logout, profile change, or restart of desktop/audio infrastructure
was performed during this checkpoint.

## Post-reboot acceptance — 2026-09-08

Verified a new boot on Linux `7.2.3-1-cachyos`, starting at 09:16:45 CEST.
The enabled service started at 09:17:05 CEST, at the same time as
`graphical-session.target`, and remained active with exactly one daemon and
zero restarts when checked at 11:11 CEST. The current-boot journal contains
no service errors; the previous boot's journal records a clean service stop.
No manual service start or restart was performed during this verification.

The legacy notifier did not start. Its desktop entry remains outside the
autostart directory at
`~/.config/autostart-disabled/asus-profile-notify.desktop`. The legacy binary,
desktop entry, and WAV hashes match the saved baseline, and the configuration
hash is unchanged. The installed binary, service, and bundled sounds match the
checkout. `--check` reports the profile interface, Plasma notification API,
and configured optional audio available.

The user cycled the physical M4 key through Quiet, Balanced, and Performance
and confirmed exactly one popup and the matching sound for each profile.
Passive D-Bus monitoring captured exactly three `Notify` calls in that order
at 11:14:51–11:14:52 CEST, all from sender `:1.67`, mapped to the installed
daemon (PID 1869). Each call used replacement ID zero. The final raw profile
was `performance`; the same single daemon remained enabled and active with
zero restarts and no new journal errors after the test.

Reboot/login acceptance passed, including automatic startup, physical profile
changes, user-confirmed popup/audio behavior, and preserved-file integrity.
Rollback was still pending at this stage; the completed rehearsal is recorded
below. Deliberate notification/audio-service recovery checks remain untested.

## Rollback rehearsal — 2026-09-08

Completed a live switch from the new notifier to the preserved legacy ASUS
notifier, followed by restoration of the new notifier. The rehearsal used
the existing installed files; neither implementation was uninstalled. A guard
restored the new service on script exit, interruption, or a five-minute timeout
while waiting for the legacy physical-key check.

| Phase | Observed result |
| --- | --- |
| Stop new notifier | At 11:30:19 CEST, disabled and stopped `platform-profile-osd.service`; verified no new daemon remained. |
| Restore legacy startup | Moved the preserved desktop entry back to `~/.config/autostart/asus-profile-notify.desktop` and reloaded the user manager. The generated unit's `SourcePath` matched that entry. Started `app-asus\x2dprofile\x2dnotify@autostart.service`; exactly one legacy process ran (PID 26353). |
| Test legacy behavior | At 11:30:38–11:30:39 CEST, captured exactly three legacy `Notify` calls: `QUIET → BALANCED → PERFORMANCE`. The user confirmed one popup and the matching sound for each physical M4 change. |
| Restore new notifier | Stopped the legacy unit at 11:31:14 CEST, moved its desktop entry back outside autostart, and reloaded the user manager. The legacy unit became `not-found`. Enabled and started the new service at 11:31:15 CEST. |
| Test restored behavior | At 11:31:43–11:31:44 CEST, captured exactly three new `Notify` calls: `Quiet → Balanced → Performance`, from sender `:1.149`, mapped to PID 27181. The user confirmed one popup and the matching sound for each change. |

Final state: the new service is enabled and active with exactly one process,
zero restarts, and no journal errors. The legacy notifier is stopped, and its
entry is preserved at
`~/.config/autostart-disabled/asus-profile-notify.desktop`. SHA-256 comparisons
before and after the round trip confirmed unchanged contents for the legacy
executable, desktop entry and three sounds, the user configuration, and the new
executable, service file and three bundled sounds. `--check` passed after
restoration. The final profile was `performance`, as before the rehearsal.

### Procedure used on the development machine

These paths and the generated legacy unit name apply to this machine's default
XDG directories. Before each move, verify the source exists and the destination
does not exist. Do not overwrite an existing entry. Check each stop completes
and that its notifier process exits before starting the other implementation.

To switch to the legacy notifier:

```bash
systemctl --user disable --now platform-profile-osd.service
mv -n -- ~/.config/autostart-disabled/asus-profile-notify.desktop \
    ~/.config/autostart/asus-profile-notify.desktop
systemctl --user daemon-reload
systemctl --user start 'app-asus\x2dprofile\x2dnotify@autostart.service'
```

To return to the new notifier:

```bash
systemctl --user stop 'app-asus\x2dprofile\x2dnotify@autostart.service'
mv -n -- ~/.config/autostart/asus-profile-notify.desktop \
    ~/.config/autostart-disabled/asus-profile-notify.desktop
systemctl --user daemon-reload
systemctl --user enable --now platform-profile-osd.service
```

Verify the resulting service state, one notifier process, notifications/audio,
and preserved-file hashes after either switch. Keeping a renamed `.disabled`
file inside the autostart directory is not sufficient on this host. This
rehearsal verified the restored legacy entry's generated unit and live behavior;
it did not include another login while the legacy configuration was restored.

## Isolated service integration verification — 2026-09-09

Reproduced the earlier exit status `4` while the installed notifier was active.
The test daemon shared the installed daemon's `XDG_RUNTIME_DIR`, so it logged
`another daemon instance is already running; exiting` and exited successfully.
The test then failed its service-active check. This was a test-isolation issue.

Updated `tests/test-service.sh` to give each run a private lock directory and
unique unit names, wait for lock acquisition, bound the duplicate-process
check, and clean up only its own units. The installed daemon was kept running.

| Check | Result |
| --- | --- |
| Checkout service lifecycle test | Pass: startup, duplicate rejection, restart after `SIGKILL`, `PartOf=` stop, and lock release |
| Fresh build and `make -j4 test` | Pass from a local clone of `fe34bf9` with the test/documentation changes applied; compilation, unit/CLI tests, shell syntax, desktop entry, and systemd-unit validation passed |
| Two concurrent service tests from that fresh checkout | Both exited `0`, with distinct runtime directories, units, and daemon PIDs; each passed every lifecycle check |
| Cleanup | All six units and all three runtime directories from the successful runs were removed |
| Live notifier continuity | Remained enabled and active at PID `1879`, with the same start timestamp and zero restarts |
| Preserved installation | All 11 legacy/new executable, configuration, service, desktop-entry, and sound files retained their SHA-256 hashes, modes, and modification times |

The sandbox initially blocked the local sockets used by systemd unit
validation; the complete fresh-build suite passed with host access. No profile
change or desktop/audio-service restart was needed. These results close the
service-integration verification gap; the recovery scenarios below remain
untested.

## Pre-publication privacy review — 2026-09-09

Fetched the remote refs and reviewed the complete, non-shallow history through
`fe34bf9`: four commits, 39 unique file-content objects, and 31 tracked paths,
plus the pending service-test and documentation changes. The remote exposed
only `main`, with no tags.

Local Python standard-library checks covered credential and private-key
patterns, high-entropy text tokens, contact addresses, home paths, network
identifiers, sensitive filenames, and commit metadata. Candidate matches were
reviewed: the template unit's `@autostart.service` suffix is not an email
address, `/home/me` is a documentation example, and author/committer addresses
use the configured GitHub noreply identity. The three WAV files contain only
format and audio-data chunks.

No secrets or unexpected personal identifiers were found within that scope.
Documented hardware/software versions, test dates, process IDs, and the GitHub
account identity remain. This was a heuristic local review, not credential
validation; unreachable Git objects and remote pull-request refs were outside
scope. Repository visibility and release publication are separate steps.

## Final pre-release review — 2026-09-09

Reviewed the C runtime, configuration parser, installer/uninstaller, test
scripts, service/autostart entries, CI workflow, release metadata, and asset
packaging. Found and reproduced one installation-isolation bug with a fake
service manager: uninstalling a temporary-home installation still attempted
to disable the live service. Desktop-method reinstall had the same issue when
a staged service file existed.

Both paths now compare the manager's `FragmentPath` with the target service
file before controlling the unit. A failed stop of an owned service aborts
removal. The new automated regression suite covers absent, unrelated, owned,
and symlinked unit files, an unavailable manager, and a failed service stop.
Staged installer tests now reject all manager mutations at the command boundary.

Verified from a fresh local clone of `69d24ae` with these review fixes applied:

| Check | Result |
| --- | --- |
| GCC build and `make -j4 test` | Pass, including the new uninstaller regression suite |
| Temporary-home install/reinstall/uninstall | Pass; configuration and unrelated files preserved; no manager mutation attempted |
| Isolated transient service lifecycle | Pass: start, duplicate rejection, failure restart, `PartOf=` stop, and lock release |
| GCC `-fanalyzer -Werror` build | Pass with no diagnostics |
| Clang `-Werror` with AddressSanitizer and UndefinedBehaviorSanitizer | Full `make test` passed; leak detection disabled for compatibility with the test environment |
| Read-only `--check` | Kernel interface, notification API, configured audio files/backend, and runtime directory available |
| Packaging | Relative Markdown file links resolve; version constants agree on `0.1.0`; all three WAV files are valid 48 kHz stereo 16-bit PCM |
| Working installation | Same enabled/active daemon, PID `1879`, start timestamp, and zero restarts; all 11 preserved-file hashes, modes, and modification times unchanged |

Physical popup/audio and reboot acceptance remain supported by the September 8
results. The recovery scenarios below remain untested. The changelog release
date is still provisional; visibility, tagging, and publication remain pending.

## Isolated notification and D-Bus recovery — 2026-09-09

Run with Python 3 and `dbus-daemon` installed:

```bash
make test-recovery
```

The C helper compiles the actual notification code from `src/main.c` and calls
the production profile-change callback with synthetic profile names. A Python
driver owns a private D-Bus broker, mock notification server, and persistent
client. Its bus configuration has no service-activation directories or desktop
integration. The helper refuses an inherited desktop bus. User configuration
is never loaded, audio is disabled, and the sysfs watcher is never started.

The first run exposed a lost notification when the private bus restarted
while the client was idle: the old connection failed, but reconnection only
helped the next profile change. The fix retries the current notification once
after a broken connection and resets the replacement ID. Timeouts and server
errors are not retried because delivery may already have occurred.

| Private test scenario | Result |
| --- | --- |
| Notification service absent at startup, then started | Pass: failures non-fatal, one warning per outage, subsequent delivery succeeds |
| Notification server killed and restarted | Pass: outage warning resets after successful recovery |
| Notification server restarted while client is idle | Pass: first subsequent notification delivered |
| Private bus killed, sends attempted during outage, then broker/server restored | Pass: reconnection and delivery without restarting the client |
| Private bus restarted while client is idle | Pass after fix: first subsequent notification delivered |
| Server receives a notification but withholds its reply | Pass: bounded timeout, no retransmission, delivery resumes after server replacement |
| Opt-in replacement across private-bus restart | Pass: replacement ID resets to zero on reconnect, then normal replacement resumes |

All scenarios kept the same client process and checked received notification
bodies and replacement IDs. Fresh-checkout `make -j4 test`, GCC static analysis
with `-fanalyzer -Werror`, and Clang AddressSanitizer/UndefinedBehaviorSanitizer
tests passed; leak detection was disabled for test-environment compatibility.

These are component integration results, not visible Plasma popup acceptance
or live session-bus recovery. The installed notifier, Plasma, session bus, and
audio services retained their original PIDs, start times, states, and restart
counts. All 11 installed/preserved files retained their hashes, modes, and
modification times, and no private test processes remained. The installed
binary was not replaced. Live recovery gaps remain listed below.

## Recovery scenarios not yet validated

The following remain explicit validation gaps. Successful startup, normal
shutdown, missing-backend tests, and a successful rollback do not establish
recovery from these events. Keep the corresponding hardware checklist items
unchecked until the actual scenarios are exercised and recorded.

| Scenario | Existing evidence and remaining check |
| --- | --- |
| Notification server crash/restart | Private mock-server recovery passed above. Actual Plasma restart and visible popup recovery remain untested. |
| Session D-Bus disconnect/restart | Private-bus loss/restoration and first-send recovery passed above with a persistent client. Actual desktop session-bus restart and full-daemon survival remain untested. |
| PipeWire restart/recovery | Missing `pw-play`, missing sounds, and an unavailable remote were non-fatal in earlier tests. Verify actual playback failure during a PipeWire restart and matching audio on subsequent profile changes after recovery. |
| Deliberate stop/start of `graphical-session.target` | Transient `PartOf=` tests and real boot/shutdown logs passed. Explicitly stopping and restarting the real graphical target in a live session was not tested; it can disrupt desktop applications. |
| Suspend/resume | Verify the watcher, notification connection, and optional audio after resume, including the first profile change. No suspend/resume acceptance test was performed. |
| Kernel profile-interface removal/recreation | Missing files are covered by automated tests. Live driver reset or sysfs removal/recreation, followed by restoration of monitoring and any systemd restart-limit effects, has not been tested. |

Schedule disruptive desktop/audio recovery tests separately when other work
can be interrupted. Record the actual interruption, recovery sequence, process
and restart counts, journal messages, and observed popup/audio results. Source
inspection alone is not a passing recovery test.

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
