# SPDX-License-Identifier: GPL-3.0-or-later
# Sanitizer support. Enable with -DO1602_SANITIZE=address;undefined (GCC/Clang; MSVC: address only).

set(O1602_SANITIZE "" CACHE STRING "Semicolon separated list of sanitizers (address, undefined, thread, leak)")

if(O1602_SANITIZE AND MSVC)
    if(NOT O1602_SANITIZE STREQUAL "address")
        message(FATAL_ERROR "Open1602: MSVC supports only O1602_SANITIZE=address, not \"${O1602_SANITIZE}\"")
    endif()
    add_compile_options(/fsanitize=address)
    add_compile_definitions(O1602_SANITIZED=1)  # the game leaves crash reports to the sanitizers
    message(STATUS "Open1602: sanitizers enabled: address")
elseif(O1602_SANITIZE)
    list(JOIN O1602_SANITIZE "," _o1602_san)
    add_compile_options(-fsanitize=${_o1602_san} -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=${_o1602_san})
    add_compile_definitions(O1602_SANITIZED=1)
    message(STATUS "Open1602: sanitizers enabled: ${_o1602_san}")
endif()
