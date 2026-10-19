# Open1602 savegame (`.o1602`)

Open1602 saves games in its own format ([ADR 0009](../architecture/0009-own-save-format.md)).
It contains the complete state of a game, depends on no other file and knows none of the
limits of the original (players, ships, soldiers, towers, map size). A saved game continues
after loading **exactly as if it had never been paused**; data tests check this with a
real savegame (lava in the air, a fire with the fire brigade) and with an endless game full of
computer opponents.

In Open1602: `engine/sim/include/o1602/sim/save_game.hpp` (`write_game`, `read_game`, `save_info`).
The format shares its sections with the extra chunk of exported `.GAM` files
([o1602-chunk.md](o1602-chunk.md)), `engine/sim/src/savegame_extension.cpp`.

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
see [o1602-chunk.md](o1602-chunk.md) for the rules.

| Identifier | Contents |
|---|---|
| `META` | width, height, game time, fingerprint of the building data (FNV-1a over Id, image and size of every building from `haeuser.cod`), name of the first human player: for a later list of savegames |
| `MAPW` | the whole map: world size, per island the header (identifier, number, size, position, template, climate), the position on the world map and **both layers of all tiles**. The values are ordered by kind (first the building ids of all tiles row by row, then all sub-tiles, rotation and phase, owner, city, random value); zlib packs that well |
| `RULE` | settings (`sim::Rules`) as pairs (u16 key, u32 value); unknown keys are skipped |
| `WRLD` | game time, city name counter, difficulty and settings word, drought date and seed, random generators, clocks of the houses and of growth, next disaster number |
| `WRL2` | further settings of the world: interval between two pirate ships |
| `ISLE` | per island: deer spots, drought, craters, deposits, fertility |
| `PLAY` `CITY` `PROD` `HOUS` `GROW` `COST` `ANIM` `DISA` `VOLC` | players, cities, production list with free slots, houses, growth, shipyards and castles, animals, fires and plagues, volcanoes with lava in flight |
| `TILE` | not in the own savegame (`MAPW` holds every tile); only in the chunk of an exported `.GAM` |
| `PSTA` | per player (order as in `PLAY`): mission, status (playing, defeated, absent), won, defeated opponents, the two counters of the defeat rules, trade statistics (purchases, sales) |
| `CSTA` | per city (island, number): trade statistics of the warehouse (purchases, sales, as city + 0x1F0/+0x1F4); if the section is missing, the values from `STADT4` apply |
| `SCOR` | per player (order as in `PLAY`): soldiers killed and lost, ships sunk and lost (`PLAYER4` +0x14..+0x1A); then per city (island, number): growth of the house looked at last and the game time since it grows (monument allowances, city + 0x1F8/+0x1E4); if the section is missing, the counters from `PLAYER4` apply and the cities start without growth |
| `MISN` | the missions (goals, loss conditions, values, text, the islands of the island goals recorded at the start) and the end of the game (time, winner) ([missions.md](../re/missions.md)) |
| `FLEE` `ARMY` `DIPL` `EPOC` `XPLR` | ships with course, route and cargo, shipyard orders, trade settings; soldiers, towers, building damage, shots in flight, castle slots; diplomacy; island epochs; exploring |
| `QUEU` `STNC` | command chains of the ships and units (Shift+right-click, up to 16 destinations each); attack move and "hold position" (ADR 0010, does not exist in the original) |
| `SIMU` | clocks of the simulation, all active figures, the state of the computer opponents and (from state version 5) the names for new cities and ships |
| `ORIG` | the file of the original the game comes from (savegame, scenario or the template of a new game), as chunks without the island tiles (`INSELHAUS`), about 12 KB packed. Only for the export as `.GAM`: it takes from it the bytes that Open1602 does not track, exactly as for a game loaded directly from the file. If the section is missing, the export writes defaults like a new game |

The writer emits the sections in this order: `META`, `MAPW`, `RULE`, `WRLD`, `WRL2`, `ISLE`, `PLAY`,
`PSTA`, `MISN`, `CITY`, `CSTA`, `SCOR`, `PROD`, `HOUS`, `GROW`, `COST`, `ANIM`, `DISA`, `VOLC`, `FLEE`,
`EPOC`, `ARMY`, `DIPL`, `XPLR`, `QUEU`, `STNC`, `ORIG` (only for a game that came from a file of the
original) and `SIMU` (`engine/sim/src/savegame_extension.cpp`, `encode_sections`). `SHIP` and `SOLD`,
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
  as after every load, also of a file of the original: islands on the map, cities, buildings, units and
  craters on known islands and within their tiles, owners that exist. If the loader is given the
  figure table (the game does that), it also reads `SIMU` as a trial run
  (`Simulation::state_readable`). Only if everything is correct is the world created; a damaged, truncated
  or newer savegame loads nothing. Every count is checked against the remaining length before
  memory is reserved. Writing goes first to a side file, which is then renamed. A fuzz test
  (`tests/unit/test_save_fuzz.cpp`) changes the sections of a saved game at random (bits, bytes, counts,
  sizes, sections cut, left out, doubled or moved) with a correct checksum: every such file loads
  nothing or a world that validates, saves again and runs.
- **Small:** Merlin04 (130 minutes, two cities) needs 248 KB instead of 885 KB as a `.GAM` with the extra chunk.

## In the viewer

- **F5** saves to `quicksave.o1602` in Open1602's user folder (Linux:
  `~/.local/share/Open1602/saves/`, Windows: `%APPDATA%\Open1602\saves\`), never into the installation.
- `--save FILE` saves on exit: in the own format, or with the extension `.gam` as a savegame of the
  original (if the game fits).
- `--load FILE` loads an Open1602 savegame (or a file of the original via its path).

## Limits of this version

- Islands are addressed by their number (8 bit) in some sections; a world has at most
  255 islands, an island at most 255 × 255 tiles (the world generator and templates reach that far too).
- Commands not yet executed between two ticks are not saved (saving
  happens between the ticks).
- The `.GAM` export of a game loaded from an `.o1602` equals that of a directly loaded one
  byte for byte (test over game00 and game10), except for the six bits of unknown meaning per island tile
  (`INSELHAUS` +4 bits 26–31), which `ORIG` does not carry along. A savegame without `ORIG` (older
  version) exports players and island templates correctly, the rest with defaults; the first export
  without both crashed in the original after ten seconds (all players red, island templates 0).
