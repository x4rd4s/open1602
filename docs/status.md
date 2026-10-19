# Implementation status

Status: 2026-10-19 (multiplayer in lockstep with lobby and LAN search, score sheets with end screen and
high score lists, UTF-8 paths, save fuzz test, game numbers in one place). This page is the overview
and the single place for the status of the project. The details are in the linked documents, each in
its "Open" section. It is updated with every completed step.

Links into `re/` (for example [bugs.md](re/bugs.md)) point to the private companion repository
`open1602-re` and only work for maintainers who have it set up (see
[CONTRIBUTING.md](../CONTRIBUTING.md#reverse-engineering)).

**Review October 2026:** The code review of October 2026 with all findings and their status is in
[review.md](review.md). Of its 99 findings, 85 are fixed (PRs #19 to #29, #32 and #33), 5 are left on purpose and
9 are partly fixed; what remains of them is listed under
[Remaining work](review.md#remaining-work).

Symbols: ✅ done · 🟡 partly · ⬜ open · ❓ behaviour of the original not confirmed (needs clarification).

## Summary

- **The simulation of the original is largely complete**: building, economy, houses and consumption,
  disasters, volcanoes, ships and trade, military, diplomacy, pirates, natives, computer opponents,
  new endless game, missions of the scenarios with victory and defeat, reading original savegames
  and exporting them, own save format. It runs in fixed ticks, deterministic and command-based,
  with unit tests and tests against the original data and real savegames (the numbers are in
  section 3).
- **Playable** via the main menu of the original (endless game, scenarios, savegames) or the
  command line, with the side panel of the original (building, combat, status, settings), controls per
  ADR 0010; feedback also in the terminal.
- **Multiplayer** works in lockstep through the host over the network, with Open1602's own menu,
  a lobby and a search for games on the local network ([ADR 0011](architecture/0011-multiplayer-lockstep.md)).
- **The large open blocks** are the editor, the rest of the interface (own game menu, mouse cursor),
  the rest of multiplayer (banner choice in the lobby, changing the host) and the modding foundations
  (content packs, factions as data, Lua).
- **Milestones** (defined in [00-plan.md](00-plan.md#6-milestones)): M0 ✅ · M1 🟡 (open: parts of the
  island header and scenario chunks; fonts, layouts, sound and videos done) · M2 ✅ · M3 ✅ · M4 ✅
  (simulation, ships, trade, savegames, sound, the side panel with the selection windows, menus and
  settings) · M5 🟡 (military, pirates, world generator stage 1, AI, missions with victory and defeat,
  points, end screen, high score lists and campaign progress done; open: AI switch and diplomacy stance
  of the scenarios, volcano events of the scenarios) · M6 🟡 (lockstep, desync detection with resync,
  reconnecting, lobby, LAN search, Open1602's own multiplayer menu, 4 to 12 players; open: see
  Multiplayer in 1.2) · M7 🟡 (freely assignable keys, mouse-wheel zoom, pixel scaling, screenshot and
  bug fixes done; open: arbitrary resolutions, free zoom, UI scaling, editor) · M8 ⬜ (banners with
  tinted flags already in place).
- **Savegames**: since 2026-10-18 Open1602 saves in its own format `.o1602`
  ([o1602-save.md](formats/o1602-save.md), [ADR 0009](architecture/0009-own-save-format.md)): without
  the limits of the original (7 players, 150 ships, 400 soldiers, 350 towers), with a checksum, compressed,
  about a quarter of the size. Exporting as `.GAM` for the original still works, as long as the game fits.

## 1. Original game

### 1.1 Simulation

| Area | Status | Open | Document |
|---|---|---|---|
| Building and demolishing | ✅ checks, costs, dragging roads and walls, beach, bridges, piers, unlocking, territory, founding a town from a ship, takeover of defenceless towns; build menu with alignment to the road | – | [construction.md](re/construction.md) |
| Production | ✅ cycle, raw materials, storage, idling, statistics, on/off | – | [production.md](re/production.md) |
| Logistics | ✅ carriers, market carts, farmers, herds, fishing boats, mines and deposits, pack mules, stonemason at the quarry, `Grundflg` buildings in pathfinding, trade settings by `Maxware` of the warehouse (`0x00481EE0`) | – (the original does not prefer urgent goods either, S-104) | [logistics.md](re/logistics.md) |
| Growth, drought, hunting | ✅ class clocks, harvest, drought, deer and hunters (field by field against Merlin02 → Merlin03) | – | [growth.md](re/growth.md), [deer.md](re/deer.md) |
| Houses, consumption, taxes | ✅ house list, supply, rising and falling, ruins, consumption, satisfaction, pride, taxes, operating costs, pay; town window as a command (tax per class, name, holding back building material) | – | [houses.md](re/houses.md), [consumption.md](re/consumption.md) |
| Fire, plague, robbers | ✅ with doctor, fire brigade, gallows | – | [disasters.md](re/disasters.md) |
| Volcanoes | ✅ crater, roll, lava, fire, destruction, message only for the discoverer | volcano events of the scenarios | [volcanoes.md](re/volcanoes.md) |
| Ships and trade | ✅ sailing and pathfinding (water near enemy towers more expensive), loading, trade at the warehouse, routes, free trader, shipyards with building and repair, exploring with ore and gold symbols (checked against video); ship window as a command (cannons, overboard, name, selling and buying between players) | – | [ships.md](re/ships.md) |
| Military | ✅ units, cannon towers, castles and training, movement, land and naval combat, embarking, conquest, curse of the natives | – | [military.md](re/military.md) |
| Diplomacy | ✅ treaties, events, mood, tribute, gestures, decisions of the computer players | scenario stance (`PLAYER4` +0x1C/+0x20) | [diplomacy.md](re/diplomacy.md) |
| Pirates, natives | ✅ pirate ships, loot, plundering, nest with founding and expansion, buying peace, bribery; natives with goodwill, consumption, warriors; defeat of the pirates when a mission requires it | – | [pirates.md](re/pirates.md) |
| Computer opponents | ✅ task stack, levels and needs, building site search, maintenance, settling, colonies, supply, fleet, trade, military, diplomacy, missions of the ships from original savegames, reaction to a founding (event 0x0E) | AI switch of the scenarios (player +0x70) | [ai.md](re/ai.md) |
| New endless game | ✅ as in the original, checked byte for byte against two endless starts | multiplayer endless game (`.szm`) | [newgame.md](re/newgame.md), [worldgen.md](re/worldgen.md) |
| Scenarios and missions | ✅ scenarios load and run; missions (`AUFTRAG4`, all 18 goals, loss conditions), victory, defeat (no town and no ship, bankruptcy, computer players, pirates), triumphal arch for the culprit, trade statistics, absent slots; **points** with all eight parts of the original's formula (0x00474F90: money, subjects, cities, land, monuments, defeated opponents, soldiers, ships; double for the winner), the counters of fights and the monument allowances of growing cities; **end screen** (ENDE.GAD) after victory or defeat with every player's sheet; high score lists by the original's rules (`.hss`/`.hsm`, campaign parts by their label, every human at a defeat and at the end) in an own folder, started from the original's lists; campaign progress in an own file; scenarios have no events (confirmed) and read by the menu (numbered missions above it are not listed, campaign parts above it are disabled) | AI switch of the scenarios | [missions.md](re/missions.md) |
| Savegames | ✅ reading original savegames; own format `.o1602` without limits, canonical, with checksum; export as `.GAM` (unchanged it comes back byte for byte, loaded and played on by the original) | export of a game loaded from `.o1602` not checked in the original | [o1602-save.md](formats/o1602-save.md), [savegame-writer.md](re/savegame-writer.md) |

### 1.2 Display, interface, sound

| Area | Status | Open | Document |
|---|---|---|---|
| Islands, buildings, world map | ✅ three zoom levels, four rotations, flags, smoke, status symbols, ownership view (image comparison 0.967) | brightness and colour-cast tables; shipyard symbols „Material fehlt“ ("material missing") and „Brand“ ("fire") | [rendering.md](re/rendering.md) |
| Figures | ✅ all figures of the economy, soldiers in every banner colour, ships with damage, bow wave, white flag, sinking, decorative figures (walkers, jugglers, whales, dolphins, krakens) | muzzle and shot smoke, hits, wreck (SHIPSUNK), health bars; decorative figures also on undiscovered islands | [figures.md](re/figures.md), [flavor.md](re/flavor.md) |
| Interface | 🟡 foundations: all 45 layouts (`.GAD`) read, fonts (`.ZEI`) with colours and line wrapping, screen areas, gadgets in operation (states, click, toggles, groups, sliders, glyph list); in the viewer the frame of the original: head of the side panel with world map, compass, rotation and zoom, status bar with help texts and message symbols; build and info modes: **build menu** (groups as data, drop-down, availability, costs, preview, production chain, demolish, rotate) and player status; money and goods at the top of the map; **selection windows**: production buildings, public buildings, house, warehouse/marketplace with lists and stock, foreign warehouse, shipyard with type selection, castle, ship with transfer of cargo, trading, warehouse founding, trade route and pirates; **controls per ADR 0010**: freely assignable keys (`keys.cfg`, overview with F1), pixel-exact selection with a frame, groups by number key, formations, filters and Tab, right-click and minimap commands, command chains, attack-move, hold position, confirmation prompts, quick save/load; **military mode** with the four windows; **ship and town list**, **diplomacy** with tribute; **settings** with savegames, music & effects, voice messages & films, mission window; **main menu** (single player: scenario list, mission with level choice and high score list, name and flag, savegames, continue, credits, intro); **score sheets** (end screen and "Bewertung" of the info panel, points in the info panel) | own game menu, mouse cursor | [ui.md](re/ui.md) |
| Sound, music, speech | ✅ sounds (building, shots, hits, commands, birds, surf, work and animals, clicks) spatial as in the original, voice messages with a queue, music with randomness, battle music and fading out, 4-bit music unpacked; volumes from `Game.dat` (read only) | tutorial speech, inhabitant and goods messages | [audio.md](re/audio.md) |
| Videos | ✅ own Smacker decoder, message, victory, defeat and pause videos, start sequence (`--intro`), intro and credits from the main menu, sound via the mixer; in a multiplayer game the game runs on during a video (M-018) | video 2990 (campaign progress) | [videos.md](re/videos.md), [smk.md](formats/smk.md) |
| Multiplayer | 🟡 Lockstep through the host over ENet: commands in complete frames, lead by round trip, sender checks, fingerprint every 5 s, resync by savegame after a desync, joining a running game and reconnecting into the same slot (M-003), spectators, slow and frozen computers. Open1602's own multiplayer menu: games on the local network found by themselves, joining by address (remembered), creating an endless game (4 to 12 players, more than four in a world of their own with banners of their own) or a scenario or savegame with password and options, lobby with slots (open, computer), ready, chat and start; a slot nobody took is played by the computer until someone joins, and again when that player leaves; leaving the game leaves its session; `--host`, `--join` | Banner choice in the lobby, the computer taking over slots of players who began as humans, changing the host | [ADR 0011](architecture/0011-multiplayer-lockstep.md) |
| Editor | ⬜ | rebuild `1602Edit.exe` (later) | – |

### 1.3 Needs clarification (behaviour of the original not confirmed)

An observation in the original would help where it says "observation"; how to record and send one is
in [requests.md](requests.md).

| Point | Where | Way to clarify |
|---|---|---|
| Speed and strength of the computer opponents in comparison | [ai.md](re/ai.md) | Observation: endless savegames after 1 and 3 hours |
| Sailing behaviour of the computer ships | [ships.md](re/ships.md) | Observation (video) |
| View rotation 1–3 against the running original | [rendering.md](re/rendering.md) | Observation: screenshot in a rotated view |
| Drought: strength, end, withered fields | [growth.md](re/growth.md) | Observation: savegames "Duerre", "NachDuerre" |
| Volcanic eruption in a running game | [volcanoes.md](re/volcanoes.md) | Observation: savegames before and after the eruption |
| Fire, plague, robbers in large towns | [disasters.md](re/disasters.md) | Observation: larger town, video |
| Effect of a fire on the ship construction of a shipyard | [disasters.md](re/disasters.md) | Observation |
| Whether units in training can be sent out | [military.md](re/military.md) | Observation |
| Who sends message 0x48 („protestiert“, "protests") | [diplomacy.md](re/diplomacy.md) | Analysis |
| TIMERS +0xA5 ("computer idle"): who sets it | [ai.md](re/ai.md) | Analysis |
| Town fields +2/+4/+8, HAUS +0x6A bit 7 (probably `Destroyflg`) | [logistics.md](re/logistics.md) | Analysis |
| Production bits +0x0F/4 and /6 | [production.md](re/production.md) | Analysis |
| Second list at island +0x30, random class of loaded fields | [growth.md](re/growth.md) | Analysis |
| Control switches of the loader (`0x0060912C`, `0x005E6AA8`) | [deer.md](re/deer.md) | Analysis |
| Special figures of branches 9/10/11/13 | [rendering.md](re/rendering.md) | Analysis |
| Island header (rest), attribute bits 26–31, BSH field `type`, palette animation | [scp.md](formats/scp.md), [bsh.md](formats/bsh.md), [col.md](formats/col.md) | Analysis |
| Parts of `TIMERS`, remaining `SZENE` chunks; `AUFTRAG4` +0x48/+0x58 (never read); TIMERS +0xA5 (probably the tutorial) | [gam.md](formats/gam.md), [missions.md](re/missions.md) | Analysis |
| U-007, S-088 (part), S-094 (part) | [bugs.md](re/bugs.md) | inferred from the code; Open1602 avoids them anyway |
| Playback of the messages, character mapping of the fonts | [cod.md](formats/cod.md) | with the interface |

## 2. Open1602 extensions

### 2.1 Done

| What | Where |
|---|---|
| **Bugs of the original**: 199 entries in the catalogue by its own count (2026-10-19), of which 109 fixed, 53 avoided, 6 switchable as a rule, 20 kept | [bugs.md](re/bugs.md) |
| **Settings** (`sim::Rules`, in the savegame): refund on demolition, ships per player (100, original 33), units per player (250, original 100), raw material guarantee, highest game speed (8, original 4), droughts, volcanoes, trader prices | [ADR 0008](architecture/0008-game-settings.md) |
| **Custom worlds** (world generator stage 1): 200–2000 fields per side, number of islands, mix of sizes, villages, volcanic islands, starting areas; chunk `O1602MAP` | [worldgen.md](re/worldgen.md) |
| **Own save format `.o1602`**: full state (map, clocks, randomness, figures, fires, lava, diplomacy, exploration, ships, soldiers, AI) without limits, canonical, CRC-32, zlib; a saved game continues exactly. Exported `.GAM` files carry the same state in the chunk `O1602` | [o1602-save.md](formats/o1602-save.md), [o1602-chunk.md](formats/o1602-chunk.md) |
| **Missions checked the same on every machine** (S-105), only with one's own mission (S-106), bankruptcy only after 200 s in a row (S-107), best assignment of the town goals (S-108), island goals with islands fixed at the start (S-110), checked mission data (S-111); play can continue after victory | [missions.md](re/missions.md) |
| **Determinism**: fixed ticks, random streams per subsystem, AI and decorative figures the same on every machine | [ADR 0004](architecture/0004-deterministic-simulation.md) |
| **Banners**: any number of players in the model, tinted standard flag, units in every colour | [ADR 0007](architecture/0007-extensibility-and-modding.md), [flags.md](modding/flags.md) |
| **No technical limits** of the original tables in the simulation (pools with stable IDs), no limit of 250 display figures | [bugs.md](re/bugs.md) (L-002, L-007) |
| **Computer opponents**: speed independent of the frame rate, wider quarry search, replanting fields instead of demolishing them, log with `O1602_AI_LOG` | [ai.md](re/ai.md) |
| **Fixes after feedback (2026-10-18)**: name entry in the menu (text input only worked in the game), name prefilled with „Namenlos“ ("Nameless") or the last one, the first key replaces it, Enter speaks „Wählt Euer Banner“ ("Choose your banner") (Speech8 107–115), Escape back to the mission choice; confirmation prompts and key overview respond only to clicks (press and release), not to hovering; sea creatures less frequent (`sea_creatures_every`, default 60 s); view frame of the minimap keeps its shape at the map edge; shadow of the build preview shows the selected building (previously the wooden wall); body text wraps in font pixels (mission window, briefing in the menu); status bar and mission window at full width without seams; event symbol in the colour of the competitor (new town, new ship); right-click first closes the drop-down of the build menu, then the building in hand | [flavor.md](re/flavor.md), [bugs.md](re/bugs.md#bugs-of-open1602-itself) |
| **Fixes after feedback, second list (2026-10-18)**: fertility of an explored island in the status bar over grass and trees ([GAME] 22); build mode darkens what cannot be built on, and when placing or clicking a building, everything outside its catchment area; forest in random variants; empty goods slots without a yellow frame; camera stays over the map; softer shadows in the build menu; right-click back to the player status (one click from any menu, build menu together with the building in hand); a farm being looked at selects its field in the build menu; when dragging fields and forest the area is hatched yellow as in the History Edition; savegames unlimited with arrows, Enter saves a new one immediately, a voice confirms the save | [ui.md](re/ui.md), [bugs.md](re/bugs.md#bugs-of-open1602-itself) |
| **Display and controls**: figures also at the smallest zoom level, zoom with the mouse wheel, pixel scaling, screenshot as PNG, own save folder (never into the installation) | [README](../README.md#running), [ADR 0010](architecture/0010-controls-and-interface.md) |

### 2.2 Open or planned

| What | Dependency | Source |
|---|---|---|
| Start dialogue with all settings in groups | Interface | [ADR 0008](architecture/0008-game-settings.md) |
| Flag menu with free RGB colour as in Anno 1503 | Interface | [ADR 0007](architecture/0007-extensibility-and-modding.md) |
| More than four players in the single player start dialogue (multiplayer and `--players` have up to 12), pirates and traders scale with the number of players | Factions as data | [worldgen.md](re/worldgen.md) |
| Factions as data (today a fixed `PlayerRole`) | – | ADR 0007 |
| Content packs with manifest and identifiers, free goods list, PNG and OGG import | M8 | ADR 0007 |
| Lua scripts, custom scenarios and campaigns | M8, scenarios | ADR 0007 |
| World generator stage 2 (custom island shapes, including ones larger than the templates) | deferred until everything runs | [00-plan.md](00-plan.md) |
| Data patches for `.cod` bugs (D-001, D-002 as candidates), switchable | mechanism missing | [bugs.md](re/bugs.md), ADR 0006 |
| Arbitrary resolution, free zoom, UI scaling (freely assignable keys are done) | M7, interface | [00-plan.md](00-plan.md) |
| Replays from the session's frames | M6 | [ADR 0011](architecture/0011-multiplayer-lockstep.md) |
| Ideas: full refund shortly after building; own road pathfinding alongside that of the original; pirate distance of the difficulty level as a setting (S-068) | open, decision needed | – |

### 2.3 Decisions still pending

- **S-035** (demolishing a marketplace destroys houses): kept, warning with the interface.

The decisions taken so far (own save format, U-013, key bindings, urgent goods and tower cost class)
are recorded in the ADRs and in [bugs.md](re/bugs.md).

## 3. Technology and quality

| Point | Status |
|---|---|
| Tests | 508 cases on 2026-10-19: 439 that need no original data (the 49 files in `tests/unit/` and one test in `tests/data/`), and 69 tests against the original data in the 11 files of `tests/data/` (they skip without `ANNO1602_DIR`). Sea and land routes, world generator, AI, multiplayer and a fuzz test over the own savegame (`O1602_FUZZ_ITERATIONS` for longer runs) have their own. Without their own unit tests: the game executable (`engine/app` outside the menus), `engine/platform`, `render_sdl`; `o1602-tool` has ctest smoke tests on synthetic files, and CI starts the game without data (also under ASan/UBSan). How to run them: [tests/README.md](../tests/README.md) |
| Builds | presets `debug`, `release`, `asan`, `ci` and `vcpkg` (the game with `-DO1602_BUILD_GAME=ON`), locally under Linux without warnings; CI for Linux (GCC, Clang, ASan), Windows (MSVC 2022), macOS 14 and the game with SDL3 on all three. Windows and macOS have never run locally |
| Player commands | complete for the simulation (since 2026-10-18 also the town window and ship window); the selection windows trigger them; white flag, scuttling, patrol, command chains and group commands with offset have been added |
| Symbol table `re/symbols/` | 596 functions (private, in `open1602-re`; evidence refers to the document) |
| Local player | the first human in a single player game; in multiplayer the slot taken in the lobby |
