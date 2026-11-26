#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The standard report of a probe or gate: one arm against a reference, set by set (P0.4).

Usage:
  python3 tools/measure/report.py RUNDIR --arm ARM --ref REF [--sets l2,l3,x3] [--human-sets l2] [--json OUT]
      [--change 'PREFIX|PREFIX'] [--war-change]

RUNDIR holds one folder per set, and in it per run ARM-KEY.txt (o1602-tool bench --per-player), ARM-KEY.rc
and, for a logged run, ARM-KEY.err (the computer log, O1602_AI_LOG). A key is a map and a phase,
MAP-pN; a key with -mNNN- starts from a save NNN minutes into the game, which the log's times count
and the bench's minutes do not. Keys are compared where both arms have their .rc. In a set of
--human-sets, seat 0 is a human who gives no commands: it is left out of inhabitants, gold and
collapses (and, as the earlier reports did, counted in cities).

From the bench output, per set:
  inhabitants  each run's inhabitants at its last minute over the reference's mean for the same map;
               the arm's mean less the reference's, with the standard error of that difference
               (measure.py se_of_difference) and the difference in standard errors;
  collapses    players whose inhabitants end below 70 % of their peak (measure.py COLLAPSE_PERCENT,
               the rule every earlier report used), in each arm; the keys with two or more the
               reference does not have; and for each extra collapse its slide window, from the last
               minute at the peak to
               the first minute after it below 70 %, with the landings by or against that player
               from 30 minutes before the peak (from the log, where there is one);
  gold, cities the sums at the last minute over all runs, the mean per run and the standard error
               of the per-run difference;
  computer 1   seat 1's end inhabitants in each run, and how many runs it lost (below 70 % of the
               reference's) or gained (the reference below 70 % of it), where either has 300 or more.
From the log, where the arm and the reference have one with main's standard lines (else n/a):
  foundings    the "decision found" lines;
  debt         the "track gold" samples below 0: minutes in debt (ten a sample) and players in debt;
  dry          per city and good, the "track city" samples in which its people asked for the good
               and its stock was 0: dry minutes, episodes of 10 minutes or more, and how many of
               them ended before the run did (covered); beside it the flow deficit (used less than
               asked for in the sample), which counts more cases.
Then rules 1 to 4 of the gate (rules(), cause()): --change names the change's own log lines by how
they begin, --war-change marks a change that acts on war (its conquests are intended).
Exit code 0, 2 on wrong use.
"""
from __future__ import annotations

import json
import math
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from measure import COLLAPSE_PERCENT, mean, se_of_difference  # noqa: E402

MINUTE = re.compile(r"^\s*(\d+)\s+[\d.]+\s")
PLAYER = re.compile(r"^\s+player (\d+): (\d+) cities, (\d+) inhabitants, (-?\d+) gold")
HASH = re.compile(r"state hash (\w+)")
LOG = re.compile(r"^\[ai (\d+) (\d+)s\] (.*)$")
LANDING = re.compile(r"landing of ship .*against player (\d+)")
TRACK_GOLD = re.compile(r"^track gold (-?\d+)$")
TRACK_CITY = re.compile(r"^track city (\d+)/(\d+) people \d+ tax -?\d+ cost -?\d+ goods(.*)$")
TRACK_MINUTES = 10  # Engine::track's period (kTrackMinutes)
LOST_FLOOR = 300  # computer 1: only seats with this many inhabitants in either run (f48's rule)


def parse_bench(text: str) -> dict:
    """S[player][minute] = (inhabitants, gold, cities), and the state hash."""
    series: dict[int, dict[int, tuple[int, int, int]]] = {}
    minute = None
    for line in text.splitlines():
        if m := MINUTE.match(line):
            minute = int(m.group(1))
        elif (m := PLAYER.match(line)) and minute is not None:
            series.setdefault(int(m.group(1)), {})[minute] = (int(m.group(3)), int(m.group(4)), int(m.group(2)))
    h = HASH.search(text)
    return {"S": series, "hash": h.group(1) if h else None}


def end_of(samples: dict[int, tuple[int, int, int]]) -> tuple[int, int, int]:
    return samples[max(samples)]


def collapsed(samples: dict[int, tuple[int, int, int]]) -> bool:
    peak = max(v[0] for v in samples.values())
    return peak > 0 and end_of(samples)[0] * 100 < peak * COLLAPSE_PERCENT


def window(samples: dict[int, tuple[int, int, int]]) -> tuple[int, int]:
    """The slide: the last minute at the peak, and the first minute after it below 70 % of the peak."""
    peak_minute = max(samples, key=lambda m: (samples[m][0], m))
    peak = samples[peak_minute][0]
    below = [m for m in sorted(samples) if m > peak_minute and samples[m][0] * 100 < peak * COLLAPSE_PERCENT]
    return peak_minute, below[0] if below else max(samples)


def save_offset(key: str) -> int:
    m = re.search(r"-m(\d+)-", key)
    return int(m.group(1)) if m else 0


def parse_log(text: str, change: tuple[str, ...] = ()) -> dict:
    """The log's standard lines; with `change`, also the seconds of each player's lines that begin
    with one of those prefixes (the change's own lines, for rules 3 and 4)."""
    landings = []  # (second, attacker, defender)
    own: dict[int, list[int]] = {}
    foundings = 0
    gold: dict[int, list[tuple[int, int]]] = {}
    dry: dict[tuple[int, int, int, int], list[tuple[int, bool, bool]]] = {}
    for line in text.splitlines():
        m = LOG.match(line)
        if not m:
            continue
        player, second, rest = int(m.group(1)), int(m.group(2)), m.group(3)
        if change and rest.startswith(change):
            own.setdefault(player, []).append(second)
        if rest.startswith("decision found "):
            foundings += 1
        elif g := TRACK_GOLD.match(rest):
            gold.setdefault(player, []).append((second, int(g.group(1))))
        elif c := TRACK_CITY.match(rest):
            for item in c.group(3).split():
                ware, flows = item.split(":")
                parts = [int(x) for x in flows.split("/")]
                if len(parts) < 5:
                    continue  # a track line from before demand and stock were logged
                used, asked, stock = parts[2], parts[3], parts[4]
                key = (player, int(c.group(1)), int(c.group(2)), int(ware))
                dry.setdefault(key, []).append((second, asked > 0 and stock == 0, asked > 0 and used < asked))
        elif a := LANDING.search(rest):
            landings.append((second, player, int(a.group(1))))
    return {"landings": landings, "foundings": foundings, "gold": gold, "dry": dry, "change": own}


def dry_summary(dry: dict) -> dict:
    """Dry minutes, episodes of 10 minutes or more, covered episodes, and flow-deficit minutes."""
    minutes = episodes = covered = deficit = 0
    for samples in dry.values():
        run = 0
        for i, (_, is_dry, short) in enumerate(samples):
            deficit += TRACK_MINUTES * short
            if is_dry:
                run += 1
                minutes += TRACK_MINUTES
            if run and (not is_dry or i == len(samples) - 1):
                # One sample is ten minutes, so every run of dry samples is an episode of 10 min or more.
                episodes += 1
                covered += not is_dry
                run = 0
    return {"dry_minutes": minutes, "episodes": episodes, "covered": covered, "deficit_minutes": deficit}


def debt_summary(gold: dict) -> dict:
    negative = sum(1 for samples in gold.values() for _, g in samples if g < 0)
    players = sum(1 for samples in gold.values() if any(g < 0 for _, g in samples))
    return {"debt_minutes": negative * TRACK_MINUTES, "players_in_debt": players}


def read_run(folder: pathlib.Path, arm: str, key: str, change: tuple[str, ...] = ()) -> dict:
    run = parse_bench((folder / f"{arm}-{key}.txt").read_text(encoding="utf-8", errors="replace"))
    err = folder / f"{arm}-{key}.err"
    run["log"] = parse_log(err.read_text(encoding="utf-8", errors="replace"), change) if err.is_file() else None
    return run


def keys_of(folder: pathlib.Path, arm: str, ref: str) -> list[str]:
    keys = []
    for rc in folder.glob(f"{arm}-*.rc"):
        key = rc.name[len(arm) + 1 : -len(".rc")]
        if (folder / f"{ref}-{key}.rc").is_file() and (folder / f"{arm}-{key}.txt").is_file():
            keys.append(key)
    return sorted(keys)


def compare(a: list[float], b: list[float]) -> dict:
    d = mean(a) - mean(b) if a else 0.0
    se = se_of_difference(a, b)
    return {"arm": mean(a) if a else None, "ref": mean(b) if b else None, "difference": d, "se": se,
            "in_se": d / se if se else None}


def cause(slide: dict, has_change: bool, war_change: bool) -> str:
    """What an extra collapse is put down to by rules 3 and 4.

    change            the change's own lines of that player lie in its slide window: the change acts
                      for it (counts);
    intended conquest the same, with landings in the window, for a change that acts on war
                      (Merlin's rule of 2026-11-18: reported as intended, not counted under rules 2-4);
    conquest          landings by or against the player in the window and no line of the change;
    divergence        no line of the change in the window: the game only took another course;
    unknown           no change lines were named, so rule 3 cannot be shown (counts: "cannot be
                      shown" is not a pass).
    """
    if not has_change:
        return "unknown"
    if slide["change_lines"]:
        return "intended conquest" if war_change and slide["landings"] else "change"
    return "conquest" if slide["landings"] else "divergence"


COUNTED = ("change", "unknown", "no log")


def rules(sets: dict) -> dict:
    """Rules 1 to 4 of the gate (ki/informationsvertrag-inventar.md, Prüfablauf: extra collapses).

    1  inhabitants not worse than 2 standard errors of the difference, on every set;
    2  no run with two or more extra collapses that count (COUNTED);
    3  no extra collapse that counts;
    4  the same computer collapsing in a second phase of the same map counts only if it counts there.
    """
    out = {"1": [], "2": [], "3": [], "4": []}
    for name, s in sets.items():
        if not s.get("runs"):
            continue
        i = s["inhabitants"]
        if i["in_se"] is not None and i["in_se"] < -2:
            out["1"].append(f"{name} {i['in_se']:.2f} SE")
        counted = [w for w in s["slides"] if w["cause"] in COUNTED]
        per_key: dict[str, int] = {}
        for w in counted:
            per_key[w["key"]] = per_key.get(w["key"], 0) + 1
        out["2"] += [f"{name} {k}" for k, n in sorted(per_key.items()) if n >= 2]
        out["3"] += [f"{name} {w['key']} P{w['player']} ({w['cause']})" for w in counted]
        seen: dict[tuple[str, int], list[dict]] = {}
        for w in s["slides"]:
            seen.setdefault((w["key"].rsplit("-p", 1)[0], w["player"]), []).append(w)
        for (m, p), ws in sorted(seen.items()):
            ws.sort(key=lambda w: w["key"])
            if len(ws) >= 2 and any(w["cause"] in COUNTED for w in ws[1:]):
                out["4"].append(f"{name} {m} P{p} in {', '.join(w['key'].rsplit('-p', 1)[1] for w in ws)}")
    return {r: {"passes": not v, "why": v} for r, v in out.items()}


def report_set(folder: pathlib.Path, arm: str, ref: str, human: bool, change: tuple[str, ...] = (),
               war_change: bool = False) -> dict:
    keys = keys_of(folder, arm, ref)
    A = {k: read_run(folder, arm, k, change) for k in keys}
    B = {k: read_run(folder, ref, k) for k in keys}
    out: dict = {"runs": len(keys)}
    if not keys:
        return out
    seats = lambda run: [p for p in run["S"] if not (human and p == 0)]  # noqa: E731
    inh = lambda run: sum(end_of(run["S"][p])[0] for p in seats(run))  # noqa: E731
    gold = lambda run: sum(end_of(run["S"][p])[1] for p in seats(run))  # noqa: E731
    cities = lambda run: sum(end_of(run["S"][p])[2] for p in run["S"])  # noqa: E731
    gmap = lambda k: k.rsplit("-p", 1)[0]  # noqa: E731
    base = {g: mean([inh(B[k]) for k in keys if gmap(k) == g]) for g in {gmap(k) for k in keys}}
    norm = lambda run, k: inh(run) / base[gmap(k)] if base[gmap(k)] else 0.0  # noqa: E731
    out["inhabitants"] = compare([norm(A[k], k) for k in keys], [norm(B[k], k) for k in keys])
    cols = {k: ([p for p in seats(A[k]) if collapsed(A[k]["S"][p])], [p for p in seats(B[k]) if collapsed(B[k]["S"][p])])
            for k in keys}
    out["collapses"] = {"arm": sum(len(a) for a, _ in cols.values()), "ref": sum(len(b) for _, b in cols.values())}
    out["two_extra"] = [k for k in keys if len([p for p in cols[k][0] if p not in cols[k][1]]) >= 2]
    slides = []
    for k in keys:
        for p in [p for p in cols[k][0] if p not in cols[k][1]]:
            peak, below = window(A[k]["S"][p])
            entry = {"key": k, "player": p, "peak": peak, "below": below, "landings": None,
                     "change_lines": None, "cause": "no log"}
            log = A[k]["log"]
            if log is not None:
                lo, hi = (peak - 30 + save_offset(k)) * 60, (below + save_offset(k)) * 60
                entry["landings"] = sum(1 for s, att, dfn in log["landings"] if lo <= s <= hi and p in (att, dfn))
                entry["change_lines"] = sum(1 for s in log["change"].get(p, []) if lo <= s <= hi)
                entry["cause"] = cause(entry, bool(change), war_change)
            slides.append(entry)
    out["slides"] = slides
    for name, f in (("gold", gold), ("cities", cities)):
        a, b = [f(A[k]) for k in keys], [f(B[k]) for k in keys]
        out[name] = {"sum_arm": sum(a), "sum_ref": sum(b), **compare(a, b)}
    c1 = []
    lost = gained = counted = 0
    for k in keys:
        if 1 in A[k]["S"] and 1 in B[k]["S"]:
            x, y = end_of(A[k]["S"][1])[0], end_of(B[k]["S"][1])[0]
            c1.append({"key": k, "arm": x, "ref": y})
            if max(x, y) >= LOST_FLOOR:
                counted += 1
                lost += x * 100 < y * COLLAPSE_PERCENT
                gained += y * 100 < x * COLLAPSE_PERCENT
    out["computer1"] = {"runs": c1, "counted": counted, "lost": lost, "gained": gained}
    has_lines = lambda log: bool(log["foundings"] or log["gold"])  # noqa: E731
    if all(A[k]["log"] is not None and B[k]["log"] is not None for k in keys) and any(
            has_lines(A[k]["log"]) or has_lines(B[k]["log"]) for k in keys):
        def logged(runs: dict) -> dict:
            total = {"foundings": 0, "debt_minutes": 0, "players_in_debt": 0,
                     "dry_minutes": 0, "episodes": 0, "covered": 0, "deficit_minutes": 0}
            for k in keys:
                log = runs[k]["log"]
                parts = {"foundings": log["foundings"], **debt_summary(log["gold"]), **dry_summary(log["dry"])}
                for f in total:
                    total[f] += parts[f]
            return total
        out["log"] = {"arm": logged(A), "ref": logged(B)}
    return out


def fmt(x, digits=3) -> str:
    if x is None:
        return "n/a"
    return f"{x:.{digits}f}" if isinstance(x, float) else str(x)


def print_report(rep: dict) -> None:
    print(f"arm {rep['arm']} vs ref {rep['ref']}")
    for name, s in rep["sets"].items():
        print(f"\n[{name}] {s['runs']} keys")
        if not s["runs"]:
            continue
        i = s["inhabitants"]
        print(f"  inhabitants {fmt(i['difference'])} (SE {fmt(i['se'])}, {fmt(i['in_se'], 2)} SE)")
        c = s["collapses"]
        print(f"  collapses {c['arm']} vs {c['ref']}; keys with 2+ extra: {', '.join(s['two_extra']) or 'none'}")
        for w in s["slides"]:
            print(f"    slide {w['key']} P{w['player']} peak {w['peak']} below {w['below']} landings {fmt(w['landings'])}"
                  f" change lines {fmt(w['change_lines'])}: {w['cause']}")
        for f in ("gold", "cities"):
            g = s[f]
            print(f"  {f} {g['sum_arm']} vs {g['sum_ref']} (per run {fmt(g['arm'], 1)} vs {fmt(g['ref'], 1)}, "
                  f"SE {fmt(g['se'], 1)}, {fmt(g['in_se'], 2)} SE)")
        c1 = s["computer1"]
        print(f"  computer 1: lost {c1['lost']}, gained {c1['gained']} of {c1['counted']}")
        if "log" in s:
            a, b = s["log"]["arm"], s["log"]["ref"]
            print("  " + ", ".join(f"{f} {a[f]} vs {b[f]}" for f in a))
        else:
            print("  log: none with main's decision and track lines (foundings, debt, dry not counted)")


def main(argv: list[str]) -> int:
    args = list(argv)
    opts = {"--arm": None, "--ref": None, "--sets": "l2,l3,x3", "--human-sets": "l2", "--json": None,
            "--change": "", "--war-change": None}
    if "--war-change" in args:
        args.remove("--war-change")
        opts["--war-change"] = True
    try:
        for o in list(opts):
            if o in args:
                i = args.index(o)
                opts[o] = args[i + 1]
                del args[i : i + 2]
    except IndexError:
        print(__doc__, file=sys.stderr)
        return 2
    if len(args) != 1 or opts["--arm"] is None or opts["--ref"] is None:
        print(__doc__, file=sys.stderr)
        return 2
    rundir = pathlib.Path(args[0])
    human = set(opts["--human-sets"].split(",")) if opts["--human-sets"] else set()
    # The change's own log lines, as the text after "[ai P Ts] " begins (rule 3); one or more,
    # separated by "|", so that no change's names are built in.
    change = tuple(x for x in opts["--change"].split("|") if x)
    rep = {"arm": opts["--arm"], "ref": opts["--ref"], "sets": {}}
    for name in opts["--sets"].split(","):
        rep["sets"][name] = report_set(rundir / name, opts["--arm"], opts["--ref"], name in human, change,
                                       bool(opts["--war-change"]))
    rep["rules"] = rules(rep["sets"])
    print_report(rep)
    print("\nrules: " + "; ".join(f"{r} {'passes' if v['passes'] else 'fails: ' + ', '.join(v['why'])}"
                                   for r, v in rep["rules"].items()))
    if opts["--json"]:
        pathlib.Path(opts["--json"]).write_text(json.dumps(rep, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
