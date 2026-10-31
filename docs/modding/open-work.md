# Modding: what is done and what is still open

This page compares the requirements of [ADR 0007](../architecture/0007-extensibility-and-modding.md)
with what the engine does today (state of 2026-10-26, after PR #224). It is the list to work from
for milestone M8 ([plan](../00-plan.md#6-milestones)). The table of fixed limits at the end of
ADR 0007 tracks the same items from the code's side.

## What a mod can do today

A mod is a folder `mods/<package>/`. The folder's name is the package's id. Its manifest
`mod.toml` gives a name, a version, the packages it depends on and the ones it loads after; the
packages are read in that order, otherwise in the order of their ids' bytes, the same on every
system ([packages.md](packages.md)).

**Multiplayer.** Before anyone joins, the computers of a multiplayer game compare a fingerprint of
the original data the simulation computes with, `haeuser.cod` and `figuren.cod`
(`assets::content_fingerprint`), and the packages the game uses, one by one with version and hash
([packages.md](packages.md#which-packages-a-game-uses)). A computer that lacks one of them is told
which, and the host offers to send them ([packages.md](packages.md#packages-a-host-sends));
packages it has in addition are not loaded for the game. Factions and balance travel with
the game besides (sections `FACT` and `BALA`), so every computer plays the host's.

| File | Changes | Guide |
|---|---|---|
| `messages.toml` | messages of the status bar: new ones and the original's, text per language, icon, own sound (WAV, Ogg Vorbis, FLAC) or original voice, original video | [messages.md](messages.md) |
| `build_menu.toml` | the build menu: a package's buildings with icon and name, the original's moved or taken out | [build-menu.md](build-menu.md) |
| `factions.toml` | how pirate nests, trader ships and the trader's reach grow with the players | [factions.md](factions.md) |
| `balance.toml` | trader amounts and restocking, castle training and landing range, fire and plague spread | [balance.md](balance.md) |
| `buildings.toml` | values of the original's buildings: production, costs, reach, workers, hit points | [values.md](values.md) |
| `figures.toml` | values of the original's figures: speed, carrying, hit points, damage, range, cannons, price | [values.md](values.md) |
| `own_buildings.toml` | buildings of the package's own, each copying one of the original | [own-buildings.md](own-buildings.md) |
| `goods.toml` | trade goods of the package's own: price, icon, names | [goods.md](goods.md) |
| `classes.toml` | classes of inhabitants: the original's changed, new ones after them, needs of any good | [classes.md](classes.md) |
| `music.toml` | music tracks of the package's own (WAV, Ogg Vorbis, FLAC), normal or battle music | [sounds.md](sounds.md) |
| `graphics.toml` | PNG images of the package's own, drawn by its buildings at all three zoom levels | [graphics.md](graphics.md) |
| `icons.toml` | PNG icons of the package's own for the build menu, goods and messages | [graphics.md](graphics.md#icons) |
| `figure_images.toml` | the original's figures and ships drawn with a package's images | [graphics.md](graphics.md#figures-and-ships) |
| `levels.toml` | unlock levels: the original's changed, new ones after the 32nd, also of a new class | [levels.md](levels.md) |
| `mod.toml` | the package's name, version, dependencies and load order | [packages.md](packages.md) |
| `scenarios.toml` | scenarios of the package's own: map, names, briefing, stars, money, goals, script | [scenarios.md](scenarios.md) |
| `campaigns.toml` | campaigns of the package's scenarios, with money and a script table carried from part to part | [scenarios.md](scenarios.md#campaigns) |

Without a mod file the engine already has banners with tinted flags for any number of players
([flags.md](flags.md)), up to 12 players, worlds and islands beyond the original's sizes
([data-limits.md](data-limits.md)) and its own save format for everything that does not fit the
original's.

The factions and the balance a game started with are saved with it, so a mod installed or removed
later does not change a running game.

## Open items

Each item names the ADR 0007 section it comes from and, where there is one, the finding of the
code review of October 2026 ([PR #109](https://github.com/x4rd4s/open1602/pull/109)).

### 1. Package manifest (ADR 0007 section 2) – done

Done on 2026-10-26 ([packages.md](packages.md)): the manifest `mod.toml` with name, version,
description, dependencies and load order; the hash of a package's files; the packages a game uses
in its savegame (section `PACK`); multiplayer games let in only computers with the game's packages
and load no others for it. Also done: a host sends missing packages to a computer whose player
agrees (kept in `mods-cache`), and the main menu's screen "Mods" turns packages on and off and adds
received ones to the `mods` folder. Still open: packages as archives, not only folders.

Dependencies done on 2026-10-31 ([packages.md](packages.md#versions-a-package-needs)), at the
owner's request: `depends.<id>` takes version ranges (`">=1.2, <2"`); turning a package on in the
screen "Mods" turns on what it needs, turning one off names what is not loaded without it; a
package that cannot be loaded says why in the screen (missing, wrong version, turned off, not
loaded itself, circle), not only in the log.
### 2. Content registry with string ids (section 2)

Part a done on 2026-10-26 ([values.md](values.md)): a package changes single values of the
original's buildings (`buildings.toml`, 16 fields: production, costs, reach, workers, hit points)
and figures (`figures.toml`, 10 fields: speed, carrying, work time, hit points, damage, range, shot
time, cannons, price, material), addressed as `["anno1602:bakery"]` by name or `["anno1602:<number>"]` with the `.cod` number;
a game keeps them (sections `BLDV`, `FIGV`).

Part b done on 2026-10-26 ([own-buildings.md](own-buildings.md)): a package adds buildings of its
own in `own_buildings.toml`, each copying a building of the original (`like`) with its own sprites
and footprint; they are named `<package>:<name>` in `buildings.toml`, in `build_menu.toml` and in
the savegame (section `OWNB`), and the engine gives them the highest free `haeuser.cod` Ids in the
packages' load order. Still open: pictures of a package's own (with the import of content), figures
of a package's own, and a package that replaces another's building.

### 3. Free goods list, resident classes and unlock levels (section 2; MD-2, MD-3, MD-8)

PR #137 moved the ware groups, unlock levels and the panels' required buildings into one place
(`assets/game_data.hpp`), and the building table is refused when it names more resident classes or
unlock levels than the rules know. These tables are still compiled in: a package cannot add a ware,
a sixth resident class, a new public service or more than 32 unlock levels. Needed: a content
object (`GameContent`) built from the packages, part of the fingerprint and the save.

Part 1 done on 2026-10-26 ([goods.md](goods.md)): a package adds trade goods in `goods.toml` (price,
the icon of a good of the original, names by language) with ware numbers from 59 in the packages'
load order; its own buildings make and use them (`ware`, `raw`, `raw2`), Kontors, ships, carts and
the free trader handle them, and a game keeps them (section `GOOD`).

Part 2 done on 2026-10-26 ([classes.md](classes.md)): `classes.toml` changes the original's classes
(values, needs of any good) and adds up to eight classes in all; a house of a package belongs to a
class (`class`), and a game keeps its classes (section `CLAS`).

Part 3 done on 2026-10-26 ([levels.md](levels.md)): `levels.toml` changes the original's unlock
levels and adds levels after the 32nd, up to 255, also of a class a package added; a building of a
package needs one (`level`), and a game keeps its levels and what its players reached (sections
`LEVL`, `UNLK`). Still open: public buildings of a class's own, the city window's tax rows for more
than five classes, messages of a package's own for its levels, and pictures of the goods' own.

### 4. Graphics and sound of a package's own (section 2)

Import of PNG atlases with a description file for buildings, figures and icons, and of OGG, WAV and
FLAC for music and sounds. This is also what the mods' messages need for icons, sounds and videos
of their own (today they use the original's).

Part 1 done on 2026-10-27 ([graphics.md](graphics.md)): `graphics.toml` names PNG images, grids of
sprites that get numbers from 40000 in the packages' load order; a building of a package draws one
(`gfx = "<package>:<name>"`) at all three zoom levels, the smaller ones given or halved from the
larger.

Part 2 done on 2026-10-27 ([sounds.md](sounds.md)): the game reads Ogg Vorbis and FLAC besides WAV
(stb_vorbis and dr_flac in `third_party`); `music.toml` adds a package's tracks to the music, as
normal or battle music, and a message's sound may be any of the three.

Part 3 done on 2026-10-27 ([graphics.md](graphics.md#icons)): `icons.toml` names PNG icons with
sprite numbers from 100000, which every sheet of the interface draws; the build menu, the goods
(`picture`, kept in the savegame section `GPIC`) and the messages name them.

Part 4 done on 2026-10-27 ([graphics.md](graphics.md#figures-and-ships)): `figure_images.toml`
draws the original's figures and ships with a package's images (the image is checked to hold every
sprite the figure draws). Still open: figures of a package's own (new figure numbers, with the
content registry), sounds of a package's buildings and figures, and videos of a mod's own.

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

Part 1 done on 2026-10-27 ([scripts.md](scripts.md)): Lua 5.4.8 in `third_party/lua` with a fixed
string hash seed, and `script::Sandbox` in `engine/script`: no io, os, package and debug libraries,
no loading of files or binary chunks, no addresses in texts, no finalizers and weak tables,
`math.random` from the game's generator, and limits on memory and on the instructions of each call.

Part 2 done on 2026-10-27 ([scripts.md](scripts.md)): a package's `script.lua` runs in a sandbox of
its own, called by the simulation at the end of every tick (`sim::ScriptHost`) with the tick's
events (`on_start`, `on_second`, buildings placed, houses changed, players won and defeated); it
reads the players (`game.player`) and posts the package's messages (`game.message`); its table
`state` and its random generator are saved with the game (section `SCRP`) and are part of the
game's fingerprint.

Part 3 done on 2026-10-31 ([scripts.md](scripts.md#what-a-script-changes)): a script gives and
takes money (`game.add_money`) and goods of a city (`game.add_goods`, read with `game.cities` and
`game.goods`), lets a player win (`game.win`) and puts one out (`game.defeat`). These changes are
no commands: the simulation makes them at the same point of the tick on every computer
(`Simulation::script_money` and the others). Still open: more events and functions.

### 7. Scenarios and campaigns (section 6)

Scenarios as map plus start conditions plus goals plus an optional script, campaigns as a sequence
of scenarios with transitions. The original's scenarios and campaigns play with their missions
(the 18 goal bits of `AUFTRAG4`, [missions.md](../re/missions.md)), but there is no format of
Open1602's own for a package's scenario: goals beyond the original's bits, start conditions for
more players, transitions with carried-over values, texts and videos. Depends on item 6 for
scripted goals.

Part 1 done on 2026-10-31 ([scenarios.md](scenarios.md)): a package's `scenarios.toml` names maps
(the original's `.szs`/`.szm` or Open1602's `.o1602`) with names and briefings per language, stars,
money at the start, goals that become the humans' mission and a script of the scenario's own. The
main menu lists them under a heading of their own; a saved game remembers its scenario (section
`SCEN`) and runs its script again.

Part 2 done on 2026-10-31 ([scenarios.md](scenarios.md#campaigns)): `campaigns.toml` puts a
package's scenarios in a row of up to five parts, each opened by winning the one before. The money
and the table `campaign` of the part's script go on to the next part. Still open: factions of a
package's own in a scenario (part 3, with item 8).

### 8. Factions of a package's own (section 3)

The base game's four factions are data (`factions.toml` changes their numbers). A new faction needs
slots in a new game and, to act differently from the original's four, a behaviour from a script.
Depends on items 2 and 6.

### 9. Island generator, stage 2 (section 5)

Stage 1 builds worlds from the original's island templates ([worldgen.md](../re/worldgen.md)).
Stage 2, islands of their own shapes from height and moisture maps with the rules (climate, size,
shape, frequencies) as data, is postponed. Climates and templates are already in one registry
(MD-7, PR #142), which stage 2 builds on.

## Order of work

Agreed on 2026-10-26:

1. Manifest (item 1), because ids, versions and load order are what every later item refers to.
2. Content registry and content object (items 2 and 3), in three steps: single fields of the
   original's buildings and figures changed by a package, then buildings of a package's own with
   savegames storing ids, then goods, resident classes and unlock levels as data.
3. Graphics and sound import (item 4), right after a package can add buildings, which need it to
   be seen.
4. Lua (item 6), then scenarios and campaigns (item 7) and factions of a package's own (item 8).
5. Computer players from data (item 5), later by the owner's decision of 2026-10-25.
6. Island generator stage 2 (item 9), last, as the [plan](../00-plan.md#7-order-of-work) says.
