# Scripts

Content packages bring Lua scripts that react to what happens in a game, and later change it through
the game's commands ([ADR 0007](../architecture/0007-extensibility-and-modding.md) section 6, item
6 of [open-work.md](open-work.md)).

**Status:** Done on 2026-10-27: Lua 5.4.8 is built in (`third_party/lua`), every package may bring a
`script.lua`, which the game runs in a sandbox at the end of every tick with what happened in it, a
script reads the players and posts the package's messages, and its table `state` is saved with the
game. Since 2026-10-31 a script also gives and takes money and goods and decides who wins and who
is out. Still open: more events.

## The script

```
<user data folder>/mods/<package>/script.lua
```

A package has at most one script, a UTF-8 text file. A scenario of a package may bring one more,
which runs only in that scenario, after the package's ([scenarios.md](scenarios.md)); in a part of
a campaign its table `campaign` goes on to the next part ([scenarios.md](scenarios.md#campaigns)). When a game starts or is loaded, the game runs
the top level of each package's script, in the packages' load order, each in a sandbox of its own:
what one package's script defines, another's does not see. Then the game calls the functions the
script defines when something happens:

| Function | When |
|---|---|
| `on_start()` | at the first tick of a new game (or of a game that ran without the script so far) |
| `on_second(seconds)` | every second of game time; `seconds` since the game began |
| `on_building_placed(e)` | a building was placed: `e.building` (its number in `haeuser.cod`), `e.island`, `e.x`, `e.y` and `e.player`, the owner of its city (nil without one) |
| `on_house_changed(e)` | a house rose, fell back or was abandoned: `e.kind` (`"upgraded"`, `"downgraded"`, `"abandoned"`), `e.class` (the class after the change), `e.island`, `e.x`, `e.y` |
| `on_player_won(player)` | a player accomplished the mission |
| `on_player_defeated(player, by)` | a player is out; `by` is who defeated them (nil: nobody) |
| `on_game_over(player)` | the game ended with `player`'s mission |

The game calls them inside the tick, on every computer of a multiplayer game alike, so a script
needs nothing of its own to stay in step with the others.

## What a script reads and does

The table `game` holds what the game offers a script:

| Function | What it gives or does |
|---|---|
| `game.package` | the package's id |
| `game.time()` | the game time in whole seconds |
| `game.players()` | the players' numbers, a list starting with 0 |
| `game.player(n)` | player `n` as a table: `id`, `name`, `role` (`"human"`, `"computer"`, `"neutral"`), `money`, `inhabitants`, `playing` (still in the game), `won`, `faction` (its id, `"anno1602:settlers"`, [factions.md](factions.md)); nil for no such player |
| `game.message(id, values, player)` | posts a message of the package's `messages.toml` ([messages.md](messages.md)); `id` is its name there, or `"<package>:<name>"` for another package's; `values` fills its placeholders (a table of names, may be nil); `player` is the only player to see it (nil: everyone) |

```lua
-- mods/lighthouse/script.lua
state = state or {lit = 0}

function on_building_placed(e)
  if e.building == 20311 and e.player then  -- a building's number in haeuser.cod
    state.lit = state.lit + 1
    game.message("lit", {name = game.player(e.player).name, count = state.lit})
  end
end
```

Cities are named by their island and their number on it, as `game.cities()` lists them. A good is
named as in a package's data files: `"anno1602:tools"` (a good of the original by name or number,
[names](goods.md#the-file)), `"<package>:<name>"`, a name of the package's own `goods.toml`, or its
ware number.

| Function | What it gives |
|---|---|
| `game.cities(player)` | the cities of `player` (nil: all), a list of tables: `island`, `city` (its number on the island), `name`, `player` (nil without an owner) |
| `game.goods(island, city, good)` | the whole tons of `good` in the city's warehouses; nil for no such city |

## What a script changes

| Function | What it does |
|---|---|
| `game.add_money(player, amount)` | gives `player` this much gold (negative: takes it; the money may go below 0) and returns what the player has then |
| `game.add_goods(island, city, good, tons)` | puts whole tons of `good` into the city's warehouses, as many as fit, or takes them (negative), as many as are there and not promised to a carrier; returns the tons that changed (negative: taken) |
| `game.win(player)` | `player` accomplished their task and the game is over for everybody, as when the victory conditions are met; true if so, false when the game is over already or the player is no human or computer still playing |
| `game.defeat(player)` | `player` is out of the game, nobody counts as having defeated them; true if so, false when the game is over or the player is not playing |

They work only while the game runs, in the `on_...` functions, not at the top level of the script.
Every computer of a multiplayer game makes the same changes at the same moment, so they need no
command over the network, and a replay makes them again. What a script changes reaches no script as
an event (`game.win` calls no `on_player_won`): the script knows what it did, and so does no script
end up calling itself for ever.

```lua
-- mods/bounty/script.lua: 1000 gold and 10 tons of tools for every fifth house that rises
state = state or {houses = 0}

function on_house_changed(e)
  if e.kind ~= "upgraded" then return end
  state.houses = state.houses + 1
  if state.houses % 5 == 0 then
    local city = game.cities(0)[1]
    game.add_money(0, 1000)
    if city then game.add_goods(city.island, city.city, "anno1602:tools", 10) end
  end
  if state.houses >= 100 then game.win(0) end
end
```

## Saved with the game

The game saves the table `state` of each script with the game, and the script's random numbers go
on where they stopped. When the game is loaded, the game runs the script's top level again and then
sets `state` to what was saved; `on_start` is not called again. So everything a game has to remember
belongs in `state`; other globals are made anew from the top level, and a computer that joins a
multiplayer game later starts from `state` too.

`state` keeps booleans, numbers, strings and tables of them (keys that are booleans, numbers or
strings). Functions and coroutines in it are not saved, nor tables nested deeper than 32. The
fingerprint that computers of a multiplayer game compare includes `state`.

## Errors

A failed script is reported in the status line and in the log with its package, line and cause; the
game goes on. A script whose top level fails is not called for the rest of the game, and neither is
one that failed 10 times. What a script prints goes to the log.

## The language

Scripts are Lua 5.4 text files. Every computer of a multiplayer game and every replay runs the same
scripts with the same events and has to come to the same results, so a script sees only what is the
same everywhere:

| Available | Not available |
|---|---|
| the base functions (`pairs`, `ipairs`, `select`, `type`, `tonumber`, `tostring`, `pcall`, `error`, `assert`, `setmetatable`, `rawget` ...) | `dofile`, `loadfile`, `load`, `collectgarbage`, `warn` |
| the libraries `string`, `table`, `math`, `utf8` and `coroutine` | the libraries `io`, `os`, `package` (`require`) and `debug` |
| `math.random` with the game's random numbers | `math.randomseed`; a script cannot choose the numbers |
| `print`, into the game's log | `string.dump` and binary chunks |

What differs from plain Lua:

- **No addresses.** `tostring` of a table, function or coroutine without `__tostring` gives just its
  type name (`table`, `function`), and so do `print` and `string.format("%s", ...)`. The format
  `%p` is refused. An address differs between computers and runs.
- **No finalizers and weak tables.** `setmetatable` refuses a metatable with `__gc` or `__mode`:
  when the garbage collector runs differs between systems, and with it what they would do.
- **Random numbers from the game.** `math.random()`, `math.random(m)`, `math.random(m, n)` and
  `math.random(0)` work as in Lua 5.4, but draw from the game's generator, so they are the same on
  every computer.
- **The order of `pairs`** over keys that are strings, numbers or booleans is the same on every
  computer for the same steps (the string hash seed is fixed). Over keys that are tables or functions
  it may differ; keep such things in an array and walk it with `ipairs`.
- **Prefer integers.** Integer arithmetic is exact everywhere. Floating point is too for `+`, `-`,
  `*`, `/` and `math.sqrt`, but `^`, `math.exp`, `math.log`, `math.sin` and the other functions of
  the C library may differ in the last bit between systems. Do not let a decision of the game hang
  on such a bit.

## Limits

| What | Limit | When it is reached |
|---|---|---|
| memory of each package's script | 32 MiB | the script stops with "the script needs more than ... bytes of memory" |
| instructions per call from the game (the top level, each `on_...`) | 10 million | the script stops with "the script ran too long"; a `pcall` in the script cannot catch it for good |
| arguments of a call from the game | 200 | the call fails |

A failed script reports its name, its line and the cause, for example `quests:2: no ships`. The game
goes on; the script's globals keep what they held before the error.

The instruction limit counts the script's own instructions; time spent inside a single library
function, such as a pattern search of `string.find` on a long text, is not counted.
