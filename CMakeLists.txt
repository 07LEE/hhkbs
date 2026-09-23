cmake_minimum_required(VERSION 3.25)

project(
    HHKBS
    VERSION 0.1.0
    DESCRIPTION "Linux keymap editor for HHKB Studio"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

add_library(hhkbs_core STATIC
    src/keymap/Keymap.cpp
    src/keymap/Keymap.h
)

target_include_directories(hhkbs_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_features(hhkbs_core PUBLIC cxx_std_20)

add_executable(hhkbs
    src/main.cpp
    src/gui/MainWindow.cpp
    src/gui/MainWindow.h
    resources/resources.qrc
)

set_target_properties(hhkbs PROPERTIES
    OUTPUT_NAME "HHKBS"
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

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(hhkbs_keymap_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_test(NAME keymap_model COMMAND hhkbs_keymap_tests)
endif()
