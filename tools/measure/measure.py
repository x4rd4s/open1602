#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Measurements of the computer players: a plan, its runs and the verdict (docs/ai-measurement.md).

Usage:
  python3 tools/measure/measure.py check PLAN.toml
      Checks the plan against the rules of its stage and prints its hash.
  python3 tools/measure/measure.py jobs PLAN.toml
      One line per run: arm, phase, game, then the arguments for `o1602-tool bench` (run.sh).
  python3 tools/measure/measure.py collect PLAN.toml RUNDIR
      Reads the runs' output in RUNDIR (ARM/PHASE/GAME.txt, and ARM/PHASE/GAME.metrics.json written
      by the plan's evaluation) and writes RUNDIR/results.jsonl, each record bound to the plan's hash.
  python3 tools/measure/measure.py verdict PLAN.toml RESULTS.jsonl [--report OUT.json]
      The verdict by the plan's stage; refuses results of another plan or with runs missing.

A game may start from a save of its own (`save` and its `sha256`), and with `human_seat` a seat of that
save is a human who gives no commands (o1602-tool bench --human-seat); a final run starts with the
world, except a game with a human seat, which starts from the save the computers made.
Every game names its difficulty level (0 to 3): below the hardest the improved computers see only
what a player sees, so a plan without it is refused.
A plan names its stage:
  mechanism  shows on at most 3 games and at most 400 minutes that a change does what it should;
  metric     the decision metrics on short runs (at most 400 minutes, one phase);
  final      several changes together, at full length, at least 4 phases on standard games and 8 on
             long ones; each change names the hash of the passed mechanism report it rests on.
Every plan names its decision metrics; the inhabitants count only as a check that nothing got worse.
A metric with `decides = false` is judged and reported but does not decide. A run whose metric has
no value (a mean over no events) writes it as "n/a": the verdict leaves that run out of that metric
and reports how many runs it left out per arm.
Minutes are those run, after the start save when the plan or the game has one.
Exit code 0 when all is well, 1 when the plan or the results break a rule, 2 on wrong use.
"""
from __future__ import annotations

import hashlib
import json
import math
import pathlib
import re
import sys
import tomllib

STAGES = ("mechanism", "metric", "final")
SETS = ("standard", "long")
# The game's difficulty levels, 0 (easiest) to 3 (hardest): NewGameOptions::kDifficulties. The level
# changes what the improved computers may see, so every game names it.
LEVELS = 4
# The rules of the stages (the review's process of 13 November).
SHORT_MINUTES = 400       # mechanism and metric runs end where the effect lives
MECHANISM_GAMES = 3
FINAL_PHASES = {"standard": 4, "long": 8}
FULL_MINUTES = {"standard": 600, "long": 1200}
# A player collapsed when it ends below this share of its peak (in percent): the definition of the
# noise band and of every run measured before these tools, so that the rates stay comparable.
COLLAPSE_PERCENT = 70
# A difference counts beyond this many standard errors.
SE_BAR = 2.0
# A metric's value in a run that had nothing to measure (a mean over no events).
NO_VALUE = "n/a"


class Refused(Exception):
    """A plan or results that break a rule."""


def file_hash(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load_plan(path: pathlib.Path) -> dict:
    try:
        with path.open("rb") as f:
            plan = tomllib.load(f)
    except (OSError, tomllib.TOMLDecodeError) as e:
        raise Refused(f"{path}: {e}") from e
    plan["_hash"] = file_hash(path)
    plan["_dir"] = path.parent
    return plan


def check_plan(plan: dict) -> list[str]:
    """Every rule the plan breaks."""
    errors: list[str] = []
    stage = plan.get("stage")
    if stage not in STAGES:
        errors.append(f"stage must be one of {', '.join(STAGES)}")
    arms = plan.get("arm", [])
    names = [a.get("name") for a in arms]
    if len(arms) < 2 or len(set(names)) != len(names) or not all(names):
        errors.append("at least two arms with distinct names")
    if plan.get("base") not in names:
        errors.append("base must name one of the arms")
    games = plan.get("game", [])
    if not games:
        errors.append("no games")
    game_names = [g.get("name") for g in games]
    if len(set(game_names)) != len(game_names):
        errors.append("game names must be distinct")
    for g in games:
        if g.get("set") not in SETS:
            errors.append(f"game {g.get('name')}: set must be standard or long")
        for key in ("players", "seed", "minutes"):
            if not isinstance(g.get(key), int) or g[key] <= 0:
                errors.append(f"game {g.get('name')}: {key} must be a positive number")
        level = g.get("level")
        if not isinstance(level, int) or isinstance(level, bool) or not 0 <= level < LEVELS:
            errors.append(f"game {g.get('name')}: level must be named, 0 to {LEVELS - 1}")
    phases = plan.get("phases", {})
    used_sets = {g.get("set") for g in games}
    for s in used_sets & set(SETS):
        if not isinstance(phases.get(s), int) or phases[s] < 1:
            errors.append(f"phases.{s} must be at least 1")
    metrics = plan.get("metric", [])
    if not metrics:
        errors.append("no decision metrics: name what the change must move")
    elif not any(m.get("decides", True) for m in metrics):
        errors.append("no decision metrics: every metric has decides = false")
    for m in metrics:
        if not m.get("name") or m.get("better") not in ("higher", "lower"):
            errors.append(f"metric {m.get('name')}: needs a name and better = higher or lower")
        if not isinstance(m.get("decides", True), bool):
            errors.append(f"metric {m.get('name')}: decides is true or false")
        if m.get("name") == "inhabitants":
            errors.append("inhabitants are no decision metric: they are checked as not worse anyway")
    for g in games:
        seat = g.get("human_seat")
        if seat is not None and (not isinstance(seat, int) or isinstance(seat, bool) or seat < 0):
            errors.append(f"game {g.get('name')}: human_seat is a player number")
        if g.get("save") is None:
            if seat is not None:
                errors.append(f"game {g.get('name')}: a human seat needs the game's own start save")
            continue
        if plan.get("start") is not None:
            errors.append(f"game {g.get('name')}: a start save for the plan or for each game, not both")
        save = plan["_dir"] / str(g["save"])
        if not save.is_file():
            errors.append(f"game {g.get('name')}: start save {g['save']} not found")
        elif file_hash(save) != g.get("sha256"):
            errors.append(f"game {g.get('name')}: start save {g['save']}: its hash is not the plan's")
    start = plan.get("start")
    if start is not None:
        save = plan["_dir"] / str(start.get("save", ""))
        if not save.is_file():
            errors.append(f"start save {start.get('save')} not found")
        elif file_hash(save) != start.get("sha256"):
            errors.append(f"start save {start.get('save')}: its hash is not the plan's")
    if stage in ("mechanism", "metric"):
        for g in games:
            if isinstance(g.get("minutes"), int) and g["minutes"] > SHORT_MINUTES:
                errors.append(f"game {g.get('name')}: a {stage} run ends by minute {SHORT_MINUTES}")
        for s in used_sets & set(SETS):
            if phases.get(s, 1) != 1:
                errors.append(f"a {stage} run has one phase; more are for the final stage")
        if plan.get("change"):
            errors.append("changes with mechanism reports belong to a final plan")
    if stage == "mechanism" and len(games) > MECHANISM_GAMES:
        errors.append(f"a mechanism run has at most {MECHANISM_GAMES} games")
    if stage == "final":
        for s in used_sets & set(SETS):
            if isinstance(phases.get(s), int) and phases[s] < FINAL_PHASES[s]:
                errors.append(f"a final run has at least {FINAL_PHASES[s]} phases on {s} games")
        if start is not None:
            errors.append("a final run starts with the world, not from a save")
        for g in games:
            if g.get("save") is not None and g.get("human_seat") is None:
                errors.append(f"game {g.get('name')}: a final run starts with the world, or from a save for a human seat")
        for g in games:
            if g.get("set") in SETS and g.get("minutes") != FULL_MINUTES[g["set"]]:
                errors.append(f"game {g.get('name')}: a final run is {FULL_MINUTES[g['set']]} minutes")
        changes = plan.get("change", [])
        if not changes:
            errors.append("a final plan names the changes it gates")
        for c in changes:
            errors.extend(check_mechanism_report(plan, c))
    return errors


def check_mechanism_report(plan: dict, change: dict) -> list[str]:
    name = change.get("name", "?")
    report = plan["_dir"] / str(change.get("report", ""))
    if not report.is_file():
        return [f"change {name}: mechanism report {change.get('report')} not found"]
    if file_hash(report) != change.get("sha256"):
        return [f"change {name}: the mechanism report's hash is not the one named"]
    try:
        data = json.loads(report.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return [f"change {name}: the mechanism report is no report"]
    if data.get("stage") != "mechanism" or data.get("passed") is not True:
        return [f"change {name}: its report is no passed mechanism report"]
    return []


def require(plan: dict) -> None:
    errors = check_plan(plan)
    if errors:
        raise Refused("; ".join(errors))


def jobs(plan: dict) -> list[tuple[str, int, dict, list[str]]]:
    out = []
    for arm in plan["arm"]:
        for g in plan["game"]:
            for phase in range(plan["phases"][g["set"]]):
                args = []
                if plan.get("start"):
                    args.append(str(plan["_dir"] / plan["start"]["save"]))
                elif g.get("save"):
                    args.append(str(plan["_dir"] / g["save"]))
                args += ["--level", str(g["level"]), "--minutes", str(g["minutes"]),
                         "--period", str(g.get("period", 10)), "--per-player", "--ai", "open1602",
                         "--players", str(g["players"]), "--seed", str(g["seed"]), "--ai-phase", str(phase)]
                if g.get("world"):
                    args += ["--world", g["world"]]
                if g.get("human_seat") is not None:
                    args += ["--human-seat", str(g["human_seat"])]
                out.append((arm["name"], phase, g, args))
    return out


WORLD_LINE = re.compile(r"^\s*(\d+)\s+[\d.]+\s+\d+\s+\d+\s+\d+\s+(\d+)\s+(\d+)\s")
PLAYER_LINE = re.compile(r"^\s+player (\d+): \d+ cities, (\d+) inhabitants")


def parse_run(text: str) -> dict:
    """The last minute's inhabitants, and each player's peak and end, from bench --per-player."""
    inhabitants = None
    peak: dict[int, int] = {}
    end: dict[int, int] = {}
    for line in text.splitlines():
        if m := WORLD_LINE.match(line):
            inhabitants = int(m.group(3))
        elif m := PLAYER_LINE.match(line):
            player, n = int(m.group(1)), int(m.group(2))
            peak[player] = max(peak.get(player, 0), n)
            end[player] = n
    if inhabitants is None:
        raise Refused("a run without a minute line (did it finish?)")
    settled = [p for p in peak if peak[p] > 0]
    collapses = sum(1 for p in settled if end[p] * 100 < peak[p] * COLLAPSE_PERCENT)
    return {"inhabitants": inhabitants, "collapses": collapses, "players": len(settled)}


def collect(plan: dict, rundir: pathlib.Path) -> pathlib.Path:
    require(plan)
    records = []
    for arm, phase, g, _ in jobs(plan):
        base = rundir / arm / str(phase) / g["name"]
        out = base.with_suffix(".txt")
        if not out.is_file():
            raise Refused(f"run missing: {out}")
        record = {"plan": plan["_hash"], "arm": arm, "phase": phase, "game": g["name"], "set": g["set"]}
        record.update(parse_run(out.read_text(encoding="utf-8", errors="replace")))
        metrics_file = base.with_suffix(".metrics.json")
        record["metrics"] = json.loads(metrics_file.read_text(encoding="utf-8")) if metrics_file.is_file() else {}
        records.append(record)
    path = rundir / "results.jsonl"
    path.write_text("".join(json.dumps(r, sort_keys=True) + "\n" for r in records), encoding="utf-8")
    return path


def mean(xs: list[float]) -> float:
    return sum(xs) / len(xs)


def se_of_difference(a: list[float], b: list[float]) -> float | None:
    """The standard error of mean(a) - mean(b) from the spread of each; None with one value."""
    if len(a) < 2 or len(b) < 2:
        return None

    def var(xs: list[float]) -> float:
        m = mean(xs)
        return sum((x - m) ** 2 for x in xs) / (len(xs) - 1)
    return math.sqrt(var(a) / len(a) + var(b) / len(b))


def judge(arm: list[float], base: list[float], better: str) -> dict:
    """gain, worse or none by the 2-SE rule (with one phase: the direction only)."""
    d = mean(arm) - mean(base)
    if better == "lower":
        d = -d
    se = se_of_difference(arm, base)
    out = {"arm": mean(arm), "base": mean(base), "difference": d, "se": se}
    if se is None:
        out["verdict"] = "better" if d > 0 else "worse" if d < 0 else "same"
    elif d > SE_BAR * se:
        out["verdict"] = "gain"
    elif d < -SE_BAR * se:
        out["verdict"] = "worse"
    else:
        out["verdict"] = "no measurable effect"
    return out


def verdict(plan: dict, results: pathlib.Path) -> dict:
    require(plan)
    records = [json.loads(line) for line in results.read_text(encoding="utf-8").splitlines() if line.strip()]
    for r in records:
        if r.get("plan") != plan["_hash"]:
            raise Refused("results of another plan, or the plan changed after the runs")
    have = {(r["arm"], r["phase"], r["game"]): r for r in records}
    for arm, phase, g, _ in jobs(plan):
        if (arm, phase, g["name"]) not in have:
            raise Refused(f"run missing: {arm} phase {phase} {g['name']}")
    metric_names = [m["name"] for m in plan["metric"]]
    for r in records:
        missing = [m for m in metric_names if m not in r.get("metrics", {})]
        if missing:
            raise Refused(f"{r['arm']} phase {r['phase']} {r['game']}: no value for {', '.join(missing)}")
    base = plan["base"]
    report: dict = {"plan": plan["_hash"], "stage": plan["stage"], "arms": {}}
    passed = True
    for arm in (a["name"] for a in plan["arm"] if a["name"] != base):
        out: dict = {"sets": {}, "metrics": {}}
        for s in SETS:
            games = [g["name"] for g in plan["game"] if g["set"] == s]
            if not games:
                continue
            n = plan["phases"][s]

            def per_phase(a: str, key, games=games, n=n) -> list[float]:
                return [sum(key(have[(a, ph, g)]) for g in games) for ph in range(n)]
            inh = judge(per_phase(arm, lambda r: r["inhabitants"]), per_phase(base, lambda r: r["inhabitants"]),
                        "higher")
            # The inhabitants only guard against harm: a gain there decides nothing.
            inh["passes"] = inh["verdict"] != "worse"
            rate = judge([c / max(p, 1) for c, p in zip(per_phase(arm, lambda r: r["collapses"]),
                                                        per_phase(arm, lambda r: r["players"]))],
                         [c / max(p, 1) for c, p in zip(per_phase(base, lambda r: r["collapses"]),
                                                        per_phase(base, lambda r: r["players"]))], "lower")
            rate["passes"] = rate["verdict"] != "worse"
            out["sets"][s] = {"inhabitants": inh, "collapse_rate": rate}
            passed = passed and inh["passes"] and rate["passes"]
        for m in plan["metric"]:
            phases = max(plan["phases"].values())

            def values(a: str, name=m["name"], phases=phases) -> tuple[list[float], int]:
                """The phases' means over the runs with a value, and how many runs had none."""
                vals = []
                left_out = 0
                for ph in range(phases):
                    runs = [have[(a, ph, g["name"])]["metrics"][name] for g in plan["game"]
                            if ph < plan["phases"][g["set"]]]
                    xs = [x for x in runs if x != NO_VALUE]
                    left_out += len(runs) - len(xs)
                    if xs:
                        vals.append(mean(xs))
                return vals, left_out
            arm_values, arm_left = values(arm)
            base_values, base_left = values(base)
            if not arm_values or not base_values:
                raise Refused(f"metric {m['name']}: no run of {arm if not arm_values else base} has a value")
            j = judge(arm_values, base_values, m["better"])
            j["runs_without_value"] = {arm: arm_left, base: base_left}
            # A mechanism moves its metrics the right way; a final run must not make them worse.
            j["passes"] = j["verdict"] in (("better", "gain") if plan["stage"] == "mechanism"
                                           else ("gain", "no measurable effect", "better", "same"))
            j["decides"] = m.get("decides", True)
            out["metrics"][m["name"]] = j
            passed = passed and (j["passes"] or not j["decides"])
        report["arms"][arm] = out
    report["passed"] = passed
    # What the report can be taken for: with one phase it shows a direction, not a measured gain.
    n = min(plan["phases"][g["set"]] for g in plan["game"])
    report["evidence"] = (f"{plan['stage']}, n={n}, no significance" if n < 2
                          else f"{plan['stage']}, n={n}, {SE_BAR:g} standard errors")
    return report


def main(argv: list[str]) -> int:
    if len(argv) < 3 or argv[1] not in ("check", "jobs", "collect", "verdict"):
        print(__doc__, file=sys.stderr)
        return 2
    try:
        plan = load_plan(pathlib.Path(argv[2]))
        if argv[1] == "check":
            require(plan)
            print(f"plan {argv[2]}: stage {plan['stage']}, hash {plan['_hash']}")
        elif argv[1] == "jobs":
            require(plan)
            for arm, phase, g, args in jobs(plan):
                print("\t".join([arm, str(phase), g["name"], *args]))
        elif argv[1] == "collect":
            if len(argv) != 4:
                return 2
            print(collect(plan, pathlib.Path(argv[3])))
        else:
            if len(argv) not in (4, 6) or (len(argv) == 6 and argv[4] != "--report"):
                return 2
            report = verdict(plan, pathlib.Path(argv[3]))
            text = json.dumps(report, indent=1, sort_keys=True) + "\n"
            if len(argv) == 6:
                out = pathlib.Path(argv[5])
                out.write_text(text, encoding="utf-8")
                print(f"report {out}: {'passed' if report['passed'] else 'failed'}, hash {file_hash(out)}")
            else:
                sys.stdout.write(text)
            return 0 if report["passed"] else 1
    except Refused as e:
        print(f"refused: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
