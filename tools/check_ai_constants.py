#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Checks that a change to the computer players brings no new constant (docs/ai-measurement.md).

Usage: python3 tools/check_ai_constants.py [BASE]   (BASE: the branch merged into, default origin/main)

Looks at what the change adds against BASE in the computer players' code (engine/sim/src/ai/), in
the balance values (engine/sim/include/o1602/sim/balance.hpp) and in the balance files
(data/balance/), and fails on
- a number the change brings in: one the added lines hold more often than the removed lines
  (moved code brings none), 0 and 1 aside;
- a new named value: a constexpr, a balance field or a key of a balance file the change adds.
Each hit is printed as path:line: reason. Exit code 1 when there are any.

A value that the change really needs, because it follows from others (a rule of the game, a
measured spread, a size of a table), gets an entry in tools/ai_constants_allowlist.txt:
    VALUE | PATH | derived from: WHAT IT FOLLOWS FROM
VALUE is the number or the name as the code writes it, PATH the file (a glob). An entry without
its derivation is not taken.
"""
from __future__ import annotations

import collections
import fnmatch
import pathlib
import re
import subprocess
import sys

PATHS = ("engine/sim/src/ai/", "engine/sim/include/o1602/sim/balance.hpp", "data/balance/")
ALLOWLIST = "tools/ai_constants_allowlist.txt"

NUMBER = re.compile(r"(?<![\w.])(0[xX][0-9a-fA-F']+|\d[\d']*(?:\.\d+)?(?:[eE][+-]?\d+)?)[uUlLfF]*(?![\w.])")
CONSTEXPR = re.compile(r"\bconstexpr\b[^=;(]*?\b(k[A-Z]\w*)\s*(?:=|\{)")
FIELD = re.compile(r"^\s*(?:std::)?[\w:<>]+\s+(\w+)\s*=\s*[^;]+;")
TOML_KEY = re.compile(r"^\s*([A-Za-z_][\w-]*)\s*=")


def code_of(line: str) -> str:
    """The line without its comment and string literals."""
    line = re.sub(r'"(?:\\.|[^"\\])*"', '""', line)
    line = re.sub(r"'(?:\\.|[^'\\])'", "''", line)
    return line.split("//", 1)[0]


def number_value(text: str) -> str:
    t = text.replace("'", "")
    # A suffix (u, l, f) ends the number; a hexadecimal number has no f suffix, its F is a digit.
    t = t.rstrip("uUlL") if t[:2].lower() == "0x" else t.rstrip("uUlLfF")
    try:
        return str(int(t, 0)) if not re.search(r"[.eE]", t) or t.lower().startswith("0x") else repr(float(t))
    except ValueError:
        return t


def diff(base: str) -> str:
    out = subprocess.run(["git", "diff", "-U0", "--no-color", f"{base}...HEAD", "--", *PATHS],
                         capture_output=True, text=True, check=True)
    return out.stdout


def changes(text: str) -> list[tuple[str, int, str, str]]:
    """(path, line in the new file, '+' or '-', text) for every changed line."""
    rows = []
    path = ""
    new_line = 0
    in_comment = False
    for raw in text.splitlines():
        if raw.startswith("+++ "):
            path = raw[6:] if raw.startswith("+++ b/") else ""
            continue
        if raw.startswith("--- ") or raw.startswith("diff "):
            continue
        if m := re.match(r"@@ -\d+(?:,\d+)? \+(\d+)", raw):
            new_line = int(m.group(1))
            in_comment = False
            continue
        if not raw or raw[0] not in "+-" or not path:
            continue
        sign, body = raw[0], raw[1:]
        # Block comments of the C++ files (a line inside one, or one that starts one).
        if path.endswith((".cpp", ".hpp")):
            stripped = body.strip()
            if in_comment or stripped.startswith("/*") or stripped.startswith("*"):
                in_comment = not stripped.endswith("*/") and (in_comment or stripped.startswith("/*"))
                if sign == "+":
                    new_line += 1
                continue
        elif path.endswith(".toml"):
            body = body.split("#", 1)[0]
        rows.append((path, new_line, sign, body))
        if sign == "+":
            new_line += 1
    return rows


def load_allowlist(root: pathlib.Path) -> list[tuple[str, str]]:
    entries = []
    path = root / ALLOWLIST
    if not path.is_file():
        return entries
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) == 3 and parts[0] and parts[1] and re.match(r"derived from:\s*\S", parts[2]):
            entries.append((parts[0], parts[1]))
    return entries


def allowed(entries: list[tuple[str, str]], value: str, path: str) -> bool:
    return any((v == value or number_value(v) == value) and fnmatch.fnmatch(path, glob) for v, glob in entries)


def check(text: str, entries: list[tuple[str, str]]) -> list[str]:
    rows = changes(text)
    added = collections.Counter()
    removed = collections.Counter()
    names_removed = set()
    for path, _, sign, body in rows:
        code = code_of(body) if not path.endswith(".toml") else body
        values = [number_value(n) for n in NUMBER.findall(code)]
        (added if sign == "+" else removed).update(v for v in values if v not in ("0", "1", "0.0", "1.0"))
        if sign == "-":
            for rx in (CONSTEXPR, FIELD, TOML_KEY):
                names_removed.update(m.group(1) for m in rx.finditer(code) if rx is not FIELD or "balance" in path)
    budget = added - removed
    hits = []
    # Where a number the change brings in stands: every added line that holds it, since moved code
    # leaves no telling which of them is the new one.
    places: dict[str, list[str]] = collections.defaultdict(list)
    for path, line, sign, body in rows:
        if sign != "+":
            continue
        code = code_of(body) if not path.endswith(".toml") else body
        for n in NUMBER.findall(code):
            v = number_value(n)
            if budget.get(v, 0) > 0 and not allowed(entries, n, path):
                places[v].append(f"{path}:{line}")
        checks = [(CONSTEXPR, "new named constant")]
        if path.endswith("balance.hpp"):
            checks.append((FIELD, "new balance value"))
        if path.endswith(".toml"):
            checks.append((TOML_KEY, "new balance key"))
        for rx, why in checks:
            for m in rx.finditer(code):
                name = m.group(1)
                if name not in names_removed and not allowed(entries, name, path):
                    hits.append(f"{path}:{line}: {why} {name}")
    for v, where in sorted(places.items()):
        hits.append(f"{where[0]}: new number {v}: {budget[v]} more than before, added at {', '.join(where)}")
    return hits


def main(argv: list[str]) -> int:
    base = argv[1] if len(argv) > 1 else "origin/main"
    root = pathlib.Path(subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True,
                                       check=True).stdout.strip())
    hits = check(diff(base), load_allowlist(root))
    for h in hits:
        print(h)
    if hits:
        print(f"{len(hits)} new constant(s) in the computer players; derive them or list them in {ALLOWLIST}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
