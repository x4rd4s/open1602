# SPDX-License-Identifier: GPL-3.0-or-later
#
# Checks the module layering of ADR 0005 on the sources: every engine module may only include
# headers of the modules it links (directly or through them), and only render_sdl and the game
# include SDL. The modules of annokit are named here with "annokit/" (annokit/core and so on). The include directories of all modules are visible to the tests and tools
# through PUBLIC links, so a stray #include "o1602/render/..." in sim would compile; this check
# catches it.
#
# Usage: cmake -DENGINE_DIR=<repo>/engine -P CheckLayering.cmake   (ctest runs it as "layering")

cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ENGINE_DIR)
    message(FATAL_ERROR "CheckLayering.cmake: pass -DENGINE_DIR=<repo>/engine")
endif()

# <directory below engine/> = <modules whose headers it may include>, matching the links in
# engine/*/CMakeLists.txt. engine/app (the game) may include everything; the parts of it in
# o1602::menus are checked below.
set(rules
    "assets=annokit/core assets"
    "sim=annokit/core assets sim"
    "net=annokit/core assets sim net annokit/net"
    "render=annokit/core assets sim render"
    "audio=annokit/core assets sim audio annokit/audio"
    "ui=annokit/core assets sim ui"
    "script=annokit/core assets sim script annokit/script"
    "editor=annokit/core assets sim editor"
    "render/sdl=annokit/core assets sim render ui annokit/platform SDL3"
)

set(violations "")
set(checked 0)
foreach(rule IN LISTS rules)
    string(REPLACE "=" ";" rule "${rule}")
    list(GET rule 0 dir)
    list(GET rule 1 allowed)
    string(REPLACE " " ", " allowed_text "${allowed}")
    string(REPLACE " " ";" allowed "${allowed}")
    # Only the module's own src/ and include/, so that engine/render does not cover engine/render/sdl.
    file(GLOB_RECURSE files "${ENGINE_DIR}/${dir}/src/*.cpp" "${ENGINE_DIR}/${dir}/src/*.hpp"
         "${ENGINE_DIR}/${dir}/include/*.hpp")
    if(NOT files)
        list(APPEND violations "engine/${dir}: no sources found (rule out of date?)")
    endif()
    foreach(file IN LISTS files)
        math(EXPR checked "${checked} + 1")
        file(STRINGS "${file}" includes REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"](o1602/[a-z_]+/|annokit/[a-z_]+/|SDL3/)")
        foreach(line IN LISTS includes)
            string(REGEX MATCH "o1602/([a-z_]+)/|(annokit/[a-z_]+)/|SDL3/" _ "${line}")
            if(CMAKE_MATCH_1)
                set(module "${CMAKE_MATCH_1}")
            elseif(CMAKE_MATCH_2)
                set(module "${CMAKE_MATCH_2}")
            else()
                set(module "SDL3")
            endif()
            if(NOT module IN_LIST allowed)
                file(RELATIVE_PATH rel "${ENGINE_DIR}/.." "${file}")
                string(STRIP "${line}" line)
                list(APPEND violations "${rel}: ${line} (engine/${dir} may include: ${allowed_text})")
            endif()
        endforeach()
    endforeach()
endforeach()

# o1602::menus: the sources engine/app/menus/CMakeLists.txt lists (and the headers beside them)
# are built without the platform, so that tests and tools have them (LC-17): no platform,
# render_sdl, video or SDL.
set(menus_allowed annokit/core assets audio annokit/audio net annokit/net render sim ui annokit/update)
string(REPLACE ";" ", " menus_allowed_text "${menus_allowed}")
file(READ "${ENGINE_DIR}/app/menus/CMakeLists.txt" menus_list)
string(REGEX MATCHALL "\\.\\./src/[a-z_]+\\.cpp" menus_sources "${menus_list}")
if(NOT menus_sources)
    list(APPEND violations "engine/app/menus: no sources found (rule out of date?)")
endif()
foreach(source IN LISTS menus_sources)
    string(REPLACE "../src/" "${ENGINE_DIR}/app/src/" file "${source}")
    string(REGEX REPLACE "\\.cpp$" ".hpp" header "${file}")
    foreach(f IN ITEMS "${file}" "${header}")
        if(NOT EXISTS "${f}")
            continue()
        endif()
        math(EXPR checked "${checked} + 1")
        file(STRINGS "${f}" includes REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"](o1602/[a-z_]+/|annokit/[a-z_]+/|SDL3/)")
        foreach(line IN LISTS includes)
            string(REGEX MATCH "o1602/([a-z_]+)/|(annokit/[a-z_]+)/|SDL3/" _ "${line}")
            set(module "${CMAKE_MATCH_1}${CMAKE_MATCH_2}")
            if(NOT module OR NOT module IN_LIST menus_allowed)
                file(RELATIVE_PATH rel "${ENGINE_DIR}/.." "${f}")
                string(STRIP "${line}" line)
                list(APPEND violations "${rel}: ${line} (o1602::menus may include: ${menus_allowed_text})")
            endif()
        endforeach()
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n  " text)
    message(FATAL_ERROR "Module layering violated (docs/architecture/0005-module-structure.md):\n  ${text}")
endif()
message(STATUS "Module layering ok (${checked} files)")
