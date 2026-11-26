#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks that a change to the computer players reads the other players' world through ai::View.

Usage: python3 tools/check_ai_view.py [BASE]   (BASE: the branch merged into, default origin/main)

The computer players see what a human in their seat sees (docs/ai-measurement.md, the information
contract). ai::View (engine/sim/src/ai/ai_view.{hpp,cpp}) is the one place that answers what that
is. This check looks at what a change adds against BASE in the decision code (engine/sim/src/ai/,
ai_view.* aside) and fails on a read of the world's objects that the change brings in: a member of
OBJECTS that the added lines call more often than the removed lines (moved code brings none).

The rules of the game (RULES: balance, settings, goods table, factions, map size) are public and
pass. A read of the objects that is the computer's own, a rule or bookkeeping gets an entry in
tools/ai_view_allowlist.txt:
    MEMBER | PATH | CLASS: WHY
MEMBER is the world's member as the code calls it (cities, ships, ...), PATH the file (a glob),
CLASS one of eigen, gesehen, Regel, Buchung (the classes of the P1.1 list). verborgen is never
taken: what a human does not see goes through View::omniscient_level(). An entry without its
reason is not taken. A single line can carry its class in a comment instead, for a read that only
the log makes or that is the computer's own:
    for (const City& c : m_world.cities()) {  // ai-view: Buchung: the log's census comparison
Each hit is printed as path:line: reason. Exit code 1 when there are any.
"""
from __future__ import annotations

import collections
import fnmatch
import pathlib
import re
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from check_ai_constants import changes, code_of  # noqa: E402

PATHS = ("engine/sim/src/ai/",)
EXEMPT = ("engine/sim/src/ai/ai_view.cpp", "engine/sim/src/ai/ai_view.hpp")
ALLOWLIST = "tools/ai_view_allowlist.txt"
CLASSES = ("eigen", "gesehen", "Regel", "Buchung")

# The world's objects: cities, islands, units, ships, buildings, players' state.
OBJECTS = ("cities", "city", "islands", "island", "soldiers", "soldier", "ships", "ship", "houses", "house",
           "productions", "production", "towers", "missions", "pending_hits", "disasters", "player", "players")
# Public rules and the map's frame.
RULES = ("balance", "game_settings", "goods", "behaviour", "acts_as", "cost_buildings", "width", "height",
         "faction", "island_index", "first_of", "diplomacy")
READ = re.compile(r"\bm_world\.(\w+)\s*\(")
MARK = re.compile(rf"//.*\bai-view:\s*({'|'.join(CLASSES)}):\s*\S")


def diff(base: str) -> str:
    out = subprocess.run(["git", "diff", "-U0", "--no-color", f"{base}...HEAD", "--", *PATHS],
                         capture_output=True, text=True, check=True)
    return out.stdout


def load_allowlist(root: pathlib.Path) -> list[tuple[str, str]]:
    entries = []
    path = root / ALLOWLIST
    if not path.is_file():
        return entries
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) == 3 and parts[0] and parts[1] and re.match(rf"({'|'.join(CLASSES)}):\s*\S", parts[2]):
            entries.append((parts[0], parts[1]))
    return entries


def allowed(entries: list[tuple[str, str]], member: str, path: str) -> bool:
    return member in RULES or any(m == member and fnmatch.fnmatch(path, glob) for m, glob in entries)


def reads(body: str) -> list[str]:
    """The world's members a line calls, none for a line that carries its class (MARK)."""
    return [] if MARK.search(body) else READ.findall(code_of(body))


def check(text: str, entries: list[tuple[str, str]]) -> list[str]:
    rows = [r for r in changes(text) if r[0].endswith((".cpp", ".hpp")) and r[0] not in EXEMPT]
    added: collections.Counter = collections.Counter()
    removed: collections.Counter = collections.Counter()
    for path, _, sign, body in rows:
        (added if sign == "+" else removed).update(m for m in reads(body) if not allowed(entries, m, path))
    budget = added - removed
    places: dict[str, list[str]] = collections.defaultdict(list)
    for path, line, sign, body in rows:
        if sign == "+":
            for m in reads(body):
                if budget.get(m, 0) > 0 and not allowed(entries, m, path):
                    places[m].append(f"{path}:{line}")
    unknown = sorted(set(budget) - set(OBJECTS))
    hits = [f"{where[0]}: new read of the world's {m}: {budget[m]} more than before, added at {', '.join(where)}"
            for m, where in sorted(places.items())]
    hits += [f"{places[m][0]}: m_world.{m} is neither an object nor a rule of this check; classify it here"
             for m in unknown if places.get(m)]
    return hits


def main(argv: list[str]) -> int:
    base = argv[1] if len(argv) > 1 else "origin/main"
    root = pathlib.Path(subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True,
                                       check=True).stdout.strip())
    hits = check(diff(base), load_allowlist(root))
    for h in hits:
        print(h)
    if hits:
        print(f"{len(hits)} world read(s) past ai::View in the computer players; read them through ai::View "
              f"or list them with their class in {ALLOWLIST}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
