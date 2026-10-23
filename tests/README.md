# KWin Touch Gesture Plus — Test Suite

This directory contains automated unit and integration tests for the KWin Touch Gesture Plus system.

---

## 1. Unit Tests

### A. Zone Manager & Action Tests (`test_zone_manager`)
Tests the extensible `ZoneManager` and `ActionRegistry` components independently of KWin runtime:
- **Default Zones Verification**: Confirms initialization of standard 6 edge zones.
- **Zone Validation**: Tests range boundaries (0.0 to 1.0), start < end, safe margins, and duplicate IDs.
- **Normalized Position Matching**: Matches touch coordinates to zones along screen edges.
- **Corner Safe Zone Margins**: Verifies rejection of swipes in configured top-left and top-right safe zones.
- **Virtual Keyboard Policies**: Verifies zone suppression (`Disabled`) and center gap routing (`SplitGapOnly`) during visible OSK.
- **Conflict & Priority Resolution**: Validates deterministic matching when overlapping zones share an edge.
- **Action String Parsing**: Validates `shortcut:`, `dbus:`, `exec:`, and built-in actions.
- **JSON Serialization Round-Trip**: Verifies seamless loading and saving to `zones.json`.

### B. Custom Command Manager Tests (`test_command_manager`)
Tests the KCModule configuration widget and custom command workflow:
- **Initial State**: Validates blank initialization and action dropdown counts.
- **Dropdown Navigation**: Verifies selecting `⚙ Custom Command... (Add / Edit)` switches directly to Tab 2 and focuses the input.
- **Blank Title Auto-Replacement**: Verifies that adding a command without a title automatically populates the title with the command string itself and updates dropdown text to `Custom: <Title>`.
- **Edit, Save, & Discard**: Verifies in-place command edits, dirty state tracking, and discard actions.
- **Delete Reference Clearing**: Verifies deleting a command clears any referencing edge action back to `none`.
- **Configured Hotspot Limits**: Validates dynamic capacity matching the number of configured hotspots.
- **User-Owned Store Persistence**: Tests serialization to `~/.config/kwin-touch-gesture-plus/commands/`.
- **Defaults Retention**: Ensures resetting defaults preserves user-defined custom commands.
- **Structured Diagnostics & Privacy**: Verifies log emission and ensures raw command arguments/tokens are never logged.

### Building and Running Unit Tests

```bash
mkdir build && cd build
cmake .. -DBUILD_TESTING=ON
cmake --build .
ctest --output-on-failure
```

---

## 2. KWin Integration Tests (`screenedges_test`)

Included in the KWin source patch series (`kwin-patches/`):
- `testScreenEdges`: 39 integration tests verifying touch-edge pending arbitration, event replay, incomplete swipe retention, latched commitment, quarantine recovery, and signal ordering.
- `testTouchInput`: 10 integration tests validating touch target management across internal and client windows.

Execute via KWin's build directory:
```bash
ctest -R testScreenEdges --output-on-failure
ctest -R testTouchInput --output-on-failure
```
