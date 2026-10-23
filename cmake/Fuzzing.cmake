# SPDX-License-Identifier: GPL-3.0-or-later
# Coverage-guided fuzzing with libFuzzer (Clang only). Enable with -DO1602_FUZZ=ON, usually together
# with -DO1602_SANITIZE="address;undefined". Every library gets the coverage instrumentation
# (-fsanitize=fuzzer-no-link); the fuzz targets in tests/fuzz link libFuzzer's main.

option(O1602_FUZZ "Build the libFuzzer targets of tests/fuzz (Clang only)" OFF)

if(O1602_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "O1602_FUZZ needs Clang (libFuzzer); this is ${CMAKE_CXX_COMPILER_ID}")
    endif()
    # C++ only: ENet (C) may be built by another compiler and needs no coverage of its own.
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fsanitize=fuzzer-no-link> -fno-omit-frame-pointer)
    # Lets code know it runs under a fuzzer (none of the engine uses it today).
    add_compile_definitions(O1602_FUZZING_BUILD=1)
    message(STATUS "Open1602: libFuzzer targets enabled")
endif()
