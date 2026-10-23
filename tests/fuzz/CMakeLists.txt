# SPDX-License-Identifier: GPL-3.0-or-later
# libFuzzer targets (cmake/Fuzzing.cmake, -DO1602_FUZZ=ON with Clang). Each target reads one kind of
# hostile input; o1602_fuzz_seeds writes their seed corpora from synthetic files, so no original
# data is needed. tests/fuzz/README.md says how to run them; tools/fuzz/run_fuzzers.sh runs all.

set(o1602_fuzz_targets
    cod            # .cod text -> BuildingTable / FigureTable
    gad            # GADDATA/*.GAD with includes
    sprites        # .BSH/.ZEI sprite sheets, .COL palettes, fonts
    chunks         # chunk files: islands, scenarios, .GAM records
    gam_world      # .GAM/.SZS loaded as a world, run, saved
    o1602_save     # Open1602 savegames
    command        # the command codec
    command_apply  # decoded commands given to a running game
    net_message    # network messages, LAN discovery, lobby setup
    settings       # settings.cfg, slots.cfg, keys.cfg, .lang, .hss, Game.dat
    media          # Smacker videos, WAVE files
)

if(O1602_FUZZ)
    foreach(name IN LISTS o1602_fuzz_targets)
        add_executable(fuzz_${name} fuzz_${name}.cpp)
        target_link_libraries(fuzz_${name} PRIVATE o1602::assets o1602::sim o1602::audio o1602::video o1602::ui
                                                   o1602::net o1602::menus)
        target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer)
        o1602_set_warnings(fuzz_${name})
    endforeach()
endif()

add_executable(o1602_fuzz_seeds fuzz_seeds.cpp)
target_include_directories(o1602_fuzz_seeds PRIVATE ${PROJECT_SOURCE_DIR}/tests/unit)
target_link_libraries(o1602_fuzz_seeds PRIVATE o1602::assets o1602::sim o1602::net o1602::menus)
o1602_set_warnings(o1602_fuzz_seeds)

# Not a fuzzer: the limits of docs/modding/data-limits.md measured on synthetic mod data.
add_executable(o1602_mod_limits mod_limits.cpp)
target_link_libraries(o1602_mod_limits PRIVATE o1602::assets o1602::sim)
o1602_set_warnings(o1602_mod_limits)

# Both link libraries built with the coverage instrumentation, so they need its runtime (without
# libFuzzer's main) also when no sanitizer brings it along.
foreach(helper IN ITEMS o1602_fuzz_seeds o1602_mod_limits)
    target_link_options(${helper} PRIVATE -fsanitize=fuzzer-no-link)
endforeach()
