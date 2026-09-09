# Testing and contributing

Thanks for considering a test or review. I'm a chemical engineer learning more
about Linux through this personal project. Reports, small fixes, and clear
explanations are welcome; you do not need to be a developer to help.

## What would help most

- Reports from compatible laptops beyond the development ASUS machine.
- Results from other desktops, notification servers, and Linux distributions.
- Checks that ordinary profile changes produce one correct notification.
- Optional sound tests, installation/uninstallation feedback, and code review.
- Comparisons with your desktop's or vendor tool's existing feedback.

The current public version is a volunteer-testing preview. Read the
[compatibility requirements and installation instructions](README.md) and the
[known limitations](README.md#known-limitations) first.

## A basic test

1. Build the preview and run `make test` as described in the README.
2. Run `./build/platform-profile-osd --check` and note the reported version,
   available profiles, and notification support.
3. Follow the README to install or run the notifier. Start only one instance.
4. Use your normal hardware profile key or profile-control tool. Note whether
   each change produces one correctly labelled popup.
5. If you choose to test sound, enable it as described in the README and report
   audio separately from visible notifications. Audio is off by default.

Normal profile changes are enough for a useful report. Suspend, reboot,
desktop-service interruption, and driver-reset tests are not required. One
development-machine suspend test caused a kernel audio-device failure requiring
reboot recovery; it is recorded in [TESTING.md](TESTING.md).

This utility only observes profiles. Leave your existing power-management
configuration in place while evaluating it. If your desktop already shows a
profile notification, mention that when reporting apparent duplicates.

## Reporting a result

[Open a compatibility report](https://github.com/pogodinim/platform-profile-osd/issues/new?template=compatibility_report.yml),
including successful results. Please include:

- preview version/tag or commit;
- laptop model, distribution, kernel, desktop/notification server, and session type;
- available profile choices and how you changed the profile;
- what you expected and what you observed, with popup and sound results separate;
- whether an existing desktop or vendor notifier was also running.

Review diagnostic output before posting and remove personal paths, usernames,
serial numbers, or other information you do not want public. A short relevant
error excerpt is more useful than a complete system journal.

## Code and documentation changes

Small, focused pull requests are welcome. Explain the problem, the resulting
behavior, and how you checked it. Keep the project read-only toward profile
settings, keep audio optional, and preserve user configuration and unrelated
files during installation changes.

Run `make test` for code changes. Tests use fake audio and a private D-Bus
server; they do not require live suspend or desktop-service recovery tests.
Independent review of process handling, configuration parsing, and installation
boundaries is particularly welcome.
