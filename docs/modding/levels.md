# Unlock levels

A content package may change when the original's unlock levels are reached (`BAUINFRA` of
`haeuser.cod`) and add levels after its 32 ([ADR 0007](../architecture/0007-extensibility-and-modding.md)
section 2, item 3 of [open-work.md](open-work.md)). A level counts the inhabitants of one class and
all classes above it in one city, so a level may ask for a class a package added
([classes.md](classes.md)), and a building a package added may need a level a package added
([own-buildings.md](own-buildings.md)).

**Status:** Done are the file `levels.toml`, levels after the 32nd, the field `level` of a package's
buildings, the savegame and the message a level gives when it is reached (`message`, 2026-11-01).
Still open: the computer players, who plan with the original's levels (item 5).

## The file

```
<user data folder>/mods/<package>/levels.toml
```

A table names the level it changes, or declares a new one:

- `["anno1602:<name or number>"]` changes a level of the original, by name or INFRA number (below).
- `["<package>:<name>"]` changes a level a package added, this one or one loaded before it.
- `[<name>]` adds the level `<package>:<name>` after the last, number 33 and up. It needs a `class`
  and `min_inhabitants`. A name holds lower case letters, digits and `_`.

```toml
# mods/spice/levels.toml
["anno1602:level_2a"]
min_inhabitants = 80             # the settlers' second step later

[harbour]
class = "spice:governors"
min_inhabitants = 400
```

| Field | Limits | Meaning |
|---|---|---|
| `class` | a class of inhabitants, `anno1602:<name or number>` or `<package>:<name>` | whose inhabitants count, with those of the classes above it (`BGruppe`) |
| `min_inhabitants` | 0 to 10000000 | how many of them one city of the player needs (`Minwohn`) |
| `message` | a message, `<package>:<name>` of [messages.toml](messages.md) or `anno1602:<number>`; `""` for none | what the player is told when a city reaches the level; without it the original's ("Euer Volk wünscht eine Schule") for its levels and none for a package's |

A table that names no level, with a field that is no field of a level or with a value outside its
limits is passed over with a warning that names the file and the line. A game has at most 255
levels: a building keeps its level in a byte.

A level, once reached, stays unlocked for the player, as in the original.

## The message of a level

When a city of the player reaches a level, the original tells what the people now wish for, for
the levels that have a building ("Euer Volk wünscht eine Schule"). `message` names a message of
its own: one of a package's `messages.toml`, with its text, icon, voice and video, or one of the
original's. `{city}` in its text is the city's name. `""` tells nothing.

```toml
# mods/spice/levels.toml
[harbour]
class = "spice:governors"
min_inhabitants = 400
message = "spice:harbour_open"

# mods/spice/messages.toml
[harbour_open]
like = 0x3C
text = "The governors of {city} want a harbour."
text.de = "Die Statthalter von {city} wünschen einen Hafen."
```

A message no package has is written to the log, and the level gives the original's message, if it
has one.

### Names of the original's levels

| Name | Number | Name | Number | Name | Number |
|---|---|---|---|---|---|
| `market` | 1 | `doctor` | 9 | `level_2d` | 19 |
| `chapel` | 2 | `gallows` | 10 | `level_2e` | 20 |
| `school` | 3 | `castle` | 11 | `level_2f` | 21 |
| `tavern` | 4 | `cathedral` | 12 | `level_3a` | 22 |
| `church` | 5 | `triumphal_arch` | 13 | `level_2g` | 23 |
| `bathhouse` | 6 | `monument` | 14 | `level_3b` .. `level_3f` | 24 .. 28 |
| `theatre` | 7 | `level_1a` | 15 | `level_4a`, `level_4b` | 29, 30 |
| `college` | 8 | `level_2a` .. `level_2c` | 16 .. 18 | `level_5a`, `level_5b` | 31, 32 |

The triumphal arch and the monument are reached by allowance in the original; their numbers in
`haeuser.cod` ask for 25,000 aristocrats.

## Buildings of a level

A building a package added needs the level `level` names ([own-buildings.md](own-buildings.md)):

```toml
# mods/spice/own_buildings.toml
[customs_house]
like = "anno1602:20531"
level = "spice:harbour"
```

`level = ""` lets it be built always. Without `level` it needs what `like` needs.

## Savegames

A game keeps its levels, the original's as the packages changed them and the new ones (section
`LEVL` of [o1602-save.md](../formats/o1602-save.md)), and what each player reached after the 32nd
(section `UNLK`). A game with more than 32 levels cannot be saved as a savegame of the original
(`.gam`).

## In the code

`engine/assets/include/o1602/assets/unlock_levels.hpp`: `apply_levels_file` and `level_of`;
`InfraLevel` (building_table.hpp) carries the id of a package's level. `sim::World::infra_levels()`
carries a game's levels; the game puts them into its building table (`sim::apply_infra_levels`),
which the simulation reads them from. `sim::Player::infra_more` holds the levels 33 to 255.
