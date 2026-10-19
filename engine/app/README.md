# engine/app

The game executable `open1602` and the wiring of all modules. Built only with `-DO1602_BUILD_GAME=ON`.

- `main.cpp`: command line (an unknown option such as `--help` prints every option and the default
  keys), start in the main menu or directly in a game.
- `main_menu.*`, `multiplayer_menu.*`, `multiplayer_texts.*`, `end_screen.*`, `high_score_file.*`,
  `app_settings.*`, `order_questions.*`, `command_sink.*`: menus, end screen, high score lists,
  settings files, the questions before orders that cannot be taken back and the way of the commands
  into the game. Those without a platform dependency are also built as the library `o1602::menus`
  (`menus/`), so the tests cover them.
- `map_viewer.*`: `MapViewer`, the running program. It owns the window, the game (`game_state.hpp`),
  the camera, the graphics of the zoom levels (`zoom_graphics.hpp`) and the panel, runs the frames
  and loads, saves and replaces games. The parts below do the rest; it only coordinates them.
- `map_input.*`, `map_gestures.*`, `build_tool.*`: `MapInput` takes the keys and the mouse on the
  map (default key bindings in `MapInput::setup_keys`), keeps the selection and gives orders;
  what it cannot do itself it asks of the viewer through `MapInput::Host`. Keys and clicks become
  `sim::Command`s, which the viewer's `CommandSink` hands to the simulation (`Simulation::submit`)
  or, in multiplayer, to the session. No other code of the game calls `Simulation::submit`.
  `map_gestures.*` (a press that becomes a selection rectangle, a drag or a right click, a group
  key twice) and the build tool are in `o1602::menus`.
- `event_presenter.*`: `EventPresenter` turns the simulation's events into the original's
  messages, videos and high score updates (the viewer plays them), keeps the end screen that
  follows a decided game, the notes of the status line and the chat's length. In `o1602::menus`.
- `video_controller.*`, `video_queue.*`: `VideoController` decides with `VideoQueue` (in
  `o1602::menus`) which video plays when and where (the whole window, the minimap's window, the
  menu's picture frame), plays it with `VideoPlayer`, and takes the input while one fills the window.
- `hud.*`, `side_panels.*`, `selection_panels.*`, `ship_panel.cpp`, `unit_panels.cpp`,
  `list_panels.cpp`, `diplomacy_panel.cpp`, `options_panel.*`, `panel_common.*`: the side panel and
  its windows.
- `multiplayer.cpp`: connects the game to an `engine/net` session.
- `game_sound.*`, `video_player.*`, `diagnostics.*`: sound, decoding and drawing a video, log and
  error reporting.

Controls: press F1 in the game; the decisions behind them are in
[ADR 0010](../../docs/architecture/0010-controls-and-interface.md).
