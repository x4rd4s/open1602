# Final gate: farms only where carts reach them (the small bundle)

The final stage (docs/ai-measurement.md) of RC1, which passed its mechanism stage
(`reports/rc1-mechanism.json`): an improved rival places a farm only where the carts of one of its
markets or Kontors reach it by road. The rest of the bundle only logs (what ships bring a city, why a
rival builds no shipyard) or changes the measurement tools (every game names its level; a human seat
that gives no commands), so it is measured along without a mechanism report of its own.

## Runs

- `plan.toml`: two arms, `main` and `bundle`, at level 3 (where the earlier gates and the noise band
  ran), 4 phases on the standard games and 8 on the long ones.
- `metrics.py`: the decision metrics from each run's AI log.

## Result

Pending.
