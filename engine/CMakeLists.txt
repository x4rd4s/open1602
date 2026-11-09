# SPDX-License-Identifier: GPL-3.0-or-later
#
# Engine modules and what each links (an arrow points from a module to what it uses):
#
#   assets -> annokit::core     sim -> assets               net -> sim, annokit::net
#   render -> sim, assets       audio -> sim, assets, annokit::audio
#   ui -> sim, assets           render_sdl -> render, ui, annokit::platform
#   script -> sim, assets, annokit::script                   editor -> sim, assets
#   menus -> sim, assets, audio, render, ui, net
#   app (open1602) -> sim, render_sdl, audio, ui, net, menus, script, editor, annokit::video,
#                     annokit::platform
#
# The game-independent modules (core, audio decoding and the mixer, video, transports, the Lua
# sandbox, platform) are annokit's (the top-level CMakeLists.txt fetches it). Only the game build
# (O1602_BUILD_GAME) adds annokit::platform, render_sdl and app; everything else builds and is
# tested without SDL.
#
# 'sim' must never depend on platform, render, audio or ui: the simulation has to stay
# deterministic and runnable headless (tests, dedicated server, replays). The ctest "layering"
# (cmake/CheckLayering.cmake) checks the includes of every module against these links.

add_subdirectory(assets)
add_subdirectory(sim)
add_subdirectory(net)
add_subdirectory(render)
add_subdirectory(audio)
add_subdirectory(ui)
add_subdirectory(script)
add_subdirectory(editor)
add_subdirectory(app/menus)

if(O1602_BUILD_GAME)
    add_subdirectory(render/sdl)
    add_subdirectory(app)
endif()
