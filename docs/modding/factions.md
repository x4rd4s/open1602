# Factions

Besides the players who found cities, a game of Anno 1602 has the free trader, the pirates and the
natives. In Open1602 these are **factions as data**
([ADR 0007](../architecture/0007-extensibility-and-modding.md), section 3): who steers a player slot
(a person, the computer or nobody) is apart from the faction the slot belongs to, and the numbers a
faction grows with when more people play are values a mod can change.

**Status:** Done are the base game's four factions as data in every world, the numbers below, saving
them with the game and the file `factions.toml` that changes them. Still open, with milestone M8:
factions of a package's own (they need slots in a new game and, to act differently from the
original's four, a behaviour of their own from a script).

## The base game's factions

| Id | Behaviour |
|---|---|
| `anno1602:settlers` | the people and computer players: found cities, trade, make treaties |
| `anno1602:trader` | the free trader: sails from Kontor to Kontor, buys and sells |
| `anno1602:pirates` | at war with everyone but the trader, hunt ships from their nests |
| `anno1602:natives` | villages on the islands, trade with the settlers |

A game keeps the factions it started with: they are saved with it (section `FACT` of
[o1602-save.md](../formats/o1602-save.md), written only when they differ from the base game's), so a
mod installed or removed later does not change a running game, and every computer of a multiplayer
game plays with the host's factions.

## The numbers

Each number grows with what the game has: `base`, and one more for each `per` of what is counted
(begun), held between `min` and `max`; below `from` it is 0. `per = 0` keeps it at `base`.

| Field | Counted | Default | Meaning |
|---|---|---|---|
| `nests` (pirates) | human and computer players | `per = 4`, `min = 1` | how many nests the pirates aim for, each with the original's ships; only with the setting "Pirate nests: one for every four players" (the original has one) |
| `ships` (free trader) | Kontors in the world | `per = 3`, `from = 2` | how many trader ships sail (the original's one for every three Kontors, none with fewer than two) |
| `reach_players` (free trader) | – | `4` | with more human and computer players than this the trader keeps to as many Kontors as a game of this many would have (setting "Trader's reach"); 0 never |

A field a faction's behaviour does not use (the settlers' `nests`) does nothing.

## Where a mod's changes are

```
<user data folder>/mods/<package>/factions.toml
```

The user data folder and the package's name are as for [messages](messages.md#where-a-mods-messages-are).
Packages are read in their [load order](packages.md#load-order); a later one can change what an earlier one set. The
changes apply to the games started afterwards: new games and scenarios or savegames of the original.
Problems are written to the log (standard error) with the file and the line.

## The file

`factions.toml` is the same part of [TOML](https://toml.io) as `messages.toml`. Each table names a
faction by its id and changes single fields; dotted keys reach the parts of a number:

```toml
# Large games: a pirate nest for every three players, at most five, and more traders.
["anno1602:pirates"]
nests.per = 3
nests.max = 5

["anno1602:trader"]
ships.per = 2
reach_players = 6
```

Fields: `nests.base`, `nests.per`, `nests.from`, `nests.min`, `nests.max`, the same for `ships`, and
`reach_players`, each a number from 0 to 65535. An unknown field or a wrong value is passed over with
a warning and the rest is used; a table for a faction that does not exist is passed over (a faction
of a package's own is not supported yet); a file that is no valid data file is passed over as a whole.
