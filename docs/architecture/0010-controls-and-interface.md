# 0010 – Controls and interface

Status: accepted · 2026-10-18 (key bindings confirmed as the default by the project owner)

## Context
The interface of the original has been rebuilt: side panel, build menu, the selection windows. Its
controls date from 1998: the left button gives commands, the right one cancels
(report on selection, to be moved into [ui.md](../re/ui.md)); many functions have no key, some keys no
function; sinking ships and declaring war happen without confirmation; some things the simulation could do
are not offered by the interface (exploring at the shipyard and the pirate warehouse, U-013).

The project owner wants to rework the interface so that new menus, settings and
information fit in and the controls follow today's real-time strategy games (2026-10-18).

## Decision
1. **The look stays, the controls become modern.** The graphics and layouts of the original are the
   basis; what the original hides or does not offer, the Open1602 interface may offer
   (example: exploring at the pirate warehouse and at the shipyard as a button, U-013). New windows and buttons
   are added as data (ADR 0007).
2. **Right-click gives commands** (default): move, attack, dock, trade depending on the target. The
   left-click controls of the original are a setting. The left button selects (click, box,
   Shift adds); Escape cancels and goes back one page. Dragging with the right or
   middle button still scrolls the map.
3. **Commands via the minimap:** a right-click on the minimap sends the selection there.
4. **Command chains:** Shift + right-click appends a command to the unit's queue
   (waypoints). The queue belongs to the simulation (command with "append", deterministic,
   saved); a trade route stays separate from it.
5. **Freely assignable keys** in the settings menu. The default follows today's strategy games
   (below); keys may be bound twice in different contexts (building, units) if
   these never apply at the same time. The original's key bindings are available as a second preset.
6. **Confirmations** before consequences that cannot be undone: sinking a ship, disbanding a unit
   and declaring war through an attack. The confirmation can be switched off in the settings.
7. **Videos of messages play in the minimap window**, and the game keeps running; a click on the
   video ends it. Intro, credits, victory and defeat stay full-screen.

## Key bindings (default)

**Camera and game**

| Key | Function |
|---|---|
| W A S D, arrow keys | scroll the map (Shift: faster) |
| Q / E | rotate view |
| Mouse wheel, Page Up / Page Down | zoom |
| Space | jump to the last message |
| Home | centre view on the selection |
| F3, Pause | pause |
| Num+ / Num− | game speed |
| F5 / F9 | quicksave / quickload |
| F10, Escape (when there is nothing to cancel) | game menu |
| F12, Print Screen | screenshot |
| F1 | help for the window under the mouse |

**Windows**

| Key | Function |
|---|---|
| B | build menu |
| K | military mode |
| I | status/info |
| O | options |
| L / Shift+L | ship list / city list |
| V | diplomacy (treaties; German „Verträge“) |

**Building**

| Key | Function |
|---|---|
| R | rotate building |
| hold Shift | keep building after placing |
| C | pipette: the building under the mouse as the building order |
| Delete | demolish tool |
| Escape | put the tool away |
| Right-click | put the tool away and return to the player status |

**Selection**

| Key | Function |
|---|---|
| Left-click / drag | select / box |
| Shift + left-click | add to or remove from the selection |
| Double-click, Ctrl + left-click | all visible units of this type |
| Ctrl+1…0 | set group |
| Shift+1…0 | add to group |
| 1…0 | select group (twice: jump there) |
| . (full stop) | next idle trading ship |
| , (comma) | next idle combat unit |
| Tab | cycle through the types in the selection (ships, infantry, cavalry …) |

**Unit commands**

| Key | Function |
|---|---|
| Right-click (also on the minimap) | command depending on the target |
| Shift + right-click | append command |
| F + click | attack-move (attacks on the way; German „Feuer“, "fire") |
| H | stop (German „Halt“) |
| G | hold position (does not pursue) |
| P + click | patrol |
| C | change formation (line, column, block) |
| Shift+C | spread out |
| Y | raise / lower the white flag |
| X | explore island |
| T | trade / transfer at the warehouse |
| R | edit trade route (ship selected) |
| Delete | disband unit / sink ship (with confirmation) |

Attack-move, hold position and jumping to idle units do not exist in the original.

No unit key is also a general key (2026-10-18: attack moved from A to F, stop from
S to H, and in exchange hold position on G, formation on C, spread out on Shift+C), so W A S D always
scroll. Only keys in contexts that never apply at the same time are bound twice: R and Delete (building:
rotate, demolish; units: route, disband), C (building: pipette; units: formation) and Tab.

Keys that matter by their place are bound by scancode (2026-10-19, review point 25): W A S D, Q and E
and the digits of the groups lie where they are on a US keyboard on every layout (AZERTY: Z Q S D, A
and E; the number row without Shift). Keys that stand for a word stay bound by keycode, where the
layout prints them (B building, K military, F attack). In `keys.cfg` a key by its place is written in
brackets, `general scroll_up = [W]`; where a press matches both kinds, the one by place wins.

Done (2026-10-18): table `ui::KeyBindings` with this default and the settings file
`keys.cfg` in the user's data folder (to be edited by hand until the settings menu exists; F1 shows the
bindings), box selection, pixel-exact like the original, groups with the original's number key,
formations, filtering and Tab (types of the selection one after another; `ui::UnitSelection`), right-click and
minimap commands, command chains in the simulation, the confirmations, the military mode windows, the
message videos in the minimap window, attack-move (ships and soldiers attack enemies in
range, pursue them and then continue; in the savegame `STNC`), hold position (they
only shoot at enemies in range, do not pursue), quicksave and quickload (F5, F9) and the
explore button next to repair and pirates in the ship window (U-013), ship and city list (L,
Shift+L), diplomacy window with tribute (V; confirmation before breaking a peace treaty), the
settings of the side panel and the main menu for single player (scenarios, endless game, savegames),
the end screen after victory or defeat as in the original (ENDE.GAD; after a victory Escape goes
on with the game, which the original ends, with a hint over the buttons). Not yet: our own game menu (Escape) with save, load, settings and quit; the original's key
bindings as a second preset; F1 as help for the window under the mouse (today F1 shows the keys). The original
has no game menu: save, load and quit are in the settings of the side panel (done
with a confirmation before „Ende“ ("Quit")).

## Consequences
- Input runs through an action → key table (settings file), not through fixed keys in the
  code. The game's default keys are entries of that table (done, `keys.cfg`).
- The simulation needs a command queue per ship and unit as well as group commands with
  an offset per unit.
- Deviations from the original in the controls are not bugs in the sense of ADR 0006, but
  interface settings; the simulation is not affected by them.
