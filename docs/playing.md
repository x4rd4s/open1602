# Playing

How to start Open1602, where it keeps its files, what a multiplayer host needs and what
`o1602-tool` does. Installing and building: [install.md](install.md).

## Running

The `release` and `vcpkg` presets put the game and `o1602-tool` into the folder `bin/` at the top of
the clone, on every platform:

| Platform | Game | Start by double click |
|---|---|---|
| Linux | `./bin/open1602` | from the menu after `cmake --install` (below) |
| macOS | `./bin/Open1602.app/Contents/MacOS/Open1602` or `open bin/Open1602.app` | `Open1602.app` in Finder |
| Windows | `bin\open1602.exe` (with `SDL3.dll` beside it) | `open1602.exe` in Explorer |

The program carries the application icon on Windows (`open1602.exe`) and macOS (`Open1602.app`). On
Linux, `cmake --install build/release --prefix ~/.local` installs the game into `~/.local/bin` with its
desktop entry and icon, so it appears in the desktop's application menu (the entry starts `open1602`,
so `~/.local/bin` must be on the `PATH`, as it is on most distributions). The other presets (`debug`,
`asan`, `tsan`, `ci`, `coverage`) keep their programs in `build/<preset>/bin/`, so a development build
never replaces the one for playing.

Without arguments the game starts in the main menu. Some more ways to start:

```sh
open1602 --intro                                   # the logos and the intro first, as the original
open1602 --scenario "Szenes/Das Atoll.szs"         # a scenario of the installation
open1602 --new-game 1 --name Merlin                # a new endless game, difficulty 0 (easy) .. 3 (hard)
open1602 --savegame NAME                           # a savegame of the original by its name
open1602 --load path/to/game.o1602                 # a savegame of Open1602
```

An option the game does not know, for example `--help`, prints every option and the default keys
(and exits with code 2).

In the game **F1** shows the keys (they can be changed in `keys.cfg`). W A S D, Q, E and the group
digits work by their place on the keyboard, so they lie the same on QWERTY, QWERTZ and AZERTY; the
other letters go by what is printed on the key (B builds). **F12** saves a screenshot in
`screenshots/` of the data folder. Savegames, settings and key settings are kept in the user's data
folder, never in the installation:

| Platform | Data folder |
|---|---|
| Linux | `~/.local/share/Open1602/` |
| macOS | `~/Library/Application Support/Open1602/` |
| Windows | `%APPDATA%\Open1602\` |

Scenarios of your own, for example maps shared by other players, go into `scenarios/` of the data
folder (or, as before, into `Szenes` of the installation); the main menu lists them under "Own
scenarios" ([scenarios.md](modding/scenarios.md#own-scenarios)).

Started from a desktop launcher, the Finder or by a double click, the game writes its messages to
`open1602.log` in the data folder; the log of the run before is kept as `open1602.previous.log`, so
it survives starting the game again after a crash. A crash writes what happened (and on Linux and
macOS a backtrace) as the log's last lines; a log that ends without the line `Open1602 ended, exit
code …` belongs to a run that crashed or was killed. Started from a terminal, the messages appear
there. Please attach the log to a bug report.

A multiplayer host takes players on UDP port 16020 (or the one given with `--port`) and answers the
local network's game search on UDP port 16021. A firewall on the host must let both in, otherwise
its game does not appear in the lobby of the other computers and joining by address fails too. With
ufw on Linux:

```sh
sudo ufw allow 16020:16021/udp comment 'Open1602'
# or only from the home network:
sudo ufw allow from 192.168.178.0/24 to any port 16020:16021 proto udp comment 'Open1602'
```

A computer that only joins needs no rule to join, but its own firewall may drop the hosts' answers to
the game search. It also needs the rule to take over the game when the host leaves (host migration).
For players on the internet the host's router must forward port 16020.

Tests that need the original data run when `ANNO1602_DIR` is set and skip themselves otherwise.
How to run the tests, filter them and use their environment variables: [tests/README.md](../tests/README.md).

## Tools

`o1602-tool` (in `bin/` beside the game, or `build/<preset>/bin/` for the development presets) inspects
and converts the original's files:

```sh
o1602-tool chunks     <file>                         # chunk structure of any binary file
o1602-tool cod        haeuser.cod haeuser.txt        # decrypt a .cod script
o1602-tool bsh-export GFX/SHIP.BSH out/ --sheet      # sprites as PNG
o1602-tool islands    NORD/big.scp                   # island data
o1602-tool cod-dump   haeuser.cod --type HAUS        # interpret a .cod script
o1602-tool render-island NORD/lar01.SCP island.png   # draw an island isometrically
o1602-tool render-world "Szenes/Das Atoll.szs" w.png # draw a whole world map
o1602-tool savegames                                 # the savegames of the installation with their names
o1602-tool bsh-info   GFX/SHIP.BSH                   # list the sprites of a sheet
o1602-tool smk        VideoSmk/58.smk --png out/     # decode a Smacker video, frames as PNG
o1602-tool state      SAVEGAME/game00.gam            # players, cities, warehouses, ships of a game
o1602-tool new-world  world.gam --world 900x600      # an own world as a savegame for the original
o1602-tool export     game.o1602 game.gam            # export a game for the original
o1602-tool bench      --players 8 --minutes 600      # run a game headless and measure it (docs/performance.md)
```

Without arguments `o1602-tool` lists every command with its options.
