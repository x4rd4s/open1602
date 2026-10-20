# Code review, October 2026

This review covers the whole repository at commit `468c58a` ("Multiplayer: lockstep through the
host, lobby, LAN search and Open1602's own menu (#8)"). That includes every engine module (core,
assets, sim with the computer players, render, audio, video, ui, platform, net, app), the tools, the
unit and data tests, the build and CI setup, the documentation, and the two newest features:
multiplayer (PR #8) and scores with the end screen (PR #14). The review was read-only. The build and
test area was also built three times (GCC 13 `ci` preset with `-Werror`, Clang 18, a GCC coverage
build) and the unit tests were run. No original game data was available, so the data tests were
not run. The app/ui/render and build/test parts were read at `3b1aac2`, the commit before the
multiplayer merge. The multiplayer part of the review covers the difference. An earlier review
(German, kept outside the repository) was checked item by item. Its open items appear below either
under a new ID or in the "Earlier items still present" notes.

The fixes since then are in PRs #19 to #29 and #32 to #44. The status column says which one fixes each
finding.
After #44 no finding is fully open: 93 are fixed, 5 are left on purpose and 1 is partly fixed, with
what remains listed under "Remaining work".

Paths in the tables are relative to `engine/` unless they start with `tests/`, `docs/`,
`third_party/`, `tools/` or `.github/`.

## Summary

The simulation core is in good shape. Ticks are fixed, commands carry tick stamps and go through a
strict byte codec. Decisions use integers only, every sort has a total order, random numbers come
from separate streams, and `tick_steps()` documents the original's subsystem order in one table.
The save format is robust by design: decoding happens before anything is applied, zlib output is
bounded, and `validate(World)` runs on every load. The lockstep design in ADR 0011 is sound, and
its wire decoder is strict. The main risks the review found were in untrusted input: crafted
savegames could write out of bounds or allocate gigabytes, and several commands accepted more
than the interface offers. In multiplayer every command is untrusted, so that mattered, and the
reconnect tokens allowed seat hijacking. All four High findings and most Medium ones in these
areas are fixed in PR #19 and PR #20. The largest open risks are now structural. The simulation
still stores localized city and ship names, so a German and an English installation cannot play
together. `MapViewer` is a god class that cannot be tested. The AI, the `.GAM` writer and the app
are barely exercised in CI without original data. The documentation has drifted from the code in
several places.

Since the review, the fixes in #19 to #44 closed all of these: the simulation no longer depends on
the local city and ship names (UI-2), `MapViewer` is split into tested parts, the AI, `new_game`,
the `.GAM` writer and the SDL layer have tests without original data, CI measures coverage, and the
documents were brought up to date. One item remains partly open (see "Remaining work").

## Inventory

Lines are `wc -l` over all `.cpp`, `.hpp`, `.h` and `.c` files of the module at `468c58a`
(headers and sources). Third-party code (`third_party/enet`) is not counted.

| Module | Files | Lines | Purpose |
|---|---|---|---|
| `engine/core` | 12 | 896 | `Result`/`Error`, byte reader and writer, files (case-insensitive lookup, atomic write, UTF-8 paths), Windows-1252 text, zlib wrapper, PNG writer |
| `engine/assets` | 32 | 5,229 | Loaders for the original formats: chunks, `.cod`, palette, BSH/ZEI sprites, islands, building and figure tables, GAD layouts, scenarios, savegames, Text.cod, high scores |
| `engine/sim` (without AI) | 83 | 29,936 | Deterministic simulation: world, construction, production, logistics, growth, houses, consumption, disasters, ships, trade, military, combat, diplomacy, missions, scores, pathfinding, commands, `.o1602` and `.GAM` saves, validation |
| `engine/sim/src/ai` | 11 | 9,698 | Computer players: task stack, planner, site search, building, settling, logistics, military |
| `engine/render` | 36 | 6,254 | Isometric world and island rendering, sprite atlas, camera, figures, ship trail, hit masks; `render/sdl` (637 lines) is the SDL3 sprite and UI renderer |
| `engine/audio` | 8 | 1,199 | WAV (including 4-bit music), mixer, sound bank, game sounds, speech and music |
| `engine/video` | 2 | 763 | Own Smacker decoder |
| `engine/ui` | 17 | 3,179 | Fonts, GAD panels and forms, build menu, key bindings, localization, unit selection |
| `engine/platform` | 4 | 233 | SDL3 window, renderer, audio output |
| `engine/net` | 15 | 3,169 | Lockstep sessions (host, client), wire protocol, ENet and loopback transports, LAN discovery |
| `engine/app` | 39 | 16,214 | The game executable: main, map viewer and input, HUD and panels, menus, multiplayer menu, end screen, video player, sound, settings |
| `tools/o1602-tool` | 1 | 1,139 | Developer tool with 13 subcommands (chunks, cod, bsh, islands, smk, state, new-world, export, ...) |
| `tests` | 57 | 20,798 | Own test framework; `tests/unit` 44 files (16,317 lines), `tests/data` 11 files (4,371 lines) |

The engine has about 76,800 lines in total; `engine/script` holds only a README. The largest files
are `sim/src/savegame_extension.cpp`, `app/src/map_viewer.cpp`, `sim/src/logistics.cpp`,
`sim/src/construction.cpp` and `sim/src/ai/ai_planner.cpp`, each over 1,800 lines.

**Tests.** At the time 383 unit tests and 67 data tests ran (448 `TEST_CASE`s in the sources; a few are skipped on purpose). The data tests need
`ANNO1602_DIR` and skip without it, so CI runs only the unit tests. Before PR #19, ctest showed all
of them as a single passing entry. They are now two entries, `unit` and `data`, and skips show as
"Skipped".

**CI matrix** (`.github/workflows/ci.yml`):

| Job | Platforms | What it does |
|---|---|---|
| Build and test | Linux GCC, Linux Clang, Linux ASan/UBSan (Clang), Windows MSVC, macOS Clang | Configure, build with `-k 0`, ctest |
| Game (SDL3) | Linux, Windows, macOS | SDL3 from source, build the game, start it once (expects exit code 2) |
| Game (vcpkg preset) | Windows | vcpkg at the manifest baseline, build, test, start once |
| Formatting | Linux | clang-format check over `engine tools tests` |

PR #19 adds zlib to the Windows jobs and a golden state hash test that every build job compares.

## Findings

Severity is the one given in the area report. "Partly" means part of the finding is fixed and the
rest stays open, as noted.

### Simulation core (SC)

| ID | Severity | Finding | Where | Status |
|---|---|---|---|---|
| SC-1 | High | `Logistics::load` trusts walker indices: out-of-bounds write or ~4 GiB allocation from a crafted save | `sim/src/logistics.cpp` | Fixed #19 |
| SC-2 | Medium | `advance_production` runs free production slots that no load check covers | `sim/src/production_cycle.cpp` | Fixed #19 |
| SC-3 | Medium | Commands run in frame-sized batches while paused; the pause is not saved | `sim/src/simulation.cpp` | Fixed #23 (state version 6 keeps speed and pause; network sessions order paused commands) |
| SC-4 | Medium | Castle, cathedral, arch and monument limits are checked against the first item of a `BuildOrder` only | `sim/src/construction.cpp` | Fixed #19 |
| SC-5 | Low | Clocks from a save are unchecked; a `time_ms` off the tick grid stops diplomacy and player checks | `sim/src/simulation.cpp` | Fixed #19 |
| SC-6 | Low | Removing growing tiles erases from the whole growth list once per tile | `sim/src/construction.cpp` | Fixed #19 |
| SC-7 | Low | German fallback player names in the sim | `sim/src/new_game.cpp` | Fixed #20 |
| SC-8 | Low | `refusal_name()` English text is shown as build status | `sim/src/construction.cpp` | Fixed #20 |
| SC-9 | Low | Leftover magic numbers and local copies of `game_data.hpp` constants | `sim/src/production_cycle.cpp`, `drag_path.cpp`, others | Fixed #22 |
| SC-10 | Low | `load_world` is one function of about 515 lines | `sim/src/world.cpp` | Fixed #29 (`load_world` calls about 20 documented stages in order) |
| SC-11 | Low | `find_case_insensitive` resolves `..` and absolute paths | `core/src/file.cpp` | Fixed #19 |
| SC-12 | Low (uncertain) | An in-place upgrade keeps the old `Production::rate`; `raw` can underflow | `sim/src/production_cycle.cpp` | Fixed #20 |
| SC-13 | Low | Crater and deer spot coordinates are `uint8_t` in the runtime model | `sim/include/o1602/sim/world.hpp` | Fixed #28 (16-bit in the runtime model and in the own format's section version 2; the `.GAM` writer refuses islands wider than 255 tiles) |
| SC-14 | Low | Small per-call costs: `state_hash()` serializes the game, linear city lookups, `vector<bool>` in `PathFinder` | `sim/src/simulation.cpp`, `consumption.cpp` | Won't fix (measured in #29: `state_hash` runs only every few frames in multiplayer, the city lookups cost about 0.2 ms every 10 s, and bytes instead of `vector<bool>` gave no wall-time gain) |

For SC-7, the identifiers "Trader4", "Pirat5", "Native6" and "Endlosspiel" stay as they are. They
are the original's internal names, not player-facing text. For SC-3, the multiplayer review found
that `Session::run_frame` drains commands frame by frame, so command order while paused is the
same on every computer in a network session. Since #23 the simulation state (version 6) keeps the
game speed and the pause, so a game saved while paused loads paused. A single player's commands
while paused need no turn rule: there is no second computer to agree with.

Earlier items still present: money is an unsaturated `int32` (`sim/include/o1602/sim/player.hpp`),
`Pool::for_each` has no guard against `add()` during iteration (`sim/include/o1602/sim/pool.hpp`),
and localized names are part of the sim state (see UI-2).

### File formats and IO (IO)

| ID | Severity | Finding | Where | Status |
|---|---|---|---|---|
| IO-1 | High | Unbounded player count in `PLAY` causes an n² diplomacy allocation and an uncaught `bad_alloc` from a ~2 KB file | `sim/src/savegame_extension.cpp` | Fixed #19 |
| IO-2 | Medium | Smacker audio decoding is quadratic, and the unpacked size is unbounded | `video/src/smacker.cpp` | Fixed #19 |
| IO-3 | Medium | `core::read_file` throws on a directory and has no size limit | `core/src/file.cpp` | Fixed #19 |
| IO-4 | Medium | Ship and unit ids are not validated; `next_*_id` can stick at `0xFFFFFFFF` | `sim/src/validate.cpp` | Fixed #19 |
| IO-5 | Medium | The documented island limits are not enforced on load; the `.GAM` export truncates silently | `sim/src/savegame_extension.cpp` | Fixed #19 |
| IO-6 | Medium | Load warnings go to stderr only; the building-data fingerprint is weak and only warns | `sim/src/savegame_extension.cpp`, `app/src/map_viewer.cpp` | Won't fix rest (#21: a status-line notice; multiplayer compares the whole `.cod` files before anyone joins; see "Open work") |
| IO-7 | Low | Mixer interpolation overflows signed `int` | `audio/src/mixer.cpp` | Fixed #19 |
| IO-8 | Low | The sub-tile of a saved tile is not checked against its building | `sim/src/savegame_extension.cpp` | Fixed #19 |
| IO-9 | Low | A ship's tile position is truncated from `pos_x`/`pos_y` | `sim/src/savegame_extension.cpp` | Fixed #19 |
| IO-10 | Low | COD and mod data: `ObjFill` blow-up, unbounded `Nummer`, `Size` wrap, double-to-int casts, GAD include size | `assets/src/cod_parser.cpp`, `building_table.cpp` | Fixed #22 (`Size`, `Nummer`, `ObjFill` in #19; saturating casts, the GAD size limit and docs/modding/data-limits.md in #22) |
| IO-11 | Low | `find_case_insensitive` resolves `..` (same as SC-11) | `core/src/file.cpp` | Fixed #19 |
| IO-12 | Low | Building and production kind numbers are duplicated in assets and sim | `assets/src/building_table.cpp` | Fixed #22 |
| IO-13 | Low | Hand-written reader/writer pairs in one file of over 2,600 lines, silent size drops, a second CRC-32 | `sim/src/savegame_extension.cpp`, `core/src/png_writer.cpp` | Fixed #29, #39 (sections of the wrong size are refused; the PNG writer uses `core::crc32`; #39: every record is one `io()` description that both writes and reads it, in `sim/src/save/` with a file per group of sections, and a golden test records the bytes of every section) |
| IO-14 | Low | Load and save error messages are English text inside localized notices | `app/src/map_input.cpp`, `map_viewer.cpp` | Fixed #20 |
| IO-15 | Low | Stale format docs: limits, section table, string encoding | `docs/formats/o1602-save.md`, `o1602-chunk.md` | Fixed #26 |

### Computer players, ships and military (AU)

| ID | Severity | Finding | Where | Status |
|---|---|---|---|---|
| AU1 | High (multiplayer) | A trade route can name any ship as a stop and move any ware, soldiers included | `sim/src/ships.cpp` | Fixed #19 |
| AU2 | Medium | `CastleOrder::Recruit` accepts any figure with Maxenergy, ships and towers included | `sim/src/castles.cpp` | Fixed #19 |
| AU3 | Medium | A ship can be repaired at any player's shipyard | `sim/src/shipyards.cpp` | Fixed #19 |
| AU4 | Medium | Shipyards never set `fetching`, so they send a new pack donkey every cycle | `sim/src/shipyards.cpp` | Fixed #19 |
| AU5 | Medium | `Fleet::load` takes `m_yard_ms` and `m_trader_ms` unbounded, so a crafted save can stall the game | `sim/src/ships.cpp` | Fixed #19 |
| AU6 | Medium | Ships from original files keep 8 holds; about ten code paths ignore the type's hold count | `sim/src/world.cpp`, `ships.cpp`, `castles.cpp` | Fixed #21 |
| AU7 | Low | O(n) scans per soldier run and per combat candidate | `sim/src/soldiers.cpp`, `combat.cpp` | Fixed #29 (soldiers in 16 x 16 squares for the crowd and blocked checks: -25% instructions in a 2,000-unit battle; a tower index measured no gain and was left out) |
| AU8 | Low | Allocations per path plan (`SeaPlan`, `LandPlan`, `std::function` goals) | `sim/src/ships.cpp`, `soldiers.cpp` | Won't fix (measured in #29: about 0.1% of a large battle's instructions; the path search itself dominates) |
| AU9 | Low | Dead state and flags (`failed_launches`, `no_colonies`, `run_pirate_task`) | `sim/src/shipyards.cpp`, `ai/ai_planner.cpp` | Fixed #32 (`failed_launches` and `run_pirate_task` removed, the save keeps a reserved byte; the scenario switch behind `no_colonies` is not located in `PLAYER4` yet, so colonies stay allowed; one shared `ship_moving()`) |
| AU10 | Low | Remaining hard-coded game numbers | `sim/src/pirates.cpp`, `military.cpp`, `combat.cpp` | Fixed #22 (some settings-flag bits in island_events, trader, volcanoes and ai_planner remain literals) |
| AU11 | Low (uncertain) | Random offsets hit only one parity, with a lopsided range | `sim/src/soldiers.cpp`, `castles.cpp`, `pirates.cpp` | Fixed #32, #42, #44 (the soldiers' one-parity offsets are the original's `Island_RandomX` 0x00443320, its only caller being the soldiers' wander 0x00456D00; #42: pirates use its world twins, clamped to the map; #44: native warriors go to the first owned road or bridge tile a search within 7 of the hut reaches, as 0x00467820, with no random number) |
| AU12 | Low (uncertain) | `war_time_ms` is written on the diagonal but read per pair | `sim/src/diplomacy.cpp` | Won't fix (#32: by design, the original writes the war time only into the victim's own entry; documented) |
| AU13 | Low | Static scratch `Relation` in `Diplomacy::relation`, and `Engine::m_nobody` | `sim/src/diplomacy.cpp` | Fixed #20 |
| AU14 | Low | A bought ship keeps the seller's exploring, attack-move and hold state | `sim/src/ship_orders.cpp` | Fixed #19 |
| AU15 | Low | Fallback ship name `"Ship N"` is not localized | `sim/src/trader.cpp` | Won't fix (#21: the simulation's names must not depend on the player's language; every installation has Text.cod's ship names, so the fallback does not show) |

Earlier items still present: `ShipNote& note()` appends while callers iterate
(`sim/src/ai/ai_logistics.cpp`). `City::trader_inbound` stays set when a trader gives up
(`sim/src/trader.cpp`). Money arithmetic in ships, diplomacy and the AI wraps on `int32`. The AI
scans the whole world after every step (`sim/src/ai/ai_core.cpp`, `ai_world.cpp`). The earlier
`SoldierOrder` item is MP-3.

### App, UI and rendering (UI)

| ID | Severity | Finding | Where | Status |
|---|---|---|---|---|
| UI-1 | Medium | Build status line, window title and default names are English and bypass localization | `app/src/map_viewer.cpp` | Fixed #20 (title and build status; default names see MP-12, AU15) |
| UI-2 | Medium | Sim state depends on the local Text.cod city and ship names | `app/src/map_viewer.cpp`, `sim/src/trader.cpp` | Fixed #20 |
| UI-3 | Medium | Control groups survive loading another game and select unrelated units | `ui/src/unit_selection.cpp` | Fixed #19 |
| UI-4 | Low | `replace_world` rebuilds the simulation in place and misses per-game state | `app/src/map_viewer.cpp` | Fixed #24 (`GameState` built whole, then swapped; out of memory keeps the old game) |
| UI-5 | Medium | `MapViewer` is a god class (about 66 members, 74 methods) | `app/src/map_viewer.cpp`, `map_input.cpp` | Fixed #24, #28, #38 (#24: `GameState` built whole and swapped in; #28: `CommandSink` is the only path to `Simulation::submit`; #38: `VideoController`, `EventPresenter` and `MapInput` take the videos, the game's messages and the map input out of `MapViewer`, from 85 to 62 members; the tested parts live in `o1602_menus`) |
| UI-6 | Low | The window title receives Windows-1252 bytes, but SDL expects UTF-8 | `app/src/map_viewer.cpp` | Fixed #20 |
| UI-7 | Low/Medium | No handling of render device loss | `app/src/map_viewer.cpp`, `render/sdl/src/sdl_ui_renderer.cpp` | Fixed #29, #40 (a render device reset rebuilds all textures; #40: a lost device makes the renderer again and every texture owner takes the new one; if that fails the game ends cleanly) |
| UI-8 | Low | HiDPI and display-scale changes after start are ignored | `app/src/map_viewer.cpp` | Fixed #29 (the pixel scale follows display-scale changes, a Ctrl +/- offset is kept) |
| UI-9 | Low | Per-frame allocations and the 512-entry text cache flush | `render/sdl/src/sdl_ui_renderer.cpp`, `render/src/world_renderer.cpp` | Fixed #29 (LRU text cache, sorted soldier index, reused draw-list containers; the mission text is still wrapped each frame) |
| UI-10 | Low | Land units are not interpolated between ticks | `render/src/land_figures.cpp` | Fixed #32 (soldiers, walkers, townsfolk and sea creatures are drawn between two ticks like ships, through a shared `TickTrail`) |
| UI-11 | Low | Audio hitches on synchronous loads other than savegames | `app/src/map_viewer.cpp`, `game_sound.cpp` | Fixed #29, #32, #33 (#29: audio prefilled before loads and videos; #32: the zoom levels are prepared on a background thread from the start, only the upload stays on the main thread; #33: the menu shows at once instead of waiting for the sprites, and quitting cancels the level being made) |
| UI-12 | Low | Text scroll positions are unbounded | `ui/src/panel.cpp`, `app/src/hud.cpp` | Fixed #26 |
| UI-13 | Low | Settings, slots and keys files: non-atomic writes, ignored errors, undefined encoding | `app/src/app_settings.cpp`, `map_viewer.cpp` | Fixed #20, #29 (#20: atomic writes and logged errors; #29: the files are UTF-8, older Windows-1252 files still read) |
| UI-14 | Low | Hard-coded game numbers and duplicated constants in the app | `app/src/build_tool.cpp`, `map_viewer.cpp` | Fixed #22, #25 (#22: building kinds, production kinds, wares and the four-colour rule named; #25: the game-setting bits, the message icons' banner limit and tile spots) |
| UI-15 | Low | `GameSound` keeps a pointer to the caller's `GameDirectory` | `app/src/map_viewer.cpp` | Fixed #26 |
| UI-16 | Low | App logic is untested; there is no `o1602_app` library | `app/CMakeLists.txt` | Fixed (#21, #24, #28, #37, #38: see BD-7 and UI-5) |
| UI-17 | Low | Fatal start-up errors are invisible on macOS and Linux desktop starts | `app/src/diagnostics.cpp` | Fixed #26 |
| UI-18 | Low | Local triggers become global speed commands; the pause video can disagree with `paused()` | `app/src/map_viewer.cpp` | Fixed #20 |

### Multiplayer and scores (MP)

| ID | Severity | Finding | Where | Status |
|---|---|---|---|---|
| MP-1 | High | Reconnect tokens are sequential and per slot, so any participant can take over another slot, the host's included | `net/src/host_session.cpp` | Fixed #20 |
| MP-2 | Medium | ENet's 1 MiB limit also applies to sending; bigger snapshots are dropped silently | `net/src/enet_transport.cpp` | Fixed #20 |
| MP-3 | Medium | Large `SoldierOrder`s with duplicate ids stall every computer | `sim/src/military.cpp` | Fixed #20 |
| MP-4 | Medium | No authority over pause, speed and host-only commands | `net/src/host_session.cpp`, `sim/src/simulation.cpp` | Fixed #20 |
| MP-5 | Medium | Flooding players crowd out other players' commands, which are dropped silently | `net/src/host_session.cpp` | Fixed #20 |
| MP-6 | Medium | Connections that never send `Hello` keep their slot forever | `net/src/host_session.cpp` | Fixed #20 |
| MP-7 | Low | Rejoining in a loop makes the host serialize and send the whole game repeatedly | `net/src/host_session.cpp` | Fixed #21 |
| MP-8 | Low | LAN discovery can be abused and spoofed | `net/src/lan.cpp` | Fixed #21, #25 (#21: bounded reads, nonce check, at most 64 games; #25: host names are looked up beside the game) |
| MP-9 | Low | German and English installations cannot play together | `app/src/multiplayer.cpp` | Fixed #20 |
| MP-10 | Low | "Main menu" or "New game" on the end screen does not leave the session | `app/src/hud.cpp`, `map_viewer.cpp` | Fixed #20 |
| MP-11 | Low | The end screen handles only 4 colours; players 5 to 12 cannot be selected | `app/src/end_screen.cpp` | Fixed #20 |
| MP-12 | Low | English default names and engine error texts reach players and the sim state | `net/src/host_session.cpp`, `app/src/main.cpp`, `app/src/multiplayer.cpp` | Fixed #21 |
| MP-13 | Low | The fingerprint serializes the game every 100 frames, so its cost grows with speed | `net/src/session.cpp` | Fixed #21 |
| MP-14 | Low | Names and chat accept control characters; a newline breaks the high-score file | `net/src/protocol.cpp`, `assets/src/high_scores.cpp` | Fixed #20 |
| MP-15 | Low | A spectator cannot be kicked; a stalling player can freeze the game for 10 s repeatedly | `net/src/host_session.cpp` | Fixed #21, #25 (#21: a repeated staller waits a quarter of the time; #25: the host can remove spectators) |
| MP-16 | Low | High-score and progress files fall back to the current directory | `app/src/map_viewer.cpp` | Fixed #20 |
| MP-17 | Low | Score and monument rules are hard-coded | `sim/src/score.cpp`, `consumption.cpp` | Fixed #27 (score curves, money steps and the monument rule named in `assets/game_data.hpp`) |
| MP-18 | Low | ENet licence notice and third-party register entry missing | `third_party/README.md`, `app/CMakeLists.txt` | Fixed #20 |
| MP-19 | Low | Text fitting in `ui::Form` is quadratic, every frame | `ui/src/form.cpp` | Fixed #29 (binary search, the same cuts as before) |
| MP-20 | Low (informational) | No host migration; the game freezes when the host leaves | `app/src/multiplayer.cpp` | Fixed #32 to #35, #41 (#41: the host names a successor in every roster; when the host leaves, the successor hosts the game on and the others follow it with their seats, ADR 0011 point 15; when that fails, each client saves `saves/hostleft.o1602`, which loads with its own seat, #33, also as .GAM, #34) |

MP-3 is the earlier review's `SoldierOrder` item. In the new numbering it is not AU14, which is the
bought-ship item.

### Build, tests and documentation (BD)

| ID | Severity | Finding | Where | Status |
|---|---|---|---|---|
| BD-1 | Medium | CI reports one green test; data tests skip invisibly | `tests/CMakeLists.txt` | Fixed #19 |
| BD-2 | Low | A test that fails a `CHECK` and then skips counts as skipped; an empty filter passes | `tests/test_main.cpp` | Fixed #19 |
| BD-3 | Medium | Data tests use the maintainer's own savegame slots as fixtures | `tests/data/` | Fixed #26 |
| BD-4 | Medium | zlib is optional, and the Windows CI build has none | `CMakeLists.txt`, `.github/workflows/ci.yml` | Fixed #19 |
| BD-5 | Medium | No cross-platform determinism check (golden state hash) | `tests/unit/test_ai.cpp` | Fixed #19 |
| BD-6 | Medium | AI, `.GAM` writer and `new_game` are barely covered without original data | `sim/src/ai/`, `sim/src/savegame_writer.cpp` | Fixed #23, #27, #36 (#23: a data test runs a new endless game for three game hours; #27: unit tests round-trip a synthetic world through the `.GAM` writer, which found and fixed a city satisfaction bug; #36: unit tests for `new_game` and AI diplomacy without data, and a CI job measures line coverage with gcovr) |
| BD-7 | Medium | App, platform, render_sdl and o1602-tool have no tests | `app/CMakeLists.txt`, `tools/o1602-tool/main.cpp` | Fixed (#21: the build tool is in the tested library with five tests; #24: the questions before orders, with four tests; #28: the command sink with three tests, 14 o1602-tool smoke tests and a game start without data in CI, also under ASan; #37: `o1602_sdl_tests` covers the window, audio output and both SDL renderers headless in the game's CI jobs) |
| BD-8 | Low | CI hygiene: actions pinned by tag, unpinned clang-format, no hardening, an untrue bug-ref "CI check" claim | `.github/workflows/ci.yml` | Fixed #19, #29 (#19: `_GLIBCXX_ASSERTIONS`; #29: actions pinned by SHA with Dependabot, clang-format-18 by name) |
| BD-9 | Low | Tests share fixed temp paths | `tests/unit/test_assets.cpp` and others | Fixed #19 |
| BD-10 | Low | CMake details: placeholder URL, stale zlib comment, wrong dependency diagram, no layering check | `CMakeLists.txt`, `CMakeLists.txt` in `engine/` | Fixed #19, #20, #29 (#29: `menus` in ADR 0005 and a layering check in ctest) |
| BD-11 | Medium | Contradictory counts and stale status in `status.md` and `00-plan.md` | `docs/status.md`, `docs/00-plan.md` | Fixed #20 |
| BD-12 | Medium | `setup-arch.md` documents controls and start behaviour that no longer exist | `docs/setup-arch.md` | Fixed #20 |
| BD-13 | Low | Module READMEs still describe the M1/M2 state | `*/README.md` | Fixed #20 |
| BD-14 | Low | Duplicated and overlapping documents | `docs/00-plan.md`, `docs/requests.md` | Fixed #20 |
| BD-15 | Low | Non-English text left in the repository | `.gitignore`, `docs/00-plan.md`, `docs/setup-arch.md` | Partly (#20: folders and docs; test names and private anchors stay) |
| BD-16 | Low | Links into the private repository are not explained; newcomer guides missing | `README.md`, `docs/status.md` | Fixed #20 |
| BD-17 | Low | o1602-tool is one 1,139-line `main.cpp`; the Python tools duplicate the C++ parsers | `tools/o1602-tool/main.cpp`, `tools/python/` | Fixed #29 (o1602-tool split by command group; the Python tools are marked as unmaintained prototypes) |

The private catalogue's three open bugs of Open1602 itself were still present at the review and
are fixed in #20:
- The AI never picked island 0 for a colony (`sim/src/ai/ai_settle.cpp`).
- Soldiers walked towards an unreachable aim inside their search window (`sim/src/land_paths.cpp`).
- Discovered-by flags in `FLEE` are keyed by island number (`sim/src/savegame_extension.cpp`); since
  #19 `validate()` refuses two islands of one number, so the case cannot arise.

The smoke test on a real installation (#20) found one more: a game hosted while it runs (`--host`)
had no slot to join, because every other start player is a computer. Those slots are open now.

### Totals

| Area | Findings | Fixed #19 | Fixed #20 | Fixed #21 | Fixed #22 | Fixed #23 | Fixed #24 | Fixed #25 | Fixed #26 | Fixed #27 | Fixed #28 | Fixed #29 | Fixed #32 | Fixed #36 to #44 | Won't fix | Partly | Open |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| SC | 14 | 6 | 3 | 0 | 1 | 1 | 0 | 0 | 0 | 0 | 1 | 1 | 0 | 0 | 1 | 0 | 0 |
| IO | 15 | 9 | 1 | 0 | 2 | 0 | 0 | 0 | 1 | 0 | 0 | 0 | 0 | 1 | 1 | 0 | 0 |
| AU | 15 | 6 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 1 | 1 | 3 | 0 | 0 |
| UI | 18 | 1 | 4 | 0 | 0 | 0 | 1 | 1 | 3 | 0 | 0 | 4 | 1 | 3 | 0 | 0 | 0 |
| MP | 20 | 0 | 12 | 3 | 0 | 0 | 0 | 2 | 0 | 1 | 0 | 1 | 0 | 1 | 0 | 0 | 0 |
| BD | 17 | 6 | 5 | 0 | 0 | 0 | 0 | 0 | 1 | 0 | 0 | 2 | 0 | 2 | 0 | 1 | 0 |
| All | 99 | 28 | 26 | 4 | 4 | 1 | 1 | 3 | 5 | 1 | 1 | 9 | 2 | 8 | 5 | 1 | 0 |

## Remaining work

Every finding is fixed, left on purpose or partly fixed. The large structural items were closed in
#36 to #41: unit tests for `new_game` and the AI with coverage in CI (BD-6, #36), tests for the SDL
layer (BD-7, UI-16, #37), `MapViewer` split into `VideoController`, `EventPresenter` and `MapInput`
(UI-5, #38), save sections as symmetric `io()` descriptions with recorded bytes (IO-13, #39), a new
renderer after a lost render device (UI-7, #40) and host migration (MP-20, #41). A test round on a
real installation after #41 found no regression: all data tests pass, own savegames from before #39
save back byte for byte, and the game moved on to the successor when the host quit or was killed.
AU11 was settled from the original binary: the pirates' random places at sea clamp to the map
(#42), and the native warriors go to the first road of theirs a search reaches (#44). What remains:

1. BD-15: the German test player names and two anchors into the private repository stay on
   purpose (#20).

Left on purpose: IO-6 rest (a stronger savegame fingerprint; multiplayer already compares the
`.cod` files), AU15, AU8 and SC-14 (measured in #29 at a fraction of a percent), AU12 (the original
writes the war time only into the victim's own entry).

## Known gaps not covered by this review

- The review itself had no original game data. The fixes from #23 on were tested on a
  maintainer's Linux installation: all data tests pass, and scripted game runs covered new games,
  saving and loading, zoom, windows, videos, UTF-8 names and a local multiplayer session.
- Nothing ran on Windows or macOS beyond CI builds and tests. Performance figures are benchmarks of
  synthetic battles and frames, not profiles of a real late game.
- The ENet library itself (`third_party/enet`) and its buffering limits were not reviewed (see
  MP-6). No real internet session with several machines was tested.
- The fixes in PR #19 and PR #20 were mapped to findings from their commit messages and diffs. They
  were not re-reviewed in the same depth as the original code.
- The private companion repository (`docs/re/`, bug catalogue, symbols) was only checked for link
  targets and the three open Open1602 bugs. Its content was not reviewed.
- `engine/script` (Lua) and the editor do not exist yet.
