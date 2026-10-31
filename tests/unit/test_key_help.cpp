// SPDX-License-Identifier: GPL-3.0-or-later
// The key overview (F1) in pages, as the History Edition shows its shortcuts
// (engine/app/src/key_help.hpp).

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

#include "../test_framework.hpp"
#include "key_help.hpp"

using namespace o1602;
using ui::KeyAction;

namespace {

/// Every action there is, in the order of ui::KeyAction.
std::vector<KeyAction> all_actions() {
    std::vector<KeyAction> all;
    for (std::uint16_t i = 0; !ui::KeyBindings::name(static_cast<KeyAction>(i)).empty(); ++i) {
        all.push_back(static_cast<KeyAction>(i));
    }
    return all;
}

std::string label(KeyAction a) {
    return std::string(ui::KeyBindings::name(a));
}

}  // namespace

TEST_CASE("key help: every action has its page, and every page its actions (History Edition tabs)") {
    const std::vector<KeyAction> all = all_actions();
    REQUIRE(all.size() > 40);
    std::vector<app::KeyHelpEntry> entries;
    for (const KeyAction a : all) {
        const app::KeyHelpPlace p = app::key_help_place(a);
        CHECK(p.page < app::kKeyHelpPages);
        CHECK(p.section < 2);
        entries.push_back(app::KeyHelpEntry{a, "K"});
    }
    const auto pages = app::key_help_pages(entries, label);
    std::size_t lines = 0;
    for (const app::KeyHelpPage& page : pages) {
        CHECK(!page.title.empty());
        CHECK(!page.sections.empty());
        for (const app::KeyHelpSection& s : page.sections) {
            CHECK(!s.heading.empty());
            lines += s.lines.size();
        }
    }
    CHECK(lines == all.size());  // none left out, none twice
    CHECK((app::key_help_place(KeyAction::NextCity) == app::KeyHelpPlace{0, 0}));
    CHECK((app::key_help_place(KeyAction::RotateBuildingBack) == app::KeyHelpPlace{2, 0}));
    CHECK((app::key_help_place(KeyAction::SelectCannoneers) == app::KeyHelpPlace{3, 0}));
    CHECK((app::key_help_place(KeyAction::ShowCoordinates) == app::KeyHelpPlace{4, 0}));
}

TEST_CASE("key help: the keys of an action on one line, each once, in the order of the actions") {
    const auto pages = app::key_help_pages({{KeyAction::Pause, "P"},
                                            {KeyAction::ScrollUp, "Up"},
                                            {KeyAction::Pause, "Pause"},
                                            {KeyAction::Pause, "P"},
                                            {KeyAction::Delete, "Del"},
                                            {KeyAction::Delete, "Del"}},
                                           label);
    REQUIRE(pages[0].sections.size() == 2);  // map and game
    REQUIRE(pages[0].sections[0].lines.size() == 1);
    CHECK(pages[0].sections[0].lines[0].keys == "Up");
    REQUIRE(pages[0].sections[1].lines.size() == 1);
    CHECK(pages[0].sections[1].lines[0].keys == "P, Pause");
    CHECK(pages[0].sections[1].lines[0].text == "pause");
    CHECK(pages[1].sections.empty());  // no window keys: the page stays, without sections
    REQUIRE(pages[3].sections.size() == 1);
    CHECK(pages[3].sections[0].lines[0].keys == "Del");
    // A key that is part of another's name still counts as its own.
    const auto shift = app::key_help_pages({{KeyAction::Cancel, "Shift+Esc"}, {KeyAction::Cancel, "Esc"}}, label);
    CHECK(shift[0].sections[0].lines[0].keys == "Shift+Esc, Esc");
}

TEST_CASE("key help: the clicks with Shift in windows join the units, also without unit keys") {
    auto pages = app::key_help_pages({{KeyAction::Stop, "H"}}, label);
    app::add_click_lines(pages);
    REQUIRE(pages[3].sections.size() == 1);
    REQUIRE(pages[3].sections[0].lines.size() == 4);
    CHECK(pages[3].sections[0].lines[0].keys == "H");
    CHECK(pages[3].sections[0].lines[1].keys == "Shift+click");
    CHECK(pages[3].sections[0].lines[1].text == "Castle, send-out arrow: all trained units");
    auto none = app::key_help_pages({}, label);
    app::add_click_lines(none);
    REQUIRE(none[3].sections.size() == 1);
    CHECK(none[3].sections[0].lines.size() == 3);
    CHECK(none[3].sections[0].heading == "Ships and units selected");
    // A building clicked twice joins the map.
    REQUIRE(none[0].sections.size() == 1);
    REQUIRE(none[0].sections[0].lines.size() == 1);
    CHECK(none[0].sections[0].lines[0].keys == "double click");
    CHECK(none[0].sections[0].heading == "Map");
}

TEST_CASE("key help: two sections side by side, one section split into two columns") {
    app::KeyHelpPage two{"", {{"a", std::vector<app::KeyHelpLine>(3)}, {"b", std::vector<app::KeyHelpLine>(5)}}};
    CHECK(app::key_help_rows(two) == 6);
    CHECK((app::key_help_cell(two, 1, SIZE_MAX) == app::KeyHelpCell{1, 0}));
    CHECK((app::key_help_cell(two, 1, 4) == app::KeyHelpCell{1, 5}));
    app::KeyHelpPage one{"", {{"a", std::vector<app::KeyHelpLine>(7)}}};
    CHECK(app::key_help_rows(one) == 5);
    CHECK((app::key_help_cell(one, 0, 3) == app::KeyHelpCell{0, 4}));
    CHECK((app::key_help_cell(one, 0, 4) == app::KeyHelpCell{1, 1}));
    CHECK(app::key_help_rows(app::KeyHelpPage{}) == 0);
}

TEST_CASE("key help: wrapped texts take the rows below them") {
    const auto lines = [](std::initializer_list<const char*> texts) {
        std::vector<app::KeyHelpLine> v;
        for (const char* t : texts) {
            v.push_back(app::KeyHelpLine{"k", t});
        }
        return v;
    };
    app::KeyHelpPage two{"", {{"a", lines({"x", "xxx", "x"})}, {"b", lines({"xx", "x"})}}};
    app::KeyHelpPage one{"", {{"a", lines({"x", "xxx", "x", "x", "x", "x"})}}};
    // The length of a text stands for its rows.
    const app::KeyHelpPage* page = &two;
    const app::KeyHelpHeight height = [&](std::size_t s, std::size_t l) {
        return page->sections[s].lines[l].text.size();
    };
    CHECK(app::key_help_rows(two, height) == 6);
    CHECK((app::key_help_cell(two, 0, 2, height) == app::KeyHelpCell{0, 5}));
    CHECK((app::key_help_cell(two, 1, 1, height) == app::KeyHelpCell{1, 3}));
    // One section splits where the columns come out the most even: 1 + 3 | 1 + 1 + 1 + 1.
    page = &one;
    CHECK(app::key_help_rows(one, height) == 5);
    CHECK((app::key_help_cell(one, 0, 1, height) == app::KeyHelpCell{0, 2}));
    CHECK((app::key_help_cell(one, 0, 2, height) == app::KeyHelpCell{1, 1}));
    CHECK((app::key_help_cell(one, 0, 5, height) == app::KeyHelpCell{1, 4}));
    // A height of 0 still takes a row.
    CHECK(app::key_help_rows(one, [](std::size_t, std::size_t) { return std::size_t{0}; }) == 4);
}
