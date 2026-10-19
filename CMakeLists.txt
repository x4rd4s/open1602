# Open1602 - open-source engine for Anno 1602
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.25)

project(Open1602
    VERSION 0.1.0
    DESCRIPTION "Open-source engine reimplementation for Anno 1602"
    HOMEPAGE_URL "https://github.com/x4rd4s/open1602"
    LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not allowed. Use a build directory or a CMake preset.")
endif()

# Open1602 runs on 64-bit systems only (project decision of 18.10.2026).
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Open1602 needs a 64-bit target; 32-bit builds are not supported.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    # Sources are UTF-8. The C library functions MSVC calls deprecated (fopen, getenv, localtime)
    # are the portable ones the engine uses on purpose.
    add_compile_options(/utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(CompilerWarnings)
include(Sanitizers)
include(EmbedFiles)
include(WindowsUtf8)

option(O1602_BUILD_TESTS  "Build unit tests"                         ON)
option(O1602_BUILD_TOOLS  "Build command line tools (o1602-tool)"    ON)
option(O1602_BUILD_GAME   "Build the game executable (needs SDL3)"   OFF)
option(O1602_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"  OFF)

# zlib packs savegames (.o1602) and PNG screenshots. The game needs it: without it, it could not
# load the compressed savegames other builds write. Libraries, tests and tools build without it.
if(O1602_BUILD_GAME)
    find_package(ZLIB REQUIRED)
else()
    find_package(ZLIB QUIET)
    if(NOT ZLIB_FOUND)
        message(WARNING "zlib not found: compressed savegames can neither be written nor read")
    endif()
endif()

add_subdirectory(third_party/enet)
add_subdirectory(engine)

if(O1602_BUILD_TOOLS)
    add_subdirectory(tools/o1602-tool)
endif()

if(O1602_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
