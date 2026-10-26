# Packages

A mod is a package: a folder in the `mods` folder of Open1602's user data folder. This page says
how a package describes itself, in which order packages are loaded and which packages a game uses
([ADR 0007](../architecture/0007-extensibility-and-modding.md), sections 2 and 7).

**Status:** Done are the manifest `mod.toml` with name, version, description, dependencies and
load order, the hash of a package's files, the list of packages every game keeps in its savegame,
multiplayer games that let in only computers with the game's packages, a host that sends its
packages to a computer that lacks them and agrees, and the main menu's screen "Mods". Still open,
with milestone M8: packages as archives and adding content of a package's own
([open-work.md](open-work.md)).

## The folder

```
<user data folder>/mods/<package>/
    mod.toml           the manifest (optional)
    messages.toml      messages of the status bar (messages.md)
    build_menu.toml    the build menu (build-menu.md)
    factions.toml      how the neutral factions grow with the players (factions.md)
    balance.toml       numbers of the game (balance.md)
```

The folder's name is the package's id, for example `lighthouse`. The ids of a package's entries
start with it (`lighthouse:beacon`), and other packages refer to it by it. It has at most 64 bytes.

## The manifest

`mod.toml` is the same part of [TOML](https://toml.io) as the other files of a package, without
tables:

```toml
name = "Leuchtturm"              # shown in lists; the id if there is none
version = "1.2.0"                # one to four numbers with dots
description = "A lighthouse for every coast."
depends.harbour = "1.0"          # needs the package harbour, version 1.0 or later
depends.coast = ""               # needs the package coast, any version
after.weather = true             # loads after weather if that is installed, without needing it
```

| Field | Meaning |
|---|---|
| `name` | the package's name for people |
| `version` | `"1"`, `"1.2"`, `"0.10.3"`: compared number by number, `1.10` comes after `1.9`, `1.2` equals `1.2.0` |
| `description` | a sentence about the package |
| `depends.<id>` | another package this one needs, with the lowest version it works with (`""`: any) |
| `after.<id>` | another package whose changes this one builds on, when it is there |

A package without a manifest is loaded as before: no version, no dependencies. What is wrong in a
field is passed over with a warning in the log that names the file and the line; a file that is no
data file at all keeps the package out.

## Load order

Packages are loaded one after the other, and a later one can change what an earlier one set. A
package comes after the ones it depends on and the ones it names with `after`; otherwise the order
is that of the ids' bytes (`Alpha` before `beta` before `zeta`), the same on every system.

A package is not loaded, with a line in the log, when

- it is turned off in the screen "Mods" (below),
- a package it depends on is not installed, has no version or an older one than it needs,
- a package it depends on is not loaded itself,
- its dependencies and `after` entries go round in a circle.

## Which packages a game uses

A new game, and a game from a scenario or savegame of the original, uses the packages installed
when it starts, in their load order. The game keeps their ids, versions and a hash of their files
in its savegame (section `PACK` of [o1602-save.md](../formats/o1602-save.md)). From then on the list,
not the `mods` folder, decides:

- **Loading a savegame** loads the messages and build menu of the packages it names, and no others.
  A package that is missing, or installed with other files, is named in the status line and the
  log; the game still loads, since its factions and balance are saved with it. A savegame of a
  version before the manifest names no packages and uses all installed ones.
- **Multiplayer:** a computer joins only with every package of the game, with the same files. The
  host refuses one that lacks a package or has another version of it, and names the packages in
  its message. Packages a computer has in addition are not loaded for the game, so more mods never
  keep a player out. The host can send the missing packages, see below.

The hash covers every file of the package's folder and its subfolders, by its path, size and
bytes. Two copies of a package compare equal only when all their files do.

## Packages a host sends

A computer that lacks packages of a multiplayer game can get them from the host. The host offers
them when it has every missing package with the game's files and they come to at most 256 MiB
together (`SessionConfig::max_share_bytes`); only a computer that gave the right password learns
of them. The player is asked first, with the names and the size: "This game needs mods this
computer does not have: lighthouse 1.2 (2.4 MB). Download them from the host?" Without a yes,
nothing is sent and the game is not joined.

With a yes, the computer joins again and asks for the packages (`Hello::fetch_packages`). The host
sends their files (`PackageFiles`, at most 16 MiB of files a message, so a single file of a
package may not be larger), then takes the computer in as usual. The computer writes each package
into `<user data folder>/mods-cache/<id>-<hash>/` and keeps it only when its files have the hash
the game names; a path that would leave that folder refuses the whole package. Packages received
once are there for the next game that needs them.

Received packages are used only by the games that name them, never by a new game of the player's
own: the `mods` folder stays as the player left it. A computer that hosts the game on after its
host left (host migration) sends the received packages on like its own.

## The screen "Mods"

The main menu's button "Mods" lists the packages of the `mods` folder with name, version and
whether new games use them, and below them the packages received from hosts.

- **On and off:** the box before a package turns it on or off for new games. A package turned off
  is not loaded, nor are those that depend on it; `settings.cfg` keeps the ids
  (`disabled_mod = lighthouse`). A savegame keeps the packages it was started with.
- **Received packages** have no box: the games that need them use them. "Add to my mods" copies
  one into the `mods` folder, where it is a package of the player's own from then on, turned on.
  This is refused while the `mods` folder has a package of that id.
- A package that cannot be loaded (a broken manifest, a missing dependency) says so; the log says
  why.
