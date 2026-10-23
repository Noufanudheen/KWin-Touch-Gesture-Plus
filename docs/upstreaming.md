# Upstreaming Proposal & KDE Integration Roadmap

## 1. Executive Summary

This document outlines the technical rationale, architectural invariants, and upstream contribution plan for integrating **Pending-Touch Edge Arbitration** into KDE KWin.

- **Target Component**: `kwin/src/screenedgegestures.cpp`, `src/screenedgegestures.h`, `src/input.cpp`.
- **Primary Goal**: Allow ordinary application touchscreen taps and inward system gestures to originate from the same screen edge margin without event loss or dead zones.
- **Reference Upstream Discussions**: [KDE Gestures in Graz and Beyond (2026)](https://blogs.kde.org/2026/05/03/gestures-in-graz-and-beyond), Plasma 6 Wayland Input Modernization.

---

## 2. Problem Statement: Why a Plugin-Only Solution is Insufficient

In KDE Plasma 6 Wayland, `ScreenEdgeInputFilter` runs at input priority order 10 (before Wayland client delivery).

When a plugin or core component registers an edge gesture via `ScreenEdges::self()->reserveTouch()`:
1. `ScreenEdgeInputFilter::touchDown()` invokes `ScreenEdgeGestureRecognizer::touchDown()`.
2. Because the touch starts within 25 px of the screen border, `touchDown()` returns `true` (event consumed).
3. The touch down event is **never delivered** to downstream filters, client window surfaces, decorations, or popups.
4. If the user was merely tapping an edge control (such as a browser tab, a window close button, a scrollbar, or an on-screen keyboard key), the tap is swallowed.
5. If the recognizer later cancels because no inward motion occurred, the initial touch-down cannot be recovered because KWin does not buffer early motion.

A third-party KWin effect or plugin cannot solve this from userspace because:
- KWin plugins do not execute before `ScreenEdgeInputFilter`.
- Synthesizing or injecting fake touch events from a plugin breaks Wayland touch protocol invariants (touch sequence IDs, frame groupings, and surface focus tracking).
- Disabling `reserveTouch()` leaves KWin without touch edge capabilities entirely.

**Conclusion**: Input arbitration must occur directly inside KWin's `ScreenEdgeGestureRecognizer`.

---

## 3. The Proposed Core Input-Routing Change

The core modification introduces pending-touch arbitration inside `ScreenEdgeGestureRecognizer`:

```
                       +-------------------------+
                       |   Physical TouchDown    |
                       +------------+------------+
                                    |
                                    v
                       +-------------------------+
                       |    Edge Margin Check    |
                       |       (within 25px)     |
                       +------------+------------+
                                    |
                                    v
                    +-------------------------------+
                    |  State: EdgeTouchState::Pending|
                    |  - Queue motion & frame       |
                    |  - Start 150ms Intent Timer   |
                    +---------------+---------------+
                                    |
            +-----------------------+-----------------------+
            | Inward motion         | Stationary / Parallel | Timeout (150ms)
            v                       v                       v
+-----------------------+   +-----------------------------------------------+
| State: Candidate      |   | Reject Gesture -> Replay Buffer to Application|
| - Keep queue intact   |   | - Validate Target Window                      |
| - started() signal    |   | - Replay Down + Motions via processFilters()  |
+-----------+-----------+   | - Trailing touch-ups stream natively to App   |
            |               +-----------------------------------------------+
            | Inward >= 44px
            v
+-----------------------+
| State: GestureOwned   |
| - Latch commitment    |
| - Clear queue         |
| - progress() updates  |
| - triggered() on lift |
+-----------------------+
```

### Architectural Invariants
1. **Re-entrancy Safety**: Replay iterates downstream filters without restarting the filter chain from order 0. A thread-local/instance recursion guard prevents recursive filter re-entry.
2. **Target Validation & Fail-Closed Quarantine**: If a target window is destroyed or minimized while events are buffered in `Pending`, buffered events are discarded immediately and the physical contact enters `EdgeTouchState::Quarantined` so lingering release events cannot misfire on a new window.
3. **Signal Ordering Guarantees**: Any gesture transitioning directly to committed `GestureOwned` emits `started()` before `progress()` and `triggered()`.
4. **Frame Synchronization**: `touchFrame()` strictly maintains Wayland frame boundaries (`wl_touch.frame`), ensuring no orphaned frames reach clients.

---

## 4. Test Coverage & Verification

### Automated Integration Tests
Tested against KWin's test suite via `ctest`:
- **`testScreenEdges`**: 39 passing tests (including `testQuarantineMixedContactRecovery` and `testDirectGestureCommitmentEmitsStartedBeforeProgress`).
- **`testTouchInput`**: 10 passing tests validating internal window and client decoration targets.
- **Pass rate**: 100% (49/49 passing).

### Physical Hardware QA
Validated on HP Elite Dragonfly G1 (Intel UHD Graphics, touchscreen, 360° hinge sensor, multi-touch capacitive panel):
- 31 manual QA tests passed with 100% success across 80+ touch interactions.
- Zero missed edge taps across 30+ repetitions per edge.
- Zero stuck gesture states following multi-touch pinch-zoom and keyboard typing.

---

## 5. Performance, Accessibility & Multi-Monitor Considerations

- **Latency**: Buffer evaluation occurs over 150 ms or terminates immediately upon 16 px inward displacement. Stationary taps feel native because 150 ms is well within human tap-release duration.
- **CPU & Memory**: The event queue stores at most ~10 touch points during the 150 ms window, adding < 1 KB of stack/heap memory per contact.
- **Accessibility**: Screen readers and assistive tools relying on edge taps (e.g. Orca, tap targets) regain full reliability.
- **Multi-Monitor**: Coordinates are normalized per `LogicalOutput`, ensuring multi-monitor setups with mixed scaling do not suffer from warped trigger distances.

---

## 6. Smallest Upstream Contribution (Phase 1 MR)

Rather than proposing a massive overhaul, the upstream contribution should be split into two incremental Merge Requests (MRs):

### MR 1: Core Recognizer Arbitration (Minimal Viable Upstream Patch)
- **Scope**: Modifications strictly confined to `src/screenedgegestures.h`, `src/screenedgegestures.cpp`, and integration tests in `autotests/integration/screenedges_test.cpp`.
- **Benefit**: Fixes broken edge taps for all existing KWin edge gesture users without altering high-level Plasma configuration.
- **Lines of Code**: ~250 lines added to `screenedgegestures.cpp`.

### MR 2: Modular Gesture Zone & Action Extensions
- **Scope**: Decoupling edge actions into an extensible zone manager and action registry (matching `KWin Touch Gesture Plus`).
- **Alignment**: Directly complements ongoing work from KDE's *Gestures in Graz* initiative to unify touchpad, touchscreen, and edge gesture bindings across Plasma.
