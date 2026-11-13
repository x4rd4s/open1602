# Debt before collapses: a diagnosis and a lever that did not work

The long games of the final gate in `../final-offroad-g4b` showed a lasting stretch of debt before
most collapses (a player whose inhabitants end below 70 % of their peak), in both arms. This note
keeps what was learnt from looking into it, so that the next attempt starts from it.

## Diagnosis (main a598b46a, logging only)

Three long games at 1,200 minutes with a logging-only patch (not in the repository): every 10
minutes each rival's budget (`gold_budget`), its cities' purchases by ware with demand and supply,
every reservation and whether the budget gate had passed it. The patched build gave the same stdout
and the same computer log (without the added lines) as main.

- 10 collapses: 5 after a lasting debt, 5 without (3 a fall of contentment, 2 mild drops at the end
  of the game).
- In the hour before the debt the running costs were no higher than in the players that did not
  collapse. The gold left through reservations that the budget gate does not weigh (farms,
  pastures, markets, workshops: 73 % of the reserved gold) and through purchases of building
  material while the city's supply already met its demand (58 % of the purchases).
- The budget's forecast saw it coming only briefly: the gold at the horizon fell below zero ten
  minutes (median) before the cash did. Healthy players have such samples too (18 % of theirs).
- The measures for debt (switching producers off, disbanding soldiers) started 13 minutes after
  the debt began.

## Lever DD1 (mechanism stage, not taken)

While a rival's cash was below its cushion and its gold at the horizon below zero, its producers
and markets went through the budget gate like other buildings (food for a city short of it, houses
and Kontors excepted). Four windows of 400 minutes from saves of main, both arms with the
logging above:

| | main | DD1 |
|---|---|---|
| lasting debt stretches per player-hour | 0.097 | 0.100 |
| collapses | 7 | 6 |
| gold leaving in the hour before a debt (median) | 42.0k | 35.1k |

The reservations halved, but debt and collapses did not move. The condition held for healthy
players in 12 to 18 % of their samples, and 63 % of what DD1 held back was theirs; in the hour after
its first hold the net income was lower for 29 of 44 players. The condition does not single out the
players at risk, so the lever was dropped. A further attempt needs a sharper signal of a coming
debt first.
