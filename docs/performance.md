# Performance and system requirements

What a computer needs to run Open1602, and how to measure it. The numbers below come from one
measurement on 2026-10-22 and are an estimate, not a guarantee.

## The budget

A tick is 50 ms of game time (`Simulation::kTickMs`), and the game runs at most at speed 8
(`Rules::max_game_speed`), so at full speed 160 ticks fall into a second: **a tick has
1000 / (20 × 8) = 6.25 ms**, everything included. The frame budget (16.7 ms at 60 fps) holds the
drawing and the ticks of that frame together. Means say little about it: a tick that takes 40 ms
once makes the game stutter however fast the others are. So the minimum machine below is judged by
the 99th percentile and the share of ticks over 6.25 ms (`o1602-tool bench` prints both), measured
with `--repeat` so that other programs do not count (see the end of this page).

### Targets and the default rules

What the engine is meant to carry, and what a game allows by default, are two different sets of
numbers:

| | Target of the engine | Default rules (`sim::Rules`) | The original |
|---|---|---|---|
| Players | 12 (`sim::kMaxPlayers`) | 4 to 12 in an endless game | 4 (7 slots) |
| World | up to 4096 × 4096 tiles loaded (`sim::kMaxWorldSide`), up to 2000 × 2000 generated (`sim::WorldSettings::kMaxSide`, also the most `--world` takes), as many islands as fit (at most 250 placed) | 500 × 350 for four players, growing with the players | 500 × 350 |
| Soldiers | 100 000 in a world | 250 a player (`max_army`), 3 000 for twelve | 100 a player |
| Ships | 20 000 in a world | 100 a player (`max_ships_per_player`), 1 200 for twelve | 33 a player |

The targets are what the simulation, the saves and the network have to stay correct and within the
budget at; the review of 2026-10 measured them (single ticks of 0.6 to 13 s with 1 000 to 20 000
ships, 34 to 40 ms per tick with 50 000 to 100 000 soldiers on a fast desktop) and the work since
has brought the costs down, but they are not reached in the budget yet. The default rules stay well
below them, so that a normal game of eight or twelve players keeps its ticks within budget on the
minimum machine; a game that raises `max_army` or `max_ships_per_player` (0 = unlimited, ADR 0008)
toward the targets needs a faster machine.

## Estimated minimum: 8 players in a late game, also in multiplayer

| | Minimum | Why |
|---|---|---|
| CPU | 64-bit, 2 cores; single-core speed about an Intel Core 4th generation (Haswell) or AMD Ryzen 1000 at 3 GHz | The simulation runs on one core (ADR 0004: deterministic, one thread). In the dense world below 99 % of the ticks took at most 2.5 to 3.3 ms on the measuring machine, about half the budget of 6.25 ms, and the cities of eight human players add about 0.25 ms on average (below); a computer of this class is about as fast per core as the measuring machine. Single peaks of 6 to 10 ms from sea routes and the computers' planning remain |
| Memory | 4 GB | The game holds about 1.4 GB, mostly the sprite atlases of the three zoom levels; the simulation itself 15 to 25 MB |
| Graphics | Any GPU with OpenGL, Direct3D 11 or Metal, textures of 2048 px, 1 GB video memory (integrated graphics such as Intel HD 4600 or AMD Vega are enough) | 40 atlas pages of 2048 x 2048 RGBA, about 650 MB |
| Disk | About 1 GB | The original game (650 MB, 450 MB of it videos); Open1602 a few MB; a savegame 0.2 to 0.6 MB |
| Network | Any broadband connection; latency matters more than bandwidth | Lockstep: only commands travel, and a savegame when a computer has to get the game anew |

In a multiplayer game every computer runs the whole world, so the slowest computer sets the pace.
A multiplayer game of more than four players makes its world larger (707 x 495 for eight, about
37 islands), between the two world sizes measured below.

## Measurements

Measuring machine: a cloud VM with an Intel Xeon at 2.1 GHz and 4 cores, no GPU, Linux, the
`release` preset (GCC 13). A tick is 50 ms of game time: 20 ticks per second at speed 1, 160 at
speed 8.

### Simulation (`o1602-tool bench`)

New own worlds (level 1, seed 7), every player handed to the computer:

| World | Players | Game hours | Mean tick | p99 | Max | Houses | Inhabitants | Peak memory |
|---|---|---|---|---|---|---|---|---|
| 500 x 350 | 4 | 10 | 0.24-0.54 ms | 3.5-7.1 ms | 20.7 ms | 122 | 1 162 | 14 MB |
| 500 x 350 | 8 | 10 | 0.22-0.42 ms | 2.7-4.6 ms | 38 ms (once) | 290 | 2 809 | 15 MB |
| 800 x 600 | 8 | 10 | 0.48-0.65 ms | 4.3-6.7 ms | 35 ms (once) | 166 | 1 379 | 24 MB |

The computer players do not build a real late game: after ten hours the eight of them have about
290 houses together. To stand in for one, their cities were filled densely by a throwaway program
(a grid of roads, houses, markets, workshops and public buildings, the goods topped up every game
minute): about 1 150 houses, 510 production buildings and 5 200 to 6 600 inhabitants. Over an
hour its ticks took 0.30 to 0.39 ms on average, 2.5 to 3.3 ms at p99, mostly 6 to 9 ms at most and
60 ms once. Shares of the tick:

| Step | Share | Mean per tick | Max |
|---|---|---|---|
| computers | 63 % | 243 us | 3.3 ms |
| ships | 23 % | 87 us | 8.6 ms |
| combat | 5 % | 20 us | 0.8 ms |
| production | 5 % | 18 us | 2.3 ms |
| logistics | 2 % | 8 us | 0.1 ms |
| houses | 0.5 % | 2 us | 0.2 ms |

Houses, production and logistics together cost about 30 us per tick for 1 000 houses. Eight human
players with 500 to 1 000 houses each would add 0.15 to 0.25 ms, which puts a late game at about
0.6 to 1 ms per tick on this machine, with peaks around 10 ms from sea routes and the computers'
planning.

The multiplayer fingerprint (`Simulation::state_hash()`, every `SessionConfig::hash_interval`
frames, 100 by default) writes a savegame in memory on the main thread. In these synthetic worlds
it took 11 to 13 ms (500 x 350) and 28 ms (800 x 600). On a real 12-player late game in a
2000 x 2000 world it took 8 to 9 ms and a savegame for a joining computer about 0.11 s on a fast
desktop (Ryzen 9 7900X3D, 2026-10, the review's device run), which is about two to three times as
fast per core as the minimum machine: there a fingerprint costs most of a tick's budget every five
seconds.

Since TB-26 the fingerprint no longer writes the tiles: each island's tiles are hashed in blocks of
16 x 16, and a block is hashed again only after one of its tiles changed (every change of a tile
gives its block a new revision, `IslandMap::block_revision`). The rest of the game is still written
as before, but without the tiles that is a small part of a large game. On a synthetic 2000 x 2000
world with 100 islands of 180 x 180 tiles in the cloud (Clang, `-O2`): the first fingerprint
29 ms, one without changes 0.1 ms, one after 1000 tiles changed on random islands 6 ms, against
86 ms for writing that game unpacked as a whole. `o1602-tool bench` prints the first fingerprint and
the next one 100 ticks later, as a session takes them.

A game for a computer that joins or resyncs is written in two steps (`sim::encode_game` and
`sim::pack_game`): the host writes the sections on the game thread, and zlib, most of the cost
(for that synthetic world 86 ms of 504 ms), runs on a worker while the game goes on. The computer
gets the game with the frames the host ran meanwhile; after `SessionConfig::pack_wait_ms` (5 s in
the game) the host waits for it.

### Drawing

The game with the dense world, SDL's software renderer (no GPU), vsync off, speed 1:

| Zoom | 1920 x 1080 | 2560 x 1440 |
|---|---|---|
| 0 (near) | 110 fps, 2 700 sprites | 74 fps, 4 200 sprites |
| 1 | 93 fps, 8 200 sprites | 57 fps, 12 800 sprites |
| 2 (far) | 60 fps, 23 600 sprites | 46 fps, 34 900 sprites |

The free zoom (2026-10-22, Merlin01, 1920 x 1080, release build, the same software renderer): at a
whole scale nothing changes (magnification 1: 58 fps, 2: 27 fps, the cost grows with the pixels
drawn). Between whole scales the map goes through a texture (ADR 0010); without a GPU it is never
larger than the window, so the scale below is drawn and stretched: 1.19 and 1.41 run at 58 fps, 2.83
at 18 fps. With a GPU the texture may have twice the window's pixels (sharper); that was not measured
on a GPU yet (`O1602_MAP_CANVAS` changes the limit for such a measurement).

The process held 1.35 GB; with the software renderer the textures are in main memory. Preparing
the sprites took 2.2 s (GFX), 0.6 s (MGFX) and 0.16 s (SGFX).

### Limits of these numbers

- No real late game with eight players existed; the dense world is a quarter to an eighth of one,
  the rest is extrapolated linearly.
- No GPU was measured; the graphics requirement follows from the size of the atlases.
- Only Linux was measured, and no multiplayer game over the internet.
- The p99 and maximum of the tables above come from single runs with a wall clock, before
  `--repeat` and the thread CPU clock existed; they include the time other programs had the core
  and are an upper bound, not the cost of the code.

## Measuring on your computer

```sh
o1602-tool bench                                    # new own world, 8 players (707 x 495), 10 game hours
o1602-tool bench --players 8 --world 500x350 --minutes 120 --period 10
o1602-tool bench SAVEGAME/game00.gam --minutes 60   # a savegame of the original or of Open1602
o1602-tool bench --minutes 600 --out late.o1602     # keep the end state, e.g. for the game's --load
o1602-tool bench --minutes 60 --repeat 5            # the fastest of five runs per tick, for comparing code
o1602-tool bench --players 4 --minutes 240 --ai classic --per-player  # the original's computers, each one's economy
```

`--ai classic` or `--ai open1602` picks the computers of a new world (the rule "Computer opponents";
Open1602's by default, a savegame keeps its own). `--per-player` adds to each period, per player, its cities, inhabitants, gold, units and
castles, its taxes, running costs, sales and purchases per minute, and per class its inhabitants,
satisfaction and tax rate: the way to compare the economies of the two kinds of computers.

Tick times are the CPU time of the game thread where the system has a precise per-thread clock
(Linux, macOS); elsewhere, and with `--wall`, they are wall-clock times. A single run's p99 and
maximum still include cache misses and frequency changes caused by other programs, and with a
wall clock they include the time other programs had the core: on a busy machine the same
deterministic run gave a founding-phase maximum of 12-45 ms against 2.9 ms. The game runs the
same ticks every time, so `--repeat N` runs it N times and keeps the fastest time of each tick:
use it, not a single run, when two versions of the code are compared. `bench` also prints the
share of ticks over the 6.25 ms a tick has at speed 8 and the load average.

Without `--world` the world is as large as a multiplayer game of that many players makes it
(`sim::settings_for_players`: the original's 500 x 350 for four, both sides growing with the square
root of the players, so 707 x 495 for eight); the islands follow the area. `bench` hands every
player to the computer, runs the game as fast as it goes and prints per period
(`--period`, game minutes) the mean, median, 99th percentile and maximum tick in microseconds with
the size of the world. At the end it shows each step of a tick (`Simulation::tick_timed()`), the
time of the multiplayer fingerprint and of a savegame, and the peak memory of the process. The
simulation does not draw, so the game itself needs more: run it with `--load late.o1602 --frames 600`
to see its frame rate.
