# Gestures, Zones, and Actions Guide

This guide explains how to define custom gesture zones, split screen borders into multiple action areas, and configure custom actions.

---

## 1. Normalized Edge Geometry

Rather than hardcoding physical screen pixel coordinates, KWin Touch Gesture Plus uses **normalized edge fractions** from `0.0` to `1.0`:

- **Horizontal Edges (`top`, `bottom`)**:
  - `0.0` corresponds to the left-most corner of the active screen.
  - `1.0` corresponds to the right-most corner of the active screen.
- **Vertical Edges (`left`, `right`)**:
  - `0.0` corresponds to the top-most corner of the active screen.
  - `1.0` corresponds to the bottom-most corner of the active screen.

This ensures that your gesture zones adapt dynamically across screen rotations, external monitors, and display scaling changes without manual recalculation.

---

## 2. Defining Multi-Zone Edges

You can partition any screen edge into as many distinct regions as desired.

### Example: Splitting the Bottom Edge into 4 Quarters

```json
[
  {
    "id": "bot_quarter_1",
    "name": "Launcher",
    "edge": "bottom",
    "startFraction": 0.0,
    "endFraction": 0.25,
    "action": "launcher"
  },
  {
    "id": "bot_quarter_2",
    "name": "File Manager",
    "edge": "bottom",
    "startFraction": 0.25,
    "endFraction": 0.50,
    "action": "exec:/usr/bin/dolphin"
  },
  {
    "id": "bot_quarter_3",
    "name": "Taskbar Dock",
    "edge": "bottom",
    "startFraction": 0.50,
    "endFraction": 0.75,
    "action": "shortcut:plasmashell:activate widget 111"
  },
  {
    "id": "bot_quarter_4",
    "name": "Notifications",
    "edge": "bottom",
    "startFraction": 0.75,
    "endFraction": 1.0,
    "action": "shortcut:plasmashell:activate widget 157"
  }
]
```

### Example: Split Left Edge (Top Half vs Bottom Half)

```json
[
  {
    "id": "left_top_half",
    "name": "Overview",
    "edge": "left",
    "startFraction": 0.0,
    "endFraction": 0.50,
    "action": "overview"
  },
  {
    "id": "left_bottom_half",
    "name": "Manage Activities",
    "edge": "left",
    "startFraction": 0.50,
    "endFraction": 1.0,
    "action": "shortcut:plasmashell:manage activities"
  }
]
```

---

## 3. Conflict Resolution & Overlaps

If two zones overlap on the same edge, the `priority` integer property resolves the conflict deterministically:
- Higher priority values take precedence over lower priority values.
- If priorities are identical, the first declared zone wins and a diagnostic warning is logged via `validateAll()`.

### Example: Custom Corner Zone Overriding an Entire Edge

```json
{
  "id": "top_general",
  "name": "Top Edge Expose",
  "edge": "top",
  "startFraction": 0.0,
  "endFraction": 1.0,
  "action": "fullscreen_or_expose",
  "priority": 0
},
{
  "id": "top_right_corner_action",
  "name": "Top Right Close / Escape",
  "edge": "top",
  "startFraction": 0.90,
  "endFraction": 1.0,
  "action": "shortcut:kwin:Window Close",
  "priority": 100
}
```

Swipes in the rightmost 10% of the top edge trigger `Window Close` (priority 100), while swipes across the rest of the edge trigger `fullscreen_or_expose` (priority 0).

---

## 4. Virtual Keyboard Policies (`oskPolicy`)

Virtual keyboards (such as `plasma-keyboard` or Maliit) sit docked at the bottom of the screen. Accidental swipes while tapping spacebar or enter are frustrating.

Zones support three `oskPolicy` modes:
1. `"Disabled"`: The zone is completely inactive while the keyboard is visible. (Default for bottom zones).
2. `"SplitGapOnly"`: The zone only activates if the swipe originates within the center gap of a split keyboard (normalized range between `0.44` and `0.56`). Swipes over keyboard keys are ignored.
3. `"Always"`: The zone always listens, even when the keyboard is open.

---

## 5. Supported Action Schemes

| Action Scheme | Syntax | Description | Example |
| :--- | :--- | :--- | :--- |
| **Builtin** | `<name>` | Standard KWin and Plasma actions | `launcher`, `fullscreen_or_expose`, `overview`, `expose`, `show_desktop`, `virtual_keyboard` |
| **Global Shortcut** | `shortcut:<component>:<action>` | Registered KDE KGlobalAccel shortcut | `shortcut:plasmashell:manage activities`, `shortcut:kwin:Overview` |
| **External Command** | `exec:<executable> [args...]` | Spawns a detached process via `QProcess` (no shell execution) | `exec:/usr/bin/spectacle -r`, `exec:/usr/bin/brightnessctl set +10%` |
| **D-Bus RPC** | `dbus:<service>:<path>:<iface>:<method>` | Asynchronous session bus D-Bus call | `dbus:org.kde.plasmashell:/PlasmaShell:org.kde.PlasmaShell:activateLauncherMenu` |
