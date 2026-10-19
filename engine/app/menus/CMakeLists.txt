# SPDX-License-Identifier: GPL-3.0-or-later
# The game's parts that need no platform: the main menu's lists, the multiplayer menu and its
# texts, the settings files, the build tool, the questions before orders. Built without the game too, so that the tests have them.

add_library(o1602_menus
    ../src/app_settings.cpp
    ../src/build_tool.cpp
    ../src/high_score_file.cpp
    ../src/main_menu.cpp
    ../src/multiplayer_menu.cpp
    ../src/multiplayer_texts.cpp
    ../src/order_questions.cpp
)
add_library(o1602::menus ALIAS o1602_menus)

target_include_directories(o1602_menus PUBLIC ../src)
target_link_libraries(o1602_menus PUBLIC o1602::assets o1602::audio o1602::net o1602::sim o1602::ui)
o1602_set_warnings(o1602_menus)
