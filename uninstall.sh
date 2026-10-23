#!/usr/bin/env bash
set -e

echo "=== Uninstalling KWin Touch Gesture Plus ==="

# Disable plugin in kwinrc
kwriteconfig6 --file kwinrc --group Plugins --key tablet_gesturesEnabled false

# Remove plugin binary
if [ -f "/usr/lib/qt6/plugins/kwin/plugins/tablet_gestures.so" ]; then
    echo "-> Removing /usr/lib/qt6/plugins/kwin/plugins/tablet_gestures.so..."
    sudo rm -f "/usr/lib/qt6/plugins/kwin/plugins/tablet_gestures.so"
fi

if [ -f "/usr/lib/qt6/plugins/kwin/effects/configs/kwin_tablet_gestures_config.so" ]; then
    echo "-> Removing /usr/lib/qt6/plugins/kwin/effects/configs/kwin_tablet_gestures_config.so..."
    sudo rm -f "/usr/lib/qt6/plugins/kwin/effects/configs/kwin_tablet_gestures_config.so"
fi

# Remove configuration effect UI
EFFECT_DIR="$HOME/.local/share/kwin/effects/tablet_gestures"
if [ -d "$EFFECT_DIR" ]; then
    echo "-> Removing $EFFECT_DIR..."
    rm -rf "$EFFECT_DIR"
fi

# Reload KWin
echo "-> Reconfiguring KWin..."
qdbus6 org.kde.KWin /Plugins org.kde.KWin.Plugins.UnloadPlugin "tablet_gestures" 2>/dev/null || true
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.unloadEffect "tablet_gestures" 2>/dev/null || true
qdbus6 org.kde.KWin /KWin reconfigure 2>/dev/null || true

echo "=== Uninstallation Complete ==="
