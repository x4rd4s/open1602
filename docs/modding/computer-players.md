# Computer players and the goods of packages

The computer players build the production chains of the packages' goods without a file of their
own: what they build comes from the building table and the classes' needs
([ADR 0007](../architecture/0007-extensibility-and-modding.md) section 3, item 5 of
[open-work.md](open-work.md), MD-4).

**Status:** Part 1 done on 2026-10-31: the producers of a package's goods that a class needs, down
to their farms and to the original's raw materials. Still open: the original's own Needs, the stage
orders, the public buildings and the cheat amounts as data (they stay as the original has them).

## What the computers build

A good of a package ([goods.md](goods.md)) that a class of inhabitants needs
([classes.md](classes.md)) gets a producer: the first building in the table that makes it and that
the computers can place:

| The producer | Placed as | Its raw material |
|---|---|---|
| a workshop (`HANDWERK`), any footprint | a workshop beside a road near the Kontor or a market | nothing, a good of the original or a good of a package |
| a plantation (`PLANTAGE`) of 2 x 2 tiles | a farm with its fields | a crop of the original (`anno1602:45` to `anno1602:51`): the island must grow it |
| a pasture (`WEIDETIER`) of 2 x 2 tiles | a farm with its grass | grass (`anno1602:52`) |

The raw material of a workshop that is a package's good gets its producer the same way, and so on
down the chain. A workshop that uses a good of the original waits for the original's producer of it
and asks for one when the city has none (the tools of a package's workshop bring a toolsmith). A
chain that comes round to itself (`a` from `b`, `b` from `a`) does not wait for itself: the first of
its producers is built without its raw material.

Mines, fisheries, hunters and farms of another size are not built for a package's good; the good
then has to come by trade. Only the first raw material (`raw`) is followed, not the second (`raw2`).

## How many

A city wants as many producers of a good as its inhabitants use of it, together with what the
producers that use the good take, at full rate: a producer that makes 1 t every 20 s (3 t a minute)
serves 3 t of the inhabitants' needs a minute. At most eight producers of a good per city. The
values come from the building's `Prodmenge`, `Rohmenge` and `Interval` ([values.md](values.md)) and
the class's `need` ([classes.md](classes.md)).

## When

A city asks for these producers after each list of its stage ([ai.md](../re/ai.md)), once the
original's buildings of the list are there or cannot be built: a producer is planned when the city
can pay for it (a farm also for its fields) and has unlocked it. As with the original's buildings, a
site that cannot be found is tried again a few times, then the city waits until land on its island
becomes free or it starts anew.

The computers count the producers of a package's goods in slots after the original's 55 (section
`AIST` of the savegame, version 5); a game of the original has no such slots, and its computers
play as before.

## In the code

`engine/sim/src/ai/ai_chains.hpp`: `package_needs` (the producers from the building table),
`package_wanted` (how many a city wants); `Engine::package_chains` in `ai_planner.cpp` plans them,
the survey counts them (`Engine::package_slot`).
