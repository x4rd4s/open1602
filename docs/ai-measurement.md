# Measuring the computer players

A change to the improved computers goes in when it is shown to do what it should, and when it makes
nothing worse. Games of the computers vary a lot by chance alone: delaying every computer's first
step by one to three steps (50 to 150 ms of game time) moved the inhabitants of 18 test games by
−7 to +0 % in all and by up to ±28 % in a single game, and made players collapse or recover. A
single run therefore says little, and the end totals least of all. The rules below, and the tools
that hold them, follow from that.

## Stages

1. **Mechanism.** A change first shows on two or three games chosen for it, in diagnostic runs,
   that it does what it is for: more farms where food runs short, more gold in hand at a founding,
   fewer orders that never end. At most 3 games and 400 minutes. When the effect lies later in a
   game, the runs start from a save written shortly before it; both arms start from the same save,
   and the 400 minutes count from there.
2. **Metric.** The decision metrics on short runs (at most 400 minutes, one phase each): numbers
   taken where the decision happens, such as the gold at a founding, a city's food balance an hour
   after its shortfall began, a new farm's output against its rate, the share of players who
   collapse. Not the inhabitants at the end.
3. **Final.** The changes that passed are measured together, at full length (600 minutes on the
   standard games, 1200 on the long ones), with at least 4 phases on standard games and 8 on long
   ones. Only when the bundle fails is it taken apart.

A phase is a run with every computer's first step delayed by that many steps
(`o1602-tool bench --ai-phase N`); phase 0 is the plain game.

## Verdict

Per set of games the verdict compares each arm with the base arm over the phases: the mean, and the
standard error of the difference from the spread of both. A difference of more than two standard
errors in the better direction is a gain, one of more than two in the other direction is worse, and
anything between is no measurable effect. The inhabitants are only checked for harm: an arm must not
be worse. Collapses (a player ending below 70 % of its peak) count as a rate per player and run.
A final run passes when no set and no decision metric is worse; a mechanism run passes when its
metrics move the right way.

## Tools

- `tools/measure/measure.py` checks a plan (`check`), lists its runs (`jobs`), collects the
  results (`collect`) and gives the verdict (`verdict`). It refuses a plan that breaks its stage's
  rules, results of another plan or of a plan changed after the runs, runs or metrics that are
  missing, and a final plan whose changes do not name a passed mechanism report by its hash.
- `tools/measure/run.sh PLAN.toml ARM=/path/to/o1602-tool ...` runs a committed plan on this
  machine: not while the game runs, only into a new folder under `/tmp`, headless and silent, and
  without writing to any repository.
- `tools/check_ai_constants.py` fails a pull request that brings a new number or named value into
  the computer players' code, the balance values or the balance files. A value that follows from
  others goes into `tools/ai_constants_allowlist.txt` with what it is derived from.
- The golden hashes of the unit tests (one per platform, `ctest`) keep "as in the original" the same.

## A plan

```toml
stage = "mechanism"            # mechanism, metric or final
base = "main"
question = "Do hungry home cities find farm sites off their roads?"

[[arm]]
name = "main"
[[arm]]
name = "offroad"

[phases]
long = 1

[start]                        # optional: the save both arms start from
save = "l-g12-404-m400.o1602"
sha256 = "…"

[[game]]
name = "L-g12-404"
set = "long"
players = 12
world = "2500x1800"
seed = 404
minutes = 400

[[metric]]
name = "food_balance_gap60"    # t/min of the hungry home cities an hour after their shortfall
better = "higher"

# A final plan also names each change and its passed mechanism report:
# [[change]]
# name = "offroad"
# report = "reports/offroad-mechanism.json"
# sha256 = "…"
```

The evaluation that computes the decision metrics from the runs' logs writes them per run as
`ARM/PHASE/GAME.metrics.json` (`{"food_balance_gap60": 0.4}`); `measure.py collect` then binds them
to the plan's hash.
