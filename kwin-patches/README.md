# KWin Pending-Touch Arbitration Patch Series

This directory contains the sequential source patches for KDE KWin (Plasma 6, tested against KWin v6.7.5).

These patches resolve the core input arbitration problem in KWin where registering a touch edge gesture via `ScreenEdges::reserveTouch()` traditionally consumed every touch contact beginning within 25 px of the screen border before user swipe intent was determined, breaking normal taps, window controls, and edge scrolling.

---

## Patch Series Overview

Apply these patches in strict numerical sequence (`0001` -> `0002` -> `0003` -> `0004`):

### 1. `0001-screenedge-pending-touch-arbitration-v2.patch`
* **Target Files**: `src/screenedgegestures.h`, `src/screenedgegestures.cpp`, `src/input.h`, `src/input.cpp`, `autotests/integration/screenedges_test.cpp`.
* **Description**: Introduces pending-touch arbitration into `ScreenEdgeGestureRecognizer`.
* **Mechanism**:
  - Adds `EdgeTouchState::Pending` and a 150 ms intent timeout window.
  - Buffers all motion events and frame boundaries for contacts starting in the edge margin.
  - If the touch is stationary, parallel, or times out without inward motion, ownership transfers to the application by replaying the buffered events downstream via `InputRedirection::processFilters()`.

### 2. `0002-screenedge-preserve-incomplete-gesture.patch`
* **Target Files**: `src/screenedgegestures.h`, `src/screenedgegestures.cpp`, `autotests/integration/screenedges_test.cpp`.
* **Description**: Preserves touch event history when an inward swipe does not reach full commitment.
* **Mechanism**:
  - Introduces `EdgeTouchState::GestureCandidate` (between 16 px and 44 px).
  - Retains the buffered motion queue during the candidate phase rather than dropping it prematurely.
  - If the contact lifts before crossing the 44 px activation distance, the buffered sequence is cleanly flushed to the application, ensuring incomplete swipes do not disappear.

### 3. `0003-screenedge-ownership-finalization.patch`
* **Target Files**: `src/screenedgegestures.h`, `src/screenedgegestures.cpp`, `autotests/integration/screenedges_test.cpp`.
* **Description**: Handles edge cases during active gesture ownership.
* **Mechanism**:
  - **Latched Commitment**: Crossing 44 px latches `m_gestureCommitted = true`. If the finger retreats towards the edge before release, the gesture remains committed and fires on release.
  - **Target Window Invalidation & Quarantine**: If the target window is closed or obscured during `Pending`, buffered events are dropped safely and the contact enters `EdgeTouchState::Quarantined` so trailing motion/release events are consumed without leaking.
  - **Multi-Touch During Gestures**: Tracks and consumes secondary touches arriving during `GestureOwned`.

### 4. `0004-screenedge-quarantine-recovery.patch`
* **Target Files**: `src/screenedgegestures.h`, `src/screenedgegestures.cpp`, `autotests/integration/screenedges_test.cpp`.
* **Description**: Quarantined contact recovery for interleaved multi-touch and signal sequence guarantees.
* **Mechanism**:
  - **Interleaved Contact Recovery**: When a quarantined contact and an independent application contact overlap in time, ensures `EdgeTouchState` properly returns to `Idle` regardless of which contact lifts first.
  - **Frame Synchronization**: Introduces `m_forwardNextFrame` to ensure application contacts receive frame boundaries while purely quarantined contacts consume them.
  - **Signal Sequence Ordering**: Guarantees that direct commitments crossing 44 px from `Pending` emit `started()` prior to `progress()` and `triggered()`.

---

## Patch Verification & Checksums

| Patch File | Size (Bytes) | SHA-256 Checksum |
| :--- | :--- | :--- |
| `0001-screenedge-pending-touch-arbitration-v2.patch` | 40,229 | `bc09bead8a06d46ebfd52bf3dc9c440a11bc91991248adb05a51a156694a6779` |
| `0002-screenedge-preserve-incomplete-gesture.patch` | 19,805 | `f819b0d3e2edfdeb4f29b0417f2c07fa49d4e22cddb03ce8078f8e609102320a` |
| `0003-screenedge-ownership-finalization.patch` | 33,376 | `98a58999ee266985944ab96818fc676b0b57a802f3223684a68d72b68ad98998` |
| `0004-screenedge-quarantine-recovery.patch` | 13,016 | `5db80aeccb71a483382ea930f21f2ba3fdefd761e0c80fc7dfca9bcb158ae767` |

---

## Applying to KWin Source

```bash
git clone https://invent.kde.org/plasma/kwin.git -b v6.7.5
cd kwin

patch -Np1 -i /path/to/kwin-patches/0001-screenedge-pending-touch-arbitration-v2.patch
patch -Np1 -i /path/to/kwin-patches/0002-screenedge-preserve-incomplete-gesture.patch
patch -Np1 -i /path/to/kwin-patches/0003-screenedge-ownership-finalization.patch
patch -Np1 -i /path/to/kwin-patches/0004-screenedge-quarantine-recovery.patch
```
