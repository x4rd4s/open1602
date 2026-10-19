// SPDX-License-Identifier: GPL-3.0-or-later
// The build tool: the building list, and the orders a click or a drag gives (engine/app/src/build_tool.hpp).

#include <set>

#include "../test_framework.hpp"
#include "build_tool.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/ship.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kRoad = 20401;
constexpr std::uint32_t kTree = 20202;
constexpr std::uint32_t kField = 21301;
constexpr std::uint32_t kKontor = 22001;
constexpr std::uint32_t kKontor2 = 22002;
constexpr std::uint32_t kHouse = 20801;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        // Ground: no cost, never in the list.
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20401\n  Kind: 1\n  Gfx: 4\n  Baugfx: 30\n  Size: 1, 1\n  KreuzBase: 1\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        // A tree in three variants one record apart.
        "  @Nummer: 2\n  Id: 20202\n  Kind: 10\n  Gfx: 8\n  Baugfx: 20\n  Size: 1, 1\n  RandAnz: 3\n  RandAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        "  @Nummer: 3\n  Id: 20203\n  Kind: 10\n  Gfx: 9\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n  EndObj\n"
        "  @Nummer: 4\n  Id: 20204\n  Kind: 10\n  Gfx: 10\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n  EndObj\n"
        "  @Nummer: 5\n  Id: 21301\n  Kind: 10\n  Gfx: 41\n  Baugfx: 10\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        "  @Nummer: 6\n  Id: 22002\n  Kind: 35\n  Gfx: 26\n  Baugfx: 41\n  Size: 2, 3\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 200\n  EndObj\n"
        "  @Nummer: 7\n  Id: 22001\n  Kind: 35\n  Gfx: 20\n  Baugfx: 40\n  Size: 2, 3\n"
        "  Objekt: HAUS_BAUKOST\n    Werkzeug: 3\n    Holz: 4\n    Money: 100\n  EndObj\n"
        // A house without a menu icon: not in the list.
        "  @Nummer: 8\n  Id: 20801\n  Kind: 14\n  Gfx: 36\n  Size: 2, 2\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 10\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// One island of ground at (10, 10), 20 x 20 tiles; player 0 has ships at both ends of the map.
struct Map {
    assets::BuildingTable buildings = make_buildings();
    sim::World world{100, 100};

    Map() {
        sim::IslandMap map(20, 20);
        for (std::int32_t y = 0; y < 20; ++y) {
            for (std::int32_t x = 0; x < 20; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
            }
        }
        map.capture_ground();
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
        world.set_players(std::vector<sim::Player>(2));
        world.add_ship(sim::make_ship("Near", 9, 9, 12, 0));
        world.add_ship(sim::make_ship("Far", 90, 90, 12, 0));
        world.add_ship(sim::make_ship("Foreign", 10, 10, 12, 1));
    }
    [[nodiscard]] std::uint32_t ship(std::size_t i) const { return world.ships()[i].id; }
};

}  // namespace

TEST_CASE("build tool: the list holds what the build menu offers, in its order") {
    const assets::BuildingTable buildings = make_buildings();
    app::BuildTool tool(buildings);
    CHECK(tool.mode() == app::BuildTool::Mode::Off);
    std::vector<std::uint32_t> order;
    for (int i = 0; i < 5; ++i) {
        order.push_back(tool.selected()->id);
        tool.cycle(1);
    }
    // By menu icon; ground, tree variants and the house without an icon are left out.
    const std::vector<std::uint32_t> expected{kField, kTree, kRoad, kKontor, kKontor2};
    CHECK(order == expected);
    CHECK(tool.selected()->id == kField);  // wrapped around
    tool.cycle(-1);
    CHECK(tool.selected()->id == kKontor2);
    CHECK(tool.select(kRoad));
    CHECK(tool.selected()->id == kRoad);
    CHECK(!tool.select(kHouse));
    CHECK(!tool.select(kGround));
    tool.rotate(-1);
    CHECK(tool.rotation() == 3);
    tool.rotate(2);
    CHECK(tool.rotation() == 1);
}

TEST_CASE("build tool: no order unless the matching mode is on") {
    const Map m;
    app::BuildTool tool(m.buildings);
    const app::BuildTool::Spot spot{0, 3, 4};
    CHECK(!tool.build_order(m.world, 0, spot).has_value());
    CHECK(!tool.demolish_order(0, spot).has_value());
    tool.set_mode(app::BuildTool::Mode::Demolish);
    CHECK(!tool.build_order(m.world, 0, spot).has_value());
    tool.press({0, 1, 8});
    const auto demolish = tool.demolish_order(0, spot);
    REQUIRE(demolish.has_value());
    CHECK(demolish->from.x == 1);
    CHECK(demolish->from.y == 8);
    CHECK(demolish->to.x == 3);
    CHECK(demolish->to.y == 4);
    // Changing the mode ends the drag.
    tool.set_mode(app::BuildTool::Mode::Build);
    CHECK(!tool.dragging());
}

TEST_CASE("build tool: a field is dragged over a rectangle, a click places one") {
    const Map m;
    app::BuildTool tool(m.buildings);
    tool.set_mode(app::BuildTool::Mode::Build);
    REQUIRE(tool.select(kField));
    auto click = tool.build_order(m.world, 0, {0, 5, 5});
    REQUIRE(click.has_value());
    REQUIRE(click->items.size() == 1);
    CHECK(click->items[0].x == 5);
    tool.press({0, 7, 2});
    CHECK(tool.hatched_drag());
    const auto drag = tool.build_order(m.world, 0, {0, 5, 4});
    REQUIRE(drag.has_value());
    CHECK(drag->items.size() == 9);  // x 5..7, y 2..4
    CHECK(drag->player == 0);
    CHECK(!drag->ship.has_value());
    tool.release();
    CHECK(!tool.dragging());
}

TEST_CASE("build tool: trees keep their variants during a drag") {
    const Map m;
    app::BuildTool tool(m.buildings);
    tool.set_mode(app::BuildTool::Mode::Build);
    REQUIRE(tool.select(kTree));
    tool.press({0, 0, 0});
    const auto first = tool.build_order(m.world, 0, {0, 9, 9});
    const auto again = tool.build_order(m.world, 0, {0, 9, 9});
    REQUIRE(first.has_value());
    REQUIRE(again.has_value());
    REQUIRE(first->items.size() == 100);
    std::set<std::uint32_t> ids;
    for (std::size_t i = 0; i < first->items.size(); ++i) {
        CHECK(first->items[i].building == again->items[i].building);
        ids.insert(first->items[i].building);
    }
    // A hundred tiles show all three variants.
    const std::set<std::uint32_t> variants{20202, 20203, 20204};
    CHECK(ids == variants);
}

TEST_CASE("build tool: a Kontor is paid from the nearest own ship, or the one chosen") {
    const Map m;
    app::BuildTool tool(m.buildings);
    tool.set_mode(app::BuildTool::Mode::Build);
    REQUIRE(tool.select(kKontor));
    const auto nearest = tool.build_order(m.world, 0, {0, 2, 2});
    REQUIRE(nearest.has_value());
    REQUIRE(nearest->ship.has_value());
    CHECK(*nearest->ship == m.ship(0));  // the foreign ship is nearer still
    // The ship window's button: the first Kontor, paid by that ship.
    tool.cycle(1);
    REQUIRE(tool.select_kontor(m.ship(1)));
    CHECK(tool.selected()->id == kKontor);
    const auto chosen = tool.build_order(m.world, 0, {0, 2, 2});
    REQUIRE(chosen.has_value());
    REQUIRE(chosen->ship.has_value());
    CHECK(*chosen->ship == m.ship(1));
    // Another player's order ignores a ship that is not theirs.
    const auto other = tool.build_order(m.world, 1, {0, 2, 2});
    REQUIRE(other.has_value());
    REQUIRE(other->ship.has_value());
    CHECK(*other->ship == m.ship(2));
    // Leaving build mode forgets the chosen ship.
    tool.set_mode(app::BuildTool::Mode::Off);
    tool.set_mode(app::BuildTool::Mode::Build);
    const auto after = tool.build_order(m.world, 0, {0, 2, 2});
    REQUIRE(after.has_value());
    CHECK(*after->ship == m.ship(0));
}
