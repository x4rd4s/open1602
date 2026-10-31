# engine/script

Scripts of content packages (ADR 0007 section 6, [docs/modding/scripts.md](../../docs/modding/scripts.md)).

- `sandbox.hpp`: `script::Sandbox`, a Lua 5.4 state (`third_party/lua`) that keeps scripts
  deterministic and closed off: no files, no clock, no addresses, random numbers only from the
  game, limits on memory and instructions; the game's functions and saving a table as bytes.
- `game_scripts.hpp`: `script::GameScripts`, the scripts of a game's packages (`script.lua`), one
  sandbox each, run by the simulation at the end of every tick (`sim::ScriptHost`) and saved with
  the game (section `SCRP`). What a script changes (money, goods, who wins and who is out) goes
  through `sim::Simulation::script_money` and the others, no commands: every computer makes the
  changes at the same point of the tick.

Still open (milestone M8): more events and functions.
