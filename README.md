# Open1602

**Open1602** is an open-source engine for *Anno 1602* (1998, Max Design / Sunflowers). It plays the
complete original game, on modern Windows, macOS and Linux, with the data of your own copy of the game.

The original is fully implemented: the endless game and the scenarios with their missions, building,
economy and population, ships and trade, military, diplomacy, pirates, natives, disasters and computer
opponents, in the original's menus and side panel. Original savegames load and can be exported again.
On top of that Open1602 brings:

- **Fixes** for the original's bugs; where they change the game, the original's behaviour is a setting.
- **Game settings** to tailor each game: economy, limits, disasters, the world and its islands, the
  opponents and optional victory conditions, with presets for the original's rules and Open1602's
  defaults.
- **New balancing** that keeps long games and big worlds playable, with the original's values one
  click away.
- **Detailed statistics:** a goods balance with production, trade and consumption per good, city and
  island over time, and how long each stock lasts.
- **Multiplayer** for up to 12 players over the network, with a lobby, a search for games on the local
  network and host migration.
- **Modern display:** any window size, full screen, free zoom, a scalable interface, freely assignable
  keys and screenshots.
- **No more limits** of the original's save format (players, ships, soldiers, towers).
- **An editor** for maps and scenarios ([docs/editor.md](docs/editor.md)).
- **Mods:** packages with buildings, figures, goods, graphics, sounds, videos, Lua scripts, scenarios
  and campaigns of their own ([docs/modding/](docs/modding/README.md)).
- **Improved computer players** by default; the original's are a setting.

What is done in detail and what is still open: [docs/status.md](docs/status.md).

## Getting started

1. **Get the game data.** Open1602 contains no game files. You need your own *Anno 1602*, for example
   the GOG "Königsedition" or Ubisoft's *Anno 1602 History Edition*. Open1602 finds a GOG, Ubisoft,
   Heroic or Wine installation on its own and otherwise asks for the folder. The game speaks the
   language of your installation.
2. **Download Open1602** for Windows (64-bit), macOS (11 or newer) or Linux (x86-64 and ARM64) from
   the [releases page](https://github.com/x4rd4s/open1602/releases), or build it yourself (below).
   The macOS app is not notarised: start it the first time with a right click on `Open1602.app` > Open.
3. **Start it.** The game opens in the main menu. **F1** shows the keys.

Details: [docs/install.md](docs/install.md) (game data, also unpacking the GOG installer without
Windows, and building) and [docs/playing.md](docs/playing.md) (command line, data folder, log file,
multiplayer ports, `o1602-tool`).

## Building

You need CMake ≥ 3.25, Ninja, a 64-bit C++20 compiler, SDL 3 and zlib. On Linux and macOS:

```sh
git clone https://github.com/x4rd4s/open1602
cd open1602
cmake --preset release
cmake --build --preset release
ctest --preset release
```

The game is then in `bin/`. On Windows use the `vcpkg` preset from the x64 Native Tools Command
Prompt. Packages per distribution, Windows and the other presets: [docs/install.md](docs/install.md#building).

## Project layout

| Folder | Contents |
|---|---|
| `engine/` | Engine modules (`assets`, `sim`, `net`, `render`, `audio`, `ui`, `script`, `editor`, `app`), each with a README. The game-independent modules come from [annokit](https://github.com/x4rd4s/annokit) ([ADR 0005](docs/architecture/0005-module-structure.md)) |
| `tools/` | `o1602-tool`, which inspects and converts the original's files |
| `tests/` | Unit tests and tests against the original data ([how to run them](tests/README.md)) |
| `docs/` | [Plan](docs/00-plan.md), [status](docs/status.md), [file formats](docs/formats/README.md), [architecture decisions](docs/architecture/README.md), [modding](docs/modding/README.md) |
| `data/` | Files the engine brings itself: interface texts, the application icon, the Linux desktop entry |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). The most important rules: no original files and no code of the
original program in the repository; behaviour is described as a specification and implemented from it.

## License

GPL-3.0-or-later, see [LICENSE](LICENSE). *Anno* is a trademark of Ubisoft; this project is not
affiliated with Ubisoft, Max Design or Sunflowers. The application icon (`data/icons/`) is CC0 1.0.
