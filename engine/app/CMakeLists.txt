# SPDX-License-Identifier: GPL-3.0-or-later
# Game executable. For now the map viewer (milestone M2).

add_executable(open1602
    src/main.cpp
    src/diagnostics.cpp
    src/build_tool.cpp
    src/game_sound.cpp
    src/hud.cpp
    src/selection_panels.cpp
    src/diplomacy_panel.cpp
    src/end_screen.cpp
    src/list_panels.cpp
    src/multiplayer.cpp
    src/options_panel.cpp
    src/ship_panel.cpp
    src/panel_common.cpp
    src/unit_panels.cpp
    src/side_panels.cpp
    src/map_viewer.cpp
    src/map_input.cpp
    src/video_player.cpp
)
target_link_libraries(open1602 PRIVATE o1602::sim o1602::render_sdl o1602::platform o1602::audio o1602::ui o1602::video o1602::net o1602::menus)
o1602_set_warnings(open1602)

if(WIN32)
    set_target_properties(open1602 PROPERTIES WIN32_EXECUTABLE ON)
    # Windows finds DLLs next to the program: SDL3.dll goes there after every build and install.
    set(runtime_dlls $<TARGET_RUNTIME_DLLS:open1602>)
    add_custom_command(TARGET open1602 POST_BUILD
        COMMAND "$<IF:$<BOOL:${runtime_dlls}>,${CMAKE_COMMAND};-E;copy_if_different;${runtime_dlls};$<TARGET_FILE_DIR:open1602>,${CMAKE_COMMAND};-E;true>"
        COMMAND_EXPAND_LISTS
        VERBATIM)
    install(FILES ${runtime_dlls} DESTINATION bin)
endif()
o1602_utf8_manifest(open1602)

install(TARGETS open1602 RUNTIME DESTINATION bin)
# The licences of the libraries built into the program (MIT asks for its notice to go along).
install(FILES ${PROJECT_SOURCE_DIR}/third_party/enet/LICENSE DESTINATION share/doc/open1602/licenses RENAME enet.txt)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE DESTINATION share/doc/open1602)
