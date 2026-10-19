# 0009 – Own save format

Status: accepted · 2026-10-18

## Context
Until now, Open1602 savegames were `.GAM` files of the original with an additional chunk `O1602`. The
writer rebuilt the chunks of the original from the records of the source file, and the additional chunk carried
the complete state a second time. This had three drawbacks:

- **Limits:** The chunks of the original hold 7 players, 150 ships, 400 soldiers and 350 towers. With
  the default rules of Open1602 (100 ships, 250 units per player) this is reachable; saving then
  failed.
- **Effort and bugs:** The same state was in the file twice, and some bytes depended on
  what the game was originally loaded from (two such bugs on 2026-10-17 alone).
- **Size:** Fixed tables of the original, uncompressed (Merlin04: 885 KB).

The project owner values an efficient, simple and robust format; that the original can load
Open1602 savegames is not important to him (2026-10-18).

## Decision
1. Open1602 saves in its **own format** (`.o1602`, [o1602-save.md](../formats/o1602-save.md)):
   a short header with version, size and CRC-32, followed by versioned sections with the whole state,
   compressed with zlib.
2. The sections are those of the previous additional chunk, extended by the whole map (`MAPW`) and whatever
   else was only in the chunks of the original. There is **one** way to write and **one** to read.
3. The format is **canonical**: same state, same bytes. Buildings are stored as an id from
   `haeuser.cod`, not as an image number (ADR 0007: ids instead of runtime numbers).
4. The files of the original continue to be **read**. A game can be **exported** as `.GAM`
   as long as it fits; the previous writer with the additional chunk is kept for that.

## Consequences
- None of the original's limits on players, ships, soldiers, towers or map size. The format has
  limits of its own: a world at most 4096 tiles per side, at most 256 MiB of unpacked sections, and
  island numbers of 8 bits in some sections ([o1602-save.md](../formats/o1602-save.md#limits-of-this-version)).
- New parts are added as new sections. A section an older reader does not know is skipped only when
  it is optional (`META`, `ORIG`); every other section is marked required, and a reader that does not
  know it refuses the file instead of running a different game. Since the first version the writer has added
  `PSTA`, `MISN`, `CSTA`, `SCOR`, `QUEU` and `STNC` this way; the full list is in
  [o1602-save.md](../formats/o1602-save.md).
- Strings (names of players, cities and ships) are stored as Windows-1252 bytes, the encoding of the
  original's files and fonts.
- Savegames are about a quarter of the size.
- Existing Open1602 `.GAM` savegames still load (with their additional chunk exactly as before).
- A game loaded from a `.o1602` carries its source file without the island tiles in the section
  `ORIG` (about 12 KB), so that the export as `.GAM` writes the same bytes as before (added
  2026-10-18, after an export without it crashed in the original).
- Later, the list of content packages with versions goes into the header or a section (ADR 0007).
