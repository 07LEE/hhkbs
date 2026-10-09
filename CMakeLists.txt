cmake_minimum_required(VERSION 3.25)

project(
    HHKBS
    VERSION 0.4.0
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
    src/app/BackupList.cpp
    src/app/BackupList.h
    src/app/KeyboardTasks.cpp
    src/app/KeyboardTasks.h
    src/app/ProfileWorkspace.cpp
    src/app/ProfileWorkspace.h
    src/device/DeviceDiscovery.cpp
    src/device/DeviceDiscovery.h
    src/device/DeviceError.h
    src/device/DeviceInfo.h
    src/device/HhkbStudioDevice.cpp
    src/device/HhkbStudioDevice.h
    src/device/HhkbProtocol.cpp
    src/device/HhkbProtocol.h
    src/device/PadMonitor.cpp
    src/device/PadMonitor.h
    src/device/HidDescriptor.cpp
    src/device/HidDescriptor.h
    src/device/HidrawTransport.cpp
    src/device/HidrawTransport.h
    src/device/Transport.h
    src/keymap/AtomicFile.cpp
    src/keymap/AtomicFile.h
    src/keymap/BackupFiles.cpp
    src/keymap/BackupFiles.h
    src/keymap/Keymap.cpp
    src/keymap/Keymap.h
    src/keymap/KeyboardLayout.cpp
    src/keymap/KeyboardLayout.h
    src/keymap/ProfileDiff.cpp
    src/keymap/ProfileDiff.h
    src/keymap/ProfileFiles.cpp
    src/keymap/ProfileFiles.h
    src/keymap/ProfileSerializer.cpp
    src/keymap/ProfileSerializer.h
    src/keymap/ScanCodeCatalog.cpp
    src/keymap/ScanCodeCatalog.h
)

target_include_directories(hhkbs_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

# The core starts threads (the pad monitor), so everything that links it needs the thread library, including the tests.
target_link_libraries(hhkbs_core PUBLIC Threads::Threads)

add_executable(hhkbs
    src/main.cpp
    src/gui/DialogWidgets.cpp
    src/gui/DialogWidgets.h
    src/gui/KeyAssignmentDialog.cpp
    src/gui/KeyAssignmentDialog.h
    src/gui/KeyboardWidget.cpp
    src/gui/KeyboardWidget.h
    src/gui/MainWindow.cpp
    src/gui/MainWindow.h
    src/gui/Text.cpp
    src/gui/Text.h
    src/gui/Theme.cpp
    src/gui/Theme.h
)

target_link_libraries(hhkbs
    PRIVATE
        hhkbs_core
        hhkbs_imgui
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

# The udev rule that lets a logged-in user reach the keyboard. Where it has to go depends on the system (see the readme),
# so it is installed as data and not into /etc or /usr/lib.
install(FILES packaging/60-hhkbs.rules DESTINATION "${CMAKE_INSTALL_DATADIR}/hhkbs")

# Keep upstream notices in place; install the consolidated distribution copies.
install(FILES
    LICENSE
    third_party/licenses/DearImGui.txt
    third_party/licenses/GLFW.txt
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/hhkbs"
)

include(CTest)
if(BUILD_TESTING)
    # One executable per source file in tests/, built with the core's warnings; `name` is also the ctest name.
    function(hhkbs_add_test name executable source)
        add_executable(${executable} ${source})
        target_link_libraries(${executable} PRIVATE hhkbs_core)
        target_include_directories(${executable} PRIVATE tests)  # for the fake keyboard shared by some tests
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${executable} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        add_test(NAME ${name} COMMAND ${executable})
    endfunction()

    hhkbs_add_test(keymap_model hhkbs_keymap_tests tests/KeymapTests.cpp)
    hhkbs_add_test(hhkb_device_protocol hhkbs_device_tests tests/HhkbStudioDeviceTests.cpp)
    hhkbs_add_test(profile_files hhkbs_profile_files_tests tests/ProfileFilesTests.cpp)
    hhkbs_add_test(keyboard_tasks hhkbs_keyboard_tasks_tests tests/KeyboardTasksTests.cpp)
    hhkbs_add_test(hidraw_transport hhkbs_hidraw_transport_tests tests/HidrawTransportTests.cpp)
    hhkbs_add_test(pad_monitor hhkbs_pad_monitor_tests tests/PadMonitorTests.cpp)
    hhkbs_add_test(hid_report hhkbs_hid_report_tests tests/HidReportTests.cpp)
    hhkbs_add_test(backup_list hhkbs_backup_list_tests tests/BackupListTests.cpp)
    hhkbs_add_test(profile_workspace hhkbs_profile_workspace_tests tests/ProfileWorkspaceTests.cpp)

    # The window is built and drawn for a few frames, which needs a display; without xvfb-run there is no such test.
    find_program(XVFB_RUN xvfb-run)
    if(XVFB_RUN)
        add_test(NAME gui_startup COMMAND "${XVFB_RUN}" -a $<TARGET_FILE:hhkbs> --smoke-test)
        set_tests_properties(gui_startup PROPERTIES TIMEOUT 20)
    else()
        message(STATUS "xvfb-run was not found: the gui_startup test is not defined")
    endif()
endif()
