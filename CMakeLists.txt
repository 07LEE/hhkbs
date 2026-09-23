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
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

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
    src/keymap/Keymap.cpp
    src/keymap/Keymap.h
    src/keymap/KeyboardLayout.cpp
    src/keymap/KeyboardLayout.h
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
    resources/resources.qrc
)

set_target_properties(hhkbs PROPERTIES
    OUTPUT_NAME "hhkbs"
)

target_link_libraries(hhkbs
    PRIVATE
        hhkbs_core
        Qt6::Widgets
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

    add_test(NAME keymap_model COMMAND hhkbs_keymap_tests)
    add_test(NAME hhkb_device_protocol COMMAND hhkbs_device_tests)
endif()
