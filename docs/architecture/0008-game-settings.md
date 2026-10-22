# 0008 – Game settings

Status: accepted · 2026-10-17

## Context
Later titles of the Anno series let you configure the endless game far more precisely than Anno 1602:
starting capital, opponents, disasters, refunds on demolition and much more. Open1602 should be able to do
this as well. Until now, `sim::Rules` only had switches for deviations from the original
(ADR 0006).

## Decision
1. **All settings of a game live in `sim::Rules`**, whether they switch off a quirk of the
   original or offer a new choice. There is no second place for them.
2. Every setting has an **original value**, which `Rules::original()` returns, and a
   default value for Open1602. Where it is a free choice and not a bug of the original,
   the default is the behaviour of the original.
3. Settings belong to the **savegame** and to the **multiplayer session**. They are chosen before the
   game starts; if they change during a game, then only via a command
   (ADR 0004), so that all machines compute the same thing.
4. The simulation reads settings only from `Rules`. The interface, the game start dialog and the
   command line fill in `Rules` and do not know the rules themselves.
5. New settings are documented in the code at the place that evaluates them, and in the
   behaviour description (`docs/re/*.md`) in the section "Open1602 and the original".

## First settings
| Setting | Values | Original | Default |
|---|---|---|---|
| Refund on demolition (`demolish_refund`, `refund_percent`) | none, partial (1–100 %, preset 50 %), full | none | none |
| Ships per player (`max_ships_per_player`, L-001) | number, 0 = unlimited | 33 | 100 |
| Units per player (`max_army`, L-008) | number, 0 = unlimited | 100 | 250 |
| Guarantee raw materials (`guarantee_resources`, S-019) | yes/no | no | yes |
| Highest game speed (`max_game_speed`, L-004) | factor | 4 | 8 |
| Droughts (`droughts`) | as the game (difficulty), off, rare, normal, frequent | as the game | as the game |
| Volcanoes (`volcanoes`) | as the game (setting), off, on | as the game | as the game |
| Pirates (`pirates`) | as the game (setting bit 0; the endless games of the two easy levels have none), off, on | as the game | as the game |
| Free trader prices (`trader_prices`, S-077) | as usual (about half the base price), by the market (as the original intends) | as usual | as usual |
| Computer players (`classic_ai`, S-097 to S-099) | improved, as the original (with its bugs in settling, counting and site search, so they stall after about two hours) | as the original | improved |
| Victory conditions of the endless game (`victory`, `VictoryConditions`) | inhabitants, residents of a level or higher, treasury, balance per minute, share of settled islands, defeat all opponents, score, prestige buildings (castle, cathedral, both), time limit (then the most points win); all or any one; computers can win or not | none (the endless game never ends) | none |
| Custom world (`WorldSettings`, new game) | size 200–2000 per side or automatic by the players (as multiplayer: 500 × 350 up to four, both sides growing with the square root of the players beyond), 2–12 players, number of islands, island sizes, villages, volcano islands, gold islands (automatic by level, islands and players, or a fixed 1–40 for very large maps; settings.cfg up to 250), starting areas separate/shared | the three fixed maps | off; on: automatic size (500 × 350 for 4 players), 4 players, normal, mixed, automatic gold, separate |

What two of the choices mean for the player (shown in the start dialog as "Free trader's prices" and
"Every raw material"; in German „Preise des freien Händlers“ and „Jeder Rohstoff“):

- **Free trader's prices** (`trader_prices`): what the free trader pays for the player's goods. "As usual" is about half
  the base price, as players know the original. The original means him to follow the market, but it
  gathers the statistics for that in a way that depends on the frame rate, so they are almost always
  empty (S-077, [ships.md](../re/ships.md)).
  "By the market" does what it intends: he pays up to three quarters of the average price at which
  the Kontors buy the ware, and stops buying a ware once all traders together hold half again as much
  of it as the Kontors want.
- **Every raw material** (`guarantee_resources`): a randomly generated world has at least one deposit
  of every raw material a building needs: every crop, iron and gold, gold if need be instead of a
  second iron vein. The original can generate endless games without any gold (S-019, in 31 % of the
  512 worlds of `Endlosspiel00`;
  [worldgen.md](../re/worldgen.md)).

## Consequences
The Open1602 savegame has its own section for the settings (`RULE` in the chunk
`O1602`, done). Savegames of the original get the original's values for the choice points; this
is satisfied because the default of every free choice is the original. The exception is
`classic_ai` (decided 2026-10-22): the original's computer players are a choice for those who want
its difficulty, but their bugs stay fixed by default, also in savegames of the original. The endless game has a
start dialog after name and banner (`engine/app/src/game_settings_menu.cpp`, since 2026-10-20): the
settings in groups (economy, limits, disasters, world, opponents), as in later Anno titles, each a button that
cycles its values, with presets for the original's rules and Open1602's defaults; the last choice is
kept in `settings.cfg` (`endless.*`, `endless.world = auto` for the automatic size). The command line (`--refund`, `--droughts`, `--volcanoes`, `--pirates`, `--ai`)
still sets them for games started from it.

### Victory conditions
Since 2026-10-22 an endless game can be won, as in Anno 1503, 1701 and 1404: the start dialog has a
group "Victory" below the disasters, whose button opens a page of its own with the conditions in
four groups (population, wealth, power, end of the game). All are off by default and in both
presets; the presets change the rules and leave the conditions as chosen, "All off" on the page
switches them off. Every value is a plain number in `settings.cfg` (`endless.victory.*`), so any
value works there, the dialog offers a few.

`sim::check_victory()` (`engine/sim/src/victory.cpp`) checks every human and computer that is
still playing every 5 s of game time in `Simulation::players_tick()`, after its mission, from the
synchronised state only (integers, no floating point), so every computer of a multiplayer game
finds the same winner. The first to meet the conditions (all at once, or any one) wins as with a
mission (`GameEvent::MissionAccomplished`, then `GameOver`): alone with message 0x54 "you have
fulfilled your task", with other humans 0x6B + the winner's colour, as the original's
multiplayer races; whoever meets them at the same moment wins too. With a time limit, when the
game time is up the player with the most points (`sim::score_sheet()`) wins, on equal points
the first slot; nobody else. The end screen and the high score list are the missions'. The
conditions are `Rules::victory` and so part of `RULE` (keys 11 to 22, written only when one is
on, so games without them keep their bytes). Scenarios have their own missions and get none; the
mission window shows the conditions after the endless game's text.

Since 2026-10-22 the same dialog serves every new game; what is played (`SetupScope`) decides
what it offers:

| Game | Offers | Kept in `settings.cfg` |
|---|---|---|
| Endless game alone | everything | `endless.*` |
| Scenario (after the mission screen; not the tutorial) | the rules only: economy, limits, disasters, opponents. Its map and deposits are the author's, so no world items and no raw material guarantee | `scenario.*` |
| Multiplayer endless game | everything; the players are those of the lobby, more than four always play on a world of their own, which starts together by default as the original's multiplayer maps | `multiplayer.*` |
| Multiplayer scenario | as a scenario alone | `multiplayer.*` |
| Multiplayer savegame | nothing to change: the lobby shows the rules the game was started with | — |

The victory conditions are offered for both kinds of endless game and shown for a savegame; a
scenario never gets them.

In multiplayer the host chooses on the create screen and, until the start, in the lobby. The
lobby passes the choice on in `Lobby::setup`, the program's own bytes the session does not read, as
`settings.cfg` lines after a line `game = multiplayer|scenario|savegame`; so the protocol stays as
it is. Everyone else opens the dialog from the lobby to look only. The rules themselves reach
every computer with the game, as the savegame's `RULE` section (ADR 0011, point 6).
