# Figures of a package's own

A content package may add figures the original's `figuren.cod` does not have
([ADR 0007](../architecture/0007-extensibility-and-modding.md) section 2, item 2 of
[open-work.md](open-work.md)). Each one copies a figure of the original and may draw an image of
the package: a monk who carries the goods of a monastery, a donkey for a mill.

**Status:** Done are the file `own_figures.toml`, the figure's own picture, its values
([values.md](values.md)), the buildings of a package that name it as their carriers or workers
([own-buildings.md](own-buildings.md)) and its number in savegames. Still open: soldiers and ships
of a package's own, which castles and shipyards would have to offer.

## The file

```
<user data folder>/mods/<package>/own_figures.toml
```

Each table declares one figure. The table's name is the figure's name in the package; together with
the package's id it is the figure's **string id**, `<package>:<name>`, and that is what everything
else names it by. A name holds lower case letters, digits and `_`.

```toml
# mods/monks/own_figures.toml
[monk]
like = "anno1602:carrier"   # the figure of the original it copies (required)
image = "monks:monk"        # drawn with this image of graphics.toml
shadow = false
```

| Field | Limits | Meaning |
|---|---|---|
| `like` | one figure: of the original, `anno1602:<name or number>` ([names](values.md#names)), or of a package, `<package>:<name>` | every value, animation and sound of that figure, as the copy's starting point. A unit's name stands for its four colours and is no single figure: name one of them by its number |
| `image` | an image of a package, `<package>:<name>` ([graphics.md](graphics.md)) | the sprites the figure draws, in the order the original's sheet has them from the figure's `Gfx` ([graphics.md](graphics.md#figures-and-ships)); without it the picture of the figure it copies |
| `shadow` | `true` or `false` | whether it keeps the shadow of the figure it copies; `true` if not given |

A table without `like`, with a field that is no field of a figure, with a value outside its limits
or with an image of fewer sprites than the figure draws is passed over with a warning that names
the file and the line; so is a name a package used already. A package declares at most 64 figures.

The figure's **values** (speed, carrying, hit points) are changed as the original's are, in
[`figures.toml`](values.md), by the package itself or by a later one:

```toml
# mods/monks/figures.toml
["monks:monk"]
speed = 40
max_carry = 2
```

A building of a package names it as its carriers or workers with `figure`
([own-buildings.md](own-buildings.md)):

```toml
# mods/monks/own_buildings.toml
[abbey]
like = "anno1602:bakery"
figure = "monks:monk"
```

## Numbers and savegames

The engine gives every figure of a package a number of the figure table, the highest free one
downwards from 1023, in the packages' load order and in the order of each file. The same packages
therefore give the same figures the same numbers on every computer, which is what a multiplayer
game needs.

A game keeps the string ids with the numbers it gave them (section `OWNF` of
[o1602-save.md](../formats/o1602-save.md)). A savegame loads only when the computer's packages give
those figures their numbers again, so a game with such figures needs its packages
([packages.md](packages.md#which-packages-a-game-uses)). Savegames of the original (`.gam`) have
none of them, and a game with them cannot be saved for the original.

## In the code

`engine/assets/include/o1602/assets/own_figures.hpp`: `OwnFigure` (string id and number),
`apply_own_figures_file`, `figure_number`; `FigureTable::add` gives a copy its number.
`sim::World::own_figures()` carries them through a game and its savegame.
