#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Installing KWin Touch Gesture Plus ==="

# Build plugin
BUILD_DIR="$SCRIPT_DIR/kwin_plugin/build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo "-> Configuring plugin build..."
cmake .. -DCMAKE_BUILD_TYPE=Release
echo "-> Building plugin..."
cmake --build .

echo "-> Installing KWin plugin to /usr/lib/qt6/plugins/kwin/plugins/..."
sudo cp "$BUILD_DIR/libtablet_gestures.so" /usr/lib/qt6/plugins/kwin/plugins/tablet_gestures.so

echo "-> Installing KWin effect config module to /usr/lib/qt6/plugins/kwin/effects/configs/..."
sudo cp "$BUILD_DIR/kwin_tablet_gestures_config.so" /usr/lib/qt6/plugins/kwin/effects/configs/kwin_tablet_gestures_config.so

# Install configuration effect
EFFECT_DIR="$HOME/.local/share/kwin/effects/tablet_gestures"
echo "-> Installing configuration effect UI to $EFFECT_DIR..."
mkdir -p "$EFFECT_DIR"
cp -r "$SCRIPT_DIR/kwin_effect/"* "$EFFECT_DIR/"

# Enable plugin and set defaults in kwinrc
echo "-> Configuring KWin settings..."
kwriteconfig6 --file kwinrc --group Plugins --key tablet_gesturesEnabled true
kwriteconfig6 --file kwinrc --group Input --key TabletMode auto
kwriteconfig6 --file kwinrc --group Wayland --key InputMethod /usr/share/applications/org.kde.plasma.keyboard.desktop
kwriteconfig6 --file kwinrc --group Wayland --key VirtualKeyboardEnabled true

# Screen locker and power settings
kwriteconfig6 --file kscreenlockerrc --group Daemon --key Autolock true
kwriteconfig6 --file kscreenlockerrc --group Daemon --key Timeout 5
kwriteconfig6 --file kscreenlockerrc --group Daemon --key LockOnResume true

# Optional keyboard split layouts
for loc in en_US en_GB; do
    if [ -d "/usr/share/plasma/keyboard/layouts/$loc" ]; then
        echo "-> Installing split keyboard layout for $loc..."
        sudo cp "$SCRIPT_DIR/keyboard_layouts/split/main.qml" "/usr/share/plasma/keyboard/layouts/$loc/main.qml"
        sudo chmod 664 "/usr/share/plasma/keyboard/layouts/$loc/main.qml"
    fi
done

# Reload KWin plugins and effects via D-Bus
echo "-> Reloading KWin components..."
qdbus6 org.kde.KWin /Plugins org.kde.KWin.Plugins.LoadPlugin "tablet_gestures" 2>/dev/null || true
qdbus6 org.kde.KWin /Effects org.kde.kwin.Effects.loadEffect "tablet_gestures" 2>/dev/null || true
qdbus6 org.kde.TabletGestures /TabletGestures org.kde.TabletGestures.reloadConfig 2>/dev/null || true
qdbus6 org.kde.KWin /KWin reconfigure 2>/dev/null || true

echo "=== Installation Complete ==="
echo "You can configure gestures and zones in System Settings -> Window Management -> Desktop Effects -> Tablet Touch & Edge Gestures"
