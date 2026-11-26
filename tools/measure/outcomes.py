#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""What came of the computers' decisions: pairs of (expected, actual) from a computer log (P0.3).

Usage:
  python3 tools/measure/outcomes.py LOG [--horizons 30,60,120] [--out PAIRS.jsonl]
      Reads a log written with O1602_AI_LOG set: the "decision" lines (Engine::log_decision) and the
      "track" lines every ten minutes of game time (Engine::track). Writes one JSON record per
      decision, with what the computer expected (cost, upkeep, value, delay, horizon, amount) and,
      for each horizon in minutes, what happened in its city and to its player's gold. Prints a
      count per kind and horizon. Exit code 0 when all is well, 2 on wrong use.

For a decision at game time t in a city, and a horizon of h minutes:
  gold       the player's gold at the first track line at or after t + h, less at the last before t;
  tax, cost  the city's taxes and running costs a minute at t + h, less at t (0 where the city had
             no line yet, as for a city just founded);
  people     the city's inhabitants at t + h, less at t;
  made, in, used   of the decision's ware (a building's good, a run's ware; none for a colony):
             what the city made, got (shipped in or bought) and its people used over the track
             lines after t up to t + h, in 1/32 t. Each track line covers its last ten minutes, so
             the sum starts up to ten minutes before t.
  to         for a decision for another city's people (a colony plan or founding: the home city it is
             weighed for), that city's people, tax and cost at t + h less at t.
What is read for a kind: made for a producer, got and used for a run of goods, people and tax
for houses, public buildings, markets and colonies (their ware is 0 or nothing they make). The
ledger holds trade goods only (Goods::is_trade), so a producer of anything else (a quarry's
stone) shows made 0; read the city's other fields for it.
The actual values are the city's change over the horizon, not the decision's own share: decisions
in one city at one time share them, and attributing them is the reader's work (P2.6).
A horizon the run did not reach is null. A decision without a city (a colony plan) gets no city fields.
"""
from __future__ import annotations

import bisect
import json
import re
import sys

LINE = re.compile(r"^\[ai (\d+) (\d+)s\] (decision|track) (.*)$")
DECISION = re.compile(
    r"^(?P<kind>[a-z ]+?) id (?P<ms>\d+)\.(?P<seq>\d+) cand (?P<cand>\d+) city (?P<island>-?\d+)/(?P<city>-?\d+) "
    r"cost (?P<cost>-?\d+) upkeep (?P<upkeep>-?\d+) value (?P<value>-?\d+) delay (?P<delay>\d+) "
    r"horizon (?P<horizon>\d+) ware (?P<ware>\d+) amount (?P<amount>\d+) to (?P<to_island>-?\d+)/(?P<to_city>-?\d+)$"
)
TRACK_GOLD = re.compile(r"^gold (-?\d+)$")
TRACK_CITY = re.compile(r"^city (\d+)/(\d+) people (\d+) tax (-?\d+) cost (-?\d+) goods(.*)$")
EXPECTED = ("cost", "upkeep", "value", "delay", "horizon", "amount")


class Series:
    """Samples of one thing by game second, in the order the log wrote them."""

    def __init__(self) -> None:
        self.times: list[int] = []
        self.values: list = []

    def add(self, t: int, value) -> None:
        self.times.append(t)
        self.values.append(value)

    def at_or_before(self, t: int):
        i = bisect.bisect_right(self.times, t)
        return self.values[i - 1] if i > 0 else None

    def at_or_after(self, t: int):
        i = bisect.bisect_left(self.times, t)
        return self.values[i] if i < len(self.values) else None

    def between(self, after: int, upto: int) -> list:
        lo = bisect.bisect_right(self.times, after)
        hi = bisect.bisect_right(self.times, upto)
        return self.values[lo:hi]

    def last_time(self) -> int:
        return self.times[-1] if self.times else -1


def parse_goods(text: str) -> dict[int, tuple[int, int, int]]:
    """made, got, used of each ware in a track line (its demand and stock are read by the report)."""
    goods = {}
    for item in text.split():
        ware, flows = item.split(":")
        made, got, used = (int(x) for x in flows.split("/")[:3])
        goods[int(ware)] = (made, got, used)
    return goods


def read_log(lines):
    """Returns (decisions, gold series by player, city series by (player, island, city))."""
    decisions = []
    gold: dict[int, Series] = {}
    cities: dict[tuple[int, int, int], Series] = {}
    for raw in lines:
        m = LINE.match(raw.rstrip("\n"))
        if not m:
            continue
        player, second, what, rest = int(m.group(1)), int(m.group(2)), m.group(3), m.group(4)
        if what == "decision":
            d = DECISION.match(rest)
            if not d:
                continue
            rec = {k: (v if k == "kind" else int(v)) for k, v in d.groupdict().items()}
            rec["player"] = player
            decisions.append(rec)
            continue
        g = TRACK_GOLD.match(rest)
        if g:
            gold.setdefault(player, Series()).add(second, int(g.group(1)))
            continue
        c = TRACK_CITY.match(rest)
        if c:
            key = (player, int(c.group(1)), int(c.group(2)))
            sample = {
                "people": int(c.group(3)),
                "tax": int(c.group(4)),
                "cost": int(c.group(5)),
                "goods": parse_goods(c.group(6)),
            }
            cities.setdefault(key, Series()).add(second, sample)
    return decisions, gold, cities


def outcome(d: dict, horizon_min: int, gold: dict, cities: dict):
    t = d["ms"] // 1000
    end = t + horizon_min * 60
    golds = gold.get(d["player"])
    if golds is None or golds.last_time() < end:
        return None
    before = golds.at_or_before(t)
    after = golds.at_or_after(end)
    result = {"gold": after - (before if before is not None else after)}
    if (d["to_island"], d["to_city"]) != (d["island"], d["city"]):
        # A colony's value lands in the city it is for (the home city colony_rank weighs).
        to = cities.get((d["player"], d["to_island"], d["to_city"]))
        start = to.at_or_before(t) if to is not None else None
        stop = to.at_or_after(end) if to is not None else None
        if start is None or stop is None:
            result["to"] = None
        else:
            result["to"] = {f: stop[f] - start[f] for f in ("people", "tax", "cost")}
    if d["island"] < 0:
        return result
    series = cities.get((d["player"], d["island"], d["city"]))
    if series is None:
        result.update(people=None, tax=None, cost=None, made=None, got=None, used=None)
        return result
    zero = {"people": 0, "tax": 0, "cost": 0, "goods": {}}
    at_start = series.at_or_before(t) or zero
    at_end = series.at_or_after(end)
    if at_end is None:
        # The city has no line at t + h although its player has: it was lost or given up.
        result.update(people=None, tax=None, cost=None, made=None, got=None, used=None)
        return result
    for field in ("people", "tax", "cost"):
        result[field] = at_end[field] - at_start[field]
    made = got = used = 0
    if d["ware"] != 0:
        for sample in series.between(t, end):
            m, g, u = sample["goods"].get(d["ware"], (0, 0, 0))
            made, got, used = made + m, got + g, used + u
    result.update(made=made, got=got, used=used)
    return result


def pairs(decisions, gold, cities, horizons):
    for d in decisions:
        yield {
            "id": f"{d['ms']}.{d['seq']}",
            "player": d["player"],
            "kind": d["kind"],
            "cand": d["cand"],
            "city": [d["island"], d["city"]],
            "to": [d["to_island"], d["to_city"]],
            "ware": d["ware"],
            "expected": {k: d[k] for k in EXPECTED},
            "actual": {str(h): outcome(d, h, gold, cities) for h in horizons},
        }


def main(argv: list[str]) -> int:
    args = list(argv)
    horizons = [30, 60, 120]
    out = None
    try:
        if "--horizons" in args:
            i = args.index("--horizons")
            horizons = [int(x) for x in args[i + 1].split(",")]
            del args[i : i + 2]
        if "--out" in args:
            i = args.index("--out")
            out = args[i + 1]
            del args[i : i + 2]
    except (IndexError, ValueError):
        print(__doc__, file=sys.stderr)
        return 2
    if len(args) != 1:
        print(__doc__, file=sys.stderr)
        return 2
    with open(args[0], encoding="utf-8", errors="replace") as f:
        decisions, gold, cities = read_log(f)
    records = list(pairs(decisions, gold, cities, horizons))
    if out is not None:
        with open(out, "w", encoding="utf-8") as f:
            for r in records:
                f.write(json.dumps(r, sort_keys=True) + "\n")
    counts: dict[str, list[int]] = {}
    for r in records:
        row = counts.setdefault(r["kind"], [0] * (len(horizons) + 1))
        row[0] += 1
        for i, h in enumerate(horizons):
            row[i + 1] += r["actual"][str(h)] is not None
    print("kind all " + " ".join(f"{h}min" for h in horizons))
    for kind in sorted(counts):
        print(kind.replace(" ", "_") + " " + " ".join(str(n) for n in counts[kind]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
