# Changelog

All notable changes to this project will be documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed

- Defaulted startup notifications, optional audio, and notification replacement
  to disabled.
- Clarified pre-install runtime audio versus bundled sounds in installer output.

### Added

- Configurable `notify_on_startup` behavior.
- Daemon-only per-session duplicate-instance protection.

## [0.1.0] - 2026-09-02

### Added

- Event-driven Linux platform-profile monitoring using sysfs `EPOLLPRI`.
- Freedesktop notifications with replacement support.
- Optional profile-specific `pw-play` audio with three bundled WAV sounds.
- XDG INI configuration and graceful unknown-profile handling.
- Compatibility diagnostics, CLI test actions, and quiet/verbose logging.
- systemd graphical-session integration plus XDG autostart fallback.
- User-local installer, conservative uninstaller, tests, and documentation.
