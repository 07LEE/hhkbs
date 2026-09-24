cmake_minimum_required(VERSION 3.25)

project(
    HHKBS
    VERSION 0.1.0
    DESCRIPTION "Unofficial HHKB Studio keymap editor for Linux"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(OpenGL REQUIRED)
set(GLFW_LIBRARY_TYPE STATIC CACHE STRING "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
add_subdirectory(third_party/glfw EXCLUDE_FROM_ALL)
find_package(Threads REQUIRED)

# Vendored, pinned Dear ImGui; only the GLFW/OpenGL backends are compiled.
add_library(hhkbs_imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(hhkbs_imgui SYSTEM PUBLIC
    third_party/imgui third_party/imgui/backends)
target_link_libraries(hhkbs_imgui PUBLIC glfw OpenGL::GL)

add_library(hhkbs_core STATIC
    src/device/DeviceDiscovery.cpp
    src/device/DeviceDiscovery.h
    src/device/DeviceError.h
    src/device/DeviceInfo.h
    src/device/HhkbStudioDevice.cpp
    src/device/HhkbStudioDevice.h
    src/device/HhkbProtocol.cpp
    src/device/HhkbProtocol.h
    src/device/HidrawTransport.cpp
    src/device/HidrawTransport.h
    src/device/Transport.h
    src/keymap/BackupFiles.cpp
    src/keymap/BackupFiles.h
    src/keymap/Keymap.cpp
    src/keymap/Keymap.h
    src/keymap/KeyboardLayout.cpp
    src/keymap/KeyboardLayout.h
    src/keymap/ProfileFiles.cpp
    src/keymap/ProfileSerializer.cpp
    src/keymap/ProfileSerializer.h
    src/keymap/ScanCodeCatalog.cpp
    src/keymap/ScanCodeCatalog.h
)

target_include_directories(hhkbs_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(hhkbs_core PUBLIC cxx_std_20)

add_executable(hhkbs
    src/main.cpp
    src/gui/KeyAssignmentDialog.cpp
    src/gui/KeyAssignmentDialog.h
    src/gui/KeyboardWidget.cpp
    src/gui/KeyboardWidget.h
    src/gui/MainWindow.cpp
    src/gui/MainWindow.h
)

set_target_properties(hhkbs PROPERTIES
    OUTPUT_NAME "hhkbs"
)

target_link_libraries(hhkbs
    PRIVATE
        hhkbs_core
        hhkbs_imgui
        Threads::Threads
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(hhkbs_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(hhkbs PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(GNUInstallDirs)
install(
    TARGETS hhkbs
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)

# Keep upstream notices in place; install the consolidated distribution copies.
install(FILES
    LICENSE
    third_party/licenses/DearImGui.txt
    third_party/licenses/GLFW.txt
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/hhkbs"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(hhkbs_keymap_tests
        tests/KeymapTests.cpp
    )

    target_link_libraries(hhkbs_keymap_tests PRIVATE hhkbs_core)

    add_executable(hhkbs_device_tests
        tests/HhkbStudioDeviceTests.cpp
    )

    target_link_libraries(hhkbs_device_tests PRIVATE hhkbs_core)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hhkbs_keymap_tests PRIVATE -Wall -Wextra -Wpedantic)
        target_compile_options(hhkbs_device_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(hhkbs_profile_files_tests tests/ProfileFilesTests.cpp)
    target_link_libraries(hhkbs_profile_files_tests PRIVATE hhkbs_core)
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        add_test(NAME gui_startup COMMAND "${XVFB_RUN}" -a $<TARGET_FILE:hhkbs> --smoke-test)
        set_tests_properties(gui_startup PROPERTIES TIMEOUT 20)
    endif()
    add_test(NAME profile_files COMMAND hhkbs_profile_files_tests)

    add_test(NAME keymap_model COMMAND hhkbs_keymap_tests)
    add_test(NAME hhkb_device_protocol COMMAND hhkbs_device_tests)
endif()
