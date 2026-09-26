# Third-party licenses

| Component | Version | License |
| --- | --- | --- |
| [Dear ImGui](https://github.com/ocornut/imgui/tree/v1.91.9b) | 1.91.9b | [MIT](licenses/DearImGui.txt) |
| [GLFW](https://github.com/glfw/glfw/tree/3.4) | 3.4 | [zlib](licenses/GLFW.txt) |

## Modifications

GLFW is an altered copy of 3.4: the Cocoa, Win32 and Wayland backends, their
context code and the MinGW toolchain files were removed because HHKBS builds
for Linux with X11 only. Nothing else was changed.
