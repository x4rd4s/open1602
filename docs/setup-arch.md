# Development environment on Arch Linux

## 1. Build tools (engine)

```sh
sudo pacman -Syu --needed base-devel git cmake ninja gcc clang gdb sdl3 python python-pillow
```

zlib is deliberately not installed explicitly: on systems with `zlib-ng-compat` (e.g. CachyOS,
or when Heroic brings it along) that package provides `zlib.h` and `libz.so` and conflicts with `zlib`.
CMake finds both variants. Check: `pacman -Qo /usr/include/zlib.h`.

Build and test:

```sh
cd open1602            # the clone of https://github.com/x4rd4s/open1602
cmake --preset debug
cmake --build --preset debug
ctest --preset debug

# Tests additionally against your own installation:
export ANNO1602_DIR="$HOME/Games/Heroic/Anno 1602"
ctest --preset debug
./build/debug/tools/o1602-tool/o1602-tool islands "$ANNO1602_DIR/NORD/big.scp"
```

More presets: `release`, `asan` (AddressSanitizer + UBSan), `ci` (warnings as errors).

### The game `open1602` (SDL3)

The program is only built with `-DO1602_BUILD_GAME=ON` and needs the `sdl3` package (see above):

```sh
cmake --preset release -DO1602_BUILD_GAME=ON
cmake --build --preset release
export ANNO1602_DIR="$HOME/Games/Heroic/Anno 1602"
./build/release/engine/app/open1602                                  # the main menu
./build/release/engine/app/open1602 --scenario "Szenes/Endlosspiel00.szs" --zoom 1
./build/release/engine/app/open1602 --savegame Merlin01           # name from the game (Game.dat)
./build/release/engine/app/open1602 --load ~/.local/share/Open1602/saves/quicksave.o1602  # own savegame
./build/release/tools/o1602-tool/o1602-tool savegames               # all savegames with names
```

Every option and the default keys: `open1602 --help` (any unknown option prints them). In the game, F1 shows the keys over the map;
they can be changed in `keys.cfg` in the user's data folder. The decisions behind the controls are in
[ADR 0010](architecture/0010-controls-and-interface.md). Note that Q and E rotate the view; Ctrl+Q quits.

The window title shows the game, the game clock, the speed or pause and, in a multiplayer game, the
number of players and the state of the session, in the player's language. With the environment
variable `O1602_DEBUG_TITLE` set, it also shows a developer readout: zoom and rotation, the tile under
the mouse with its island, city and owner, the selection, the frame rate and the number of sprites
drawn. With `--frames N --screenshot image.png` the program runs for N frames, saves the last one and
reports the frame rate (for tests without user input).

## 2. Reverse engineering tools

### Node.js + rea

rea needs Node.js 22.19+ / 24.11+ / 26+ (the Arch packages are recent enough).

```sh
sudo pacman -S --needed nodejs npm
# global npm packages without sudo:
npm config set prefix "$HOME/.local"
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc   # or ~/.zshrc
source ~/.bashrc

npm install --global rea-agents
rea --help
```

Run `rea setup` only **after** the Ghidra installation (next section), so that it
puts `GHIDRA_INSTALL_DIR` and the JDK into the MCP registration.

### Ghidra 12.1.x + JDK

rea requires **Ghidra 12.1.x** and the matching 64-bit JDK (according to the Ghidra release notes, currently JDK 21).

```sh
sudo pacman -S --needed jdk21-openjdk
sudo archlinux-java set java-21-openjdk

# Option A: Arch package (check that the version is 12.1.x!)
pacman -Si ghidra | grep '^Version'
sudo pacman -S ghidra                # installs to /opt/ghidra

# Option B: official release from GitHub (NationalSecurityAgency/ghidra), e.g. to ~/opt
#   unzip ghidra_12.1.x_PUBLIC_*.zip -d ~/opt

export GHIDRA_INSTALL_DIR=/opt/ghidra   # or ~/opt/ghidra_12.1.x_PUBLIC
echo "export GHIDRA_INSTALL_DIR=$GHIDRA_INSTALL_DIR" >> ~/.bashrc
```

Check the Ghidra version (must be 12.1.x): `grep application.version "$GHIDRA_INSTALL_DIR/Ghidra/application.properties"`.
The JDK must be complete (`javac` present) – `jdk21-openjdk`, not just `jre21-openjdk`.

### Setting up rea

**Quit Claude Desktop completely** beforehand (not just close the window), otherwise it overwrites the configuration.

```sh
source ~/.bashrc                  # GHIDRA_INSTALL_DIR must be set in this shell
npx rea-agents setup              # answer the question "Install Hopper?" with **No**
npx rea-agents doctor             # checks Ghidra version, analyzeHeadless, Java/javac
```

Hopper is not needed (we use Ghidra). Its Arch package also conflicts with `zlib-ng-compat`.

### Feasibility test

```sh
export ANNO1602_DIR="$HOME/Games/Heroic/Anno 1602"
re/scripts/rea-smoke-test.sh
```

The log ends up in `re/notes/rea-smoke-test-*.log`. It is not committed (`.gitignore`), because analysis
logs may contain decompiled code of the original ([ADR 0003](architecture/0003-reimplementation.md));
summarise the result by hand, as in
[re/notes/rea-smoke-test-result.md](../re/notes/rea-smoke-test-result.md). It decides whether rea is used directly or Ghidra
headless with the scripts from `re/scripts/ghidra/`.

### More tools

```sh
sudo pacman -S --needed imhex rizin binutils    # hex editor with pattern language, disassembler, objdump
```

## 3. The original under Wine (reference for parity tests)

```sh
sudo pacman -S --needed wine winetricks
# If pacman does not find 'wine': enable [multilib] in /etc/pacman.conf, then pacman -Syu
```

A separate prefix just for the analysis (the Heroic installation stays untouched):

```sh
export WINEPREFIX="$HOME/.wine-anno1602"
WINEARCH=win32 wineboot -i 2>/dev/null || wineboot -i
cd "$ANNO1602_DIR" && wine 1602.exe
```

Debugger: `winedbg --gdb 1602.exe` (GDB protocol, can be connected to e.g. `gdb` or the Ghidra debugger).
If DirectDraw acts up under Wine: put [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw) as `ddraw.dll`
into a *copy* of the game folder.
