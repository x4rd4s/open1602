// SPDX-License-Identifier: GPL-3.0-or-later
// The neutral buildings (FactionBehaviour::Neutral): placed by a map on free land for nobody, they
// hold only the ground they stand on, make no treaties and go over whole to the soldiers of a
// player whose land reaches them.
#include <string>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod_parser.hpp"
#include "o1602/editor/objects.hpp"
#include "o1602/editor/players.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kKontor = 20108;
constexpr std::uint32_t kMarket = 20120;
constexpr std::uint32_t kFort = 20130;
constexpr std::uint32_t kTower = 20305;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const std::string text =
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"  // open ground
        "  @Nummer: 1\n  Id: 20108\n  Kind: 35\n  Gfx: 30\n  Size: 2, 2\n  Maxenergy: 20\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n  EndObj\n"  // Kontor
        "  @Nummer: 2\n  Id: 20120\n  Kind: 14\n  Gfx: 40\n  Size: 2, 2\n  Maxenergy: 115\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Radius: 8\n  EndObj\n"                     // market
        "  @Nummer: 3\n  Id: 20130\n  Kind: 14\n  Gfx: 50\n  Size: 2, 2\n  Maxenergy: 50\n"  // a fort
        "  @Nummer: 4\n  Id: 20305\n  Kind: 6\n  Gfx: 60\n  Size: 1, 1\n  Maxenergy: 50\n"   // a tower
        "EndObj\n";
    const auto doc = assets::cod::parse(text, types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// An island of 40 x 20 of open ground; player 0 (a human) with city 0 in the west: its Kontor at
/// (2, 5), its land up to x = 12. The neutral buildings' player added by the editor.
struct Island {
    assets::BuildingTable buildings = make_buildings();
    sim::World world{80, 60};
    sim::PlayerId neutral = sim::kNoPlayer;

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
        map.capture_ground();
        REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 10, 10}).ok());
        sim::City c;
        c.island = 0;
        c.index = 0;
        c.owner = 0;
        c.warehouse_x = 2;
        c.warehouse_y = 5;
        world.add_city(c);
        sim::IslandMap& m = world.island(0).map;
        REQUIRE(m.place(*buildings.by_id(kKontor), 2, 5, 0, 0, 0, 0).ok());
        for (std::uint32_t y = 0; y < 20; ++y) {
            for (std::uint32_t x = 0; x <= 12; ++x) {
                m.set_city(x, y, 0, 0);
            }
        }
        neutral = editor::add_neutral_player(world);
    }

    [[nodiscard]] sim::BuildPlan build(std::uint32_t id, std::int32_t x, std::int32_t y, sim::PlayerId player) {
        sim::BuildOrder order;
        order.player = player;
        order.island = 0;
        order.free = true;
        order.items = {sim::BuildItem{id, x, y, 0, 0}};
        editor::PlacedBuildings placed = editor::place_buildings(world, buildings, order, {}, "Neutral");
        if (placed.step) {
            placed.step->apply(world);
        }
        return placed.plan;
    }
    [[nodiscard]] const sim::MapCell& at(std::uint32_t x, std::uint32_t y) const {
        return world.islands()[0].map.top(x, y);
    }
};

}  // namespace

TEST_CASE("neutral buildings: a faction of the base game, in a slot behind every player's") {
    const std::vector<sim::Faction> base = sim::base_factions();
    REQUIRE(base.size() > sim::faction::kNeutral);
    CHECK(base[sim::faction::kNeutral].id == "anno1602:neutral");
    CHECK(base[sim::faction::kNeutral].behaviour == sim::FactionBehaviour::Neutral);
    CHECK(sim::base_faction(sim::FactionBehaviour::Neutral) == sim::faction::kNeutral);
    CHECK(std::string(sim::behaviour_name(sim::FactionBehaviour::Neutral)) == "neutral");

    Island f;
    CHECK(f.neutral == editor::kNeutralSlot);
    CHECK(editor::neutral_player(f.world) == editor::kNeutralSlot);
    const sim::Player& p = f.world.players()[f.neutral];
    CHECK(p.role == sim::PlayerRole::Neutral);
    CHECK(p.status == sim::PlayerStatus::Playing);
    // The slots between hold nobody; a second call adds none.
    CHECK(f.world.players()[2].status == sim::PlayerStatus::Absent);
    CHECK(editor::add_neutral_player(f.world) == f.neutral);
    CHECK(!editor::add_neutral(f.world).ok());
    CHECK(f.world.players().size() == std::size_t{editor::kNeutralSlot} + 1);
    // Not one of the players of the map.
    CHECK((editor::player_slots(f.world) == std::vector<sim::PlayerId>{0, 1}));

    // A world whose factions lack it (a package's game) gets the faction too.
    sim::World other(20, 20);
    std::vector<sim::Player> players(1);
    players[0].role = sim::PlayerRole::Human;
    other.set_players(players);
    std::vector<sim::Faction> four = sim::base_factions();
    four.resize(4);
    other.set_factions(four);
    const sim::PlayerId n = editor::add_neutral_player(other);
    REQUIRE(n != sim::kNoPlayer);
    CHECK(other.factions().size() == 5);
    CHECK(other.acts_as(n, sim::FactionBehaviour::Neutral));
}

TEST_CASE("neutral buildings: at war with the settlers alone, and no treaties") {
    Island f;
    const sim::Diplomacy& d = f.world.diplomacy();
    CHECK(d.hostile(0, f.neutral));
    CHECK(d.hostile(f.neutral, 1));
    CHECK(!d.may_trade(0, f.neutral));
    // Neither peace nor trade can be made with them.
    CHECK(!f.world.diplomacy().set_peace(f.world, 0, f.neutral, sim::Treaty::Offered, 0));
    CHECK(!f.world.diplomacy().set_peace(f.world, f.neutral, 0, sim::Treaty::Signed, 0));
    CHECK(!f.world.diplomacy().set_trade(f.world, 0, f.neutral, sim::Treaty::Offered, 0));
    CHECK(d.hostile(0, f.neutral));

    // At peace with the natives and pirates; the players among each other as before.
    std::vector<sim::Player> players = f.world.players();
    sim::make_neutral(players[2], sim::faction::kNatives);
    sim::make_neutral(players[3], sim::faction::kPirates);
    f.world.diplomacy().reset(players, f.world.factions());
    CHECK(!f.world.diplomacy().hostile(2, f.neutral));
    CHECK(!f.world.diplomacy().hostile(3, f.neutral));
    CHECK(!f.world.diplomacy().hostile(0, 1));
}

TEST_CASE("neutral buildings: on free land, holding only their ground, never a Kontor or a market") {
    Island f;
    const sim::BuildPlan fort = f.build(kFort, 20, 5, f.neutral);
    REQUIRE(fort.placed == 1);
    // A city of theirs without a Kontor, holding the fort's tiles and no more.
    REQUIRE(f.world.cities().size() == 2);
    const sim::City& city = f.world.cities()[1];
    CHECK(city.owner == f.neutral);
    CHECK(!city.warehouse_x);
    CHECK(city.name == "Neutral");
    CHECK(f.at(20, 5).city == city.index);
    CHECK(f.at(21, 6).owner == f.neutral);
    CHECK(f.at(22, 5).city == sim::MapCell::kNoCity);
    CHECK(f.at(19, 6).city == sim::MapCell::kNoCity);
    // A second building on the island joins it.
    REQUIRE(f.build(kTower, 30, 10, f.neutral).placed == 1);
    CHECK(f.world.cities().size() == 2);
    CHECK(f.at(30, 10).city == city.index);
    CHECK(f.at(29, 10).city == sim::MapCell::kNoCity);

    // Neither a Kontor nor a market.
    CHECK(f.build(kKontor, 25, 15, f.neutral).items[0].refusal == sim::BuildRefusal::Neutral);
    CHECK(f.build(kMarket, 25, 15, f.neutral).items[0].refusal == sim::BuildRefusal::Neutral);
    // Not in a player's city, and the player builds nothing on their ground.
    CHECK(f.build(kTower, 8, 8, f.neutral).items[0].refusal == sim::BuildRefusal::ForeignCity);
    CHECK(f.build(kTower, 30, 10, 0).placed == 0);

    // The land is checked again (a market of the player's goes up): the fort keeps its ground.
    REQUIRE(f.build(kMarket, 10, 5, 0).placed == 1);
    CHECK(f.at(20, 5).city == city.index);
    CHECK(f.world.cities().size() == 2);
}

TEST_CASE("neutral buildings: taken whole where the taker's land reaches, else nothing changes") {
    Island f;
    REQUIRE(f.build(kFort, 15, 5, f.neutral).placed == 1);
    REQUIRE(f.build(kTower, 35, 10, f.neutral).placed == 1);
    const std::uint8_t neutral_city = f.at(15, 5).city;
    // The Kontor's circle (radius 8 from 2, 5) does not reach the fort: no capture.
    CHECK(!sim::capture_neutral_building(f.world, f.buildings, 0, {15, 5}, 0, nullptr));
    CHECK(f.at(15, 5).city == neutral_city);
    // Nor for the neutral player itself or nobody.
    CHECK(!sim::capture_neutral_building(f.world, f.buildings, 0, {15, 5}, f.neutral, nullptr));

    // A market of the player's whose circle covers the fort: the fort goes over to city 0.
    REQUIRE(f.build(kMarket, 10, 5, 0).placed == 1);
    CHECK(f.at(15, 5).city == neutral_city);  // the circle takes no land of another city
    REQUIRE(sim::capture_neutral_building(f.world, f.buildings, 0, {15, 5}, 0, nullptr));
    for (const auto& [x, y] : {std::pair{15U, 5U}, std::pair{16U, 6U}}) {
        CHECK(f.at(x, y).city == 0);
        CHECK(f.at(x, y).owner == 0);
    }
    CHECK(f.buildings.by_sprite(f.at(15, 5).sprite)->id == kFort);
    // The neutral city goes on with the tower.
    CHECK(f.world.city(0, neutral_city) != nullptr);
    CHECK(f.at(35, 10).city == neutral_city);
    CHECK(!sim::capture_neutral_building(f.world, f.buildings, 0, {15, 5}, 0, nullptr));  // the player's now
}

TEST_CASE("neutral buildings: destroyed, their ground is free and their city gone") {
    Island f;
    REQUIRE(f.build(kFort, 20, 5, f.neutral).placed == 1);
    const std::uint8_t city = f.at(20, 5).city;
    REQUIRE(sim::destroy_building(f.world, f.buildings, 0, {20, 5}, nullptr));
    CHECK(f.at(20, 5).city == sim::MapCell::kNoCity);
    CHECK(f.at(21, 6).owner == sim::kNoPlayer);
    CHECK(f.world.city(0, city) == nullptr);
    // The player's city stays.
    CHECK(f.world.city(0, 0) != nullptr);
}

TEST_CASE("neutral buildings: the savegame keeps their player, faction and buildings") {
    Island f;
    REQUIRE(f.build(kFort, 20, 5, f.neutral).placed == 1);
    auto bytes = sim::write_game(f.world, f.buildings);
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, f.buildings);
    REQUIRE(loaded.ok());
    CHECK(loaded->players().size() == f.world.players().size());
    CHECK(loaded->acts_as(f.neutral, sim::FactionBehaviour::Neutral));
    CHECK(loaded->players()[f.neutral].role == sim::PlayerRole::Neutral);
    CHECK(loaded->diplomacy().hostile(0, f.neutral));
    REQUIRE(loaded->cities().size() == 2);
    CHECK(loaded->cities()[1].owner == f.neutral);
    CHECK(!loaded->cities()[1].warehouse_x);
    CHECK(loaded->islands()[0].map.top(20, 5).owner == f.neutral);
}
