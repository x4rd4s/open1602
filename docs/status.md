# Implementation status

Status: 2026-10-22 (any window size, full screen, the interface's size and a free zoom; comparison with the running original and its first fixes: pirates make peace after
every surrender, idle computers in the tutorial, computers plan a shipyard when they want ships; top bar
inhabitants with the workers; a click in settings mode switches to info mode). This page is the overview
and the single place for the status of the project. The details are in the linked documents, each in
its "Open" section. It is updated with every completed step.

Links into `re/` (for example [bugs.md](re/bugs.md)) point to the private companion repository
`open1602-re` and only work for maintainers who have it set up (see
[CONTRIBUTING.md](../CONTRIBUTING.md#reverse-engineering)).

**Review October 2026:** The code review of October 2026 with all findings and their status is in
[review.md](review.md). Of its 99 findings, 93 are fixed (PRs #19 to #29, #32 to #42 and #44), 5 are left on
purpose and 1 is partly fixed; what remains of them is listed under
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
- **The large open blocks** are the editor, host migration beyond the local network and the modding
  foundations (content packs, factions of a package's own, Lua).
- **Compared with the running original** on 2026-10-20 ([original-comparison.md](original-comparison.md)):
  the differences found and what is still open about them are in section 1.4.
- **Milestones** (defined in [00-plan.md](00-plan.md#6-milestones)): M0 ✅ · M1 🟡 (open: parts of the
  island header and scenario chunks; fonts, layouts, sound and videos done) · M2 ✅ · M3 ✅ · M4 ✅
  (simulation, ships, trade, savegames, sound, the side panel with the selection windows, menus and
  settings) · M5 🟡 (military, pirates, world generator stage 1, AI, missions with victory and defeat,
  points, end screen, high score lists, campaign progress, the scenarios' switches of the computers and
  their attitude done) · M6 🟡 (lockstep, desync detection with resync,
  reconnecting, lobby, LAN search, Open1602's own multiplayer menu, 4 to 12 players, host migration; open: see
  Multiplayer in 1.2) · M7 🟡 (freely assignable keys, mouse-wheel zoom, pixel scaling, screenshot,
  bug fixes, any window size and full screen, free zoom and the interface's size done; open: editor) · M8 ⬜ (banners with
  tinted flags and the mods' own messages already in place).
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
| Volcanoes | ✅ crater, roll, lava, fire, destruction, message only for the discoverer; scenarios bring their next eruption and craters (they have no volcano events of their own) | – | [volcanoes.md](re/volcanoes.md) |
| Ships and trade | ✅ sailing and pathfinding (water near enemy towers more expensive), loading, trade at the warehouse, routes (Open1602: eight stops on two pages of the editor, numbered, and taking the stops of another own ship; per stop as many wares as the ship has holds, up to eight, on the stop's own page), free trader, shipyards with building and repair, exploring with ore and gold symbols (checked against video); ship window as a command (cannons, overboard, name, selling and buying between players); computers complain about human ships blocking their Kontor or shipyard (event 10) | – | [ships.md](re/ships.md) |
| Military | ✅ units, cannon towers, castles and training, movement, land and naval combat, embarking, conquest, curse of the natives | – | [military.md](re/military.md) |
| Diplomacy | ✅ treaties, events, mood, tribute, gestures, decisions of the computer players, the attitude a scenario gives a computer (`PLAYER4` +0x1C..+0x25) | – | [diplomacy.md](re/diplomacy.md) |
| Pirates, natives | ✅ pirate ships, loot, plundering, nest with founding and expansion, buying peace, bribery; peace with every player after a surrender, loot or not; natives with goodwill, consumption, warriors; defeat of the pirates when a mission requires it | – | [pirates.md](re/pirates.md) |
| Computer opponents | ✅ task stack, levels and needs, building site search, maintenance, settling, colonies, supply, fleet, trade, military, diplomacy, missions of the ships from original savegames, reaction to a founding (event 0x0E), the switches a scenario gives a computer (`PLAYER4` +0x1C, runtime +0x70); a shipyard planned whenever a computer wants ships and has none; idle in the tutorial (`TIMERS` +0xA5, savegame section `TUTR`) | stronger than the original's in long games (section 1.4) | [ai.md](re/ai.md) |
| New endless game | ✅ as in the original, checked byte for byte against two endless starts; multiplayer from the `.szm` levels (every player with the small trading cog) | multiplayer start not compared with a savegame of the original | [newgame.md](re/newgame.md), [worldgen.md](re/worldgen.md) |
| Scenarios and missions | ✅ scenarios load and run; missions (`AUFTRAG4`, all 18 goals, loss conditions), victory, defeat (no town and no ship, bankruptcy, computer players, pirates), triumphal arch for the culprit, trade statistics, absent slots; **points** with all eight parts of the original's formula (0x00474F90: money, subjects, cities, land, monuments, defeated opponents, soldiers, ships; double for the winner), the counters of fights and the monument allowances of growing cities; **end screen** (ENDE.GAD) after victory or defeat with every player's sheet; high score lists by the original's rules (`.hss`/`.hsm`, campaign parts by their label, every human at a defeat and at the end) in an own folder, started from the original's lists; campaign progress in an own file; scenarios have no events (confirmed) and read by the menu (numbered missions above it are not listed, campaign parts above it are disabled) | – | [missions.md](re/missions.md) |
| Savegames | ✅ reading original savegames; own format `.o1602` without limits, canonical, with checksum; export as `.GAM` (unchanged it comes back byte for byte, loaded and played on by the original) | export of a game loaded from `.o1602` not checked in the original | [o1602-save.md](formats/o1602-save.md), [savegame-writer.md](re/savegame-writer.md) |

### 1.2 Display, interface, sound

| Area | Status | Open | Document |
|---|---|---|---|
| Islands, buildings, world map | ✅ three zoom levels and a free zoom between and beyond them (sharp: drawn at the next whole scale and shrunk), four rotations, flags, smoke, status symbols (also the shipyard's „Material fehlt“, "material missing", and „Brand“, "fire"), ownership view (image comparison 0.967), dimmed land as the original's shading tables; an island the player has not discovered shows only its ground layer, so native villages and foreign towns appear when a ship comes near (the minimap hides them too; unlike the original's, it shows native villages and pirates once discovered) | – | [rendering.md](re/rendering.md) |
| Figures | ✅ all figures of the economy, soldiers in every banner colour, ships with damage, bow wave, white flag, sinking, cannon smoke of ships and towers, hits, wreck (SHIPSUNK), health bars, decorative figures (walkers, jugglers, whales, dolphins, krakens); land figures, flags, smoke and volcano fire only on discovered islands, deer and ships everywhere | – | [figures.md](re/figures.md), [flavor.md](re/flavor.md), [military.md](re/military.md#effects) |
| Interface | ✅ foundations: all 45 layouts (`.GAD`) read, fonts (`.ZEI`) with colours and line wrapping, screen areas, gadgets in operation (states, click, toggles, groups, sliders, glyph list); in the viewer the frame of the original: head of the side panel with world map, compass, rotation and zoom, status bar with help texts and message symbols; build and info modes: **build menu** (groups as data, drop-down, availability, costs, preview, production chain, demolish, rotate) and player status; money and goods at the top of the map; **selection windows**: production buildings, public buildings, house, warehouse/marketplace with lists and stock, foreign warehouse, shipyard with type selection, castle, ship with transfer of cargo, trading, warehouse founding, trade route and pirates; **controls per ADR 0010**: freely assignable keys (`keys.cfg`, overview with F1), pixel-exact selection with a frame, groups by number key, formations, filters and Tab, right-click and minimap commands, command chains, attack-move, hold position, confirmation prompts, quick save/load; **military mode** with the four windows; **ship and town list**, **diplomacy** with tribute (more than three partners: a strip of coats of arms; offers not answered yet in the status bar); **settings** with savegames, music & effects, voice messages & films, mission window; **main menu** (single player: scenario list, mission with level choice and high score list, name and flag, savegames with the time played, continue, credits, intro); **start dialog of new games** (endless game, scenario, multiplayer) with all settings in groups (ADR 0008); **score sheets** (end screen and "Bewertung" of the info panel, points in the info panel); **mouse cursors** of the original (arrow, target while a window waits for a click on the map); warning before a demolition takes land from houses (S-035); inhabitants in the top bar with the workers of the city's buildings; a building or ship clicked in settings mode switches to info mode; **tutorial** with its pages, help bubbles (`BUBBLE.GAD`), speech and the next chapter after „Einführungsspiel beenden“ | – | [ui.md](re/ui.md), [tutorial.md](re/tutorial.md) |
| Sound, music, speech | ✅ sounds (building, shots, hits, commands, birds, surf, work and animals, clicks) spatial as in the original, voice messages with a queue, music with randomness, battle music and fading out, 4-bit music unpacked; volumes from `Game.dat` (read only); messages about inhabitants, missing goods and the people's wishes; the tutorial speech per chapter | – | [audio.md](re/audio.md) |
| Videos | ✅ own Smacker decoder, message, victory, defeat and pause videos, start sequence (`--intro`), intro and credits from the main menu, the end of the campaigns (2990 after the success video of campaign 5), sound via the mixer; in a multiplayer game the game runs on during a video (M-018) | – | [videos.md](re/videos.md), [smk.md](formats/smk.md) |
| Multiplayer | 🟡 Lockstep through the host over ENet: commands in complete frames, lead by round trip, sender checks, fingerprint every 5 s, resync by savegame after a desync, joining a running game and reconnecting into the same slot (M-003), spectators, slow and frozen computers. Open1602's own multiplayer menu: games on the local network found by themselves, joining by address (remembered), creating an endless game (4 to 12 players, more than four in a world of their own with banners of their own) or a scenario or savegame with password and options, lobby with slots (open, computer), a banner per slot chosen from the twelve player colours (no two alike, a computer's swapped), ready, chat and start; a slot nobody took is played by the computer until someone joins, and again when that player leaves; a person's slot is played on by the computer 60 s after they left, until they are back (the host's choice); leaving the game leaves its session; when the host leaves, the game moves on to a successor it named (host migration; if that fails, each computer saves it as `hostleft.o1602`); `--host`, `--join` | Host migration beyond the local network | [ADR 0011](architecture/0011-multiplayer-lockstep.md) |
| Editor | ⬜ | rebuild `1602Edit.exe` (later) | – |

### 1.3 Needs clarification (behaviour of the original not confirmed)

An observation in the original would help where it says "observation"; how to record and send one is
in [requests.md](requests.md).

| Point | Where | Way to clarify |
|---|---|---|
| Sailing behaviour of the computer ships | [ships.md](re/ships.md) | Observation (video) |
| Drought: how many fields wither (strength, end and recovery match the original, confirmed again on 2026-10-20) | [growth.md](re/growth.md) | Observation: savegames "Duerre", "NachDuerre" |
| Robbers in large towns (fires and plague: section 1.4) | [disasters.md](re/disasters.md) | Observation: larger town, video |
| Effect of a fire on the ship construction of a shipyard | [disasters.md](re/disasters.md) | Observation |
| Second list at island +0x30, random class of loaded fields | [growth.md](re/growth.md) | Analysis |
| Control switches of the loader (`0x0060912C`, `0x005E6AA8`) | [deer.md](re/deer.md) | Analysis |
| Special figures of branches 9/10/11/13 | [rendering.md](re/rendering.md) | Analysis |
| Island header (rest), attribute bits 26–31, BSH field `type`, palette animation | [scp.md](formats/scp.md), [bsh.md](formats/bsh.md), [col.md](formats/col.md) | Analysis |
| Parts of `TIMERS`, remaining `SZENE` chunks; `AUFTRAG4` +0x48/+0x58 (never read) | [gam.md](formats/gam.md), [missions.md](re/missions.md) | Analysis |
| U-007, S-088 (part), S-094 (part) | [bugs.md](re/bugs.md) | inferred from the code; Open1602 avoids them anyway |
| Playback of the messages, character mapping of the fonts | [cod.md](formats/cod.md) | with the interface |

### 1.4 Differences from the running original

Found in the comparison of 2026-10-20 ([original-comparison.md](original-comparison.md)); the fixed ones
are listed in 2.1.

| Difference | Status | Where |
|---|---|---|
| Computers grow on in long games where the original's stall (up to 30 % more inhabitants at two hours, clearly more after three; second towns earlier on "hard"), mostly because of the fixes S-097 to S-099 | the original's computer players are a setting (`Rules::classic_ai`, "Computer players: as the original" in the start dialog, `--ai classic`); Open1602's stay the default | [ai.md](re/ai.md); comparison section 3 |
| No warships built by the computers in six hours (the original's after about 100 minutes) | fixed in #58 (a shipyard when they want ships); checked on 2026-10-23 in a six-hour run from `game00.gam` (difficulty 3, every player a computer): each of the three computers had a warship (KRIEG1) within 30 minutes, and after six hours they had six warships between them, two of them KRIEG2; and in three new endless games (average, seeds 7, 11 and 23, every player a computer) the first warships came between 60 and 120 minutes, as in the original, with three to five per game after six hours | [ai.md](re/ai.md); comparison section 3 |
| Fires in a computer's large town end within three epochs in Open1602; the original's burned their full 25 epochs, again and again | not a bug: the original's fires last because a burning house catches fire again and its count starts over (S-050), and its fire brigade also sets out for the plague and comes back empty (S-053; the town had eight plagues at the time). Open1602 fixes both on purpose | [disasters.md](re/disasters.md); comparison section 7 |
| Rotated views: Open1602 darkened parts of the map with sharp diagonal edges | not a difference: Open1602 was in build mode in that screenshot, which dims everything outside the own territory (as asked for on 2026-10-18); outside build mode nothing is dimmed, as in the original | [rendering.md](re/rendering.md); comparison section 4 |
| The top bar showed the goods of the town under the camera where the original showed only the gold | not a difference: both show the goods of the town under the mouse cursor, not under the camera (`Hud_DrawResourceBar 0x0040D160` reads the city the hover handler `0x0040A190` stores; Open1602's `MapViewer::resources` takes the tile under the mouse); in the original's screenshot the mouse was elsewhere | [ui.md](re/ui.md); comparison section 4 |

## 2. Open1602 extensions

### 2.1 Done

| What | Where |
|---|---|
| **Bugs of the original**: 217 entries in the catalogue (counted 2026-10-23), most of them fixed or avoided, some switchable as a rule (S-097 to S-099 since 2026-10-22) or kept; open is only T-009 (Wine) | [bugs.md](re/bugs.md) |
| **Corrections of the original's data** (2026-10-23): the bakery's building sound (`Backer.wav`, D-001), the pirate Kontor's flag (D-002), the accents of capitals above the letter instead of a line below it (D-012), a place of its own for every unit of the block formation (D-014); applied to the tables after loading, only where the data still has the original's value; off with `data_fixes = off` in `settings.cfg` or `--original-data` | [ADR 0006](architecture/0006-handling-original-bugs.md), [bugs.md](re/bugs.md) |
| **Settings** (`sim::Rules`, in the savegame): refund on demolition, ships per player (100, original 33), units per player (250, original 100), raw material guarantee, highest game speed (8, original 4), droughts, volcanoes, pirates, pirate nests per four players, trader prices, free trader's reach | [ADR 0008](architecture/0008-game-settings.md) |
| **Custom worlds** (world generator stage 1): 200–2000 fields per side, number of islands, mix of sizes, villages, volcanic islands, gold islands (automatic or a fixed number), starting areas; chunk `O1602MAP` | [worldgen.md](re/worldgen.md) |
| **Own save format `.o1602`**: full state (map, clocks, randomness, figures, fires, lava, diplomacy, exploration, ships, soldiers, AI) without limits, canonical, CRC-32, zlib; a saved game continues exactly. Exported `.GAM` files carry the same state in the chunk `O1602` | [o1602-save.md](formats/o1602-save.md), [o1602-chunk.md](formats/o1602-chunk.md) |
| **Missions checked the same on every machine** (S-105), only with one's own mission (S-106), bankruptcy only after 200 s in a row (S-107), best assignment of the town goals (S-108), island goals with islands fixed at the start (S-110), checked mission data (S-111); play can continue after victory | [missions.md](re/missions.md) |
| **Determinism**: fixed ticks, random streams per subsystem, AI and decorative figures the same on every machine | [ADR 0004](architecture/0004-deterministic-simulation.md) |
| **Banners**: any number of players in the model, tinted standard flag, units, coats of arms and message icons in every colour (the white art recoloured at runtime) | [ADR 0007](architecture/0007-extensibility-and-modding.md), [flags.md](modding/flags.md) |
| **No technical limits** of the original tables in the simulation (pools with stable IDs), no limit of 250 display figures | [bugs.md](re/bugs.md) (L-002, L-007) |
| **Computer opponents**: speed independent of the frame rate, wider quarry search, replanting fields instead of demolishing them, log with `O1602_AI_LOG` | [ai.md](re/ai.md) |
| **Fixes from the comparison with the running original (2026-10-22)**: the pirates make peace after every surrender, loot or not, so they no longer sink the computers' ships one by one; the computers of the tutorial stay idle (`TIMERS` +0xA5, kept in the savegame as `TUTR`); a computer without a shipyard plans one when it wants ships, so computers get more ships and warships as in the original | [ai.md](re/ai.md#tutorial), [pirates.md](re/pirates.md) |
| **Tutorial** as in the original: the five chapters with their pages, the 64 help bubbles that follow what the player does, the speech of each chapter; „Einführungsspiel beenden“ starts the next chapter; the bubbles move with the side panel and the window on any window size; diplomacy and settings point at the right mode in chapter 1 (U-038) | [tutorial.md](re/tutorial.md), [bugs.md](re/bugs.md) |
| **Fixes after feedback (2026-10-18)**: name entry in the menu (text input only worked in the game), name prefilled with „Namenlos“ ("Nameless") or the last one, the first key replaces it, Enter speaks „Wählt Euer Banner“ ("Choose your banner") (Speech8 107–115), Escape back to the mission choice; confirmation prompts and key overview respond only to clicks (press and release), not to hovering; sea creatures less frequent (`sea_creatures_every`, default 60 s); view frame of the minimap keeps its shape at the map edge; shadow of the build preview shows the selected building (previously the wooden wall); body text wraps in font pixels (mission window, briefing in the menu); status bar and mission window at full width without seams; event symbol in the colour of the competitor (new town, new ship); right-click first closes the drop-down of the build menu, then the building in hand | [flavor.md](re/flavor.md), [bugs.md](re/bugs.md#bugs-of-open1602-itself) |
| **Fixes after feedback, second list (2026-10-18)**: fertility of an explored island in the status bar over grass and trees ([GAME] 22); build mode darkens what cannot be built on, and when placing or clicking a building, everything outside its catchment area; forest in random variants; empty goods slots without a yellow frame; camera stays over the map; softer shadows in the build menu; right-click back to the player status (one click from any menu, build menu together with the building in hand); a farm being looked at selects its field in the build menu; when dragging fields and forest the area is hatched yellow as in the History Edition; savegames unlimited with arrows, Enter saves a new one immediately, a voice confirms the save | [ui.md](re/ui.md), [bugs.md](re/bugs.md#bugs-of-open1602-itself) |
| **Start dialog of the endless game**: after name and banner, the settings in groups (economy, limits, disasters, world, opponents: the computer players improved or as the original), each a button that cycles its values; presets for the original's rules and Open1602's defaults; the world items for a world of its own (size, fixed or automatic by the players as in multiplayer, 2 to 12 players, islands, villages, volcanic islands, starting areas, raw material guarantee); remembered in `settings.cfg`. The same dialog for **scenarios** (the rules only, the tutorial without it) and in **multiplayer**: the host chooses on the create screen and in the lobby until the start, everyone else sees the settings in the lobby, a savegame shows its own | [ADR 0008](architecture/0008-game-settings.md) |
| **Flag menu of the endless game**: the four hall banners of the original and eight coats of arms for the further colours beside the name board; over one, its name and the hall banner of its pattern repainted in it; a further colour swapped with the computer that had it; no more „Der weiße Spieler“ for a further colour's victory: one's own plays „Ihr konntet den Euch gestellten Auftrag erfüllen!“, a rival's shows „Der grüne Spieler konnte seinen Auftrag erfüllen!“ in the status bar, which now also shows texts of Open1602's own | [flags.md](modding/flags.md#the-flag-menu) |
| **Factions as data** (ADR 0007): who steers a slot (person, computer, nobody) is apart from its faction (settlers, free trader, pirates, natives); the world holds its factions and saves them with the game (section `FACT`), so every computer of a multiplayer game plays the host's; pirate nests, trader ships and the trader's reach grow with the players as the factions' numbers say, which `mods/<package>/factions.toml` changes for new games | [factions.md](modding/factions.md) |
| **Balance as data** (MD-9): the free trader's amounts and restocking, a castle's training step and landing range, how fires and plagues spread are values with the original's defaults that `mods/<package>/balance.toml` changes for new games; a game keeps them (section `BALA`) | [balance.md](modding/balance.md) |
| **Mods' own messages**: `mods/<package>/messages.toml` adds messages of the status bar and changes the original's, with text per language and placeholders, icon, own WAV sound or an original voice, an original video; `--message ID` shows one at the start | [messages.md](modding/messages.md) |
| **Build menu as data**: `mods/<package>/build_menu.toml` adds a package's buildings with icon and name, moves the original's to another category or row and takes them out; the ids are checked against `haeuser.cod`, rows and icons per row are bounded | [build-menu.md](modding/build-menu.md) |
| **Renaming ships and cities** (2026-10-23): a click on the name in the ship window (also the military mode's) or on the city's name in the warehouse window starts typing it (16 characters, as the original); Enter or a click elsewhere keeps it, Escape takes it back; the new name is a command, so every computer of a multiplayer game has it at the same tick, the savegame keeps it, and every window, list and message reads it from the one place it is kept | [ui.md](re/ui.md#name-input) |
| **Savegame lists** show what the section `META` says: the time played in the main menu's list, player, time and map size in the status bar over a slot of the settings | [o1602-save.md](formats/o1602-save.md) |
| **Goods balance „Warenbilanz“** (F2, or the line under „Punkte“ in the player status): per good what the player's cities, all or one island's, produced, imported, used and exported per minute over the last 1, 5, 15 or 60 game minutes, the balance and how long the stock lasts; drawn on a parchment in the original's style; a good's page with every flow, what each kind of building made, how many of them work or wait for raw material, the inhabitants' supply and a chart of the last hour or twelve hours. Tabs for the production buildings (utilisation, waiting for raw material, costs), the population (classes, satisfaction, taxes, supply, history), the trade at the Kontor (tons, gold, average prices, free trader and other players) and the finances (now, average and history). At the Kontor's stock the trend of every good in green or red (a setting of „Anzeige“); a click on it opens its page. An early warning in the voice of „Es fehlt an …“ before a good the inhabitants use runs out. Only one's own cities, also in multiplayer; saved in `GLDG` | [ADR 0012](architecture/0012-goods-statistics.md) |
| **Display and controls**: figures also at the smallest zoom level, zoom with the mouse wheel, pixel scaling, screenshot as PNG, own save folder (never into the installation); any window size, full screen (F11, Alt+Enter), the interface's size, free zoom (window "Anzeige" of the settings) | [README](../README.md#running), [ADR 0010](architecture/0010-controls-and-interface.md) |

### 2.2 Open or planned

| What | Dependency | Source |
|---|---|---|
| Flag menu with free RGB colour as in Anno 1503 | Interface | [ADR 0007](architecture/0007-extensibility-and-modding.md) |
| Factions of a package's own (with slots in the game and a behaviour of their own) | Lua (M8) | [factions.md](modding/factions.md) |
| Content packs with manifest and identifiers, free goods list, PNG and OGG import (also icons, sounds and videos of the mods' messages) | M8 | ADR 0007, [open-work.md](modding/open-work.md) |
| Scripts post the mods' messages (`MapViewer::announce`) | Lua (M8) | [messages.md](modding/messages.md) |
| Lua scripts, custom scenarios and campaigns | M8, scenarios | ADR 0007 |
| World generator stage 2 (custom island shapes, including ones larger than the templates) | deferred until everything runs | [00-plan.md](00-plan.md) |
| Replays from the session's frames | M6 | [ADR 0011](architecture/0011-multiplayer-lockstep.md) |
| Ideas: full refund shortly after building; own road pathfinding alongside that of the original; pirate distance of the difficulty level as a setting (S-068) | open, decision needed | – |

### 2.3 Decisions still pending

- ~~S-035~~ (demolishing a marketplace destroys houses): decided, kept; a question names the houses and
  other buildings that would lose their land before the demolition (`dialog.demolish_land`).

The decisions taken so far (own save format, U-013, key bindings, urgent goods and tower cost class)
are recorded in the ADRs and in [bugs.md](re/bugs.md).

## 3. Technology and quality

| Point | Status |
|---|---|
| Tests | 823 test cases on 2026-10-24 (counted with `grep -c '^TEST_CASE'`; ctest runs them as the tests `unit` and `data`): 748 in the 78 files of `tests/unit/`, which need no original data, and 75 in the 12 files of `tests/data/`, which skip without `ANNO1602_DIR` (one of them needs no data). Sea and land routes, world generator, new games, AI (founding, diplomacy, determinism), multiplayer and a fuzz test over the own savegame (`O1602_FUZZ_ITERATIONS` for longer runs) and the recorded bytes of every savegame section have their own. The SDL layer (`engine/platform`, `render_sdl`) has its own executable `o1602_sdl_tests` (36 cases), built with the game and run headless in the game's CI jobs (dummy drivers, software renderer). Without their own unit tests: the game executable (`engine/app` outside the menus); `o1602-tool` has ctest smoke tests on synthetic files, and CI starts the game without data (also under ASan/UBSan). A CI job measures the line and branch coverage of `engine/` by the tests without data (gcovr report as an artifact). How to run them: [tests/README.md](../tests/README.md) |
| Builds | presets `debug`, `release`, `asan`, `tsan`, `ci`, `coverage` and `vcpkg` (the game with `-DO1602_BUILD_GAME=ON`), locally under Linux without warnings; CI for Linux (GCC, Clang, ASan, TSan, coverage), Windows (MSVC 2026, Visual Studio 2026), macOS 14 and the game with SDL3 on all three. Windows and macOS have never run locally |
| Player commands | complete for the simulation (since 2026-10-18 also the town window and ship window); the selection windows trigger them; white flag, scuttling, patrol, command chains and group commands with offset have been added |
| Symbol table `re/symbols/` | 613 functions and 50 globals on 2026-10-24 (private, in `open1602-re`; evidence refers to the document). The code cites about 250 more addresses that only its comments explain; `re/scripts/check_symbol_refs.py` lists them |
| Local player | the first human in a single player game; in multiplayer the slot taken in the lobby |
| Comparison with the original | the original ran headless under Wine/Proton, its globals read live from memory ([original-comparison.md](original-comparison.md)); the driver for it (`tools/original/original_wine.py`) is not in the repository yet |
