// SPDX-License-Identifier: GPL-3.0-or-later
// Taking a market, as in the original: when a city of a human or computer player loses its last
// standing market, Kontor or tower, its land within the circles of another standing city goes
// to that city with what stands on it; a market can then be built over the destroyed one there.
#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod_parser.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kHouse = 20103;
constexpr std::uint32_t kTower = 20105;
constexpr std::uint32_t kKontor = 20108;
constexpr std::uint32_t kMarket = 20120;
constexpr std::uint32_t kKontorDestroyed = 22121;  // ruin slot 8
constexpr std::uint32_t kMarketDestroyed = 20823;  // ruin slot 14

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const std::string text =
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"  // open ground
        "  @Nummer: 1\n  Id: 20103\n  Kind: 14\n  Gfx: 2\n  Size: 2, 2\n  Maxenergy: 20\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n  EndObj\n"  // a house
        "  @Nummer: 2\n  Id: 20108\n  Kind: 35\n  Gfx: 30\n  Size: 2, 2\n  Maxenergy: 20\n"
        "  KreuzBase: 6\n  Ausbauflg: 1\n  Ruinenr: 8\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n  EndObj\n"  // Kontor
        "  @Nummer: 3\n  Id: 20120\n  Kind: 14\n  Gfx: 40\n  Size: 2, 2\n  Maxenergy: 115\n"
        "  KreuzBase: 7\n  Ausbauflg: 1\n  Ruinenr: 14\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Radius: 8\n  EndObj\n"  // market
        "  @Nummer: 4\n  Id: 20823\n  Kind: 14\n  Gfx: 50\n  Size: 2, 2\n  Destroyflg: 1\n"
        "  KreuzBase: 7\n  Ausbauflg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Radius: 8\n  EndObj\n"  // destroyed market
        "  @Nummer: 5\n  Id: 22121\n  Kind: 35\n  Gfx: 60\n  Size: 2, 2\n  Destroyflg: 1\n"
        "  KreuzBase: 6\n  Ausbauflg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n  EndObj\n"  // destroyed Kontor
        "  @Nummer: 6\n  Id: 20105\n  Kind: 6\n  Gfx: 70\n  Size: 1, 1\n  Maxenergy: 55\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 16\n  EndObj\n"  // tower
        "EndObj\n";
    const auto doc = assets::cod::parse(text, types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// An island of 40 x 20: player 0's city 0 in the west (its Kontor at 2, 5, a market at 12, 5,
/// land up to x = 13), player 1's city 1 in the east (its Kontor at 34, 5) with a destroyed market
/// at 20, 5 and a house at 18, 9 within the circle of player 0's market.
struct Island {
    assets::BuildingTable buildings = make_buildings();
    sim::World world{80, 60};

    Island() {
        std::vector<sim::Player> players(2);
        players[0].role = sim::PlayerRole::Human;
        players[0].money = 10000;
        players[1].role = sim::PlayerRole::Computer;
        world.set_players(players);
        sim::IslandMap map(40, 20);
        for (std::int32_t y = 0; y < 20; ++y) {
            for (std::int32_t x = 0; x < 40; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
            }
        }
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
        for (const sim::PlayerId owner : {sim::PlayerId{0}, sim::PlayerId{1}}) {
            sim::City c;
            c.island = 0;
            c.index = static_cast<std::uint8_t>(owner);
            c.owner = owner;
            c.warehouse_x = static_cast<std::uint16_t>(owner == 0 ? 2 : 34);
            c.warehouse_y = 5;
            world.add_city(c);
        }
        sim::IslandMap& m = world.island(0).map;
        REQUIRE(m.place(*buildings.by_id(kKontor), 2, 5, 0, 0, 0, 0).ok());
        REQUIRE(m.place(*buildings.by_id(kMarket), 12, 5, 0, 0, 0, 0).ok());
        REQUIRE(m.place(*buildings.by_id(kKontor), 34, 5, 0, 0, 1, 1).ok());
        REQUIRE(m.place(*buildings.by_id(kMarketDestroyed), 20, 5, 0, 0, 1, 1).ok());
        REQUIRE(m.place(*buildings.by_id(kHouse), 18, 9, 0, 0, 1, 1).ok());
        for (std::uint32_t y = 0; y < 20; ++y) {
            for (std::uint32_t x = 0; x < 40; ++x) {
                if (x <= 13) {
                    m.set_city(x, y, 0, 0);
                } else {
                    m.set_city(x, y, 1, 1);
                }
            }
        }
    }

    [[nodiscard]] const sim::IslandMap& map() const { return world.islands()[0].map; }

    [[nodiscard]] std::uint32_t id_at(std::uint32_t x, std::uint32_t y) const {
        return buildings.by_sprite(map().top(x, y).sprite)->id;
    }

    [[nodiscard]] sim::BuildOrder market_order(std::int32_t x = 21, std::int32_t y = 6) const {
        sim::BuildOrder order;
        order.player = 0;
        order.island = 0;
        order.items = {sim::BuildItem{kMarket, x, y, 0, 0}};
        return order;
    }
};

}  // namespace

TEST_CASE("market capture: no market over the destroyed market of a city that still stands") {
    Island f;
    const sim::BuildPlan plan = sim::plan_build(f.world, f.buildings, f.market_order());
    CHECK(plan.placed == 0);
    CHECK(plan.items[0].refusal == sim::BuildRefusal::ForeignCity);
    // Nothing changes hands while the city has its Kontor.
    CHECK(f.map().top(20, 5).city == 1);
    CHECK(f.map().top(18, 9).city == 1);
}

TEST_CASE("market capture: a city that loses its last Kontor gives the land in another city's circle") {
    Island f;
    REQUIRE(sim::destroy_building(f.world, f.buildings, 0, {34, 5}, nullptr));
    const sim::IslandMap& m = f.map();
    CHECK(f.id_at(34, 5) == kKontorDestroyed);
    // The land within the circle of player 0's market goes to its city, with what stands on it.
    CHECK(m.top(20, 5).city == 0);
    CHECK(m.top(20, 5).owner == 0);
    CHECK(f.id_at(20, 5) == kMarketDestroyed);
    CHECK(m.top(18, 9).city == 0);
    CHECK(m.top(18, 9).owner == 0);
    CHECK(f.id_at(18, 9) == kHouse);
    // The rest, with the destroyed Kontor, stays with the city, which goes on.
    CHECK(m.top(30, 5).city == 1);
    CHECK(m.top(30, 5).owner == 1);
    CHECK(m.top(34, 5).city == 1);
    REQUIRE(f.world.city(0, 1) != nullptr);
    CHECK(f.world.city(0, 1)->owner == 1);

    // Now the destroyed market stands on player 0's land: a market replaces it.
    const sim::BuildPlan plan = sim::execute_build(f.world, f.buildings, f.market_order(20, 5), {}, nullptr);
    REQUIRE(plan.placed == 1);
    CHECK(plan.city == 0);
    CHECK(plan.items[0].x == 20);
    CHECK(plan.items[0].y == 5);
    CHECK(f.id_at(20, 5) == kMarket);
    CHECK(m.top(20, 5).city == 0);
}

TEST_CASE("market capture: a tower keeps the city's land") {
    Island f;
    sim::IslandMap& m = f.world.island(0).map;
    REQUIRE(m.place(*f.buildings.by_id(kTower), 28, 15, 0, 0, 1, 1).ok());
    REQUIRE(sim::destroy_building(f.world, f.buildings, 0, {34, 5}, nullptr));
    CHECK(m.top(20, 5).city == 1);
    CHECK(m.top(18, 9).city == 1);
    const sim::BuildPlan plan = sim::plan_build(f.world, f.buildings, f.market_order());
    CHECK(plan.placed == 0);
}

TEST_CASE("market capture: an already defenceless city keeps its land until something falls") {
    // As in the original, a city found defenceless (a loaded game) gives nothing by itself.
    Island f;
    sim::IslandMap& m = f.world.island(0).map;
    REQUIRE(m.place(*f.buildings.by_id(kKontorDestroyed), 34, 5, 0, 0, 1, 1).ok());
    CHECK(sim::city_defenceless(f.world, f.buildings, 0, 1));
    CHECK(m.top(20, 5).city == 1);
    CHECK(sim::plan_build(f.world, f.buildings, f.market_order()).placed == 0);
}
