# Limits of the data files

Open1602 reads the original's `haeuser.cod`, `figuren.cod` and `.gad` layouts, and modded copies
of them. A modded file can hold any number, so the loaders check the ones that index tables or
size memory and refuse or cut what lies outside. These are the limits.

| File | Value | Limit | Outside the limit |
|---|---|---|---|
| `haeuser.cod` | `Id` | 20000 to 23500 (as `Haus_GetById` of the original) | the file is refused |
| `haeuser.cod` | `Nummer` | 0 to 65535 | the file is refused |
| `haeuser.cod` | `Size` | 1 to 255 tiles each way (an island of the original is at most 255 wide) | the file is refused |
| `haeuser.cod` | `Arbeiter` | 65535 | clamped |
| `figuren.cod` | `Nummer` | 0 to 1023 | the file is refused |
| both | `ObjFill` | 10000 copies | clamped, with a warning |
| both | a number with a decimal point | the range of a 32-bit integer | clamped |
| both | times, amounts, health, radius (`Worktime`, `Maxenergy`, ...) | the range of the field | clamped (`core::saturating_cast`) |
| `.gad` | `Include` | 4 levels deep, 1 MiB of text in all | the rest is left out, with a warning |

Decimal numbers become integers by truncating toward zero and clamping, never by a plain cast:
a cast of a value out of range is undefined in C++ and gives different results on x86 and ARM,
and every computer of a multiplayer game must read the same data the same way.

The game numbers the engine relies on (ware, building kind, production kind and figure numbers)
are named in one place, `engine/assets/include/o1602/assets/game_data.hpp`. A data set that
renumbers them needs that header changed too; a data loader for that is planned
([ADR 0007](../architecture/0007-extensibility-and-modding.md)).

The score of the end screen and the monument allowance are named there too, in
`assets::scoring`: the city and fight curves as tables of straight segments, the money steps, the
monument and defeat points, and the rule that lets a growing city build a monument (every 30
minutes of game time with 200 inhabitants of class Bürger and higher). A mod changes them in that
header.
