# HHKBS

HHKBS is a Linux desktop application for configuring an HHKB Studio with a
visual keymap editor.

The first release targets the US-layout HHKB Studio on Ubuntu 26.04. The
application is written in C++20 with Qt 6 Widgets and will be distributed as a
single AppImage.

## Current status

Phase 2 provides the project foundation and read-only device communication:

- a CMake-based Qt 6 desktop application
- a Qt-independent keymap data model
- profile decoding and encoding for four 240-byte layers
- change tracking and reset support
- core model tests through CTest
- automatic discovery of HHKB Studio HID interfaces
- device information and current-profile reading
- non-blocking device I/O with connection and permission status in the GUI

Visual key rendering and assignment editing are planned for phase 3.

## Build

Requirements:

- a C++20 compiler
- CMake 3.25 or newer
- Qt 6 Widgets development files

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

Run the application:

```bash
./build/HHKBS
```

Run the tests:

```bash
ctest --test-dir build --output-on-failure
```

## Device permissions

HHKBS needs read/write access to the HHKB Studio hidraw interface. If the app
reports a permission error, install the included udev rule once:

```bash
sudo install -m 0644 packaging/60-hhkbs.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

Reconnect the keyboard after installing the rule. The application itself
should always be run as a normal user.
