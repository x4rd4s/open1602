# Open1602 savegame (`.o1602`)

Open1602 saves games in its own format ([ADR 0009](../architecture/0009-own-save-format.md)).
It contains the complete state of a game, depends on no other file and knows none of the
limits of the original (players, ships, soldiers, towers, map size). A saved game continues
after loading **exactly as if it had never been paused**; data tests check this with a
real savegame (lava in the air, a fire with the fire brigade) and with an endless game full of
computer opponents.

In Open1602: `engine/sim/include/o1602/sim/save_game.hpp` (`write_game`, `read_game`, `save_info`).
The format shares its sections with the extra chunk of exported `.GAM` files
([o1602-chunk.md](o1602-chunk.md)); they are written and read in `engine/sim/src/save/` (see
[In the code](#in-the-code)).

Open1602 still reads the files of the original (`.SZS`, `.SZM`, `.GAM`). A game can be
exported as `.GAM` as long as it fits within the limits of the original ([savegame-writer.md](../re/savegame-writer.md)).

## Structure

All numbers little-endian. A **string** is a u32 byte count followed by that many bytes, without a
terminating zero. The bytes are **Windows-1252**, not UTF-8: names of players, cities and ships are
kept as the game's fonts draw them, the same encoding as the original's savegames and texts. Typed
text is converted from UTF-8 when it is entered (`core::utf8_to_cp1252`); a reader that shows a name
elsewhere converts it back (`core::cp1252_to_utf8`).

| Offset | Type | Contents |
|---|---|---|
| 0 | 8 bytes | identifier `OPEN1602` |
| 8 | u16 | format version, currently 1 |
| 10 | u16 | flags: bit 0 = the sections are a zlib stream |
| 12 | u32 | 0 |
| 16 | u64 | size of the sections (unpacked) |
| 24 | u32 | CRC-32 (IEEE, as in zlib and PNG) of the unpacked sections |
| 28 | u32 | size of the data after the header (packed or not) |
| 32 | … | the sections, packed with zlib (level 6) if bit 0 is set |

Without zlib in the build, Open1602 writes the sections unpacked; such a build refuses to load a packed file
with a clear message.

**Section:** u32 identifier (four characters), u16 version, u16 flags, u32 length, data. A reader skips
sections it does not know and versions it cannot read, and reports each in the load's warnings. Later
versions can thus add parts without invalidating older savegames. Flag bit 0 marks a section as
**required**: a reader that cannot read such a section loads nothing (a game without it would run
differently, e.g. desynchronise in multiplayer). All sections are required except `META` and `ORIG`;
see [o1602-chunk.md](o1602-chunk.md) for the rules. A file is refused if a section comes twice, if a
section it reads has bytes left over after its data, or if `WRLD`, `ISLE` or `PLAY` is missing.

| Identifier | Contents |
|---|---|
| `META` | width, height, game time, fingerprint of the building data (FNV-1a over Id, image and size of every building from `haeuser.cod`), name of the first human player: for the lists of savegames (`save_info` unpacks only the head of the file) |
| `MAPW` | the whole map: world size, per island the header (identifier, number, size, position, template, climate), the position on the world map and **both layers of all tiles**. The values are ordered by kind (first the building ids of all tiles row by row, then all sub-tiles, rotation and phase, owner, city, random value); zlib packs that well. Version 2 (written since 2026-10-24, version 1 is still read): number, width and height of the header as u16, and each column of a layer (all building ids, all sub-tiles, ...) as a byte 0 and its values, or as a byte 1, the number of runs (u32) and per run its length (u32) and value, whichever is shorter. The runs of a column must together be exactly as long as the column, and the islands of the section together no larger than the world |
| `RULE` | settings (`sim::Rules`) as pairs (u16 key, u32 value); unknown keys are skipped |
| `WRLD` | game time, city name counter, difficulty and settings word, drought date and seed, random generators, clocks of the houses and of growth, next disaster number |
| `WRL2` | further settings of the world: interval between two pirate ships |
| `TUTR` | one byte, 1: the game is the tutorial (`TIMERS` +0xA5, [ai.md](../re/ai.md#tutorial)); written only then, optional, so a reader that does not know it loads the game as an ordinary one |
| `ISLE` | per island: deer spots, drought, craters, deposits, fertility (version 2: 16-bit coordinates, version 3: in addition 16-bit island numbers and deposit coordinates, see [o1602-chunk.md](o1602-chunk.md#section-versions)) |
| `PLAY` `CITY` `PROD` `HOUS` `GROW` `COST` `ANIM` `DISA` `VOLC` | players, cities, production list with free slots, houses, growth, shipyards and castles, animals, fires and plagues, volcanoes with lava in flight |
| `FACT` | the world's factions (id, behaviour, the numbers they grow with: pirate nests, trader ships, trader reach; [factions.md](../modding/factions.md)) and the faction of each player (order as in `PLAY`). Written only when the factions are not the base game's, optional: a reader without it plays the base game's factions that the role byte of `PLAY` gives (0 human, 1 computer, 2 free trader, 3 pirates, 4 natives; a faction of a content package goes there as the one it acts as) |
| `TILE` | not in the own savegame (`MAPW` holds every tile); only in the chunk of an exported `.GAM` |
| `PSTA` | per player (order as in `PLAY`): mission, status (playing, defeated, absent), won, defeated opponents, the two counters of the defeat rules, trade statistics (purchases, sales); version 2 also the scenario switches of a computer ([o1602-chunk.md](o1602-chunk.md#section-versions)) |
| `CSTA` | per city (island, number): trade statistics of the warehouse (purchases, sales, as city + 0x1F0/+0x1F4); if the section is missing, the values from `STADT4` apply |
| `MLCK` | per city (island, number): the game time the building material was locked or the people last complained about it (city + 0x1E8; message 0x48 every 18 minutes); if the section is missing, the clock starts at the next city tick |
| `CPOP` | per city (island, number): the last population message (city + 0x253, `STADT4` +0x05; 0x11 = 100 inhabitants … 0x1D = 9000, each comes once); if the section is missing, the value from `STADT4` applies |
| `CGUP` | per city (island, number): the ships that gave up their way to its Kontor since a route last stopped there (city + 0x252; at four a computer owner complains, [ships.md](../re/ships.md#free-trader)); written only when a city has some, optional: without it the counts start at 0 |
| `BALA` | the values of the balance a package changed (seven `u32` in the order of `kBalanceFields`: the trader's most held, most bought and restock seconds, a castle's interval and unload range, the chance of fire spreading and the reach of a plague; [balance.md](../modding/balance.md)). Written only when they are not the original's, required: a reader without it would compute another game; a value outside its limits loads nothing. Does not exist in the original |
| `BLDV` | the values of the original's buildings that a package or a setting changed (`sim::BuildingValue`: u32 building Id, u8 field in the order of `kBuildingFields`, u32 value, sorted by Id and field; [balance.md](../modding/balance.md#the-buildings-values)). Written only when there are any, required: a reader without it would compute another game; an unknown field or building or a value outside its limits loads nothing. Does not exist in the original |
| `PACK` | the content packages the game uses, in their load order (`assets::ContentPackage`: u32 count, then per package its id and its version as u32 length and UTF-8 bytes, and the u64 hash of its files; [packages.md](../modding/packages.md)). Written whenever the world names its packages, which every game started since 2026-10-26 does, even with none; optional: the packages decide only what a computer loads beside the game. A file without it plays with the packages installed. Does not exist in the original |
| `GLDG` | per city (island, number) that has goods statistics: the minutes closed, then per good ever booked its ledger (warning given, the running minute's and quarter's counts, the last 60 minutes and the last 48 quarters of an hour as samples of 16 flows and the stock), then whether the city keeps the figures of its window and if so their history (minutes closed, 60 minutes and 48 quarters of 13 figures, the sums of the running quarter; [ADR 0012](../architecture/0012-goods-statistics.md)); written only when a city has statistics, optional: without it the statistics start empty. Does not exist in the original |
| `SCOR` | per player (order as in `PLAY`): soldiers killed and lost, ships sunk and lost (`PLAYER4` +0x14..+0x1A; u16 in version 1, u32 in version 2, which is written only when a counter is above 65 535); then per city (island, number): growth of the house looked at last and the game time since it grows (monument allowances, city + 0x1F8/+0x1E4); if the section is missing, the counters from `PLAYER4` apply and the cities start without growth |
| `MISN` | the missions (goals, loss conditions, values, text, the islands of the island goals recorded at the start) and the end of the game (time, winner) ([missions.md](../re/missions.md)) |
| `FLEE` `ARMY` `DIPL` `EPOC` `XPLR` | ships with course, route and cargo, shipyard orders, trade settings (`FLEE` version 2: routes with eight stops, version 3: stops with eight wares, version 4: 16-bit island numbers, see [o1602-chunk.md](o1602-chunk.md#section-versions)); soldiers, towers, building damage, shots in flight, castle slots; diplomacy; island epochs (`EPOC` version 2: 16-bit island numbers); exploring |
| `QUEU` `STNC` | command chains of the ships and units (Shift+right-click, up to 16 destinations each); attack move and "hold position" (ADR 0010, does not exist in the original) |
| `SIMU` | the state of the running game: clocks of the simulation, all active figures, the state of the computer opponents, the names for new cities and ships, the game speed and the pause. Version 2 holds it in parts with their own versions ([o1602-chunk.md](o1602-chunk.md#simu)) |
| `ORIG` | the file of the original the game comes from (savegame, scenario or the template of a new game), as chunks without the island tiles (`INSELHAUS`), about 12 KB packed. Only for the export as `.GAM`: it takes from it the bytes that Open1602 does not track, exactly as for a game loaded directly from the file. If the section is missing, the export writes defaults like a new game |

The writer emits the sections in this order: `META`, `MAPW`, `RULE`, `WRLD`, `WRL2`, `TUTR`, `ISLE`, `PLAY`,
`FACT` (only with factions of their own), `BALA` (only with values of a package), `BLDV` (only with building values), `PACK` (only when the world names its packages), `PSTA`, `MISN`, `CITY`, `CSTA`, `MLCK`, `CPOP`, `CGUP` (only when a city has a count), `GLDG` (only when a city has statistics), `SCOR`, `PROD`, `HOUS`, `GROW`, `COST`, `ANIM`, `DISA`, `VOLC`, `FLEE`,
`EPOC`, `ARMY`, `DIPL`, `XPLR`, `QUEU`, `STNC`, `ORIG` (only for a game that came from a file of the
original) and `SIMU` (`engine/sim/src/save/sections.cpp`, `encode_sections`). `SHIP` and `SOLD`,
the lists of older versions, are only read. The details of the sections from `RULE` onwards are in
[o1602-chunk.md](o1602-chunk.md).

**Buildings** are stored as Id from `haeuser.cod` with sub-tile, never as image number. If the fingerprint
of the building data does not match, Open1602 loads anyway and reports it; an Id that the data does not know
makes the savegame unreadable (instead of wrong buildings).

## Properties

- **Canonical:** the same state gives the same bytes, no matter where the game came from. Saving,
  loading and saving again yields the file unchanged; two games can be compared byte for byte
  (the tests do that).
- **Robust:** the loader first checks header, size and CRC-32, then reads all sections and checks them
  against the world (islands, tiles within their island, ships in the world, units on known
  islands, random generators). Then `validate(World)` (`validate.hpp`) checks the finished world as a whole,
  as after every load, also of a file of the original: islands on the map, cities, buildings, units,
  craters and deer spots on known islands and within their tiles, owners that exist. If the loader is given the
  figure table (the game does that), it also reads `SIMU` as a trial run
  (`Simulation::state_readable`). Only if everything is correct is the world created; a damaged, truncated
  or newer savegame loads nothing. Every count is checked before memory is reserved: lists against
  the remaining length (they grow with the records actually read), the world size against 4096 tiles
  per side, the player count of `DIPL` (one entry per pair of players) against the player limit.
  Writing goes first to a side file, which is then renamed. A fuzz test
  (`tests/unit/test_save_fuzz.cpp`) changes the sections of a saved game at random (bits, bytes, counts,
  sizes, sections cut, left out, doubled or moved) with a correct checksum: every such file loads
  nothing or a world that validates, saves again and runs.
- **Fixed bytes:** `tests/unit/test_save_format.cpp` writes a world with every saved field set to a
  value of its own, as the own savegame and as the chunk of a `.GAM` (`ISLE` in version 1 and 3,
  `VOLC` in version 1 and 2), and compares every section with its recorded size and CRC-32; reading those bytes and
  writing them again gives the same bytes. A change of a section's bytes needs a new version of it.
- **Small:** Merlin04 (130 minutes, two cities) needs 248 KB instead of 885 KB as a `.GAM` with the extra chunk.

## In the code

`engine/sim/src/save/` holds the sections, a file per group: `map.cpp` (`MAPW`, `TILE`), `world.cpp`
(`RULE`, `WRLD`, `EPOC`), `islands.cpp` (`ISLE`, `XPLR`), `players.cpp` (`PLAY`, `FACT`, `PSTA`, `MISN`,
`SCOR`, `DIPL`), `cities.cpp` (`CITY`, `CSTA`, `MLCK`, `CPOP`, `CGUP`, `GLDG`, `PROD`, `HOUS`, `GROW`, `COST`), `ships.cpp` (`SHIP`,
`FLEE`), `army.cpp` (`SOLD`, `ARMY`), `nature.cpp` (`ANIM`, `DISA`, `VOLC`) and `orders.cpp` (`QUEU`,
`STNC`). Each record is described once, as a function `io_...(a, record)` that writes it with a
`save::Writer` and reads it with a `save::Reader` (`archive.hpp`); the method names give the width in
the file (`a.u16(x)`). What only the reader does (checks, values rebuilt from others) stands in an
`if constexpr (A::kReading)` block in the description. Written by hand on each side are only `RULE`
(keys and values; the reader skips unknown keys and clamps), `META` (the reader takes only the
fingerprint), the old lists `SHIP` (holds that are filled only) and `SOLD` (read only), and the raw
bytes of `ORIG` and `SIMU`. `sections.cpp` puts the sections together (order, versions, flags) and
reads them all; `apply.cpp` checks what was read against the world and applies it.
`savegame_extension.cpp` wraps them as the chunk of a `.GAM`, `save_game.cpp` as the own file.

## In the viewer

- **F5** saves to `quicksave.o1602` in Open1602's user folder (Linux:
  `~/.local/share/Open1602/saves/`, Windows: `%APPDATA%\Open1602\saves\`), never into the installation.
- `--save FILE` saves on exit: in the own format, or with the extension `.gam` as a savegame of the
  original (if the game fits).
- `--load FILE` loads an Open1602 savegame (or a file of the original via its path).

## Limits of this version

- A world has at most 4096 tiles per side (a larger one is refused when reading). The unpacked
  sections have at most 20 bytes per tile of the world (`MAPW` needs that much for land) plus
  128 MiB, and never more than 1 GiB; a file without `META` at most 256 MiB. A larger game is not
  written (refused before it is packed) and a file promising more is refused before it is unpacked.
- Islands are addressed by their number: every island needs a number of its own (`validate` refuses
  two with the same number). Numbers above 255 (more than 256 islands) use version 3 of `ISLE`,
  version 2 of `EPOC` and version 4 of `FLEE`; `MAPW` version 2 holds them always. An island can be
  larger than 255 × 255 tiles (up to the world size): its craters and deer spots then use version 2
  of `ISLE` and `VOLC`, its deposits version 3 of `ISLE`. Such a game cannot be exported as `.GAM`,
  whose records keep island numbers and coordinates in one byte; the writer refuses it. The world
  generator still plans at most 250 islands (`kMaxPlannedIslands`): it builds the original's island
  records, whose tiles carry the number in eight bits.
- Commands not yet executed between two ticks are not saved (saving
  happens between the ticks).
- The `.GAM` export of a game loaded from an `.o1602` equals that of a directly loaded one
  byte for byte (test over game00 and game10), except for the six bits of unknown meaning per island tile
  (`INSELHAUS` +4 bits 26–31), which `ORIG` does not carry along. A savegame without `ORIG` (older
  version) exports players and island templates correctly, the rest with defaults; the first export
  without both crashed in the original after ten seconds (all players red, island templates 0).
