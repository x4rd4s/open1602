# Installing and building

How to give Open1602 the original game data and how to build it from source. Ready-built packages
are on the [releases page](https://github.com/x4rd4s/open1602/releases); how to start and play the
game is in [playing.md](playing.md).

The release packages hold only the engine. They are built by
[.github/workflows/release.yml](../.github/workflows/release.yml), which publishes a release for every
pushed tag `vX.Y.Z` that matches the version in `CMakeLists.txt`; its notes start with what changed,
written by hand in [releases/](releases) as `vX.Y.Z.md`. Linux packages need glibc 2.39 or newer.

## Original game data

Open1602 contains **no** game files. You need your own copy of *Anno 1602*, for example the GOG
"Königsedition" (*Anno 1602 – Creation of a New World*) or Ubisoft's *Anno 1602 History Edition*.
Both work completely, the History Edition's Bink 2 videos included. The game speaks the language of your
installation: a German Anno 1602 gives a German game, an English one an English game
([details](modding/languages.md)).

Open1602 finds a GOG or Ubisoft installation on its own: on Windows through the folders GOG's
installer and Ubisoft Connect record and in `C:\GOG Games`, `GOG Galaxy\Games` and
`Ubisoft\Ubisoft Game Launcher\games` under Program Files, on Linux and macOS in `~/GOG Games`,
`~/Games/Heroic`, `~/Games`, the default Wine prefix and the prefixes of Heroic, Bottles and
CrossOver (a folder whose name contains "1602"). The game files are only read, so the installation can stay under Program Files. If it
finds none, Open1602 asks for the folder of the installation with the system's folder dialog and
remembers it (`game_folder` in `settings.cfg` in the user's data folder); it asks again only when
that folder is moved or deleted. On Linux the dialog needs the desktop portal or `zenity`.

Open1602 looks in this order: `--game-dir DIR` on the command line, the environment variable
`ANNO1602_DIR`, the folder chosen in the dialog, the usual folders above, and only then asks. A
folder given with `--game-dir` or `ANNO1602_DIR` is never replaced: if it is wrong, Open1602 says so
and ends. To set the variable:

| Shell | Command |
|---|---|
| bash / zsh | `export ANNO1602_DIR="$HOME/Games/Heroic/Anno 1602"` |
| fish | `set -gx ANNO1602_DIR "$HOME/Games/Heroic/Anno 1602"` |
| PowerShell | `$env:ANNO1602_DIR = "C:\GOG Games\Anno 1602"` |
| cmd | `set ANNO1602_DIR=C:\GOG Games\Anno 1602` |

To keep it set, add the line to `~/.bashrc` / `~/.zshrc`, use `set -Ux` once in fish, or `setx ANNO1602_DIR "..."`
on Windows. Quotes around the path are ignored, so `set ANNO1602_DIR="C:\GOG Games\Anno 1602"` works too.

### GOG installer without Windows (macOS, Linux)

GOG ships the game as a Windows installer (`setup_anno_1602_*.exe`, sometimes with `.bin` files next to
it). Open1602 does not need it to run: [innoextract](https://constexpr.org/innoextract/) unpacks it
without Windows, Wine, a virtual machine or CrossOver. On macOS with [Homebrew](https://brew.sh):

```sh
brew install innoextract unar     # Linux: the distribution's innoextract and unar packages
mkdir -p /tmp/anno1602 ~/"GOG Games"
innoextract --gog -d /tmp/anno1602 ~/Downloads/setup_anno_1602_*.exe
mv /tmp/anno1602/app ~/"GOG Games/Anno 1602"
```

Keep any `.bin` files in the same folder as the `.exe`; `--gog` unpacks them too (that part needs
`unar`). The game files end up in the `app` folder of the output; the folder to use is the one that
holds `haeuser.cod` and `gfx/`. In `~/GOG Games/Anno 1602` Open1602 finds it on its own; anywhere else,
pick that folder in the dialog or set `ANNO1602_DIR`. The game folder can also simply be copied from an
existing installation on Windows or Linux (GOG, Heroic or Wine).

## Building

Get the source:

```sh
git clone https://github.com/x4rd4s/open1602
cd open1602
```

Requirements on every platform: CMake ≥ 3.25, Ninja, a 64-bit C++20 compiler and, for the game
itself, **SDL 3** and **zlib** (savegames and screenshots are compressed with it). The libraries,
tests and tools also build without zlib, but then cannot read or write compressed savegames. CI
builds with GCC 13, Clang 18, AppleClang 15 and MSVC from Visual Studio 2026; older compilers may
work but are not tested. 32-bit platforms are not supported.
The first configure downloads [annokit](https://github.com/x4rd4s/annokit), the engine's shared
library, at the commit `CMakeLists.txt` names (git and an internet connection are needed once; for
a build without network pass `-DFETCHCONTENT_SOURCE_DIR_ANNOKIT=<annokit clone>`).

The `release` and `vcpkg` presets build the game; every other preset builds only the libraries, the
tests and `o1602-tool` unless you add `-DO1602_BUILD_GAME=ON`. Configure says at the end whether the
game is built, and stops with the packages to install when SDL 3 or zlib is missing.
The presets are in [CMakePresets.json](../CMakePresets.json): `release` for playing, `debug`, `asan`
(AddressSanitizer + UBSan) and `tsan` (ThreadSanitizer) for development, `ci` (warnings as errors) as in CI,
`coverage` (GCC, line and branch coverage with gcovr as in CI), `vcpkg` for Windows.

### Linux

Install the compiler, CMake, Ninja, SDL 3 and zlib, for example:

```sh
sudo pacman -S --needed base-devel cmake ninja sdl3                 # Arch, CachyOS, Manjaro (zlib is already there)
sudo dnf install gcc-c++ cmake ninja-build SDL3-devel zlib-devel     # Fedora 41+
sudo apt install build-essential cmake ninja-build libsdl3-dev zlib1g-dev  # Debian 13, Ubuntu 25.04+
```

Ubuntu 24.04 has no SDL 3 package; build it from source as CI does
(see [.github/workflows/ci.yml](../.github/workflows/ci.yml)) and pass `-DCMAKE_PREFIX_PATH=<sdl3 install dir>`.
To build only the libraries, tests and `o1602-tool` without SDL 3, add `-DO1602_BUILD_GAME=OFF`.

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
```

### macOS

With [Homebrew](https://brew.sh) (Apple Silicon and Intel):

```sh
xcode-select --install
brew install cmake ninja sdl3      # zlib comes with macOS
cmake --preset release
cmake --build --preset release
ctest --preset release
```

### Windows

Install Visual Studio 2026 (or its Build Tools) with the workload "Desktop development with C++"; it
brings CMake and Ninja. SDL 3 and zlib come from [vcpkg](https://vcpkg.io), which the `vcpkg` preset
uses. Open1602 is 64-bit only, so open the **x64 Native Tools Command Prompt for VS 2026** from the
start menu (not the "Developer Command Prompt", which targets 32-bit x86, and not a plain PowerShell
or terminal, which has no compiler). In it:

```bat
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
set VCPKG_ROOT=C:\vcpkg

cmake --preset vcpkg
cmake --build --preset vcpkg
ctest --preset vcpkg
```

`set VCPKG_ROOT=...` works in the Command Prompt only and lasts until it is closed, so repeat it in
every new prompt. In PowerShell, for example a Developer PowerShell started with `-Arch amd64`, write
`$env:VCPKG_ROOT = "C:\vcpkg"` instead; there `set` silently does nothing for CMake, which then fails
with "Could not find toolchain file: /scripts/buildsystems/vcpkg.cmake". Visual Studio's prompts may
already point VCPKG_ROOT at the vcpkg bundled with it; the line above switches to the clone, which is
what CI tests. `bootstrap-vcpkg.bat` has no 32- or 64-bit variant: it always downloads the 64-bit `vcpkg.exe`
(the ARM64 one on ARM). Which architecture the packages and the game are built for comes from the
prompt: vcpkg picks the 64-bit triplet `x64-windows` from the x64 prompt by itself; in a 32-bit
prompt the configure step stops with a hint. After a failed first configure, delete `build\vcpkg` and
run `cmake --preset vcpkg` again.

The `vcpkg` preset builds the game and copies `SDL3.dll` next to it; `vcpkg.json` pins the versions
with its `builtin-baseline`. Started by a double click, the game shows fatal errors in a message box
and writes its other messages to `open1602.log` in the data folder (see [Running](playing.md#running)); started
from a console, it writes them there.
