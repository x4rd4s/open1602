# engine/sim

The deterministic game simulation ([ADR 0004](../../docs/architecture/0004-deterministic-simulation.md)):
fixed ticks, no floating point in the state, every change through a command. Depends only on
`assets` and `core`; it never uses platform, render, audio or ui, so it runs headless in tests,
tools and on a multiplayer host.

Main entry points:

| Header | Contents |
|---|---|
| `simulation.hpp` | `Simulation`: the running game, `tick()` (its steps in the original's order: `tick_steps()`), commands (`submit`, `schedule`), state hash |
| `world.hpp`, `player.hpp`, `island_map.hpp` | The world: islands and their tile grids, players, cities |
| `command_io.hpp` | Commands as bytes (multiplayer, replays) |
| `rules.hpp` | Game settings (`sim::Rules`, [ADR 0008](../../docs/architecture/0008-game-settings.md)) |
| `random.hpp`, `pool.hpp`, `id_index.hpp` | Random streams per subsystem, entity pools with stable handles, id lookup |

Game systems: `construction`, `drag_path`, `production`, `production_cycle`, `logistics`,
`pathfinding`, `growth`, `houses`, `consumption`, `disasters`, `volcanoes`, `island_events`,
`ship`/`ships`, `sea_paths`, `soldier`/`military`, `land_paths`, `combat`, `target`, `diplomacy`,
`mission`, `score`, `ai` (with `src/ai/`), `event_buffer` (events for the interface), `game_data`
(the numbers of the original's data from `assets/game_data.hpp` in this namespace).

New games and saving: `new_game`, `world_plan`, `world_generator`; `save_game` (the file of the own
format `.o1602`: header, zlib, CRC-32), `savegame_extension` (the `O1602` chunk of an exported `.GAM`),
both made of the sections in `src/save/` (one `io_...` description per record that writes and reads
it, a file per group of sections; see [o1602-save.md](../../docs/formats/o1602-save.md#in-the-code)),
with `state_io` (bounds-checked reading and writing of the state),
`savegame_writer` (export as `.GAM` for the original), `validate` (the last check of every load).
