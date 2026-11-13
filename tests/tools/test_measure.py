# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests of tools/measure/measure.py and tools/check_ai_constants.py (run by CI's checks job)."""
import hashlib
import json
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "measure"))
sys.path.insert(0, str(ROOT / "tools"))
import check_ai_constants  # noqa: E402
import measure  # noqa: E402

GAME = '[[game]]\nname = "{name}"\nset = "{set}"\nplayers = {players}\nseed = {seed}\nminutes = {minutes}\n'


def plan_text(stage="mechanism", games=None, phases='standard = 1', metrics=True, extra=""):
    games = games if games is not None else [dict(name="g", set="standard", players=4, seed=1, minutes=400)]
    text = f'stage = "{stage}"\nbase = "main"\n[[arm]]\nname = "main"\n[[arm]]\nname = "new"\n'
    text += f"[phases]\n{phases}\n"
    for g in games:
        text += GAME.format(**g)
    if metrics:
        text += '[[metric]]\nname = "food"\nbetter = "higher"\n'
    return text + extra


class PlanTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.dir.name)

    def tearDown(self):
        self.dir.cleanup()

    def plan(self, text):
        path = self.root / "plan.toml"
        path.write_text(text, encoding="utf-8")
        return measure.load_plan(path)

    def test_a_short_mechanism_plan_passes(self):
        self.assertEqual(measure.check_plan(self.plan(plan_text())), [])

    def test_a_mechanism_run_has_at_most_three_games_and_400_minutes(self):
        games = [dict(name=f"g{i}", set="standard", players=4, seed=i, minutes=400) for i in range(4)]
        self.assertTrue(measure.check_plan(self.plan(plan_text(games=games))))
        long = [dict(name="g", set="long", players=12, seed=1, minutes=1200)]
        self.assertTrue(measure.check_plan(self.plan(plan_text(games=long, phases="long = 1"))))

    def test_only_a_final_run_has_more_phases(self):
        self.assertTrue(measure.check_plan(self.plan(plan_text(stage="metric", phases="standard = 4"))))

    def test_every_plan_names_decision_metrics_and_not_the_inhabitants(self):
        self.assertTrue(measure.check_plan(self.plan(plan_text(metrics=False))))
        bad = plan_text(metrics=False) + '[[metric]]\nname = "inhabitants"\nbetter = "higher"\n'
        self.assertTrue(measure.check_plan(self.plan(bad)))

    def test_a_final_plan_needs_its_phases_lengths_and_passed_mechanism_reports(self):
        games = [dict(name="s", set="standard", players=4, seed=1, minutes=600),
                 dict(name="l", set="long", players=12, seed=2, minutes=1200)]
        report = self.root / "offroad.json"
        report.write_text(json.dumps({"stage": "mechanism", "passed": True}), encoding="utf-8")
        digest = hashlib.sha256(report.read_bytes()).hexdigest()
        change = f'[[change]]\nname = "offroad"\nreport = "offroad.json"\nsha256 = "{digest}"\n'
        good = plan_text(stage="final", games=games, phases="standard = 4\nlong = 8", extra=change)
        self.assertEqual(measure.check_plan(self.plan(good)), [])
        few = plan_text(stage="final", games=games, phases="standard = 4\nlong = 4", extra=change)
        self.assertTrue(measure.check_plan(self.plan(few)))
        self.assertTrue(measure.check_plan(self.plan(plan_text(stage="final", games=games,
                                                               phases="standard = 4\nlong = 8"))))
        wrong = change.replace(digest, "0" * 64)
        self.assertTrue(measure.check_plan(self.plan(plan_text(stage="final", games=games,
                                                               phases="standard = 4\nlong = 8", extra=wrong))))
        report.write_text(json.dumps({"stage": "mechanism", "passed": False}), encoding="utf-8")
        failed = change.replace(digest, hashlib.sha256(report.read_bytes()).hexdigest())
        self.assertTrue(measure.check_plan(self.plan(plan_text(stage="final", games=games,
                                                               phases="standard = 4\nlong = 8", extra=failed))))

    def test_a_start_save_is_bound_by_its_hash(self):
        save = self.root / "start.o1602"
        save.write_bytes(b"save")
        digest = hashlib.sha256(b"save").hexdigest()
        ok = plan_text(extra=f'[start]\nsave = "start.o1602"\nsha256 = "{digest}"\n')
        self.assertEqual(measure.check_plan(self.plan(ok)), [])
        self.assertTrue(measure.check_plan(self.plan(ok.replace(digest, "1" * 64))))

    def run_results(self, plan, food, inhabitants):
        records = []
        for (arm, phase), value in food.items():
            records.append({"plan": plan["_hash"], "arm": arm, "phase": phase, "game": "g", "set": "standard",
                            "inhabitants": inhabitants[(arm, phase)], "collapses": 0, "players": 4,
                            "metrics": {"food": value}})
        path = self.root / "results.jsonl"
        path.write_text("".join(json.dumps(r) + "\n" for r in records), encoding="utf-8")
        return path

    def test_the_verdict_refuses_results_of_a_changed_plan(self):
        plan = self.plan(plan_text())
        results = self.run_results(plan, {("main", 0): 1.0, ("new", 0): 2.0}, {("main", 0): 100, ("new", 0): 100})
        self.assertTrue(measure.verdict(plan, results)["passed"])
        changed = self.plan(plan_text() + "# edited\n")
        with self.assertRaises(measure.Refused):
            measure.verdict(changed, results)

    def test_a_one_phase_report_says_it_has_no_significance(self):
        plan = self.plan(plan_text())
        results = self.run_results(plan, {("main", 0): 1.0, ("new", 0): 2.0}, {("main", 0): 100, ("new", 0): 100})
        self.assertEqual(measure.verdict(plan, results)["evidence"], "mechanism, n=1, no significance")

    def test_the_verdict_refuses_missing_runs_and_metrics(self):
        plan = self.plan(plan_text())
        results = self.run_results(plan, {("main", 0): 1.0}, {("main", 0): 100})
        with self.assertRaises(measure.Refused):
            measure.verdict(plan, results)

    def test_a_mechanism_must_move_its_metric_the_right_way(self):
        plan = self.plan(plan_text())
        results = self.run_results(plan, {("main", 0): 2.0, ("new", 0): 1.0}, {("main", 0): 100, ("new", 0): 100})
        self.assertFalse(measure.verdict(plan, results)["passed"])

    def test_two_standard_errors_decide(self):
        a = [100.0, 102.0, 98.0, 100.0]
        self.assertEqual(measure.judge([x + 10 for x in a], a, "higher")["verdict"], "gain")
        self.assertEqual(measure.judge([x - 10 for x in a], a, "higher")["verdict"], "worse")
        self.assertEqual(measure.judge([x + 1 for x in a], a, "higher")["verdict"], "no measurable effect")
        self.assertEqual(measure.judge([x - 10 for x in a], a, "lower")["verdict"], "gain")

    def test_a_run_s_output_gives_inhabitants_and_collapses(self):
        text = ("minute    mean    p50     p99     max  houses  inhabit.  prods ships soldiers walkers\n"
                "    10    12.0     10      40      50      20      500     30     2        0      10\n"
                "         player 0: 1 cities, 300 inhabitants, 100 gold, 0 units, 0 castles\n"
                "         player 1: 1 cities, 200 inhabitants, 100 gold, 0 units, 0 castles\n"
                "    20    12.0     10      40      50      20      350     30     2        0      10\n"
                "         player 0: 1 cities, 300 inhabitants, 100 gold, 0 units, 0 castles\n"
                "         player 1: 1 cities, 50 inhabitants, 100 gold, 0 units, 0 castles\n")
        self.assertEqual(measure.parse_run(text), {"inhabitants": 350, "collapses": 1, "players": 2})


DIFF = """diff --git a/engine/sim/src/ai/ai_x.cpp b/engine/sim/src/ai/ai_x.cpp
--- a/engine/sim/src/ai/ai_x.cpp
+++ b/engine/sim/src/ai/ai_x.cpp
@@ -10,2 +10,0 @@
-    if (d < 13) { score -= 32 << (5 - d); }
-    // a comment with 77
@@ -40,0 +38,4 @@
+void moved() { if (d < 13) { score -= 32 << (5 - d); } }
+    // only a comment: 99
+    const char* s = "text 88";
+    if (stock > 250) { more(); }
"""


class ConstantsTest(unittest.TestCase):
    def test_moved_code_brings_no_number_but_a_new_one_counts(self):
        hits = check_ai_constants.check(DIFF, [])
        self.assertEqual(len(hits), 1)
        self.assertIn("new number 250", hits[0])
        self.assertIn("ai_x.cpp:41", hits[0])

    def test_a_derived_value_in_the_allowlist_passes(self):
        self.assertEqual(check_ai_constants.check(DIFF, [("250", "engine/sim/src/ai/*")]), [])

    def test_new_named_values_count(self):
        diff = """diff --git a/engine/sim/src/ai/ai_x.hpp b/engine/sim/src/ai/ai_x.hpp
--- a/engine/sim/src/ai/ai_x.hpp
+++ b/engine/sim/src/ai/ai_x.hpp
@@ -1,0 +1,1 @@
+inline constexpr std::uint64_t kNewFlag = std::uint64_t{1} << kShift;
diff --git a/data/balance/balance.toml b/data/balance/balance.toml
--- a/data/balance/balance.toml
+++ b/data/balance/balance.toml
@@ -1,0 +1,1 @@
+ai_new_threshold = 1
"""
        hits = check_ai_constants.check(diff, [])
        self.assertEqual(sorted(h.split(": ", 1)[1] for h in hits),
                         ["new balance key ai_new_threshold", "new named constant kNewFlag"])

    def test_a_hexadecimal_number_keeps_its_digits(self):
        self.assertEqual(check_ai_constants.number_value("0xFF"), "255")
        self.assertEqual(check_ai_constants.number_value("0xFFu"), "255")
        self.assertEqual(check_ai_constants.number_value("2.5f"), "2.5")

    def test_a_changed_balance_value_counts(self):
        diff = """diff --git a/data/balance/balance.toml b/data/balance/balance.toml
--- a/data/balance/balance.toml
+++ b/data/balance/balance.toml
@@ -5,1 +5,1 @@
-ai_food_ahead_minutes = 30
+ai_food_ahead_minutes = 40
"""
        hits = check_ai_constants.check(diff, [])
        self.assertEqual(len(hits), 1)
        self.assertIn("new number 40", hits[0])
        self.assertIn("balance.toml:5", hits[0])

    def test_an_allowlist_entry_needs_its_derivation(self):
        with tempfile.TemporaryDirectory() as d:
            root = pathlib.Path(d)
            (root / "tools").mkdir()
            (root / check_ai_constants.ALLOWLIST).write_text(
                "250 | engine/* | derived from: two full warehouses\n300 | engine/* |\n", encoding="utf-8")
            self.assertEqual(check_ai_constants.load_allowlist(root), [("250", "engine/*")])


class RunnerTest(unittest.TestCase):
    def test_the_runner_writes_only_under_tmp_and_never_to_a_repository(self):
        text = (ROOT / "tools" / "measure" / "run.sh").read_text(encoding="utf-8")
        code = "\n".join(line.split("#", 1)[0] for line in text.splitlines())
        for word in ("git push", "git commit", "git add", "git checkout", "git reset", "git stash", "rm -rf"):
            self.assertNotIn(word, code)
        self.assertIn("mktemp -d /tmp/", code)
        self.assertIn("pgrep -x 1602.exe", code)
        # Checked again before every run, not only at the start.
        loop = code.split("run_one \"${job[@]}\" &")[0].rsplit("while IFS", 1)[1]
        self.assertIn("game_runs", loop)
        self.assertIn("SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy", code)


if __name__ == "__main__":
    unittest.main()
