# Balance

Some numbers of the game's balance are values a content package can change instead of constants in
the code ([ADR 0007](../architecture/0007-extensibility-and-modding.md): a mod changes data, not
code). Their defaults are the original's.

**Status:** Done are the values below, the values of the original's buildings ([values.md](values.md)),
saving them with the game and the file `balance.toml` that changes them. More values follow as they
are needed; the needs of the inhabitants and the prices of the wares are still only in the original's
`.cod` files.

## The values

| Table | Field | Default | Limits | Meaning |
|---|---|---|---|---|
| `trader` | `max_held` | 50 | 1–200 | tons of a ware the free trader holds at most |
| `trader` | `max_buy` | 10 | 1–200 | tons of a ware he buys at most in one visit |
| `trader` | `restock_seconds` | 60 | 1–3600 | seconds until he takes on tools or iron ore again when he has few |
| `castle` | `interval_seconds` | 10 | 1–600 | seconds of a castle's training step (one more when it has nothing to do) |
| `castle` | `unload_range` | 6 | 1–16 | tiles from a ship within which soldiers land |
| `disasters` | `fire_spread` | 19 | 0–128 | chance of 128 per epoch that a fire spreads to a building nearby; 0 never |
| `disasters` | `plague_radius` | 4 | 0–16 | tiles a plague reaches to infect a neighbouring house; 0 never |
| `computers` | `steady_below`, `steady_money` | 3791, 379 | 0–1000000, 0–100000 | a computer with less money than the first gets the second at every refresh of its city (every 5.1 s) |
| `computers` | `founding_below`, `founding_money` | 500, 799 | 0–1000000, 0–100000 | a computer that founds a city with less money than the first gets the second |
| `computers` | `founding_bricks` | 8 | 0–100 | tons of bricks a computer's new city gets |
| `computers` | `subsidy_poor_below`, `subsidy_poor_money` | 4733, 797 | 0–1000000, 0–100000 | a computer behind the others with less money than the first gets the second |
| `computers` | `subsidy_below`, `subsidy_money` | 9376, 1793 | 0–1000000, 0–100000 | the same from the settlers on, when it has more than the poor one's limit |
| `computers` | `subsidy_wood`, `subsidy_tools` | 7, 3 | 0–100 | tons of wood and tools a computer behind the others gets while its city has less |
| `computers` | `colony_wood_below`, `colony_wood` | 19, 13 | 0–100 | a young city of a computer behind the others with less wood than the first (tons) gets the second |
| `computers` | `colony_tools_below`, `colony_tools` | 15, 9 | 0–100 | the same with tools, when it has the wood |
| `computers` | `colony_food_below`, `colony_food` | 19, 9 | 0–100 | the same with food, before the computer's second inhabited city |
| `computers` | `colony_bricks_below`, `colony_bricks` | 19, 9 | 0–100 | the same with bricks, from its second inhabited city on |

The `computers` values are the original's cheats: what the computer players get from nowhere, on
every difficulty ([computer-players.md](computer-players.md#what-they-get-from-nowhere)). Values of
0 take a gift away.

A game keeps the values it started with: they are saved with it (section `BALA` of
[o1602-save.md](../formats/o1602-save.md), written only when they differ from the original's; version
2 holds the `computers` values and is written only when one of them differs), so a
mod installed or removed later does not change a running game, and every computer of a multiplayer
game plays with the host's.

## The buildings' values

A table named after a building of the original, `["anno1602:<name>"]` or `["anno1602:<Id>"]` with
the building's `HAUS` Id from `haeuser.cod` ([names](values.md#names)), changes single values of that building: production, running and build costs,
reach, hit points. These tables belong in `buildings.toml` now; `balance.toml` still takes them, as
it did before. The fields and their limits are in [values.md](values.md), with those of the
figures.

```toml
# A cheaper bakery that makes twice the bread.
["anno1602:bakery"]
produce = 2
running_cost = 10
```

### Revised bread production

The game setting "Bread production" (`Rules::revised_bread`, [ADR 0008](../architecture/0008-game-settings.md))
uses the same values, from `data/balance/revised_bread.toml`, compiled into the game: the grain farm
makes 2 t of grain per ripe field, the bakery 2 t of bread from 2 t of flour for 15 gold a minute
instead of 5. Three farms with 21 fields, two mills and a bakery then make about 6.3 t of bread a
minute instead of 1.5 t, for the same 6.3 gold per ton as two cattle farms and a butcher; meat and
fish stay as they are (`tests/data/test_food_chains.cpp` measures it). A package's values for the
same buildings go before the setting's. The setting is on by default; the original's settings
switch it off.

## Where a mod's changes are

```
<user data folder>/mods/<package>/balance.toml
```

The user data folder and the package's name are as for [messages](messages.md#where-a-mods-messages-are).
Packages are read in their [load order](packages.md#load-order); a later one can change what an earlier one set. The
changes apply to the games started afterwards: new games and scenarios or savegames of the original.
Problems are written to the log (standard error) with the file and the line.

## The file

`balance.toml` is the same part of [TOML](https://toml.io) as `messages.toml`. Each table changes
single fields; a field that is not given keeps its value:

```toml
# A harder game: the trader buys less, fires spread more easily.
[trader]
max_buy = 5

[disasters]
fire_spread = 40
```

A value outside its limits or that is no whole number is passed over with a warning, as are unknown
tables and fields. A file that is no valid data file as a whole changes nothing.

## In the code

`engine/sim/include/o1602/sim/balance.hpp`: `sim::Balance` with the values, `kBalanceFields` with
their names and limits, `apply_balance_file`; `World::balance()` is what the simulation reads.
The buildings' values are `Balance::buildings` with `kBuildingFields`; `apply_building_values` writes
them into the game's building table after restoring the original's, and `rule_building_values`
gives those of the settings (`data/balance/*.toml`).
