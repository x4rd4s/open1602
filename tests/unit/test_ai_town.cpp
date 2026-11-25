// SPDX-License-Identifier: GPL-3.0-or-later
// An improved computer's town layout (p46b): every house keeps a tile the doctor can come in by.

#include <cstdint>
#include <utility>
#include <vector>

#include "../../engine/sim/src/ai/ai_engine.hpp"
#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kRoad = 20001;
constexpr std::uint32_t kHouse = 20605;
constexpr sim::PlayerId kComputer = 1;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}};
    const auto doc = assets::cod::parse(
        "Objekt: BGRUPPE\n  Nummer: 0\n  Prozent: 0\n  Maxwohn: 8\n  Steuer: 1.4\nEndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n  Wegspeed: 145, 120, 170, 100\n"
        "  @Nummer: +1\n  Id: 20001\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n  Wegspeed: 100, 100, 100, 100\n"
        "  @Nummer: +1\n  Id: 20605\n  Kind: 14\n  Gfx: 50\n  Size: 2, 2\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n    Radius: 6\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc =
        assets::cod::parse("Objekt: FIGUR\n  Nummer: 1\n  Kind: 1\n  Gfx: 0\n  Speed: 260\nEndObj\n", types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// One island of 20 x 14 tiles of open ground with roads along the rows in `roads`.
struct Town {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{40, 30};

    explicit Town(const std::vector<std::int32_t>& roads) {
        std::vector<sim::Player> players(2);
        players[kComputer].role = sim::PlayerRole::Computer;
        world.set_players(players);
        sim::IslandMap map(20, 14);
        for (std::int32_t y = 0; y < 14; ++y) {
            for (std::int32_t x = 0; x < 20; ++x) {
                REQUIRE(map.place(*buildings.by_id(kGround), x, y, 0, 0).ok());
            }
        }
        map.capture_ground();
        sim::WorldIsland island{assets::IslandHeader{}, std::move(map), 5, 5};
        island.header.width = 20;
        island.header.height = 14;
        REQUIRE(world.add_island(std::move(island)).ok());
        sim::City c;
        c.owner = kComputer;
        world.add_city(c);
        for (const std::int32_t y : roads) {
            for (std::int32_t x = 0; x < 20; ++x) {
                REQUIRE(world.island(0).map.place(*buildings.by_id(kRoad), x, y, 0, 0, kComputer, 0).ok());
            }
        }
    }

    /// A row of houses from x 0 to 19 with its top at `y`, without the one at `gap_x`.
    void houses(std::int32_t y, std::int32_t gap_x = -1) {
        for (std::int32_t x = 0; x < 20; x += 2) {
            if (x != gap_x) {
                house(x, y);
            }
        }
    }

    void house(std::int32_t x, std::int32_t y) {
        REQUIRE(world.island(0).map.place(*buildings.by_id(kHouse), x, y, 0, 0, kComputer, 0).ok());
        sim::House h;
        h.x = static_cast<std::uint16_t>(x);
        h.y = static_cast<std::uint16_t>(y);
        world.add_house(h);
    }

    [[nodiscard]] bool keeps(std::int32_t x, std::int32_t y) {
        sim::Simulation s(world, buildings, figures);
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::ai::AiPlayer p;
        p.player = kComputer;
        p.island = 0;
        p.city = 0;
        return engine.house_keeps_entry(p, x, y, x + 1, y + 1);
    }
};

}  // namespace

TEST_CASE("ai town: an improved computer builds no house its doctor cannot come in to") {
    // Two roads 9 rows apart and four rows of houses between them (gen_double_row from both): the
    // inner rows touch houses only, as the plague houses of p46b.
    Town four({2, 11});
    four.houses(3);
    four.houses(9);
    four.houses(7);
    four.houses(5, 8);
    CHECK(!four.keeps(8, 5));

    // Behind a single row a free tile beside the next road's free tile is a way in.
    Town two({2, 8});
    two.houses(3);
    two.houses(5, 8);
    CHECK(two.keeps(8, 5));

    // A house that would close the last way in to the one behind it is not built either.
    Town seal({2, 9});
    seal.houses(3);
    seal.house(8, 5);
    CHECK(!seal.keeps(8, 7));
}
