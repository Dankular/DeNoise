# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.16)
project(denoise LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(DENOISE_BUILD_TESTS "Build unit tests" ON)
option(DENOISE_BUILD_CLI "Build the offline WAV-in/WAV-out CLI tool" ON)
option(DENOISE_BUILD_LINUX_FILTER "Build the PipeWire virtual-mic filter (Linux only)" ${UNIX})
option(DENOISE_BUILD_MACOS_DRIVER "Build the CoreAudio AudioServerPlugIn skeleton (macOS only, UNTESTED -- see platform/macos/README.md)" OFF)

find_package(ONNXRuntime REQUIRED)

add_subdirectory(core)
add_subdirectory(platform/windows)  # portable half only -- see platform/windows/CMakeLists.txt

if(DENOISE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(DENOISE_BUILD_CLI)
    add_subdirectory(tools/denoise_cli)
endif()

if(DENOISE_BUILD_LINUX_FILTER AND UNIX AND NOT APPLE)
    find_package(PkgConfig)
    if(PkgConfig_FOUND)
        pkg_check_modules(PIPEWIRE IMPORTED_TARGET libpipewire-0.3)
    endif()
    if(PIPEWIRE_FOUND)
        add_subdirectory(platform/linux)
    else()
        message(STATUS "libpipewire-0.3 not found via pkg-config; skipping platform/linux filter target")
    endif()
endif()

if(DENOISE_BUILD_MACOS_DRIVER)
    if(NOT APPLE)
        message(FATAL_ERROR "DENOISE_BUILD_MACOS_DRIVER requires APPLE; this is unbuilt/untested "
                             "outside macOS -- see platform/macos/README.md")
    endif()
    add_subdirectory(platform/macos)
endif()
