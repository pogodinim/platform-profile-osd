# Changelog

All notable changes to this project will be documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.1.0-preview.1] - 2026-09-09

### Changed

- Introduced the project as a volunteer-testing preview and personal Linux
  learning project by a chemical engineer.
- Added a contribution guide, a compatibility-report form, and comparisons
  with existing desktop/vendor feedback and standalone prior art.
- Marked the program version as a preview. Runtime behavior is unchanged from
  the tested private candidate below.

## [0.1.0] - 2026-09-09 (unpublished private checkpoint)

### Added

- Event-driven Linux platform-profile monitoring using sysfs `EPOLLPRI`.
- Freedesktop notifications with replacement support.
- Optional profile-specific `pw-play` audio with three bundled WAV sounds.
- XDG INI configuration and graceful unknown-profile handling.
- Compatibility diagnostics, CLI test actions, and quiet/verbose logging.
- systemd graphical-session integration plus XDG autostart fallback.
- User-local installer, conservative uninstaller, tests, and documentation.
- Configurable `notify_on_startup` behavior.
- Daemon-only per-session duplicate-instance protection.

### Changed

- Defaulted startup notifications, optional audio, and notification replacement
  to disabled.
- Clarified pre-install runtime audio versus bundled sounds in installer output.

### Fixed

- Limited optional playback to ten seconds so a stalled audio backend cannot
  leave permanent `pw-play` children; added timeout and recovery regression tests.
- Retried the current notification once after a broken D-Bus connection, so an
  idle bus restart no longer loses the first subsequent profile notification.
  Added isolated notification-service and D-Bus recovery regression tests.
- Isolated service lifecycle tests from the installed notifier with private
  runtime locks and unique transient units.
- Prevented staged uninstalls and desktop-method reinstalls from stopping a
  service belonging to another installation; added regression coverage and
  protection against service-manager mutations in staged installer tests.
