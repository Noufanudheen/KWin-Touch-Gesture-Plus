# Arch Linux / CachyOS Packaging for Patched KWin

This directory contains the `PKGBUILD` to build a patched `kwin` package for Arch Linux and CachyOS incorporating the Pending-Touch Arbitration override (v6.7.5 release).

---

## Building the Package

1. Ensure the patch files from `kwin-patches/` are present alongside `PKGBUILD`:
   ```bash
   cd packaging/arch
   cp ../../kwin-patches/*.patch .
   ```

2. (Optional on CachyOS/Arch) If `plasma-wayland-protocols` is required:
   ```bash
   git clone https://invent.kde.org/libraries/plasma-wayland-protocols.git -b v1.16.0 plasma-wayland-protocols
   tar -czf plasma-wayland-protocols.tar.gz plasma-wayland-protocols
   ```

3. Run `makepkg` to build the standalone package archive:
   ```bash
   makepkg -s --skippgpcheck
   ```

4. Install the generated `.pkg.tar.zst` package:
   ```bash
   sudo pacman -U kwin-6.7.5-3.5-*.pkg.tar.zst
   ```

5. Log out and back into KDE Plasma to load the updated compositor binary.
