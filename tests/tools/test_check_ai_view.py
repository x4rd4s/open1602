# SPDX-License-Identifier: GPL-3.0-or-later
"""Tests of tools/check_ai_view.py (run by CI's checks job)."""
import pathlib
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import check_ai_view  # noqa: E402


def diff(path, removed, added):
    text = f"diff --git a/{path} b/{path}\n--- a/{path}\n+++ b/{path}\n@@ -10,{len(removed)} +10,{len(added)} @@\n"
    return text + "".join(f"-{r}\n" for r in removed) + "".join(f"+{a}\n" for a in added)


class CheckAiViewTest(unittest.TestCase):
    def test_a_new_read_of_the_cities_fails(self):
        hits = check_ai_view.check(diff("engine/sim/src/ai/ai_settle.cpp", [],
                                        ["    for (const City& c : m_world.cities()) {"]), [])
        self.assertEqual(len(hits), 1)
        self.assertIn("engine/sim/src/ai/ai_settle.cpp:10: new read of the world's cities", hits[0])

    def test_moved_code_and_view_pass(self):
        line = "    for (const City& c : m_world.cities()) {"
        self.assertEqual(check_ai_view.check(diff("engine/sim/src/ai/ai_settle.cpp", [line], [line]), []), [])
        self.assertEqual(check_ai_view.check(diff("engine/sim/src/ai/ai_view.cpp", [], [line]), []), [])
        self.assertEqual(check_ai_view.check(diff("engine/sim/src/ai/ai_settle.cpp", [],
                                                  ["    for (const City* c : view(p).own_cities()) {"]), []), [])

    def test_rules_comments_and_listed_reads_pass(self):
        added = ["    const Balance& bal = m_world.balance();", "    // m_world.cities() in a comment",
                 "    const Ship* s = m_world.ship(id);"]
        self.assertEqual(check_ai_view.check(diff("engine/sim/src/ai/ai_core.cpp", [], added),
                                             [("ship", "engine/sim/src/ai/ai_core.cpp")]), [])

    def test_a_line_that_carries_its_class_passes(self):
        ok = "    for (const City& c : m_world.cities()) {  // ai-view: Buchung: the log's census comparison"
        bad = "    for (const City& c : m_world.cities()) {  // ai-view: verborgen: every city"
        self.assertEqual(check_ai_view.check(diff("engine/sim/src/ai/ai_strategy.cpp", [], [ok]), []), [])
        self.assertEqual(len(check_ai_view.check(diff("engine/sim/src/ai/ai_strategy.cpp", [], [bad]), [])), 1)

    def test_the_allowlist_takes_a_class_and_a_reason_but_never_verborgen(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            (root / "tools").mkdir()
            (root / check_ai_view.ALLOWLIST).write_text(
                "# comment\nship | engine/sim/src/ai/*.cpp | eigen: its own ships by id\n"
                "cities | engine/sim/src/ai/ai_core.cpp | verborgen: every city\n"
                "soldiers | engine/sim/src/ai/ai_core.cpp | Buchung:\n", encoding="utf-8")
            self.assertEqual(check_ai_view.load_allowlist(root), [("ship", "engine/sim/src/ai/*.cpp")])

    def test_an_unclassified_member_says_so(self):
        hits = check_ai_view.check(diff("engine/sim/src/ai/ai_core.cpp", [], ["    m_world.weather();"]), [])
        self.assertEqual(len(hits), 2)
        self.assertIn("classify it here", hits[1])


if __name__ == "__main__":
    unittest.main()
