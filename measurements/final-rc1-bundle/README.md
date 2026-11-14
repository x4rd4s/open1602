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

Passed (`gate-report.json`: final, n=4, 2 standard errors; 184 runs).

| | main | bundle | difference (se) | verdict |
|---|---|---|---|---|
| unreachable farms per run (decides) | 6.10 | 0.58 | 5.53 fewer (0.44) | gain |
| food balance an hour into a shortfall (reported) | -11.25 | -11.40 | -0.15 (1.91) | no measurable effect |
| inhabitants, standard set | 138,926 | 141,260 | +2,334 (1,903) | no measurable effect |
| collapse rate, standard set | 0.0030 | 0.0030 | 0 (0.0042) | no measurable effect |
| inhabitants, long set | 106,502 | 107,628 | +1,126 (2,612) | no measurable effect |
| collapse rate, long set | 0.104 | 0.089 | 0.016 lower (0.021) | no measurable effect |
