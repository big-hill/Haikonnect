# Changelog

All notable Haikonnect changes will be documented here.

## Unreleased

### Added

- Native Haiku x86_64 platform detection and build target.
- `BScreen`-based capture, cursor and clipboard integration.
- Authenticated Haiku input-server devices for pointer and keyboard control.
- Per-user launch service, persistent Haikonnect Deskbar status replicant and
  runtime doctor.
- Background-flagged per-user Python launcher so the agent remains visible in
  process monitors without appearing as a separate Deskbar application.
- Haiku-compatible Filesystem backend and file-backed inter-process screen maps.
- Repository security policy, contribution guide, CI and secret scanning.

### Changed

- Renamed the project and native Haiku identity from BeRD to Haikonnect, with
  scoped cleanup of the former Deskbar replicant and private Python launcher.
- Disabled unsupported upstream binary auto-updates, local HTTP listener and
  sound capture in generated Haiku source-mode configurations.

### Fixed

- Generate native Haiku keymap bytes and modifier-change sequences for Control,
  Shift and Alt/Command shortcuts when the client delivers their modifier
  flags, including terminal control keys.
- Hold both coalesced and raw mouse-button transitions long enough for Haiku
  menu tracking, release old buttons before new presses and attach native
  multi-click metadata.
- Stage input add-on updates outside Haiku's monitored device directory so the
  final image is loaded under the correct name without waiting for a reboot.
