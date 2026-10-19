# Anno 1602 – Open-Source Engine: Project Plan

Status: 2026-10-19 · Basis: GOG installation „Anno 1602 – Königsedition“ ("Anno 1602 – King's Edition") (de-DE)

## 1. Goal

A standalone, cross-platform engine that runs with the **original data** of a legally acquired copy
(approach as in OpenRCT2, OpenTTD, Julius/Augustus, OpenMW). No original assets are distributed.
The engine should be playable faithfully to the original, fix known bugs of the original and provide a clean basis for
new features, QoL and mods.

### Requirements for extensibility and modding

Set on 2026-10-16, binding for all new code. The architecture for this is in
[ADR 0007](architecture/0007-extensibility-and-modding.md).

- New **buildings, colours, production chains, islands, landscapes, factions, animations,
  graphics, music and sounds** can be added.
- Custom **campaigns and scenarios** can be written.
- New **island types and island sizes** are possible, including huge islands.
- The **island generator** is more flexible and more varied than in the original.
- The **number of players** is not fixed at 4 (+ traders, pirates, natives). For
  example, five people can play in multiplayer, and neutral parties such as pirates and travelling
  traders scale with it.
- **Colours and flags** are extensible, with a flag selection menu as in Anno 1503.
- The **endless game** can be configured in more detail than in the original, as in later Anno titles
  (for example the refund on demolition: none, partial or full). The settings
  live in `sim::Rules` ([ADR 0008](architecture/0008-game-settings.md)).

## 2. Inventory (first analysis of the game folder)

| File / folder | Finding | Significance |
|---|---|---|
| `1602.exe` (632 KB) | PE32, Intel 386, MSVC 6 (linker 6.0), **not packed**, no copy protection | Ideal for static analysis |
| `.text` 0x94E70 (~595 KB code) | estimated 2,000–3,000 functions | manageable scope |
| `.data` ends at 0x49D000, `.rsrc` only at 0x72D000 | **~2.9 MB uninitialised globals (BSS)** | Game state lives largely in global arrays → memory layout easy to reconstruct |
| Imports `DDRAW`, `Maxsound.dll` (→ DSOUND), `Maxnet.dll` (→ DPLAYX), `smackw32.dll`, `Language.dll` | clear subsystem boundaries | Entry points for graphics, sound, network, video, texts |
| `haeuser.cod`, `figuren.cod`, `Text.cod`, `Editor.cod` | Plain-text scripts, **every byte negated** (`b = -b & 0xFF`) | Buildings, units, costs, texts are **data-driven** – huge advantage |
| `GADDATA/*.GAD`, `Gadedit/*.GAD` | Plain-text UI definitions (gadgets) | UI layouts can be taken over directly |
| `GFX/`, `MGFX/`, `SGFX/` `*.BSH` | Sprite containers (`BSH\0` header), 3 zoom levels | Graphics |
| `TOOLGFX/*.ZEI`, `*.COL` | Fonts (`ZEI\0`), palette | Fonts, 8-bit palette |
| `NORD/`, `SUED/`, `*NAT/`, `NOKLIMA/` `*.SCP` (677 files) | Island templates, chunk-based | Map generation |
| `Szenes/*.SZS`, `*.SZM` (138) | Scenarios / multiplayer maps | Same chunk format as savegames (`.GAM`) |
| `VideoSmk/*.SMK` (51) | Smacker videos | own decoder based on the public description (`engine/video`, [smk.md](formats/smk.md)) |
| `SAMPLES/`, `Speech8/`, `MUSIC8/` | WAV | usable directly |
| `1602Edit.exe` + `Editor.dll` | Map editor | later, same code base |

Existing community work (format knowledge, reference – check licences before reuse):
`stwe/MDCII` (C++, Anno 1602 world rebuild, BSH/SCP parser), `stwe/Benno4j` (Java),
a Python grammar/parser generator for the `.cod` scripts.

## 3. Reverse-engineering methodology

### 3.1 Principles
- **Document, don't copy**: Findings from the binary are recorded as a specification (formats, structures,
  algorithms, formulas). The engine is **rewritten** on the basis of this documentation – no
  copy-paste of decompiled code. This keeps the code readable and legally cleaner.
- **Symbols are source code**: Function/variable names, structs and comments are regularly exported from the
  analysis database and versioned (`re/symbols/`). rea works with an *ephemeral*
  Ghidra database – without an export, annotations are lost.
- **Every claim with evidence**: address, decompiled excerpt, test case or memory dump.

### 3.2 Toolchain
| Purpose | Tool |
|---|---|
| Agent-assisted analysis | **rea** (`rea-agents`, MCP + CLI) with Ghidra backend |
| Disassembler/decompiler | **Ghidra 12.1.x** + JDK 21 (headless + GUI) |
| Run the original as reference | **Wine** (32-bit prefix) on Linux, possibly dgVoodoo/cnc-ddraw for DirectDraw |
| Runtime observation | `winedbg`/x32dbg under Wine, memory dumps of the BSS region, savegame diffs |
| Format analysis | Python 3 (+ `construct`/Kaitai Struct), ImHex with pattern files |
| Quick static checks | objdump, rizin (optional) |

**Risk**: The rea documentation does not explicitly mention 32-bit PE (Ghidra itself supports it fully).
The first technical step is therefore a feasibility test "rea opens `1602.exe`". Fallback: Ghidra headless with
our own scripts, rea for the remaining targets.

### 3.3 Procedure
1. **Assets first** (quick wins, independent of the binary): `.cod` decoder, BSH+COL → PNG, ZEI fonts,
   SCP/SZS/GAM chunks, GAD parser. Result: asset viewer and static map renderer.
2. **Function inventory** of `1602.exe`: record all functions, assign them to subsystems via imports, strings
   (`gaddata\*.gad`, `%sSavegame\%s.gam`, …) and cross-references.
3. **Reconstruct global data structures**: map/island fields, building, figure, ship, player and
   storage tables. Compare with savegame chunks and runtime dumps under Wine.
4. **Understand the simulation**, subsystem by subsystem: game loop & tick, building, production chains,
   population/needs/taxes, trade & routes, ships/pathfinding, AI, military, pirates, events,
   mission goals.
5. **Parity tests**: load savegame → N ticks in the original (Wine) and in the new engine → compare state.
6. **Bug catalogue** (`docs/re/bugs.md`): document bugs of the original with their cause; fix in the engine,
   switchable via an "original behaviour" option, so that parity stays measurable.

## 4. Technology proposal

| Area | Choice | Rationale |
|---|---|---|
| Engine language | **C++20** | The original is MSVC 6 C++ → structures/classes map directly; proven in comparable projects; maximum performance; large ecosystem |
| Build | **CMake** + presets, **vcpkg** (manifest) | Linux/Windows/macOS from one build |
| Platform | **SDL3** (window, input, audio, timer) | one abstraction for all systems |
| Rendering | SDL3 GPU API (Vulkan/Metal/D3D12) with 2D sprite batching; software fallback | high resolutions, zoom, scaling |
| Audio | SDL3 audio + own software mixer, own Smacker decoder for videos | |
| Network | Deterministic **lockstep** over UDP (**ENet** or GameNetworkingSockets) | replaces DirectPlay; low bandwidth |
| Modding (later) | Lua 5.4 (sol2) + own data format (TOML/JSON) as an overlay to the `.cod` files | Extensibility without recompiling |
| Debug UI | Dear ImGui | Inspectors for the sim state |
| Tests | Catch2 / doctest, golden-file and parity tests | |
| RE/asset tools | **Python 3** | fast prototyping of format parsers, Ghidra scripts |
| CI | GitHub Actions (Linux, Windows, macOS), sanitizer builds (ASan/UBSan) | Stability |

This table is the proposal from the start of the project. Where the project went another way: the
tests use a small framework of their own (`tests/test_framework.hpp`) with unit tests and tests
against the original data instead of Catch2 or doctest; there is no Dear ImGui; the game draws with
the SDL renderer (`engine/render/sdl`) and the tools with a software rasteriser; the network is ENet.
Lua is not started yet.

Alternative **Rust**: stronger memory safety, but a weaker mapping of the original C++ structures and
a smaller game-development ecosystem. The recommendation remains C++20 with strict rules (RAII, no raw
owning pointers, sanitizers in CI).

### Architecture principles (performance, stability, multiplayer)
- **Simulation strictly separated** from rendering/platform: `sim/` knows neither SDL nor graphics.
- **Deterministic**: fixed tick, integer/fixed-point arithmetic only, own seedable RNG, no
  dependence on the iteration order of unordered containers.
- **Command-based**: player inputs become serialised commands → at the same time the basis for
  multiplayer lockstep, replays, undo in the editor and headless tests.
- **Desync detection**: a hash of the state compared between the computers in multiplayer.
- **Memory**: pools/arenas for entities, no allocations in the tick hot path → stable in long sessions.
- **Data-driven**: buildings/goods/units from `.cod` data, not hard-coded.
- **No fixed limits of the original in the engine model** (ADR 0007): number of players, banners (colours and
  flags), world size, island size and number of islands are open. The values of the original apply only when
  loading and saving the original formats.

## 5. Project structure

The folders and what they hold are listed in the README under
[Project layout](../README.md#project-layout); each engine module has a README of its own
(for example [engine/sim](../engine/sim/README.md)). The module rules are in
[ADR 0005](architecture/0005-module-structure.md). The analysis of the original (`docs/re/`,
`re/symbols/`) lives in the private companion repository `open1602-re`
([CONTRIBUTING.md](../CONTRIBUTING.md#reverse-engineering)).

Original data is **not** copied into the repository; the engine gets the path to the installation
from `ANNO1602_DIR` or `--game-dir`.

## 6. Milestones

| # | Milestone | Result |
|---|---|---|
| M0 | Setup | Repo, toolchain, rea+Ghidra feasibility test, CI skeleton |
| M1 | Assets | All formats documented, `tools/` + `libanno`, asset viewer |
| M2 | Map renderer | Load a scenario/savegame and display it statically in isometric view (3 zoom levels) |
| M3 | Core simulation | Building, production, population, storage, taxes – parity tests against the original |
| M4 | Playable endless game | Ships, trade, figures, UI, saving/loading (compatible with `.GAM`) |
| M5 | Complete game | Military, pirates, world generator stage 1, AI, events, scenarios/mission goals (already in the extensible model: factions and scaling with the number of players, ADR 0007) |
| M6 | Multiplayer | Lockstep, lobby with any number of players and flag choice, desync detection, reconnect |
| M7 | Improvements | Bug fixes, arbitrary resolutions, free zoom, hotkeys, UI scaling, editor |
| M8 | Content and modding | Content packs with manifest, content registry with identifiers, PNG/OGG import, Lua scripts, campaigns, custom island shapes (world generator stage 2, ADR 0007) |

**Status of the milestones (2026-10-19)**, details in [status.md](status.md): M0 ✅ · M1 🟡 (open:
parts of the island header and scenario chunks; fonts, layouts, sound and videos done) · M2 ✅ ·
M3 ✅ · M4 ✅ (simulation, ships, trade, savegames, sound, the side panel with the selection windows,
menus and settings) · M5 🟡 (military, pirates, world generator stage 1, AI, missions with victory and
defeat, points, end screen, high score lists and campaign progress done; open: AI switch and diplomacy
stance of the scenarios, volcano events of the scenarios) · M6 🟡 (lockstep, desync detection with
resync, reconnecting, lobby, LAN search, Open1602's own multiplayer menu, 4 to 12 players; open: banner
choice in the lobby, changing the host) · M7 🟡 (freely assignable keys, mouse-wheel zoom, pixel
scaling, screenshot and bug fixes done; open: arbitrary resolutions, free zoom, UI scaling, editor) ·
M8 ⬜ (banners with tinted flags already in place).

## 7. Order of work

**Basic decisions (2026-10-16):** GPL-3.0-or-later · C++20 · reimplementation instead of decompiled
code · project name *Open1602* (namespace `o1602`) · details in [docs/architecture](architecture/README.md).

**Agreed order (2026-10-17):** first complete the game content, then multiplayer, then the
extensions. Every new system stays deterministic and command-based, so that multiplayer, replays and
headless tests build on it without rework. World generator stage 2 (custom island shapes, including
ones larger than the templates) is deferred until everything else is done and running.

Being able to load original formats and continue standard scenarios in the original is desirable, but not
a requirement: extended features of Open1602 do not have to be compatible with the original.

What is done and what is open is kept only in [status.md](status.md); the history of the work is in
the git history.
