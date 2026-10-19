# engine/app

The game executable `open1602` and the wiring of all modules. Built only with `-DO1602_BUILD_GAME=ON`.

- `main.cpp`: command line (`open1602 --help` lists every option and the default keys), start in
  the main menu or directly in a game.
- `main_menu.*`, `multiplayer_menu.*`, `multiplayer_texts.*`, `end_screen.*`, `high_score_file.*`,
  `app_settings.*`: menus, end screen, high score lists and settings files. Those without a platform
  dependency are also built as the library `o1602::menus` (`menus/`), so the tests cover them.
- `map_viewer.*`, `map_input.cpp`, `build_tool.*`: the running game, the default key bindings
  (`MapViewer::setup_keys`) and building.
- `hud.*`, `side_panels.*`, `selection_panels.*`, `ship_panel.cpp`, `unit_panels.cpp`,
  `list_panels.cpp`, `diplomacy_panel.cpp`, `options_panel.*`, `panel_common.*`: the side panel and
  its windows.
- `multiplayer.cpp`: connects the game to an `engine/net` session.
- `game_sound.*`, `video_player.*`, `diagnostics.*`: sound, videos, log and error reporting.

Controls: press F1 in the game; the decisions behind them are in
[ADR 0010](../../docs/architecture/0010-controls-and-interface.md).
