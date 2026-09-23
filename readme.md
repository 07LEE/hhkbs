# HHKBS

HHKBS is a visual HHKB Studio keymap editor for Linux. It discovers the keyboard automatically, reads the active profile, and lets you edit all four key layers without relying on a fixed `/dev/hidraw` path.

The current release targets the US-layout HHKB Studio on Ubuntu 26.04. HHKBS is written in C++20 with Qt 6 Widgets. A single-file AppImage is planned for a later release.

## Status

The editor currently supports:

- automatic USB device and configuration-interface discovery
- read-only loading of device information and the active 960-byte profile
- a scalable 60-key US layout and three pointing-stick mouse buttons
- Base, Fn1, Fn2, and Fn3 layer editing
- keyboard, keypad, media, mouse, and HHKB Studio device functions
- searchable assignments and direct 16-bit scan-code entry
- TOML profile import and export
- per-key change highlighting and discarding unsaved edits
- restoring the HHKB Studio US Profile 1 factory defaults in the editor
- four gesture pads with editable assignments for all eight slide directions
- an offline demo mode for development without a connected keyboard

Writing profiles to the keyboard is intentionally disabled until the backup, validation, and recovery flow is implemented. Editing, importing, restoring defaults, and exporting only change the in-memory profile.

## Requirements

- Linux with `hidraw` support
- a C++20 compiler
- CMake 3.25 or newer
- Qt 6 Widgets development files

On Ubuntu, install the build dependencies with:

```bash
sudo apt install build-essential cmake qt6-base-dev
```

## Build

Create a release build from the repository root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

Run the tests:

```bash
ctest --test-dir build --output-on-failure
```

## Device permissions

HHKBS needs read and write permission for the HHKB Studio `hidraw` interface. Install the included udev rule once:

```bash
sudo install -m 0644 packaging/60-hhkbs.rules /etc/udev/rules.d/60-hhkbs.rules
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Reconnect the keyboard after installing the rule. Run HHKBS as your normal user; do not run the application with `sudo`.

## Run

Connect the keyboard over USB and start HHKBS:

```bash
./build/HHKBS
```

If no keyboard is available, start with the built-in US Profile 1 defaults:

```bash
./build/HHKBS --demo
```

## Editing a profile

1. Select Base, Fn1, Fn2, or Fn3.
2. Select a key or gesture-pad direction and choose an assignment.
3. Use **Export** to save the complete profile as TOML.

**Restore defaults** replaces the editor contents with the built-in US Profile 1 factory layout. **Discard changes** returns to the profile most recently read or imported. Neither action writes to the keyboard.

## Profile files

TOML exports contain four layers with 120 unsigned 16-bit scan codes per layer. HHKBS preserves every slot, including reserved entries that are not shown in the visual editor, so profiles can be exported and imported without losing data.

## Scope

HHKBS currently supports the HHKB Studio USB identifier `04fe:0016` and the US layout. Bluetooth editing, JIS layout support, profile selection, device writing, recovery backups, and AppImage packaging remain future work.
