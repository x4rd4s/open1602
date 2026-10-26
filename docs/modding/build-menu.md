# Build menu

The build menu's eight categories and the buildings in their pop-ups are a table in the original's
code. In Open1602 they are data ([ADR 0007](../architecture/0007-extensibility-and-modding.md)):
the original's lists are the default, and a package's `build_menu.toml` adds buildings, moves them
to another category or row, and takes them out of the menu.

**Status:** Done are the file `build_menu.toml`, buildings of a package with their own icon and
name, moving and removing the original's buildings. Still open, with milestone M8: categories of a
package's own (they need buttons in `BAU.GAD`) and icons of a package's own (PNG comes with the
import of content; until then `icon` picks a sprite of the original's `TOOLS` sheet).

## Where a mod's changes are

```
<user data folder>/mods/<package>/build_menu.toml
```

The user data folder and the package's name are as for [messages](messages.md#where-a-mods-messages-are).
Packages are read in their [load order](packages.md#load-order); a later one can change what an earlier one set.
Problems are written to the log (standard error) with the file and the line.

## The file

`build_menu.toml` is the same part of [TOML](https://toml.io) as `messages.toml`. Each table puts
one building into the menu, or takes it out; the table's name is free and only shows in messages:

```toml
# A lighthouse of this package in a third row of the harbour buildings.
[lighthouse]
id = 22201            # the HAUS record of haeuser.cod
category = 31007      # the category's button in BAU.GAD
row = 3               # 1-based; one past the last row starts a new one
icon = 90             # the icon: a sprite of TOOLS, counted from GFXBAUHAUS
name = "Leuchtturm"   # the building's name in the menu, the windows and the status line

# The church goes to the harbour buildings too, first in that row; icon and name stay its own.
[church]
id = 20805
category = 31007
row = 3
position = 1

# No sheep farm in this game.
[sheep]
id = 21515
remove = true
```

| Field | Value | Default |
|---|---|---|
| `id` | the Id of a HAUS record of `haeuser.cod` | needed |
| `category` | `31001` military, `31002` roads, `31003` mines, `31005` craftsmen, `31006` farms, `31007` harbour, `31008` public buildings (`31004` is the demolish tool) | needed, unless `remove` |
| `row` | 1 to 6, at most one past the category's last row | the last row while it has room, else a new one |
| `position` | 1 to one past the buildings of the row | after the last |
| `icon` | sprite of `TOOLS` from `GFXBAUHAUS` | needed for a building new to the menu, else its own |
| `name` | text (UTF-8; what the game's fonts cannot show is left out) | needed for a building new to the menu, else its own |
| `remove` | `true` takes the building out of the menu | `false` |

A row holds four buildings and a pop-up six rows. A building listed already is moved, not listed
twice; a row left empty goes. A table with a wrong value, an `id` that `haeuser.cod` does not have
or no room in its row is passed over with a warning, and the rest of the file is used; an unknown
field is passed over alone. A file that is no valid data file is passed over as a whole.

The menu only offers a building: whether a player may build it (its `Bauinfra`, the costs) is the
record's in `haeuser.cod`. Computers do not build a package's new production chains yet: they
find their producers by ware and raw material, so a building that makes a known ware from a known
raw material is used, but nothing tells them when they need a new chain. A format for that is to
be added later (MD-4, [status.md](../status.md) section 2.2).
