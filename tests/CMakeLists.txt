# SPDX-License-Identifier: GPL-3.0-or-later

add_executable(o1602_tests
    test_main.cpp
    unit/test_assets.cpp
    unit/test_cod_parser.cpp
    unit/test_commands.cpp
    unit/test_island_render.cpp
    unit/test_world_render.cpp
    unit/test_players_ships.cpp
    unit/test_ships.cpp
    unit/test_diplomacy.cpp
    unit/test_military.cpp
    unit/test_naval.cpp
    unit/test_pirates.cpp
    unit/test_explore.cpp
    unit/test_world_plan.cpp
    unit/test_figures.cpp
    unit/test_production.cpp
    unit/test_logistics.cpp
    unit/test_raw_producers.cpp
    unit/test_consumption.cpp
    unit/test_random.cpp
    unit/test_houses.cpp
    unit/test_island_events.cpp
    unit/test_construction.cpp
    unit/test_build_tool.cpp
    unit/test_order_questions.cpp
    unit/test_app_settings.cpp
    unit/test_command_sink.cpp
    unit/test_drag_path.cpp
    unit/test_deer.cpp
    unit/test_disasters.cpp
    unit/test_volcanoes.cpp
    unit/test_ambient.cpp
    unit/test_units.cpp
    unit/test_save_game.cpp
    unit/test_missions.cpp
    unit/test_scores.cpp
    unit/test_audio.cpp
    unit/test_ui.cpp
    unit/test_localization.cpp
    unit/test_unit_selection.cpp
    unit/test_video.cpp
    unit/test_net.cpp
    unit/test_multiplayer_menu.cpp
    unit/test_sea_paths.cpp
    unit/test_land_paths.cpp
    unit/test_world_generator.cpp
    unit/test_ai.cpp
    unit/test_save_fuzz.cpp
    unit/test_savegame_writer.cpp
    data/test_original_data.cpp
    data/test_new_game.cpp
    data/test_ai_data.cpp
    data/test_ships_data.cpp
    data/test_military_data.cpp
    data/test_missions_data.cpp
    data/test_audio_data.cpp
    data/test_ui_data.cpp
    data/test_video_data.cpp
    data/test_save_continuity.cpp
    data/test_net_data.cpp
)
target_link_libraries(o1602_tests PRIVATE o1602::assets o1602::sim o1602::render o1602::audio o1602::ui o1602::video o1602::net o1602::menus)
o1602_set_warnings(o1602_tests)
# Tests dereference lookup results (world.player(0)->money) right after creating them; a null there
# crashes the test, which reports it as failed. GCC's -Wnull-dereference flags every such line at -O2,
# so it stays on for the engine but is off for the tests.
# MSVC's C4242/C4244 fire inside std::optional and std::pair when tests assign int literals to small
# fields (city.warehouse_x = 20); the engine keeps them.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(o1602_tests PRIVATE -Wno-null-dereference)
elseif(MSVC)
    target_compile_options(o1602_tests PRIVATE /wd4242 /wd4244)
endif()

# Two ctest entries: the unit tests, and the tests against the original game data, which skip
# without ANNO1602_DIR. Exit code 77 means every selected test skipped; ctest shows "Skipped".
add_test(NAME unit COMMAND o1602_tests --no-data)
add_test(NAME data COMMAND o1602_tests --only-data)
set_tests_properties(unit data PROPERTIES SKIP_RETURN_CODE 77)
set_tests_properties(data PROPERTIES TIMEOUT 3600)  # it plays whole games of the original

# The module layering of ADR 0005 on the sources (cmake/CheckLayering.cmake): no module includes
# headers of one it does not link, and no module below the game includes SDL.
add_test(NAME layering COMMAND ${CMAKE_COMMAND} -DENGINE_DIR=${PROJECT_SOURCE_DIR}/engine
                                -P ${PROJECT_SOURCE_DIR}/cmake/CheckLayering.cmake)

# Smoke tests of o1602-tool on small synthetic files (tests/tool/write_tool_files.cpp), so they run
# without the original data: every command that can, and the errors for a missing, truncated or
# foreign file. tests/tool/expect.cmake checks the exit code and the output together.
if(TARGET o1602-tool)
    add_executable(o1602_tool_files tool/write_tool_files.cpp)
    target_link_libraries(o1602_tool_files PRIVATE o1602::assets)
    o1602_set_warnings(o1602_tool_files)

    set(_files "${CMAKE_CURRENT_BINARY_DIR}/tool-files")
    add_test(NAME tool:files COMMAND o1602_tool_files "${_files}")
    set_tests_properties(tool:files PROPERTIES FIXTURES_SETUP o1602_tool_files)

    # o1602_tool_test(NAME EXIT_CODE [STDOUT regex] [STDERR regex] ARGS ...): "\n" in a regex is a
    # line end; a regex must not contain ";".
    function(o1602_tool_test name exit_code)
        cmake_parse_arguments(PARSE_ARGV 2 arg "" "STDOUT;STDERR" "ARGS")
        set(expect "-DEXIT_CODE=${exit_code}")
        if(DEFINED arg_STDOUT)
            list(APPEND expect "-DSTDOUT=${arg_STDOUT}")
        endif()
        if(DEFINED arg_STDERR)
            list(APPEND expect "-DSTDERR=${arg_STDERR}")
        endif()
        add_test(NAME tool:${name}
                 COMMAND ${CMAKE_COMMAND} ${expect} -P "${CMAKE_CURRENT_SOURCE_DIR}/tool/expect.cmake"
                         -- $<TARGET_FILE:o1602-tool> ${arg_ARGS})
        set_tests_properties(tool:${name} PROPERTIES FIXTURES_REQUIRED o1602_tool_files)
    endfunction()

    o1602_tool_test(usage 2 STDERR "^usage:\\n  o1602-tool chunks ")
    o1602_tool_test(unknown-command 2 STDERR "^usage:\\n" ARGS no-such-command)
    o1602_tool_test(missing-argument 2 STDERR "^usage:\\n" ARGS chunks)
    o1602_tool_test(missing-file 1 STDERR "^error: [^\\n]*no-such-file.gam" ARGS chunks "${_files}/no-such-file.gam")
    o1602_tool_test(truncated-file 1 STDERR "^error: [^\\n]*truncated.gam: chunk at offset 0 exceeds file size\\n$" ARGS chunks "${_files}/truncated.gam")
    o1602_tool_test(chunks 0
        STDOUT "^name +offset +size\\nINSEL5 +20 +116\\nINSELHAUS +156 +16\\nPLAYER4 +192 +1072\\nWIFF +[0-9]+ +4\\n4 chunks\\n$"
        ARGS chunks "${_files}/world.gam")
    o1602_tool_test(islands 0
        STDOUT "^INSEL5 #3  size 20x15  pos \\(40,30\\)  layers 1\\n  layer 0: 2 tiles, 1 distinct ids. most common: 21201\\(x2\\)\\n1 islands\\n$"
        ARGS islands "${_files}/world.gam")
    o1602_tool_test(state 0
        STDOUT "^player 0: kind 0x00, colour 1, money 12345 Tester\\ndeer: 0 on 0 islands\\n$"
        ARGS state "${_files}/world.gam")
    o1602_tool_test(cod 0
        STDOUT "^Nahrung = 4\\nVersion: 2\\nObjekt: HAUS\\n  @Nummer: 0\\n  Id: 7\\n  @Nummer: \\+1\\n  Id: 8\\nEndObj.\\n$"
        ARGS cod "${_files}/haeuser.cod")
    o1602_tool_test(cod-dump 0
        STDOUT "^Version: 2\\n\\[HAUS #0\\]\\n  Id: 7\\n\\[HAUS #1\\]\\n  Id: 8\\n$"
        STDERR " 0 diagnostics \\(predefined: haeuser\\)\\n$"
        ARGS cod-dump "${_files}/haeuser.cod")
    o1602_tool_test(bsh-info 0
        STDOUT "^ index  width height  type   length     offset\\n     0      4      2     1       28          4\\n1 sprites\\n$"
        ARGS bsh-info "${_files}/sprites.bsh")
    o1602_tool_test(bsh-export 0
        STDOUT "^wrote [^\\n]*sprites_sheet.png \\(4x2, 1 sprites\\)\\n$"
        ARGS bsh-export "${_files}/sprites.bsh" "${_files}/sprites" --palette "${_files}/palette.col" --sheet)
    # Commands that need the installation say so.
    o1602_tool_test(new-world-without-data 1
        STDERR "^error: [^\\n]*does not look like an Anno 1602 installation \\(missing haeuser.cod\\)\\n$"
        ARGS new-world "${_files}/new.gam")
    set_tests_properties(tool:new-world-without-data PROPERTIES ENVIRONMENT "ANNO1602_DIR=${_files}/no-installation")
endif()
