# Scenarios

A content package brings scenarios of its own: a map, its names and briefing in every language, its
difficulty, the money at the start, the goals of the people's mission and a script of its own
([ADR 0007](../architecture/0007-extensibility-and-modding.md) section 6, item 7 of
[open-work.md](open-work.md)).

**Status:** Part 1 done on 2026-10-31: scenarios of packages in the scenario list of the main
menu, with goals, money and a script. Still open: campaigns (a sequence of scenarios with values
carried from one to the next) and factions of a package's own in a scenario.

## The file

```
<user data folder>/mods/<package>/scenarios.toml
```

One table per scenario; its name holds lower case letters, digits and `_` and makes the scenario's
id `<package>:<name>`:

```toml
[north]
file = "maps/north.szs"
script = "north.lua"
stars = 3
money = 25000
name.en = "The North"
name.de = "Der Norden"
briefing.en = "Settle the cold islands of the north. Build a castle and keep 50 t of tools."
briefing.de = "Besiedelt die kalten Inseln des Nordens. Baut ein Schloss und lagert 50 t Werkzeug."
goal.inhabitants = 2000
goal.castle = true
goal.goods.tools = 50
```

| Key | Value |
|---|---|
| `file` | the map, a file of the package: a scenario of the original's format (`.szs`, `.szm`) or a game saved by Open1602 (`.o1602`). Required |
| `script` | a Lua script of the package that runs only in this scenario ([scripts.md](scripts.md)) |
| `stars` | difficulty 0 to 5, shown as the original's stars |
| `money` | money of every human player at the start; without it the map's |
| `name.<language>` | the name in the scenario list; without one the id |
| `briefing.<language>` | the text of the mission screen and of the mission window; without one the map's mission text |
| `goal.…` | the goals, see below |

A scenario whose map is missing or outside the package's folder, or with a value that cannot be
read, is left out; the game's log names the file, the line and the key.

## Goals

The goals become the mission of every human player; the computers keep the missions the map gives
them. All goals given must hold at the same time, as in the original's missions
([missions.md](../re/missions.md)). Without any goal the map's own missions stay.

| Key | Goal |
|---|---|
| `goal.inhabitants` | at least so many inhabitants in all own cities together |
| `goal.cities` | at least so many cities |
| `goal.balance` | a balance per minute of at least this |
| `goal.trade_balance` | a trade balance per minute of at least this |
| `goal.defeated` | at least so many opponents defeated |
| `goal.castle`, `goal.cathedral` | `true`: a castle, a cathedral built |
| `goal.goods.<good>` | at least so many tons in the warehouses of all own cities; at most two goods. `<good>` is a name of the original's goods (`tools`), of the package's own (`amber`, [goods.md](goods.md)) or of another package's in quotes (`goal.goods."spice:pepper"`). A good the game does not know when the scenario starts is left out of the goal |
| `goal.city.inhabitants`, `goal.city.class`, `goal.city.count` | one city with at least `inhabitants`, `count` of them of class `class` or higher (0 pioneers … 4 aristocrats, then the classes of packages, [classes.md](classes.md)) |

## In the game

The scenario list of the main menu shows the packages' scenarios under the heading `----Mods----`,
after the game's own, with their stars, names and briefing in the game's language. Their high
scores are kept beside the others' under `<package>_<name>`.

A started scenario loads the map, then sets the money and the mission. Its script runs after the
package's `script.lua`, in a sandbox of its own: its table `state` is saved under the scenario's id
and it posts the package's messages. The game remembers the scenario it was started from (section
`SCEN` of the [savegame](../formats/o1602-save.md)), so a saved game runs the scenario's script
again after loading, as long as the package is installed and turned on.

A map saved by Open1602 (`.o1602`) keeps the packages it was saved with
([packages.md](packages.md#which-packages-a-game-uses)): save it with the package turned on, so its
buildings and goods are part of the map.
