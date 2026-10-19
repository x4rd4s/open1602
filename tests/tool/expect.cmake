# SPDX-License-Identifier: GPL-3.0-or-later
#
# cmake -DEXIT_CODE=N [-DSTDOUT=REGEX] [-DSTDERR=REGEX] -P expect.cmake -- PROGRAM [ARGS...]
#
# Runs a program and passes only when it ends with exit code N and its standard output and error
# match the regular expressions (CMake syntax, and \n for a line end; carriage returns are removed
# from the output first, so Windows line ends match too). ctest alone checks either the exit code
# or the output, not both.

set(_command "")
set(_after_separator FALSE)
math(EXPR _last "${CMAKE_ARGC} - 1")
foreach(_i RANGE ${_last})
    if(_after_separator)
        list(APPEND _command "${CMAKE_ARGV${_i}}")
    elseif(CMAKE_ARGV${_i} STREQUAL "--")
        set(_after_separator TRUE)
    endif()
endforeach()
if(NOT _command OR NOT DEFINED EXIT_CODE)
    message(FATAL_ERROR "usage: cmake -DEXIT_CODE=N [-DSTDOUT=REGEX] [-DSTDERR=REGEX] -P expect.cmake -- PROGRAM [ARGS...]")
endif()

execute_process(COMMAND ${_command} RESULT_VARIABLE _code OUTPUT_VARIABLE _output_STDOUT ERROR_VARIABLE _output_STDERR)
string(REPLACE "\r" "" _output_STDOUT "${_output_STDOUT}")
string(REPLACE "\r" "" _output_STDERR "${_output_STDERR}")
message("exit code: ${_code}\n--- stdout:\n${_output_STDOUT}--- stderr:\n${_output_STDERR}---")

if(NOT _code STREQUAL EXIT_CODE)
    message(SEND_ERROR "expected exit code ${EXIT_CODE}, got ${_code}")
endif()
foreach(_stream STDOUT STDERR)
    if(DEFINED ${_stream})
        string(REPLACE "\\n" "\n" _regex "${${_stream}}")  # a line end, written as \n on the command line
        if(NOT _output_${_stream} MATCHES "${_regex}")
            message(SEND_ERROR "${_stream} does not match: ${${_stream}}")
        endif()
    endif()
endforeach()
