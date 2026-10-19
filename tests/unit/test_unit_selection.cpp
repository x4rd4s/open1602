// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <variant>

#include "../test_framework.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/ui/unit_selection.hpp"

using namespace o1602;
using ui::UnitRef;

namespace {

assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}, {"FORMATION", 2}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 1\n  Kind: 1\n  Gfx: 0\n  Maxenergy: 20\n"
        "  Nummer: 9\n  Kind: 3\n  Gfx: 0\n  Maxenergy: 15\n"
        "  Nummer: 21\n  Gfx: 0\n  Maxenergy: 40\n"
        "EndObj\n"
        "Objekt: FORMATION\n"
        "  Nummer: 0\n  Offs: 0, 0, 0\n  Offs: 1, -1, 0\n  Offs: 2, 1, 0\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

struct Field {
    assets::FigureTable figures = make_figures();
    sim::World world{80, 60};
    Field() {
        std::vector<sim::Player> players(2);
        players[1].role = sim::PlayerRole::Computer;
        world.set_players(players);
    }
    UnitRef ship(std::uint32_t x, std::uint32_t y, sim::PlayerId owner = 0) {
        world.add_ship(sim::make_ship("S", x, y, 21, owner, 0));
        return {UnitRef::Kind::Ship, world.ships().back().id};
    }
    UnitRef soldier(std::uint32_t x, std::uint32_t y, std::uint16_t figure = 1, sim::PlayerId owner = 0) {
        world.add_soldier(sim::make_soldier(x, y, figure, owner));
        return {UnitRef::Kind::Soldier, world.soldiers().back().id};
    }
};

}  // namespace

TEST_CASE("unit selection: the rectangle takes own units of the kind with the largest part inside") {
    Field f;
    const UnitRef s1 = f.soldier(10, 10);
    const UnitRef s2 = f.soldier(12, 10);
    const UnitRef ship = f.ship(30, 30);
    const UnitRef enemy = f.soldier(11, 11, 1, 1);
    ui::UnitSelection sel;
    sel.box(f.world, {{s1, 100}, {s2, 50}, {ship, 120}, {enemy, 500}}, 0, false);
    // Own units only; the ship has the largest part: only ships.
    REQUIRE(sel.size() == 1);
    CHECK(sel.units()[0] == ship);
    sel.box(f.world, {{s1, 100}, {s2, 50}, {ship, 20}}, 0, false);
    CHECK(sel.size() == 2);
    CHECK(sel.kind() == UnitRef::Kind::Soldier);
    // Only somebody else's: one of them, to look at.
    sel.box(f.world, {{enemy, 10}}, 0, false);
    CHECK(sel.size() == 1);
    CHECK(sel.owner(f.world) == 1);
    // An empty rectangle changes nothing; Shift adds and toggles.
    sel.box(f.world, {{s1, 10}}, 0, false);
    sel.box(f.world, {}, 0, false);
    CHECK(sel.size() == 1);
    sel.toggle(f.world, s2);
    CHECK(sel.size() == 2);
    sel.toggle(f.world, s1);
    CHECK(sel.size() == 1);
    CHECK(sel.units()[0] == s2);
    sel.toggle(f.world, ship);  // another kind: alone
    CHECK(sel.units() == std::vector<UnitRef>{ship});
}

TEST_CASE("unit selection: no more than 21, each once; units that are gone leave it") {
    Field f;
    std::vector<ui::UnitCandidate> many;
    for (std::uint32_t k = 0; k < 30; ++k) {
        many.push_back({f.soldier(5 + k, 20), 10});
    }
    many.push_back(many.front());
    ui::UnitSelection sel;
    sel.box(f.world, many, 0, false);
    CHECK(sel.size() == ui::UnitSelection::kMaxUnits);
    f.world.soldier(sel.units()[0].id)->motion.phase = sim::SoldierMotion::Phase::Dying;
    CHECK(sel.prune(f.world));
    CHECK(sel.size() == ui::UnitSelection::kMaxUnits - 1);
}

TEST_CASE("unit selection: a move keeps the shape of the group, only own units are ordered") {
    Field f;
    const UnitRef a = f.soldier(10, 10);
    const UnitRef b = f.soldier(12, 10);
    const UnitRef s1 = f.ship(40, 40);
    const UnitRef s2 = f.ship(44, 40);
    ui::UnitSelection sel;
    sel.box(f.world, {{a, 1}, {b, 1}}, 0, false);
    // Units stand on the upper left cell of their tile: centre (11.25, 10.25), offsets -8 and 8.
    CHECK((sel.offsets()[0] == ui::UnitSelection::Offset{-8, 0}));
    auto orders = sel.move_orders(f.world, 0, 20.5, 30.5, true);
    REQUIRE(orders.size() == 1);
    const auto* o = std::get_if<sim::SoldierOrder>(&orders[0]);
    REQUIRE(o != nullptr);
    CHECK(o->target == sim::Target::spot(41, 61));
    CHECK(o->offsets.size() == 2);
    CHECK(o->queue);
    sel.box(f.world, {{s1, 1}, {s2, 1}}, 0, false);
    orders = sel.move_orders(f.world, 0, 20.5, 30.5, false);
    REQUIRE(orders.size() == 2);
    CHECK(std::get<sim::ShipOrder>(orders[0]).target == sim::Target::position(18, 30));
    CHECK(std::get<sim::ShipOrder>(orders[1]).target == sim::Target::position(22, 30));
    // Somebody else's selection gives no orders.
    sel.select(f.world, f.ship(50, 50, 1));
    CHECK(sel.move_orders(f.world, 0, 1, 1, false).empty());
}

TEST_CASE("unit selection: formation, filters and groups") {
    Field f;
    const UnitRef a = f.soldier(10, 10);
    const UnitRef b = f.soldier(10, 14, 9);
    const UnitRef c = f.soldier(10, 18);
    ui::UnitSelection sel;
    sel.box(f.world, {{a, 1}, {b, 1}, {c, 1}}, 0, false);
    // Kinds: infantry (1) and musketeers (3).
    CHECK(sel.kind_mask(f.world, f.figures) == ((1U << 1) | (1U << 3)));
    // A line around the centre: each to the nearest free place.
    sel.formation(f.world, f.figures, 0);
    std::vector<ui::UnitSelection::Offset> offsets = sel.offsets();
    std::sort(offsets.begin(), offsets.end());
    CHECK((offsets == std::vector<ui::UnitSelection::Offset>{{-8, 0}, {0, 0}, {8, 0}}));
    sel.store_group(1);
    sel.filter_kinds(f.world, f.figures, 1U << 3, true);  // only musketeers
    CHECK(sel.units() == std::vector<UnitRef>{b});
    f.world.soldier(a.id)->health = 8 * 32;  // 8 of 20
    CHECK(sel.recall_group(f.world, 1));
    CHECK(sel.size() == 3);
    CHECK(sel.group_of(c) == 1U);
    sel.filter_wounded(f.world, f.figures, true);
    CHECK(sel.units() == std::vector<UnitRef>{a});
    // Shift + number adds without changing the selection.
    sel.select(f.world, f.soldier(30, 30));
    sel.add_to_group(f.world, 1);
    CHECK(sel.size() == 1);
    CHECK(sel.recall_group(f.world, 1));
    CHECK(sel.size() == 4);
    CHECK(!sel.recall_group(f.world, 5));
    // Another game loaded: the groups go with the old one.
    sel.reset();
    CHECK(sel.size() == 0);
    CHECK(!sel.recall_group(f.world, 1));
    CHECK(!sel.group_of(c));
}

TEST_CASE("unit selection: Tab narrows to one type after the other, then back to all") {
    Field f;
    const UnitRef a = f.soldier(10, 10, 1);
    const UnitRef b = f.soldier(11, 10, 9);
    const UnitRef c = f.soldier(12, 10, 1);
    ui::UnitSelection sel;
    sel.box(f.world, {{a, 10}, {b, 10}, {c, 10}}, 0, false);
    REQUIRE(sel.size() == 3);
    CHECK(sel.cycle_types(f.world));
    CHECK(sel.size() == 2);  // figure 1
    CHECK(sel.contains(a));
    CHECK(sel.contains(c));
    CHECK(sel.cycle_types(f.world));
    CHECK(sel.size() == 1);  // figure 9
    CHECK(sel.contains(b));
    CHECK(sel.cycle_types(f.world));
    CHECK(sel.size() == 3);  // all again
    CHECK(sel.cycle_types(f.world));
    CHECK(sel.size() == 2);
    // Another selection starts anew; one type alone does not cycle.
    sel.select(f.world, b);
    CHECK(!sel.cycle_types(f.world));
    CHECK(sel.size() == 1);
}
