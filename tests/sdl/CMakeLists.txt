# SPDX-License-Identifier: GPL-3.0-or-later
#
# Tests of the SDL layer (engine/platform, engine/render/sdl), built with the game. They run
# headless: SDL's dummy video and audio drivers, and software renderers drawing into surfaces.

add_executable(o1602_sdl_tests
    ../test_main.cpp
    test_window.cpp
    test_audio_output.cpp
    test_sdl_sprite_renderer.cpp
    test_sdl_ui_renderer.cpp
)
target_link_libraries(o1602_sdl_tests PRIVATE o1602::render_sdl o1602::platform)
o1602_set_warnings(o1602_sdl_tests)

if(WIN32)
    # Windows finds DLLs next to the program: SDL3.dll goes there after every build.
    set(runtime_dlls $<TARGET_RUNTIME_DLLS:o1602_sdl_tests>)
    add_custom_command(TARGET o1602_sdl_tests POST_BUILD
        COMMAND "$<IF:$<BOOL:${runtime_dlls}>,${CMAKE_COMMAND};-E;copy_if_different;${runtime_dlls};$<TARGET_FILE_DIR:o1602_sdl_tests>,${CMAKE_COMMAND};-E;true>"
        COMMAND_EXPAND_LISTS
        VERBATIM)
endif()

add_test(NAME sdl COMMAND o1602_sdl_tests)
set_tests_properties(sdl PROPERTIES ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
