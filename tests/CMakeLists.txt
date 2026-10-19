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
