# Modding: what is done and what is still open

This page compares the requirements of [ADR 0007](../architecture/0007-extensibility-and-modding.md)
with what the engine does today (state of 2026-10-24, after PR #159). It is the list to work from
for milestone M8 ([plan](../00-plan.md#6-milestones)). The table of fixed limits at the end of
ADR 0007 tracks the same items from the code's side.

## What a mod can do today

A mod is a folder `mods/<package>/`. The folder's name is the package's id, and the packages are
read in the order of their names (UTF-8 bytes, the same on every system, `assets::mod_packages`).
Every computer of a multiplayer game compares a fingerprint of the original data and the packages
(`assets::content_fingerprint`) before anyone joins.

| File | Changes | Guide |
|---|---|---|
| `messages.toml` | messages of the status bar: new ones and the original's, text per language, icon, WAV sound or original voice, original video | [messages.md](messages.md) |
| `build_menu.toml` | the build menu: a package's buildings with icon and name, the original's moved or taken out | [build-menu.md](build-menu.md) |
| `factions.toml` | how pirate nests, trader ships and the trader's reach grow with the players | [factions.md](factions.md) |
| `balance.toml` | trader amounts and restocking, castle training and landing range, fire and plague spread | [balance.md](balance.md) |

Without a mod file the engine already has banners with tinted flags for any number of players
([flags.md](flags.md)), up to 12 players, worlds and islands beyond the original's sizes
([data-limits.md](data-limits.md)) and its own save format for everything that does not fit the
original's.

The factions and the balance a game started with are saved with it, so a mod installed or removed
later does not change a running game.

## Open items

Each item names the ADR 0007 section it comes from and, where there is one, the finding of the
code review of October 2026 ([PR #109](https://github.com/x4rd4s/open1602/pull/109)).

### 1. Package manifest (ADR 0007 section 2)

A manifest per package with id, version, dependencies and load order. Today the folder's name is
the id and the order is alphabetical; a package cannot say that it needs another one or must load
after it. The versions belong into the fingerprint, the savegame and the lobby (section 7), so that
a game names the packages it needs. Packages as archives (not only folders) come with it.

### 2. Content registry with string ids (section 2)

Buildings and figures are still addressed by their `.cod` numbers. A package cannot add a building
of its own except by shipping a changed `haeuser.cod`, and it cannot change single fields of an
original record (overlay). Needed: entries with ids such as `mymod:lighthouse`, read from the
packages' data files, added, overlaid or replaced, and turned into flat index tables after loading.
Savegames store the ids, not the indexes.

### 3. Free goods list, resident classes and unlock levels (section 2; MD-2, MD-3, MD-8)

PR #137 moved the ware groups, unlock levels and the panels' required buildings into one place
(`assets/game_data.hpp`), and the building table is refused when it names more resident classes or
unlock levels than the rules know. These tables are still compiled in: a package cannot add a ware,
a sixth resident class, a new public service or more than 32 unlock levels. Needed: a content
object (`GameContent`) built from the packages, part of the fingerprint and the save.

### 4. Graphics and sound of a package's own (section 2)

Import of PNG atlases with a description file for buildings, figures and icons, and of OGG, WAV and
FLAC for music and sounds. This is also what the mods' messages need for icons, sounds and videos
of their own (today they use the original's).

### 5. Computer players from data (section 3; MD-4)

PR #141 (MD-4, part 1) named the Kontor levels, the tower records and the Kontor's size in the AI.
Still compiled in: the 32 rows of needs (`ai_planner.cpp`, `kNeeds`), the 55 fixed building slots
of a city (`ai_state.hpp`), the stage orders, the public buildings it builds (`kPublicTasks`), which
building produces what (`producer_slot`), the first colony's crop by `player % 4` (`ai_core.cpp`),
the four unit families and ship types, and the amounts of its cheats. So computers never build a
package's production chain. Needed: needs and producers derived from the building table, slots as
a table keyed by building family, stage orders and cheat amounts as data with today's values as
defaults. Depends on items 2 and 3.

### 6. Lua scripts (section 6)

`engine/script` exists only as a README. Needed: Lua 5.4 with a deterministic, sandboxed interface
(no files, no clock, random numbers only from the game's generator), triggered by events, changing
the game only through commands, with its state in the savegame. Scripts also post the mods'
messages.

### 7. Scenarios and campaigns (section 6)

Scenarios as map plus start conditions plus goals plus an optional script, campaigns as a sequence
of scenarios with transitions. The original's scenarios and campaigns play with their missions
(the 18 goal bits of `AUFTRAG4`, [missions.md](../re/missions.md)), but there is no format of
Open1602's own for a package's scenario: goals beyond the original's bits, start conditions for
more players, transitions with carried-over values, texts and videos. Depends on item 6 for
scripted goals.

### 8. Factions of a package's own (section 3)

The base game's four factions are data (`factions.toml` changes their numbers). A new faction needs
slots in a new game and, to act differently from the original's four, a behaviour from a script.
Depends on items 2 and 6.

### 9. Island generator, stage 2 (section 5)

Stage 1 builds worlds from the original's island templates ([worldgen.md](../re/worldgen.md)).
Stage 2, islands of their own shapes from height and moisture maps with the rules (climate, size,
shape, frequencies) as data, is postponed. Climates and templates are already in one registry
(MD-7, PR #142), which stage 2 builds on.

## Suggested order

1. Manifest (item 1), because ids, versions and load order are what every later item refers to.
2. Content registry and content object (items 2 and 3).
3. Graphics and sound import (item 4) and the AI from data (item 5), which can run side by side.
4. Lua (item 6), then scenarios, campaigns and factions of a package's own (items 7 and 8).
5. Island generator stage 2 (item 9) does not depend on the others and can come at any point.
