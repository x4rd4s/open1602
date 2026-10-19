# 0007 – Extensibility, content and modding

Status: accepted · 2026-10-16

## Requirements

Set by the project owner (2026-10-16). Open1602 should allow:

1. **New content:** buildings, colours, production chains, islands, landscapes, factions,
   animations and graphics, music and sounds.
2. **Custom campaigns and scenarios.**
3. **New island types and island sizes**, for example huge islands.
4. **A more flexible, more varied island generator.**
5. **No fixed player count.** Instead of four players plus traders, pirates and natives, it should
   be possible, for example, to play with five in multiplayer. The game then scales accordingly,
   for instance for pirates and travelling traders.
6. **Extensible colours and flags** with a flag selection menu as in Anno 1503.

These requirements apply to all new code, even if the feature concerned only comes later.
Fixed numbers of the original may only appear in the loaders and savers of the original formats.

## Decision

### 1. Two layers: original format and engine model

| Layer | Modules | Content |
|---|---|---|
| Original format | `assets` | Reads the files of the original as they are: 7 player slots, colours 0–6, owner 7 = nobody, world 500 × 350, at most 255 islands, `.cod` numbers |
| Engine model | `sim`, `render`, `ui`, `net`, `script` | Knows none of these limits. Players, banners (colour and flag), buildings, goods and figures are entries in tables of any size |

The translation happens exclusively when loading (e.g. `sim::load_world`) and later when
saving in the original format. Savegames of the original stay loadable. Saving uses our own format.
An export to the original format is only possible if the state fits into it; otherwise there is
a clear message.

### 2. Content as packages with stable ids

- **Base game = one content package** (`anno1602`). It is generated at runtime from the original installation
  (`.cod`, `.bsh`, `.scp`, sounds), so that no original data is distributed (ADR 0002).
- **Mods are further packages** (folder or archive) with a manifest: id, version,
  dependencies, load order. They contain data (TOML/JSON), graphics (PNG with
  atlas description), audio (OGG/WAV/FLAC) and scripts (Lua).
- **Ids are strings** with a package prefix (`anno1602:kontor1`, `meinmod:leuchtturm`).
  A package can add entries, modify them (overlay on individual fields) or replace them.
- **At runtime only indices count.** After loading, the tables become flat arrays.
  Strings are only needed for loading, saving and multiplayer reconciliation. This costs
  nothing in the simulation (performance requirement).

| Content type | Source in the original | Extension |
|---|---|---|
| Buildings, production, goods | `haeuser.cod` (`HAUS`, `HAUS_PRODTYP`, `HAUS_BAUKOST`) | custom records and production chains, custom goods list (no fixed number of goods) |
| Figures, animations | `figuren.cod`, `*.BSH` | custom figures, sprites from PNG atlases, any number of frames |
| Banners (colour + flags) | colours 0–6, flag sprites in `SHIP.BSH` | any number of banners (section 4) |
| Factions | hard-coded in the program: settlers, traders, pirates, natives | factions as data with AI behaviour and scaling rules (section 3) |
| Climate zones, landscapes | `NORD`, `SUED`, `*NAT`, island templates | custom climate zones, ground and vegetation sets |
| Islands | 677 templates (`.scp`) in fixed sizes | any size, custom templates, generator rules (section 5) |
| Music, sounds | `MUSIC8`, `SAMPLES`, `Speech8` | custom tracks, sounds per building/figure/event |
| Scenarios, campaigns | `.szs`/`.szm`, hard-wired goals | section 6 |

### 3. Players and factions

- `sim::PlayerId` is an index into the match's player list (8 bits). `sim::kNoPlayer` (0xFF) stands for
  "nobody". This allows up to 254 slots. The real upper limit is set by the lobby and the game rules.
- **Control and faction are separate.** A slot is controlled by a human, an AI or
  nobody (neutral). It belongs to a faction: settlers, traders, pirates, natives
  or a faction from a mod. Until factions become data, `sim::PlayerRole` maps the roles of the original (the computer opponents build on it too).
- **Neutral factions scale with the player count.** Their data contains rules such as "pirates:
  ships = base value + k · settler players" or "traders: one route per n warehouses". This way the
  world adapts to five or eight players.
- Everything that applies per player (money, stock, statistics, diplomacy, discovery) lives in containers
  that grow with the player count. Matrices such as diplomacy have size *n × n*, not 7 × 7.

### 4. Banners: colours and flags

- A **banner** bundles a player's identifying features: colour (flag, minimap,
  ownership view, interface), flag type and later the colour variants of the soldiers. Players refer
  to a banner (`sim::BannerId`), not to a fixed colour number.
- **Player flags are a single flag that is tinted** (proposal by the project owner,
  2026-10-16). In the flag menu you choose one of twelve preset colours or any RGB colour, as
  in OpenRA; the engine tints the flag when loading the graphics (`render::TintedFlagSource`, a set
  of 8 images per banner and zoom level). New players need no new images.
- The template follows a simple rule: pure grey (R = G = B) is cloth, grey 128 is exactly the
  player colour, darker and lighter greys shade it. Everything else stays as it is
  (guide for modders: [docs/modding/flags.md](../modding/flags.md)).
- The base game makes the template from the white original flag. Red, blue, yellow and white are tinted in
  the colours determined from the original images; anyone who wants the original images switches
  them on (`BannerOptions::original_player_flags`, `--original-flags`).
- **Units in player colour** (soldiers, cavalry, cannoneers) follow the same rule. Because the four
  original versions are drawn individually and are not recolourings (measurement in
  [figures.md](../re/figures.md#soldiers-and-player-colours)), red, blue, yellow and white keep the
  original images (`Banner::unit_variant`); every further colour tints a template generated at load time
  (`render::TintedUnitSource`). Soldiers only store their unit type, not the colour.
- Banners with their own motif keep finished images (`FlagStyle::Sprites`, e.g. the pirates'
  skull); natives and traders have no flag (`FlagStyle::None`).
- In a match each banner is assigned at most once. The menu warns about colours that are too similar.

### 5. World, islands and island generator

- **The world size is a property of the map** (`sim::World::width()/height()`). Original maps
  have 500 × 350 tiles.
- **Islands have no size limit** other than memory. Tile coordinates and island numbers
  are 16 bits wide; the original uses 8 bits (L-003).
- **Island generator:** deterministic from a seed (multiplayer, repeatability). It
  works with height and moisture maps from noise functions and generates coasts, mountains,
  rivers, forests and raw materials from them. The rules (climate zone, size, shape, frequencies) are data.
  The original templates (`.scp`) remain as one source among several.
- Huge islands and maps must run smoothly. The renderer only draws the visible
  section (this is already done); the simulation works with regions instead of
  passes over the whole map.

### 6. Scenarios and campaigns

- **Scenario** = map (template or generator with seed) + starting conditions (players, banners,
  goods, buildings) + goals + optionally a script.
- **Campaign** = sequence of scenarios with transitions (conditions, carried-over values, texts, videos).
- **Scripts** are Lua with a deterministic, sandboxed interface: no file access,
  no clock time, randomness only from the game RNG. Events (tick, building, trade, combat) trigger them,
  and changes run as a `Command` (ADR 0004). The script state belongs to the savegame.
- The scenarios of the original are loaded; their hard-wired goals are translated into this model.

### 7. Multiplayer and savegames

- The list of content packages with versions and checksums is part of the savegame and the lobby.
  All participants need the same content, otherwise the match is not started.
- Savegames store ids (strings), not runtime indices. That way they stay valid
  when a package adds entries.

## Consequences

- Some places need more effort at first (tables instead of constants). In return, a mod changes
  data instead of code.
- New features are designed as data and tables first. Fixed numbers in engine code must be
  justified (a comment referring to this ADR).

## Fixed limits in the code and their removal

| Place | Limit | Status |
|---|---|---|
| `sim::MapCell::owner`, `sim::Ship::owner` | owner 0–6, 7 = nobody | ✅ `sim::PlayerId`, `kNoPlayer`; translation when loading |
| `sim::Player` | colour as `assets::PlayerColor` (0–6), kind as `assets::PlayerKind` | ✅ `BannerId` and `PlayerRole` |
| `render::player_rgb`, `FigureTable::flag_for_color` in the renderer | 7 fixed colours, 5 flags | ✅ `render::BannerTable` with tinted single flag, any colours |
| Overlay colour `DrawCommand::tint` | index into the 7 colours | ✅ RGBA directly in the command |
| `sim::World::kWidth/kHeight`, `Cell` with 8-bit island number and coordinates | 500 × 350, 255 islands, islands up to 255 × 255 | ✅ size per world, 16 bits |
| Buildings, figures | `.cod` numbers as index | ⬜ content registry with ids (with the first custom content) |
| Goods | fixed list of the original | ⬜ the economy simulation is done with the original's list; a free goods list comes with content packs (M8) |
| Factions, scaling of neutral players | – | ⬜ `sim::PlayerRole` is still a fixed enum; the computer opponents (2026-10-18) build on it, factions as data are still pending |
| Player count at game start, saving | 4 players at start, 7 players, 150 ships, 400 soldiers, 350 towers in the savegame | ⬜ the world plan supports 16 players; saving beyond the original limits is done with the own format `.o1602` ([ADR 0009](0009-own-save-format.md)) |
| Island generator | – | 🟡 stage 1: custom worlds from the original templates (200–2000 tiles, [worldgen.md](../re/worldgen.md)); stage 2 with custom island shapes postponed |
| Lua, campaigns, content packages | – | ⬜ own milestones (M8) |
