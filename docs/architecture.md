# System Architecture

KWin Touch Gesture Plus provides an end-to-end touchscreen gesture solution for KDE Plasma 6.

Its architecture separates **low-level input arbitration** (which requires compositor-level touch-point lifecycle ownership) from **high-level gesture configuration and policy dispatching** (which runs as a modular KWin plugin).

---

## 1. Architectural Overview

```
                      +-----------------------------+
                      |       Physical Touch        |
                      +--------------+--------------+
                                     |
                                     v
                      +-----------------------------+
                      |      libinput / Evdev       |
                      +--------------+--------------+
                                     |
                                     v
+-------------------------------------------------------------------------+
| KWin Wayland Compositor (Core Engine)                                  |
|                                                                         |
|  +-------------------------------------------------------------------+  |
|  | ScreenEdgeInputFilter (Order 10)                                  |  |
|  |                                                                   |  |
|  |  +-------------------------------------------------------------+  |  |
|  |  | ScreenEdgeGestureRecognizer (Patched Arbitration)           |  |  |
|  |  |                                                             |  |  |
|  |  |   - Edge margin touch enters 'Pending' state (buffers events)| |  |
|  |  |   - 150 ms timer / distance evaluation                      |  |  |
|  |  |   - If tap / parallel scroll: REPLAY buffer to Application   |  |  |
|  |  |   - If inward swipe: transition to 'Candidate' -> 'Owned'    |  |  |
|  |  |   - Emits: started() -> progress() -> triggered()            |  |  |
|  |  +-----------------------------+-------------------------------+  |  |
|  +--------------------------------|----------------------------------+  |
|                                   | QAction::triggered()                |
+-----------------------------------|-------------------------------------+
                                    |
                                    v
+-------------------------------------------------------------------------+
| KWin Touch Gesture Plus (Plugin Layer)                                  |
|                                                                         |
|  +-------------------------------------------------------------------+  |
|  | TabletGesturesPlugin                                              |  |
|  |                                                                   |  |
|  |  +-----------------------------------+  +-----------------------+ |  |
|  |  | ZoneManager                       |  | ActionRegistry        | |  |
|  |  |                                   |  |                       | |  |
|  |  | - Data-driven multi-zone lookup   |  | - Global Shortcuts    | |  |
|  |  | - Normalized edge fractions       |  | - QProcess (No shell) | |  |
|  |  | - Safe-zone corner exclusion      |  | - DBus RPC Calls      | |  |
|  |  | - OSK policy arbitration          |  | - KWin Builtins       | |  |
|  |  | - Deterministic priority sorting  |  | - Failure isolation   | |  |
|  |  +-----------------+-----------------+  +-----------+-----------+ |  |
|  +--------------------|--------------------------------|-------------+  |
+-----------------------|--------------------------------|----------------+
                        |                                |
                        v                                v
                 Selected Action                 Executed Target
          (e.g., Application Launcher,       (PlasmaShell, App Launch,
              Task Switcher, Dock)               or Window Action)
```

---

## 2. Low-Level Input Arbitration (Core KWin)

### The Problem in Stock KWin
In stock KWin, `ScreenEdgeInputFilter::touchDown()` intercepts any touch starting within 25 px of a registered screen edge and unconditionally returns `true` (consumed). Because the touch down is never delivered to Wayland client windows or window decorations, ordinary taps on close buttons, browser tabs, or scrollbars are permanently dropped or delayed until gesture timeout.

### The Solution: Pending-Touch Arbitration
The core patch series introduces a non-blocking queue inside `ScreenEdgeGestureRecognizer`:

1. **State `Pending`**:
   - The first touch contact within the edge margin enters `Pending`.
   - The original `touchDown`, subsequent `touchMotion`, and frame boundaries are queued in `m_pendingQueue`.
   - A 150 ms intent timer starts.
2. **Rejection & Replay**:
   - If the finger lifts within 150 ms without exceeding inward threshold, or moves parallel to the edge, or times out:
   - The target window is validated.
   - All buffered events are replayed downstream via `InputRedirection::processFilters()` using a re-entrancy guard.
   - The application receives the tap or scroll natively as if no gesture filter existed.
3. **Candidate & Latched Ownership**:
   - Inward displacement past 16 px transitions to `GestureCandidate`.
   - Displacement past 44 px transitions to `GestureOwned` and latches commitment (`m_gestureCommitted = true`).
   - If the user swiped past 44 px and retreats before lifting, the gesture still triggers cleanly once.

---

## 3. High-Level Plugin Layer

### ZoneManager
- Replaces fixed hardcoded edge actions with a normalized, data-driven coordinate model.
- Screen edges are represented as continuous fractions from `0.0` to `1.0`.
- Allows multiple arbitrary zones on any edge (e.g. splitting the bottom edge into 3 or 4 independent action triggers).
- Resolves conflicts deterministically through zone priority.

### ActionRegistry
- Decouples gesture detection from command execution.
- Security-first execution: external programs are invoked directly via `QProcess` without passing arbitrary strings through `/bin/sh`.
- Global shortcuts route cleanly through KDE's `org.kde.kglobalaccel` service.
- DBus calls are validated and executed asynchronously.
- Fails safely without leaving the compositor or recognizer in a corrupted state.
