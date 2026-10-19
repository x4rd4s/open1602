# Open1602

**Open1602** is an open-source reimplementation of the engine of *Anno 1602* (1998, Max Design / Sunflowers).
The goal is a faithful game on modern systems (Linux, Windows, macOS), with the original's bugs fixed,
stable multiplayer and room for new features, quality-of-life improvements and mods.

> **Status (October 2026):** The simulation of the original is largely complete (construction, economy,
> population, disasters, ships and trade, military, diplomacy, pirates, computer opponents, new endless
> game, savegames in the original format). The game starts in the original's main menu and is played with
> its side panel; multiplayer runs in lockstep over the network with a lobby. The editor, parts of the
> interface and the modding foundations are still missing. What is done, open and unclear:
> [docs/status.md](docs/status.md).

## Original game data

Open1602 contains **no** game files. You need your own copy of *Anno 1602*, for example the GOG
"Königsedition" (*Anno 1602 – Creation of a New World*). The game speaks the language of your
installation: a German Anno 1602 gives a German game, an English one an English game
([details](docs/modding/languages.md)). Tell Open1602 where the installation is with the environment
variable `ANNO1602_DIR` (or `--game-dir DIR` on the command line):

| Shell | Command |
|---|---|
| bash / zsh | `export ANNO1602_DIR="$HOME/Games/Heroic/Anno 1602"` |
| fish | `set -gx ANNO1602_DIR "$HOME/Games/Heroic/Anno 1602"` |
| PowerShell | `$env:ANNO1602_DIR = "C:\GOG Games\Anno 1602"` |
| cmd | `set ANNO1602_DIR=C:\GOG Games\Anno 1602` |

To keep it set, add the line to `~/.bashrc` / `~/.zshrc`, use `set -Ux` once in fish, or `setx ANNO1602_DIR "..."`
on Windows. On Linux and macOS the GOG installer can be unpacked without Windows:
`innoextract setup_anno_1602*.exe` (or install it through Heroic or Wine).

## Building

Get the source:

```sh
git clone https://github.com/x4rd4s/open1602
cd open1602
```

Requirements on every platform: CMake ≥ 3.25, Ninja, a 64-bit C++20 compiler and, for the game
itself, **SDL 3** and **zlib** (savegames and screenshots are compressed with it). The libraries,
tests and tools also build without zlib, but then cannot read or write compressed savegames. CI
builds with GCC 13, Clang 18, AppleClang 15 and MSVC 2022; older compilers may work but are not
tested. 32-bit platforms are not supported.

Without `-DO1602_BUILD_GAME=ON` only the libraries, the tests and `o1602-tool` are built, not the game.
The presets are in [CMakePresets.json](CMakePresets.json): `release` for playing, `debug` and `asan`
(AddressSanitizer + UBSan) for development, `ci` (warnings as errors) as in CI, `vcpkg` for Windows.

### Linux

Install the tools and SDL 3, for example:

```sh
sudo pacman -S --needed base-devel cmake ninja sdl3                 # Arch, CachyOS, Manjaro (zlib is already there)
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel zlib-devel     # Fedora 41+
sudo apt install build-essential cmake ninja-build libsdl3-dev zlib1g-dev  # Debian 13, Ubuntu 25.04+
```

Ubuntu 24.04 has no SDL 3 package; build it from source as CI does
(see [.github/workflows/ci.yml](.github/workflows/ci.yml)) and pass `-DCMAKE_PREFIX_PATH=<sdl3 install dir>`.

```sh
cmake --preset release -DO1602_BUILD_GAME=ON
cmake --build --preset release
ctest --preset release
```

### macOS

With [Homebrew](https://brew.sh) (Apple Silicon and Intel):

```sh
xcode-select --install
brew install cmake ninja sdl3      # zlib comes with macOS
cmake --preset release -DO1602_BUILD_GAME=ON
cmake --build --preset release
ctest --preset release
```

### Windows

Install Visual Studio 2022 (or its Build Tools) with the workload "Desktop development with C++"; it
brings CMake and Ninja. SDL 3 and zlib come from [vcpkg](https://vcpkg.io), which the `vcpkg` preset
uses. In an **x64 Native Tools Command Prompt for VS 2022** (or the matching Developer PowerShell):

```bat
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
set VCPKG_ROOT=C:\vcpkg

cmake --preset vcpkg
cmake --build --preset vcpkg
ctest --preset vcpkg
```

The `vcpkg` preset builds the game and copies `SDL3.dll` next to it; `vcpkg.json` pins the versions
with its `builtin-baseline`. Started by a double click, the game shows fatal errors in a message box
and writes its other messages to `open1602.log` in the data folder; started from a console, it writes
them there.

## Running

| Platform | Game |
|---|---|
| Linux, macOS | `./build/release/engine/app/open1602` |
| Windows | `build\vcpkg\engine\app\open1602.exe` |

Without arguments the game starts in the main menu. Some more ways to start:

```sh
open1602 --intro                                   # the logos and the intro first, as the original
open1602 --scenario "Szenes/Das Atoll.szs"         # a scenario of the installation
open1602 --new-game 1 --name Merlin                # a new endless game, difficulty 0 (easy) .. 3 (hard)
open1602 --savegame NAME                           # a savegame of the original by its name
open1602 --load path/to/game.o1602                 # a savegame of Open1602
open1602 --help                                    # lists every option and the default keys
```

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

Tests that need the original data run when `ANNO1602_DIR` is set and skip themselves otherwise.
How to run the tests, filter them and use their environment variables: [tests/README.md](tests/README.md).

## Tools

`o1602-tool` (in `build/<preset>/tools/o1602-tool/`) inspects and converts the original's files:

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
```

Without arguments `o1602-tool` lists every command with its options.

## Project layout

| Folder | Contents |
|---|---|
| `engine/` | Engine modules: `core`, `assets`, `sim`, `net`, `render`, `platform`, `audio`, `video`, `ui`, `app`; each has a README |
| `tools/` | `o1602-tool` (C++) and Python prototypes |
| `tests/` | Unit tests and tests against the original data ([how to run them](tests/README.md)) |
| `docs/` | [Plan](docs/00-plan.md), [status](docs/status.md), [file formats](docs/formats/README.md), [architecture decisions](docs/architecture/README.md), [languages](docs/modding/languages.md) and [data limits](docs/modding/data-limits.md) for modding |
| `re/` | Analysis scripts and notes |

The analysis of the original game (`docs/re/`) and the symbol tables (`re/symbols/`) are kept in the
private companion repository **open1602-re**, which holds the reverse-engineering notes. They are not
part of a public clone, so links into `docs/re/` only work for maintainers who have it set up
([CONTRIBUTING.md](CONTRIBUTING.md#reverse-engineering)).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The most important rules: no original files and no decompiled
code in the repository; every finding about the original is documented with its evidence.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). *Anno* is a trademark of Ubisoft; this project is not
affiliated with Ubisoft, Max Design or Sunflowers.
