# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests of tools/measure/report.py (run by CI's checks job)."""
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "measure"))
import report  # noqa: E402


def bench(rows):
    """rows: {minute: {player: (cities, inhabitants, gold)}}"""
    text = "world ...\nminute    mean    p50     p99     max  houses  inhabit.  prods ships soldiers walkers\n"
    for minute, players in sorted(rows.items()):
        text += f"    {minute:>2}   146.8     97    1254    6327      18       28    206    64      168    1534\n"
        for p, (c, i, g) in sorted(players.items()):
            text += f"             player {p}: {c} cities, {i} inhabitants, {g} gold, 0 units, 0 castles\n"
            text += "               taxes 0/min, running costs 0/min, sales 0/min, purchases 0/min; classes\n"
    return text + "state hash abcdef0123456789 in 2.7 ms\n"


class ReportTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        d = pathlib.Path(self.tmp.name) / "x3"
        d.mkdir()
        # Two phases of one map. Arm A: player 2 collapses in p0 (peak 200 at 20, 100 at 30).
        runs = {
            ("A", "g4-1-p0"): {10: {1: (1, 100, 500), 2: (1, 150, 100)}, 20: {1: (1, 300, 400), 2: (1, 200, 50)},
                              30: {1: (2, 400, 300), 2: (1, 100, -20)}},
            ("B", "g4-1-p0"): {10: {1: (1, 100, 500), 2: (1, 150, 100)}, 30: {1: (2, 380, 600), 2: (1, 220, 80)}},
            ("A", "g4-1-p1"): {30: {1: (1, 300, 100), 2: (1, 200, 100)}},
            ("B", "g4-1-p1"): {30: {1: (1, 320, 200), 2: (1, 180, 100)}},
        }
        for (arm, key), rows in runs.items():
            (d / f"{arm}-{key}.txt").write_text(bench(rows))
            (d / f"{arm}-{key}.rc").write_text("0\n")
        log = ("[ai 2 1500s] landing of ship 5 against player 2\n"
               "[ai 3 60s] decision found id 60000.0 cand 50 city 1/0 cost 1 upkeep 1 value 0 delay 0 horizon 30 "
               "ware 0 amount 0 to 1/0\n"
               "[ai 2 600s] track gold -5\n[ai 2 1200s] track gold 10\n"
               "[ai 2 600s] track city 1/0 people 9 tax 1 cost 1 goods 4:0/0/0/8/0\n"
               "[ai 2 1200s] track city 1/0 people 9 tax 1 cost 1 goods 4:0/0/0/8/0\n"
               "[ai 2 1800s] track city 1/0 people 9 tax 1 cost 1 goods 4:0/0/4/8/32\n")
        (d / "A-g4-1-p0.err").write_text(log)
        (d / "B-g4-1-p0.err").write_text("")
        (d / "A-g4-1-p1.err").write_text("")
        (d / "B-g4-1-p1.err").write_text("")
        self.rep = report.report_set(d, "A", "B", human=False)

    def tearDown(self):
        self.tmp.cleanup()

    def test_inhabitants_against_the_maps_reference_mean(self):
        # Reference mean of the map: (600 + 500) / 2 = 550. Arm 500, 500; reference 600, 500.
        i = self.rep["inhabitants"]
        self.assertAlmostEqual(i["difference"], 1000 / 550 / 2 - 1100 / 550 / 2)

    def test_collapse_and_slide_window(self):
        self.assertEqual(self.rep["collapses"], {"arm": 1, "ref": 0})
        self.assertEqual(self.rep["two_extra"], [])
        self.assertEqual(self.rep["slides"], [{"key": "g4-1-p0", "player": 2, "peak": 20, "below": 30,
                                               "landings": 1}])

    def test_gold_and_cities_are_sums_at_the_end(self):
        self.assertEqual(self.rep["gold"]["sum_arm"], 300 - 20 + 200)
        self.assertEqual(self.rep["cities"]["sum_ref"], 3 + 2)

    def test_log_counts(self):
        a = self.rep["log"]["arm"]
        self.assertEqual(a["foundings"], 1)
        self.assertEqual(a["debt_minutes"], 10)
        self.assertEqual((a["dry_minutes"], a["episodes"], a["covered"], a["deficit_minutes"]), (20, 1, 1, 30))

    def test_slide_starts_at_the_last_minute_at_the_peak(self):
        self.assertEqual(report.window({10: (5, 0, 0), 20: (9, 0, 0), 30: (9, 0, 0), 40: (6, 0, 0)}), (30, 40))

    def test_a_player_without_people_never_collapses(self):
        self.assertFalse(report.collapsed({10: (0, 0, 0), 20: (0, 0, 0)}))


if __name__ == "__main__":
    unittest.main()
