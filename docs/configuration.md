# Configuration Guide

KWin Touch Gesture Plus provides three complementary configuration mechanisms:
1. **KDE System Settings GUI** (Desktop Effects module)
2. **KConfig (`~/.config/kwinrc`)** for desktop integration
3. **JSON Zone Schema (`~/.config/kwin-touch-gesture-plus/zones.json`)** for extensible multi-zone customization

---

## 1. KDE System Settings GUI

Navigate to:
**System Settings -> Window Management -> Desktop Effects -> Tablet Touch & Edge Gestures**

The configuration UI is organized into four tabs:

### Tab 1: Touchscreen Gestures
- **Enable Gestures**: Master toggle for all edge gestures.
- **Top / Left / Right / Bottom Edge Toggles**: Individually enable or disable gesture listening per screen edge.
- **Swipe Pull Threshold**: Inward distance (in pixels, 15–120 px; default: 45 px) required to trigger gestures.
- **Screen Edge Gestures Mapping**: Dropdown action selectors for Top, Left, Right, Bottom-Left, Bottom-Center, and Bottom-Right zones.
  - Select builtins, KDE global shortcuts, or custom commands (displayed as `Custom: <Title>`).
  - Includes a dedicated `⚙ Custom Command... (Add / Edit)` navigation option that switches directly to Tab 2 to create or manage commands.
- **Hide Virtual Keyboard on Gesture**: Dismisses the virtual keyboard whenever an intentional navigation gesture triggers.
- **Bottom Navigation with Keyboard**:
  - `Disabled`: Suppresses all bottom edge gestures while the virtual keyboard is visible (prevents mis-hits while typing spacebar/enter).
  - `SplitGapOnly`: Only allows navigation gestures originating from the center split gap between keyboard halves.
  - `Always`: Always triggers gestures regardless of keyboard state.

### Tab 2: Custom Commands
- The command list starts empty on a new installation. Select an existing command to edit it, or click **New Command** to start with blank fields.
- **Add Command** becomes enabled after a non-empty command is entered. **Save Changes**, **Delete Command**, and **Discard** apply to the currently selected item.
- **Blank Title Auto-Replacement**: When adding or editing a command, leaving the Title field empty automatically populates it with the command string itself.
- Each command has a stable `custom_N` identifier, and editing its title does not change that identifier.
- The maximum number of commands is derived from the number of configured gesture hotspots. Existing legacy entries in `CustomCommand1` through `CustomCommand5` are migrated into the extensible `CustomCommandsJson` store.
- **Command Syntax Guide**: Inline reference explaining `exec:`, direct paths, `shortcut:`, and `dbus:` syntax.

### Tab 3: Tablet Mode & Auto-Scaling
- **Automatic Tablet Mode**: Sensor-driven detection via 360° hinge angle (IIO accelerometer quaternion), physical tablet switches (`/dev/input/event*`), or screen orientation.
- **Window Titlebar Button Scaling**: Automatically resizes titlebar buttons (e.g. from *Medium* in laptop mode to *Very Large* in tablet mode) across Breeze, Klassy, and SierraBreezeEnhanced.
- **Display Scaling**: Automatically switches display DPI scaling between tablet and laptop modes via `kscreen-doctor`.

### Tab 4: Safe Zones
- **Top-Left Safe Zone Width**: Horizontal margin in pixels (default: 100 px) where top edge gestures are ignored (protects browser tabs, hamburger menus, and back buttons).
- **Top-Right Safe Zone Width**: Horizontal margin in pixels (default: 150 px) where top edge gestures are ignored (protects window minimize/maximize/close buttons and extension icons).

---

## 2. KConfig Settings (`kwinrc`)

Stored under `~/.config/kwinrc`:

```ini
[Plugins]
tablet_gesturesEnabled=true

[Effect-tablet_gestures]
EnableTopEdge=true
EnableBottomEdge=true
EnableLeftEdge=true
EnableRightEdge=true

SwipeThreshold=45
HideKeyboardOnGesture=true
BottomGesturesWithOsk=Disabled

TopLeftSafeWidth=100
TopRightSafeWidth=150

# Edge Action Mappings
TopAction=fullscreen_or_expose
LeftAction=shortcut:plasmashell:manage activities
RightAction=shortcut:plasmashell:activate widget 157
BottomLeftAction=launcher
BottomCenterAction=shortcut:plasmashell:activate widget 111
BottomRightAction=shortcut:plasmashell:activate widget 145

# Legacy Custom Commands (read for migration; new saves use CustomCommandsJson)
# Example legacy values:
CustomCommand1=exec:/usr/bin/konsole
CustomCommand2=exec:flatpak run org.mozilla.firefox

# Tablet Hardware Automation
HingeAngleThreshold=185
HingePollInterval=350
EnableButtonScaling=true
TabletButtonSize=Very Large
LaptopButtonSize=Medium
EnableDisplayScaling=true
TabletScale=100%
LaptopScale=100%
```

---

## 3. Extensible JSON Zone Schema

To define custom zones, configure multiple regions on a single edge, or assign custom scripts, edit `~/.config/kwin-touch-gesture-plus/zones.json` (or import from `config/default_zones.json`):

```json
{
  "version": "1.0",
  "zones": [
    {
      "id": "bottom_left_launcher",
      "name": "Bottom Left: Application Launcher",
      "edge": "bottom",
      "startFraction": 0.0,
      "endFraction": 0.25,
      "gestureType": "inward_swipe",
      "activationThreshold": 45,
      "enabled": true,
      "action": "launcher",
      "oskPolicy": "Disabled",
      "priority": 0
    },
    {
      "id": "bottom_mid_dock",
      "name": "Bottom Center: Dock Toggle",
      "edge": "bottom",
      "startFraction": 0.25,
      "endFraction": 0.75,
      "gestureType": "inward_swipe",
      "activationThreshold": 45,
      "enabled": true,
      "action": "shortcut:plasmashell:activate widget 111",
      "oskPolicy": "Disabled",
      "priority": 0
    },
    {
      "id": "bottom_right_quick_calc",
      "name": "Bottom Right: Calculator",
      "edge": "bottom",
      "startFraction": 0.75,
      "endFraction": 1.0,
      "gestureType": "inward_swipe",
      "activationThreshold": 45,
      "enabled": true,
      "action": "exec:/usr/bin/kcalc",
      "oskPolicy": "Disabled",
      "priority": 10
    }
  ]
}
```

Reload configuration immediately without restarting KWin:
```bash
qdbus6 org.kde.TabletGestures /TabletGestures org.kde.TabletGestures.reloadConfig
```
