# Troubleshooting & Diagnostics Guide

This guide assists in diagnosing issues with touchscreen edge gestures, hardware sensor detection, and input arbitration.

---

## 1. Quick Diagnostic Checks

### Verify Plugin and Effect Status
Check if KWin has loaded the `tablet_gestures` plugin and effect:
```bash
# Check loaded effects
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadedEffects | grep tablet_gestures

# Check loaded plugins
qdbus6 org.kde.KWin /Plugins org.kde.KWin.Plugins.LoadedPlugins | grep tablet_gestures
```

### Inspect Live D-Bus Interface
```bash
# Check current tablet mode state
qdbus6 org.kde.TabletGestures /TabletGestures org.kde.TabletGestures.isTabletMode

# List currently loaded zones
qdbus6 org.kde.TabletGestures /TabletGestures org.kde.TabletGestures.listZones

# Validate active zones configuration
qdbus6 org.kde.TabletGestures /TabletGestures org.kde.TabletGestures.validateZones

# Test-run an action directly via D-Bus
qdbus6 org.kde.TabletGestures /TabletGestures org.kde.TabletGestures.executeAction "launcher"
```

### Inspect Live Diagnostics Log (No Root Required)
Monitor structured diagnostic events from the runtime and configuration UI in real-time:
```bash
tail -F ~/.local/state/kwin-touch-gesture-plus/diagnostics.log
```
The log records button clicks, mode transitions, store migrations, and runtime reloads with privacy redaction for command parameters.

---

## 2. Inspecting KWin Journal Logs

To view real-time log messages from KWin:
```bash
journalctl --user -u plasma-kwin_wayland.service -f
```

Filter for gesture or input warnings:
```bash
journalctl --user -u plasma-kwin_wayland.service --since "10 minutes ago" | grep -iE "(tablet|touch|screenedge|gesture)"
```

---

## 3. Common Issues and Solutions

### Issue: Taps near screen corners are ignored
* **Cause**: You may be tapping within the configured corner safe zones (designed to protect close buttons and browser tabs).
* **Fix**: In **System Settings -> Desktop Effects -> Tablet Touch & Edge Gestures -> Tab 4 (Safe Zones)**, reduce `Top-Left Safe Zone Width` or `Top-Right Safe Zone Width` to `0` or a smaller pixel value.

### Issue: Swiping up from the bottom doesn't work while typing
* **Cause**: By default, `BottomGesturesWithOsk` is set to `Disabled` to prevent accidental gesture activations when tapping the virtual keyboard's spacebar or Enter key.
* **Fix**: If you use a split keyboard layout, change `BottomGesturesWithOsk` to `SplitGapOnly` so swiping through the center gap triggers the gesture.

### Issue: Tablet mode does not auto-switch on 360° hinge rotation
* **Cause**: IIO sensor proxy might not be exposing the hinge angle quaternion.
* **Diagnosis**:
  ```bash
  # Check IIO devices
  ls -l /sys/bus/iio/devices/
  # Test sensor proxy
  gdbus introspect --system --dest net.hadess.SensorProxy --object-path /net/hadess/SensorProxy
  ```
* **Fix**: If hardware sensors are unavailable on your model, you can toggle tablet mode manually via:
  ```bash
  qdbus6 org.kde.TabletGestures /TabletGestures org.kde.TabletGestures.toggleTabletMode
  ```

---

## 4. Emergency Rollback Procedure (Arch / CachyOS)

If you installed the patched KWin package and wish to revert to the untouched distribution stock package:

1. Switch to a virtual terminal (`Ctrl` + `Alt` + `F3`).
2. Log in with your user credentials.
3. Reinstall the stock package from pacman cache:
   ```bash
   sudo pacman -U --noconfirm /var/cache/pacman/pkg/kwin-6.7.5-3.1-*.pkg.tar.zst
   ```
4. Reboot into the stock desktop:
   ```bash
   sudo systemctl reboot
   ```
