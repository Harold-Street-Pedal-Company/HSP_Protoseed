# HaroldPCB Library Changelog

All notable changes to this project will be documented in this file.

## [1.1.1] - 2025-08-11
### Changed
- Mono audio path now explicitly mutes the Right channel (`_MonoThunk` sets `out[1][i] = 0.0f`), matching docs and passthrough example.

## [1.1.0] - 2025-08-11
### Added
- Inline reusable helpers inside `HaroldPCB`: `LFO`, `OnePole`, `Mix()`, `ModToGain()`, and `EveryN`, plus `LEDLFO` for visual tempo.
- `Version()` method and compile-time macros `HPCB_VERSION_*` / `HPCB_VERSION_STR`.
- Doxygen comments for all public APIs.
- Example: basic tremolo (depth/rate with triangle/square via TS1), LED pulse.

### Changed
- None.

### Fixed
- None.

---

## [1.0.0] - 2025-08-01
### Added
- Initial public release targeting HaroldPCB v1.2 mapping.
- Mono audio callback path and audio start/stop.
- Pot read (raw/mapped/smoothed), toggle read, basic footswitch read.
- LED control (active-low abstraction).
