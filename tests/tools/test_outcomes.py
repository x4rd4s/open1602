# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests of tools/measure/outcomes.py (run by CI's checks job)."""
import pathlib
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "measure"))
import outcomes  # noqa: E402

LOG = """\
[ai 3 0s] track gold 10000
[ai 3 0s] track city 15/0 people 40 tax 5 cost 20 goods 9:0/0/3
[ai 3 60s] decision build id 60000.0 cand 20809 city 15/0 cost 200 upkeep 10 value 0 delay 0 horizon 30 ware 9 amount 0 to 15/0
[ai 3 60s] decision colony plan id 60000.1 cand 47 city -1/-1 cost 2000 upkeep 15 value 49 delay 15 horizon 30 ware 0 amount 0 to 15/0
[ai 3 600s] track gold 9500
[ai 3 600s] track city 15/0 people 50 tax 6 cost 30 goods 9:64/0/10
[ai 3 1200s] track gold 9800
[ai 3 1200s] track city 15/0 people 60 tax 7 cost 30 goods 9:96/32/20
[ai 3 1800s] track gold 9900
[ai 3 1800s] track city 15/0 people 70 tax 8 cost 30 goods 9:96/0/20
[ai 3 2400s] track gold 10100
[ai 3 2400s] track city 15/0 people 80 tax 9 cost 30 goods 9:96/0/20
noise line
"""


class OutcomeTest(unittest.TestCase):
    def setUp(self):
        decisions, gold, cities = outcomes.read_log(LOG.splitlines(True))
        self.records = list(outcomes.pairs(decisions, gold, cities, [30, 60]))

    def test_parses_decisions_with_their_id(self):
        self.assertEqual([r["id"] for r in self.records], ["60000.0", "60000.1"])
        self.assertEqual(self.records[0]["expected"]["upkeep"], 10)
        self.assertEqual(self.records[0]["ware"], 9)

    def test_city_outcome_after_30_minutes(self):
        # t = 60 s, end = 1860 s: the first line at or after it is 2400 s; goods sum 600..1800 s.
        a = self.records[0]["actual"]["30"]
        self.assertEqual(a["gold"], 10100 - 10000)
        self.assertEqual(a["tax"], 9 - 5)
        self.assertEqual(a["people"], 80 - 40)
        self.assertEqual((a["made"], a["got"], a["used"]), (64 + 96 + 96, 32, 50))

    def test_horizon_not_reached_is_null(self):
        self.assertIsNone(self.records[0]["actual"]["60"])

    def test_colony_plan_gets_gold_and_the_city_it_is_for(self):
        self.assertEqual(self.records[1]["actual"]["30"], {"gold": 100, "to": {"people": 40, "tax": 4, "cost": 10}})


if __name__ == "__main__":
    unittest.main()
