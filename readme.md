# HHKBS

HHKBS is a Linux desktop application for configuring an HHKB Studio with a
visual keymap editor.

The first release targets the US-layout HHKB Studio on Ubuntu 26.04. The
application is written in C++20 with Qt 6 Widgets and will be distributed as a
single AppImage.

## Current status

Phase 1 provides the project foundation:

- a CMake-based Qt 6 desktop application
- a Qt-independent keymap data model
- profile decoding and encoding for four 240-byte layers
- change tracking and reset support
- core model tests through CTest

Device discovery and read-only communication are planned for phase 2.

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
