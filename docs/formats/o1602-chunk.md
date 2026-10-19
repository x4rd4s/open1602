# Open1602's own chunk (`O1602`)

Since 2026-10-18 Open1602 saves in its own format ([o1602-save.md](o1602-save.md),
[ADR 0009](../architecture/0009-own-save-format.md)). The chunk `O1602` remains for games that are
**exported** as `.GAM` for the original, and for older Open1602 savegames; its sections
from `RULE` onwards are the same as in the own format.

Exported savegames are `.GAM` files of the original ([savegame-writer.md](../re/savegame-writer.md))
with an additional chunk `O1602` after `WIFF`. The original does not know the name and skips
it; if it saves the game again, the chunk is dropped. Open1602 reads it after the chunks of the original and
uses it to replace whatever it contains.

In Open1602: `engine/sim/include/o1602/sim/savegame_extension.hpp`.

## What it contains and why

It is **authoritative** for everything it contains. Lists are stored in full and in their order,
not just what the original lacks. That way, continuing the game does not depend on how the original
rounds values, combines bits or orders lists:

- game settings (`sim::Rules`) of the match;
- game time to the millisecond, clocks of the houses and of growth, drought date and seed, the
  random generators of the simulation, the next disaster number;
- per island: deer spots with exact time, phase of the island epochs, drought, craters, ore deposits,
  fertility;
- players, cities (stock of all goods, reservations, deliveries, supply with 64-bit counters and
  history, pride, upgrade queue, event date), the production list with its free slots,
  houses, growth entries, shipyards and castles, ships with exact cargo, soldiers with exact
  position, animals;
- fires and plagues (S-001), running volcanic eruptions and lava in flight (S-062);
- the clocks of the simulation and all figures: carriers, carts, farmers, herds, fishermen, deer, hunters,
  doctors, fire brigade, robbers, with path, cargo and reservations, plus the logistics statistics;
- diplomacy, exploring, ships with course and route, soldiers, towers and castles;
- the state of the computer opponents (task stack, plans per city, search, reservations, settlement,
  ship missions, attacks; [ai.md](../re/ai.md)), which the original does not save at all.

**Tiles** are only included where loading the chunks of the original would give something different:
ripe grass and fishing grounds outside the cities, which the original does not save. For this the
writer checks what the written `INSELHAUS` chunks give when loaded, and compares both layers
of every tile.

Result (data test): a game that is saved and loaded continues **exactly as if it had never
been paused**. Even with lava in the air, a fire and the fire brigade on its way, five
minutes later it is byte for byte the same savegame as without interruption.

## Structure

All numbers little-endian. A **string** is a u32 byte count followed by that many bytes, without a
terminating zero. The bytes are **Windows-1252** (not UTF-8), as in the original's chunks: names of
players, cities and ships are kept as the game's fonts draw them.

| Field | Type | Contents |
|---|---|---|
| Identifier | 8 bytes | `O1602SAV` |
| Version | u16 | 1 |
| Flags | u16 | 0 |
| Fingerprint | u64 | FNV-1a (64 bit) over all bytes of the file before the chunk (i.e. chunks of the original and `WIFF`) |
| Sections | … | up to the end of the chunk |

**Section:** u32 identifier (four characters), u16 version, u16 flags, u32 length, data. A reader
skips sections it does not know and versions it cannot read and reports them; for that part, what the
chunks of the original say then applies. Later versions can thus add parts without invalidating older
savegames.

In the chunk the writer emits `RULE` (when the settings are known), `WRLD`, `ISLE`, `TILE`, `PLAY`,
`PSTA`, `MISN`, `CITY`, `CSTA`, `SCOR`, `PROD`, `HOUS`, `GROW`, `COST`, `SHIP`, `ANIM`, `DISA`, `VOLC`,
`FLEE`, `EPOC`, `ARMY`, `DIPL`, `XPLR`, `QUEU`, `STNC` and `SIMU`, in this order. `META`, `MAPW`,
`WRL2` and `ORIG` exist only in the own savegame ([o1602-save.md](o1602-save.md)).

Flag bit 0 (**required**): the section holds state the game cannot do without. A reader that cannot
read it (unknown identifier or later version) does not apply the chunk at all, and the game loads from
the original's chunks alone, with a warning. Open1602 sets it on every section except `META`, `ORIG`
and `SHIP` (the copy for readers older than `FLEE`), so a reader older than a required section (for
example `QUEU` or `STNC`) refuses the chunk. Files written before the flags existed have 0
there; every section of theirs is known. A section that changes in a way an older reader cannot follow
gets the next version number (not a new identifier); the old version can be written next to it for
older readers, marked optional.

| Identifier | Contents |
|---|---|
| `RULE` | settings as pairs (u16 key, u32 value); unknown keys are skipped |
| `WRLD` | game time, city name counter, difficulty and settings word, drought date and seed, random generators (state, increment), clocks of the houses and of growth, next disaster number |
| `ISLE` | per island in the order of the world, with island number for checking |
| `TILE` | differing tiles: island number, x, y, layer, building id, sub-tile, rotation, phase, owner, city, random value |
| `PLAY` `CITY` `PROD` `HOUS` `GROW` `COST` `SHIP` `ANIM` `DISA` `VOLC` | the lists of the world, complete (`SOLD`, the soldiers of older versions, is still read) |
| `PSTA` | per player (order as in `PLAY`): mission, status (playing, defeated, absent), won, defeated opponents, the two counters of the defeat rules, trade statistics |
| `MISN` | the missions (goals, loss conditions, values, text, the islands of the island goals recorded at the start) and the end of the game (time, winner) ([missions.md](../re/missions.md)) |
| `CSTA` | trade statistics per city (island, number, purchases, sales) |
| `SCOR` | the score's counters per player (soldiers killed and lost, ships sunk and lost), then per city the growth of the house looked at last and the game time since it grows |
| `FLEE` | ships in full (position in 1/4096 tile, course, holds with price, trade route, destinations with the original's reference, the trader's goods), next ship number, discovered islands, trade settings and affection of the cities, shipyard fields (order, build points, wood, cloth, tick, repair); `SHIP` remains for older readers |
| `ARMY` | soldiers in full (position in 1/4096 tile, hit points, home, destination, planning, clocks, patrol, path), next soldier number, cannon towers, building damage, shots in flight, weapons and slots of the castles |
| `EPOC` | phase of the island epochs and, per island, the phase last seen |
| `XPLR` | exploring: per island "growing explored" per player, and per deposit the player mask (32 bit) ([ships.md](../re/ships.md#exploring)) |
| `DIPL` | diplomacy: per player pair peace, trade, time of war, tribute, trade and damage; per player all 32 events and the culprit ([diplomacy.md](../re/diplomacy.md)) |
| `QUEU` | command chains: per ship, then per soldier with a queue, the id and up to 16 waiting destinations (Shift+right-click) |
| `STNC` | per ship, then per soldier with a stance, the id and the flags attack move (bit 0) and hold position (bit 1) (ADR 0010) |
| `SIMU` | clocks of the simulation and figures (own version number at the start; from version 2 with the shipyards' pack mules and the fleet clocks, from 3 with the military, from 4 with the state of the computer opponents, itself with a version number) |

Buildings are stored as **Id** (from `haeuser.cod`) and sub-tile, not as image number, so that a savegame
also fits with changed graphics; an unknown Id makes the chunk invalid.

## Section versions

All sections are of version 1, except two that have a version 2 since islands can be wider or
higher than 255 tiles (SC-13):

| Section | Version 1 | Version 2 |
|---|---|---|
| `ISLE` | x and y of the eight deer spots and of the craters as u8 | the same as u16 |
| `VOLC` | x and y of the last crater and of each eruption's crater as u8 (after the u16 island) | the same as u16 |

The writer takes version 2 only when one of these coordinates is above 255. A world whose
coordinates fit one byte (every world of the original, and every one written before) gives the
bytes of version 1 as before, which older readers still load; a reader of this version reads both.
A file with version 2 needs an Open1602 that knows it: an older one refuses it, since both
sections are required. Version 2 never occurs in the chunk of a `.GAM`: the export refuses
islands wider or higher than 255 tiles.

## Safety when loading

- **Fingerprint:** if it does not match (another tool has changed the chunks of the original), Open1602
  loads only the chunks of the original and reports it.
- **Check before applying:** first everything is read and checked against the world (islands, tiles
  within their island, free slots of the production list, number of random generators, and that `PSTA`,
  `SCOR` and `DIPL` have as many players as `PLAY`, `FLEE` and `ARMY` as many buildings as `COST`, and
  `XPLR` as many islands and deposits as `ISLE`); only if everything is correct is it applied. A
  damaged or truncated chunk changes nothing.
- Every count is checked before memory is reserved: lists against the remaining length, the player
  count of `DIPL` (one entry per pair of players) against the player limit.

## Limits of this version

- A map larger than the original's (own world) carries its size in the chunk `O1602MAP`
  (u32 width, u32 height; after `WIFF`, before `O1602`), with up to 255 islands. Such savegames are loaded only by Open1602.
- The chunk cannot yet carry more than 7 players, 150 ships, 400 soldiers or 350 towers, nor an
  island wider or higher than 255 tiles (the original's records keep island coordinates in one
  byte); the writer rejects such worlds. With Open1602's rules (fleet limit 100 per player,
  unit limit 250) this is reachable ([savegame-writer.md](../re/savegame-writer.md#open)).
- Commands not yet executed between two ticks are not saved (saving
  happens between the ticks).
