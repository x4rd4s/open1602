# Final gate: offroad farms and the G4b colony plan

The final stage (docs/ai-measurement.md) of two changes that passed their mechanism stage:

- **offroad**: a hungry rival's pasture or grain farm with no site beside its roads takes one off
  them, with a road to it (`reports/offroad-mechanism.json`).
- **G4b**: one colony decision among the original's motives, ranked by the city the colony becomes,
  keeping its gold and ship (`reports/g4b-mechanism.json`).

Both were refactored onto shared code after their mechanism runs; each refactor was checked to give
byte-identical runs (stdout and computer log) on the mechanism games.

## Runs

- `plan.toml`: two arms, `main` (e3fa2169) and `bundle` (main with both changes), 13 standard games
  at 600 minutes with 4 phases and 5 long games at 1,200 minutes with 8 phases, 184 runs.
- Both arms ran with the same logging-only patch (not in the repository): the net of each city 30
  and 60 minutes after its founding, and every 5 minutes each home city's food made and eaten. The
  patched main gave the same stdout and the same computer log (without the added lines) as main.
- `metrics.py` wrote each run's metrics from those logs; `measure.py collect` and `verdict` gave
  `gate-report.json` (sha256 0ceb683d…).

## Verdict

Passed: no set and no metric worse; every difference is within two standard errors.

| | main | bundle |
|---|---|---|
| inhabitants, standard (per phase) | 133,892 | 138,926 |
| inhabitants, long (per phase) | 105,230 | 106,502 |
| collapse rate, standard | 0.0060 | 0.0030 |
| collapse rate, long | 0.081 | 0.104 |
| food balance an hour into a shortfall (t/min, summed) | −14.28 | −11.25 |
| foundings with less than 3,000 gold | 14.46 | 13.05 |

The long games' collapse rate leans the other way (40 players against 31 over all runs, under one
standard error). The collapses barely pair up between the arms (4 in both), and in the hour before
the decline neither the colony plan, a kept ship nor an off-road farm appears more often than
before the players that did not collapse; debt precedes most collapses in both arms.
