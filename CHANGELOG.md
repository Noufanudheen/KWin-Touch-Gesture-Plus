# Changelog

All notable changes to **KWin Touch Gesture Plus** are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.3.2] - 2026-10-23

### Changed
- **Unified Custom Command Management**:
  - Removed edge dropdown pencil buttons from the configuration UI in favor of unified command management on Tab 2 (Custom Commands).
  - Added dedicated navigation action `⚙ Custom Command... (Add / Edit)` in all edge-action dropdowns to seamlessly switch to Tab 2 and focus the command input.
  - Dropdown custom command labels now display as `Custom: <Title>` for instant clarity.
- **Blank Title Automatic Fallback**:
  - If the Title field is left empty when adding or editing a custom command, the title automatically defaults to the command string itself.

### Added
- **Structured Diagnostics Logging**:
  - Non-root user diagnostics stream located at `~/.local/state/kwin-touch-gesture-plus/diagnostics.log`.
  - Privacy preservation: shell command arguments and sensitive credentials are redacted.
- **Automated Command Manager Test Suite**:
  - Integrated `test_command_manager` into `ctest` suite verifying dropdown navigation, blank title auto-population, add/edit/save/discard/delete workflows, store persistence, and log privacy.

---

## [1.0.0] - 2026-10-22

### Added
- **Pending-Touch Edge Arbitration (V5)**:
  - 150 ms intent arbitration queue in `ScreenEdgeGestureRecognizer`.
  - Motion event and frame boundary replay downstream to Wayland clients.
  - Safe-zone corner exclusion (top-left and top-right margins).
  - Incomplete gesture preservation: buffered history flushes to applications when swipes lift before 44 px.
  - Latched commitment: crossing 44 px guarantees gesture trigger even if finger retreats prior to release.
  - Target window invalidation handling with fail-closed quarantine.
  - Interleaved multi-touch quarantine recovery and frame synchronization (`m_forwardNextFrame`).
  - Strict signal ordering guarantees: `started()` -> `progress()` -> `triggered()`.
- **Extensible Zone Management (`ZoneManager`)**:
  - Data-driven multi-zone engine supporting arbitrary user-defined regions on any screen edge.
  - Normalized continuous fractions (`0.0` to `1.0`) adapting dynamically to screen rotations and scaling.
  - Deterministic priority conflict resolution.
  - Virtual keyboard awareness (`Disabled`, `SplitGapOnly`, `Always`).
  - JSON configuration import/export with default 6-zone fallback.
- **Extensible Action System (`ActionRegistry`)**:
  - Secure decoupled action dispatching.
  - Native support for KDE Global Shortcuts (`shortcut:`), External Commands via discrete `QProcess` arguments (`exec:`), D-Bus RPC (`dbus:`), and KWin Builtins.
  - Graceful failure handling and isolation from recognizer lifecycle.
- **Hardware Tablet Mode Integration**:
  - Sensor monitoring via IIO quaternion relative orientation (360° convertible hinges).
  - Physical switch monitoring (`SW_TABLET_MODE` via `/dev/input/event*`).
  - Automatic titlebar button scaling (Breeze, Klassy, SierraBreezeEnhanced) and display DPI scaling.
- **Comprehensive Test Suite**:
  - 10 automated unit test cases in `test_zone_manager`.
  - 49 integration tests in KWin test suite (`testScreenEdges` and `testTouchInput`).
  - 31 physical hardware QA test cases verified on HP Elite Dragonfly G1.
