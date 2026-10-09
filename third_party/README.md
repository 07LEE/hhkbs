# Third-party licenses

| Component | Version | License |
| --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui/tree/v1.91.9b) | 1.91.9b | [MIT](licenses/DearImGui.txt) |
| [GLFW](https://github.com/glfw/glfw/tree/3.4) | 3.4 | [zlib](licenses/GLFW.txt) |

## Modifications

GLFW is an altered copy of 3.4. The files that are left are byte for byte the
ones in the 3.4 release; files were removed, none were edited.

Removed, because HHKBS builds for Linux with X11 only: the Cocoa, Win32 and
Wayland backends and their context code, the MinGW toolchain files, and the
platform helper files (`Info.plist.in`, `glfw.rc.in`). Also removed, because
they are not needed to build the library: `docs/`, `examples/`, `tests/`,
`deps/`, `README.md`, `CONTRIBUTORS.md` and the upstream repository's own files
(`.editorconfig`, `.github/`, `.mailmap`).

The `CMakeLists.txt` files that remain still mention some of the removed files.
The top-level `CMakeLists.txt` turns those parts off (`GLFW_BUILD_WAYLAND`,
`GLFW_BUILD_EXAMPLES`, `GLFW_BUILD_TESTS` and `GLFW_BUILD_DOCS` are forced to
OFF), so configuring only works with those options left as they are.

Dear ImGui is not modified: the sixteen files kept are the ones of 1.91.9b that
the core and the GLFW and OpenGL 3 backends need.
