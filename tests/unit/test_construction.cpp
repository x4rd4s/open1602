// SPDX-License-Identifier: GPL-3.0-or-later
// Building and demolishing.

#include <algorithm>
#include <optional>

#include "../test_framework.hpp"
#include "build_tool.hpp"

#include "o1602/assets/cod_parser.hpp"
#include "o1602/editor/cities.hpp"
#include "o1602/editor/objects.hpp"
#include "o1602/editor/units.hpp"
#include "o1602/script/game_scripts.hpp"
#include "o1602/sim/blueprints.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/growth.hpp"
#include "o1602/sim/original_limits.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/savegame_writer.hpp"
#include "o1602/sim/ships.hpp"
#include "o1602/sim/simulation.hpp"
#include "o1602/sim/validate.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kSea = 21201;
constexpr std::uint32_t kBeach = 20501;
constexpr std::uint32_t kTree = 20202;
constexpr std::uint32_t kRoad = 20401;
constexpr std::uint32_t kRoadCurve = 20402;
constexpr std::uint32_t kRoadT = 20403;
constexpr std::uint32_t kKontor = 22001;
constexpr std::uint32_t kKontor2 = 22002;
constexpr std::uint32_t kMarket = 20701;
constexpr std::uint32_t kHouse = 20801;
constexpr std::uint32_t kField = 21301;
constexpr std::uint32_t kSapling = 20211;
constexpr std::uint32_t kCastle = 21501;
constexpr std::uint32_t kArch = 21502;
constexpr std::uint32_t kWall = 22501;
constexpr std::uint32_t kTower = 20305;
constexpr std::uint32_t kGate = 20304;
constexpr std::uint32_t kPlantation = 20599;
constexpr std::uint32_t kForester = 20598;
constexpr std::uint32_t kSapling2 = 20213;
constexpr std::uint32_t kRiver = 21401;
constexpr std::uint32_t kWaterMill = 20509;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n  Randwachs: 100\n"
        "  @Nummer: 1\n  Id: 21201\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n  EndObj\n"
        "  @Nummer: 2\n  Id: 20501\n  Kind: 23\n  Gfx: 2\n  Size: 1, 1\n  Destroyflg: 1\n"
        "  @Nummer: 3\n  Id: 20202\n  Kind: 10\n  Gfx: 3\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
        // Road family: straight, curve, T, cross (KreuzBase = record 4).
        "  @Nummer: 4\n  Id: 20401\n  Kind: 1\n  Gfx: 4\n  Size: 1, 1\n  KreuzBase: 4\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        "  @Nummer: 5\n  Id: 20402\n  Kind: 1\n  Gfx: 5\n  Size: 1, 1\n  KreuzBase: 4\n"
        "  @Nummer: 6\n  Id: 20403\n  Kind: 1\n  Gfx: 6\n  Size: 1, 1\n  KreuzBase: 4\n"
        "  @Nummer: 7\n  Id: 20404\n  Kind: 1\n  Gfx: 7\n  Size: 1, 1\n  KreuzBase: 4\n"
        // Kontor I and II (family record 8).
        "  @Nummer: 8\n  Id: 22001\n  Kind: 35\n  Gfx: 20\n  Size: 2, 3\n  Strandflg: 1\n  PlaceFlg: 1\n"
        "  KreuzBase: 8\n  Ausbauflg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 6\n    Maxlager: 30\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Werkzeug: 3\n    Holz: 4\n    Money: 100\n  EndObj\n"
        "  @Nummer: 9\n  Id: 22002\n  Kind: 35\n  Gfx: 26\n  Size: 2, 3\n  Strandflg: 1\n  PlaceFlg: 1\n"
        "  KreuzBase: 8\n  Ausbauflg: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 6\n    Maxlager: 50\n    Bauinfra: 17\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 200\n  EndObj\n"
        "  @Nummer: 10\n  Id: 20701\n  Kind: 14\n  Gfx: 32\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Radius: 5\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n"
        "  @Nummer: 11\n  Id: 20801\n  Kind: 14\n  Gfx: 36\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Holz: 1\n    Money: 10\n  EndObj\n"
        "  @Nummer: 12\n  Id: 22722\n  Kind: 11\n  Gfx: 40\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 52\n  EndObj\n"
        // A crop: growing, ripe, withered.
        "  @Nummer: 13\n  Id: 21301\n  Kind: 10\n  Gfx: 41\n  Size: 1, 1\n  AnimAnz: 5\n  AnimAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 300\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        "  @Nummer: 14\n  Id: 21302\n  Kind: 10\n  Gfx: 46\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 46\n  EndObj\n"
        "  @Nummer: 15\n  Id: 21303\n  Kind: 10\n  Gfx: 47\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Doerrflg: 1\n  EndObj\n"
        // A castle (Bauinfra 11: one per player).
        "  @Nummer: 16\n  Id: 21501\n  Kind: 14\n  Gfx: 50\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 3\n    Bauinfra: 11\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 30\n  EndObj\n"
        // Walls (straight, curve, T) and their beach variants three records on.
        "  @Nummer: 17\n  Id: 22501\n  Kind: 4\n  Gfx: 60\n  Size: 1, 1\n  Rotate: 1\n  KreuzBase: 17\n  Strandoff: 3\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 18\n  EndObj\n"
        "  @Nummer: 18\n  Id: 22502\n  Kind: 4\n  Gfx: 64\n  Size: 1, 1\n  Rotate: 1\n  KreuzBase: 17\n  Strandoff: 3\n"
        "  @Nummer: 19\n  Id: 22503\n  Kind: 4\n  Gfx: 68\n  Size: 1, 1\n  Rotate: 1\n  KreuzBase: 17\n  Strandoff: 3\n"
        "  @Nummer: 20\n  Id: 22541\n  Kind: 5\n  Gfx: 72\n  Size: 1, 1\n  Rotate: 1\n  KreuzBase: 20\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 20\n  EndObj\n"
        "  @Nummer: 21\n  Id: 22542\n  Kind: 5\n  Gfx: 76\n  Size: 1, 1\n  Rotate: 1\n  KreuzBase: 20\n"
        "  @Nummer: 22\n  Id: 22543\n  Kind: 5\n  Gfx: 80\n  Size: 1, 1\n  Rotate: 1\n  KreuzBase: 20\n"
        // Tower and beach tower, a gate, the pier.
        "  @Nummer: 23\n  Id: 20305\n  Kind: 6\n  Gfx: 84\n  Size: 1, 1\n  Rotate: 1\n  Strandoff: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 16\n    Radius: 8\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 40\n  EndObj\n"
        "  @Nummer: 24\n  Id: 20306\n  Kind: 7\n  Gfx: 88\n  Size: 1, 1\n  Rotate: 1\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 40\n  EndObj\n"
        "  @Nummer: 25\n  Id: 20304\n  Kind: 3\n  Gfx: 92\n  Size: 1, 1\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 15\n  EndObj\n"
        "  @Nummer: 26\n  Id: 21073\n  Kind: 30\n  Gfx: 96\n  Size: 1, 1\n  Rotate: 1\n"
        // A tree the player plants: growing, ripe; no withered record.
        // A second variant (RandAnz 2) eleven records on.
        "  @Nummer: 27\n  Id: 20211\n  Kind: 10\n  Gfx: 100\n  Size: 1, 1\n  AnimAnz: 5\n  AnimAdd: 1\n"
        "  RandAnz: 2\n  RandAdd: 11\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 900\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        "  @Nummer: 28\n  Id: 20212\n  Kind: 10\n  Gfx: 105\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
        // A triumphal arch (Bauinfra 13: by allowance).
        "  @Nummer: 29\n  Id: 21502\n  Kind: 14\n  Gfx: 110\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 3\n    Bauinfra: 13\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 30\n  EndObj\n"
        // The palisade (straight, curve, T) and its beach variants, a family of its own.
        "  @Nummer: 30\n  Id: 22521\n  Kind: 4\n  Gfx: 120\n  Size: 1, 1\n  Rotate: 1\n  KreuzBase: 30\n  Strandoff: "
        "3\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 10\n  EndObj\n"
        "  @Nummer: 31\n  Id: 22522\n  Kind: 4\n  Gfx: 124\n  Size: 1, 1\n  Rotate: 1\n  KreuzBase: 30\n  Strandoff: "
        "3\n"
        "  @Nummer: 32\n  Id: 22523\n  Kind: 4\n  Gfx: 128\n  Size: 1, 1\n  Rotate: 1\n  KreuzBase: 30\n  Strandoff: "
        "3\n"
        "  @Nummer: 33\n  Id: 22561\n  Kind: 5\n  Gfx: 132\n  Size: 1, 1\n  Rotate: 1\n  KreuzBase: 33\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 12\n  EndObj\n"
        "  @Nummer: 34\n  Id: 22562\n  Kind: 5\n  Gfx: 136\n  Size: 1, 1\n  Rotate: 1\n  KreuzBase: 33\n"
        "  @Nummer: 35\n  Id: 22563\n  Kind: 5\n  Gfx: 140\n  Size: 1, 1\n  Rotate: 1\n  KreuzBase: 33\n"
        // A plantation that harvests the crop above (ware 46) within 3 tiles.
        "  @Nummer: 36\n  Id: 20599\n  Kind: 14\n  Gfx: 144\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Ware: 6\n    Rohstoff: 46\n    Radius: 3\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 20\n  EndObj\n"
        // A forester that harvests the trees (ware 53) within 3 tiles.
        "  @Nummer: 37\n  Id: 20598\n  Kind: 14\n  Gfx: 148\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Ware: 23\n    Rohstoff: 53\n    Radius: 3\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 20\n  EndObj\n"
        // The second variant of the planted tree: growing, ripe.
        "  @Nummer: 38\n  Id: 20213\n  Kind: 10\n  Gfx: 152\n  Size: 1, 1\n  AnimAnz: 5\n  AnimAdd: 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 900\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
        "  @Nummer: 39\n  Id: 20214\n  Kind: 10\n  Gfx: 157\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
        // A river and the water mill that stands on it.
        "  @Nummer: 40\n  Id: 21401\n  Kind: 16\n  Gfx: 162\n  Size: 1, 1\n"
        "  @Nummer: 41\n  Id: 20509\n  Kind: 37\n  Gfx: 163\n  Size: 2, 2\n  Rotate: 4\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 100\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// An island of 30 x 30 tiles: sea x 0..3, beach x 4, ground beyond, a wood at x 20..24, y 5..9;
/// with `river_x` a river runs north-south along that column.
struct Island {
    assets::BuildingTable buildings = make_buildings();
    sim::World world{100, 100};
    std::vector<std::string> names{"Abemsberg", "Abmont"};

    explicit Island(std::uint32_t fertility = 0x2U, std::uint8_t climate = 0, std::int32_t river_x = -1) {
        sim::IslandMap map(30, 30);
        for (std::int32_t y = 0; y < 30; ++y) {
            for (std::int32_t x = 0; x < 30; ++x) {
                std::uint32_t id = kGround;
                if (x < 4) {
                    id = kSea;
                } else if (x == 4) {
                    id = kBeach;
                } else if (x == river_x) {
                    id = kRiver;
                } else if (x >= 20 && x <= 24 && y >= 5 && y <= 9) {
                    id = kTree;
                }
                // The beach turns its sea side west (rotation 1).
                REQUIRE(map.place(*buildings.by_id(id), x, y, id == kBeach ? 1 : 0, 0).ok());
            }
        }
        map.capture_ground();
        sim::WorldIsland island{assets::IslandHeader{}, std::move(map), 10, 10};
        island.header.fertility = fertility;
        island.header.climate = climate;
        REQUIRE(world.add_island(std::move(island)).ok());
        std::vector<sim::Player> players(2);
        players[0].role = sim::PlayerRole::Human;
        players[0].money = 10000;
        players[0].infra_mask = 0xFFFFFFFFU & ~(1U << 16);  // all but level 17 (Kontor II)
        players[1].role = sim::PlayerRole::Computer;
        players[1].money = 10000;
        world.set_players(players);
        sim::Ship ship = sim::make_ship("Patria", 12, 21, 21, 0, 0);
        ship.cargo.resize(4);
        ship.cargo[0] = sim::CargoSlot{buildings.materials().tools, 10 * 32, 0};
        ship.cargo[1] = sim::CargoSlot{buildings.materials().wood, 10 * 32, 0};
        world.add_ship(ship);
    }

    sim::BuildPlan build(std::vector<sim::BuildItem> items, std::optional<std::size_t> ship = std::nullopt,
                         sim::PlayerId player = 0) {
        sim::BuildOrder order;
        order.player = player;
        order.island = 0;
        order.items = std::move(items);
        if (ship) {
            order.ship = world.ships()[*ship].id;  // tests name the ship by its place in the list
        }
        return sim::execute_build(world, buildings, order, names, &events);
    }
    std::size_t demolish(sim::TilePos a, sim::TilePos b) {
        sim::DemolishOrder order{0, 0, a, b};
        return sim::execute_demolish(world, buildings, order, &events).removed;
    }
    const assets::BuildingType* at(std::int32_t x, std::int32_t y) const {
        const auto& cell = world.islands()[0].map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
        return buildings.by_sprite(cell.sprite);
    }
    std::uint8_t city_at(std::int32_t x, std::int32_t y) const {
        return world.islands()[0].map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).city;
    }
    /// Kontor at the beach, founded from the ship.
    void found() {
        const sim::BuildPlan plan = build({{kKontor, 5, 10, 0, 0}}, 0);
        REQUIRE(plan.placed == 1);
        // The ship unloads its wood (the founding leaves the cargo on board).
        sim::City* city = world.city(0, 0);
        city->ensure_ware(buildings.materials().wood);
        city->goods[buildings.materials().wood] = 6 * 32;
    }

    std::vector<sim::ConstructionEvent> events;
};

}  // namespace

TEST_CASE("construction: a new city passes over a name a city has already") {
    Island island;
    sim::City village;  // a native village with the list's next name
    village.island = 0;
    village.index = 5;
    village.owner = 1;
    village.name = island.names[0];
    island.world.add_city(std::move(village));
    const sim::BuildPlan plan = island.build({{kKontor, 5, 10, 0, 0}}, 0);
    REQUIRE(plan.placed == 1);
    REQUIRE(island.world.city(0, 0) != nullptr);
    CHECK(island.world.city(0, 0)->name == island.names[1]);
    CHECK(island.world.city_name_index() == 0);  // after the last, the first again
}

TEST_CASE("construction: the first Kontor at the coast founds a city, paid from the ship") {
    Island island;
    CHECK(!island.world.islands()[0].discovered_by(1));
    const sim::BuildPlan plan = island.build({{kKontor, 5, 10, 0, 0}}, 0);
    REQUIRE(plan.placed == 1);
    // The city of a human or a computer shows its island to its founder; to the other
    // players as the game setting says, which the simulation applies.
    CHECK(island.world.islands()[0].discovered_by(0));
    CHECK(!island.world.islands()[0].discovered_by(1));
    // The long side faces the beach on the left.
    CHECK(plan.items[0].rotation == 2);
    REQUIRE(island.world.cities().size() == 1);
    const sim::City& city = island.world.cities()[0];
    CHECK(city.name == "Abemsberg");
    CHECK(city.owner == 0);
    CHECK(city.warehouse_x == 5);
    CHECK(city.warehouse_y == 10);
    CHECK(island.world.city_name_index() == 1);
    // Territory: radius max(6, 8) around the Kontor, never the sea.
    CHECK(island.city_at(5, 10) == city.index);
    CHECK(island.city_at(13, 11) == city.index);
    CHECK(island.city_at(16, 11) == sim::MapCell::kNoCity);
    CHECK(island.city_at(2, 11) == sim::MapCell::kNoCity);
    CHECK(island.world.production_index(0, 5, 10).has_value());
    // The ship paid the goods, the player the gold.
    CHECK(island.world.ships()[0].load(island.buildings.materials().tools) == 7 * 32);
    CHECK(island.world.ships()[0].load(island.buildings.materials().wood) == 6 * 32);
    CHECK(island.world.players()[0].money == 10000 - 100);
    // One city per player and island.
    const sim::BuildPlan again = island.build({{kKontor, 5, 20, 0, 0}}, 0);
    CHECK(again.items[0].refusal == sim::BuildRefusal::CityLimit);
}

TEST_CASE("construction: an island holds as many cities as the rules allow, beyond the original's seven") {
    Island island;
    // Nine cities of others hold the numbers 0 to 8 (their land lies elsewhere).
    for (std::uint8_t i = 0; i < 9; ++i) {
        sim::City other;
        other.island = 0;
        other.index = i;
        other.owner = 1;
        other.name = "Stadt " + std::to_string(i);
        island.world.add_city(std::move(other));
    }
    // A world no game runs keeps the original's seven.
    CHECK(island.world.cities_per_island() == sim::original::kMaxCitiesPerIsland);
    CHECK(island.build({{kKontor, 5, 10, 0, 0}}, 0).items[0].refusal == sim::BuildRefusal::CityLimit);
    // The limit counts the cities, whatever their numbers.
    island.world.set_cities_per_island(9);
    CHECK(island.build({{kKontor, 5, 10, 0, 0}}, 0).items[0].refusal == sim::BuildRefusal::CityLimit);
    // The game's rules give the island room: the city takes the first free number, 9, and its land.
    const assets::FigureTable figures;
    const sim::Simulation game(island.world, island.buildings, figures, sim::Rules{});
    CHECK(island.world.cities_per_island() == 16);
    const sim::BuildPlan plan = island.build({{kKontor, 5, 10, 0, 0}}, 0);
    REQUIRE(plan.placed == 1);
    const sim::City* city = island.world.city(0, 9);
    REQUIRE(city != nullptr);
    CHECK(city->owner == 0);
    CHECK(island.city_at(5, 10) == 9);
    CHECK(island.city_at(13, 11) == 9);
    // A road inside its land is built there: the city's bit in the masks of its land holds.
    CHECK(island.build({{kRoad, 8, 10, 0, 0}}).placed == 1);
    CHECK(island.city_at(8, 10) == 9);
    // The tile word of the original keeps three bits: the export refuses rather than cut the number.
    const auto chunks = sim::savegame_chunks(island.world, island.buildings);
    REQUIRE(!chunks.ok());
    CHECK(chunks.error().code == annokit::core::ErrorCode::LimitExceeded);
    CHECK(chunks.error().message.find("more than 7 cities") != std::string::npos);
    // Open1602's own file keeps it.
    const auto bytes = sim::write_game(island.world, island.buildings, nullptr, sim::SaveOptions{false});
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, island.buildings);
    REQUIRE(loaded.ok());
    REQUIRE(loaded->city(0, 9) != nullptr);
    CHECK(loaded->islands()[0].map.top(5, 10).city == 9);
    // A number beyond the masks' bits is a damaged world.
    CHECK(sim::validate(*loaded).ok());
    loaded->city(0, 9)->index = sim::kCitySlotsPerIsland;
    CHECK(!sim::validate(*loaded).ok());
}

TEST_CASE("construction: a Kontor needs the coast along its long side and a ship nearby") {
    Island island;
    CHECK(island.build({{kKontor, 10, 10, 0, 0}}, 0).items[0].refusal == sim::BuildRefusal::NoCoast);
    island.world.ships()[0].x = 90;  // far away
    CHECK(island.build({{kKontor, 5, 10, 0, 0}}, 0).items[0].refusal == sim::BuildRefusal::ShipTooFar);
    // Without a ship there are no goods.
    CHECK(island.build({{kKontor, 5, 10, 0, 0}}).items[0].refusal == sim::BuildRefusal::Tools);
}

TEST_CASE("construction: roads fit to their neighbours, a drag stops when the gold runs out") {
    Island island;
    island.found();
    const sim::BuildPlan plan = island.build({{kRoad, 8, 14, 0, 0}, {kRoad, 9, 14, 0, 0}, {kRoad, 10, 14, 0, 0}});
    CHECK(plan.placed == 3);
    CHECK(plan.bill.money == 15);
    CHECK(island.at(9, 14)->id == kRoad);
    // A branch makes a T piece (mask W + E + S), its end a straight piece.
    CHECK(island.build({{kRoad, 9, 15, 0, 0}}).placed == 1);
    CHECK(island.at(9, 14)->id == kRoadT);
    CHECK(island.world.islands()[0].map.top(9, 14).rotation == 1);
    // A curve: W + S at (10, 15) after adding (10, 15) next to (9, 15) and (10, 14).
    CHECK(island.build({{kRoad, 10, 15, 0, 0}}).placed == 1);
    CHECK(island.at(10, 15)->id == kRoadCurve);
    // Gold for two roads only: the rest of the drag stays unbuilt.
    island.world.player(0)->money = 12;
    const sim::BuildPlan poor = island.build({{kRoad, 8, 17, 0, 0}, {kRoad, 9, 17, 0, 0}, {kRoad, 10, 17, 0, 0}});
    CHECK(poor.placed == 2);
    CHECK(poor.items[2].refusal == sim::BuildRefusal::Money);
    CHECK(island.world.players()[0].money == 2);
}

TEST_CASE("construction: a building on a buried treasure gives its gold to the builder and tells") {
    Island island;
    island.found();
    assets::IslandHeader::Deposit treasure;
    treasure.ware = sim::ware::kTreasure;
    treasure.x = 9;
    treasure.y = 14;
    treasure.amount = 1000;
    island.world.island(0).header.deposits.push_back(treasure);
    const std::int64_t before = island.world.players()[0].money;
    island.events.clear();
    // A road beside it finds nothing; the next one lies on it.
    CHECK(island.build({{kRoad, 8, 14, 0, 0}}).placed == 1);
    CHECK(island.world.players()[0].money == before - 5);
    CHECK(island.world.islands()[0].header.deposits.size() == 1);
    CHECK(island.build({{kRoad, 9, 14, 0, 0}}).placed == 1);
    CHECK(island.world.players()[0].money == before - 10 + 1000);
    CHECK(island.world.islands()[0].header.deposits.empty());
    const auto found = std::find_if(island.events.begin(), island.events.end(), [](const sim::ConstructionEvent& e) {
        return e.kind == sim::ConstructionEvent::Kind::TreasureFound;
    });
    REQUIRE(found != island.events.end());
    CHECK(std::count_if(island.events.begin(), island.events.end(), [](const sim::ConstructionEvent& e) {
              return e.kind == sim::ConstructionEvent::Kind::TreasureFound;
          }) == 1);
    CHECK(found->player == 0);
    CHECK(found->x == 9);
    CHECK(found->y == 14);
    CHECK((found->treasure == sim::TilePos{9, 14}));
    CHECK(found->money == 1000);
    CHECK(found->type == island.buildings.by_id(kRoad));
    // Found once: building there again finds nothing.
    island.events.clear();
    CHECK(island.demolish({9, 14}, {9, 14}) == 1);
    CHECK(island.build({{kRoad, 9, 14, 0, 0}}).placed == 1);
    CHECK(std::none_of(island.events.begin(), island.events.end(), [](const sim::ConstructionEvent& e) {
        return e.kind == sim::ConstructionEvent::Kind::TreasureFound;
    }));
}

TEST_CASE("construction: the preview shows what an order costs, affordable or not") {
    Island island;
    island.found();
    const auto preview = [&](std::vector<sim::BuildItem> items) {
        sim::BuildOrder order;
        order.player = 0;
        order.island = 0;
        order.items = std::move(items);
        return sim::plan_build(island.world, island.buildings, order);
    };
    const auto& house = island.buildings.by_id(kHouse)->build_cost;
    // Enough of everything: what is shown is what is paid.
    const sim::BuildPlan rich = preview({{kHouse, 8, 13, 0, 0}});
    CHECK(rich.placed == 1);
    CHECK(rich.shown.money == rich.bill.money);
    CHECK(rich.shown.wood == rich.bill.wood);
    // Without wood nothing is built, but the costs are still shown.
    island.world.city(0, 0)->goods[island.buildings.materials().wood] = 0;
    const sim::BuildPlan poor = preview({{kHouse, 8, 13, 0, 0}});
    CHECK(poor.items[0].refusal == sim::BuildRefusal::Wood);
    CHECK(poor.bill.money == 0);
    CHECK(poor.shown.money == house.money);
    CHECK(poor.shown.wood == house.wood);
    // The building under the mouse shows its costs where it cannot stand, too.
    const sim::BuildPlan outside = preview({{kHouse, 14, 11, 0, 0}});
    CHECK(outside.items[0].refusal == sim::BuildRefusal::Territory);
    CHECK(outside.shown.money == house.money);
    // A drag counts every item that fits the ground, also those beyond the gold.
    island.world.player(0)->money = 7;
    const sim::BuildPlan road =
        preview({{kRoad, 8, 17, 0, 0}, {kRoad, 9, 17, 0, 0}, {kRoad, 25, 25, 0, 0}, {kRoad, 10, 17, 0, 0}});
    CHECK(road.placed == 1);
    CHECK(road.items[2].refusal == sim::BuildRefusal::Territory);
    CHECK(road.bill.money == 5);
    CHECK(road.shown.money == 15);
}

TEST_CASE("construction: buildings stay inside their city, do not overlap and need their unlock") {
    Island island;
    island.found();
    CHECK(island.build({{kRoad, 25, 25, 0, 0}}).items[0].refusal == sim::BuildRefusal::Territory);
    // S-008: half outside is refused (the original annexed it).
    CHECK(island.build({{kHouse, 14, 11, 0, 0}}).items[0].refusal == sim::BuildRefusal::Territory);
    const sim::BuildPlan two = island.build({{kHouse, 8, 13, 0, 0}, {kHouse, 9, 13, 0, 0}});
    CHECK(two.placed == 1);
    CHECK(two.items[1].refusal == sim::BuildRefusal::Overlap);
    // Kontor II: locked until level 17, then it replaces Kontor I in place.
    CHECK(island.build({{kKontor2, 5, 11, 0, 0}}).items[0].refusal == sim::BuildRefusal::Locked);
    island.world.player(0)->infra_mask = 0xFFFFFFFFU;
    island.world.productions()[*island.world.production_index(0, 5, 10)].enabled = false;
    const sim::BuildPlan up = island.build({{kKontor2, 5, 11, 0, 0}});
    REQUIRE(up.placed == 1);
    CHECK(up.items[0].upgrade);
    CHECK(up.items[0].x == 5);
    CHECK(up.items[0].y == 10);
    CHECK(island.at(5, 10)->id == kKontor2);
    // The upgrade keeps the state of the building (the original reset it).
    CHECK(!island.world.production(0, 5, 10)->enabled);
    // A second Kontor in the city is refused.
    CHECK(island.build({{kKontor, 5, 16, 0, 0}}).items[0].refusal == sim::BuildRefusal::SecondKontor);
}

TEST_CASE("construction: the editor's free order pays nothing, unlocks everything and leaves treasures") {
    Island island;
    island.world.player(0)->money = 0;
    assets::IslandHeader::Deposit treasure;
    treasure.ware = sim::ware::kTreasure;
    treasure.x = 9;
    treasure.y = 14;
    treasure.amount = 500;
    island.world.island(0).header.deposits.push_back(treasure);
    sim::BuildOrder order;
    order.player = 0;
    order.island = 0;
    order.free = true;
    order.items = {{kKontor, 5, 10, 0, 0}};  // no ship, no goods, no gold
    REQUIRE(sim::execute_build(island.world, island.buildings, order, island.names, nullptr).placed == 1);
    order.items = {{kKontor2, 5, 11, 0, 0}, {kHouse, 8, 13, 0, 0}};  // Kontor II is locked for players
    const sim::BuildPlan plan = sim::execute_build(island.world, island.buildings, order, island.names, nullptr);
    CHECK(plan.placed == 2);
    CHECK(island.at(5, 10)->id == kKontor2);
    CHECK(island.world.players()[0].money == 0);
    CHECK(island.world.islands()[0].header.deposits.size() == 1);
    // Still checked like a player's order: the city's land, the terrain.
    order.items = {{kRoad, 25, 25, 0, 0}};
    CHECK(sim::plan_build(island.world, island.buildings, order).items[0].refusal == sim::BuildRefusal::Territory);
}

TEST_CASE("construction: the editor places and takes away buildings, each a step of undo") {
    Island island;
    sim::BuildOrder order;
    order.player = 1;
    order.island = 0;
    order.items = {{kKontor, 5, 10, 0, 0}};
    editor::PlacedBuildings kontor = editor::place_buildings(island.world, island.buildings, order, island.names);
    REQUIRE(kontor.step != nullptr);
    CHECK(island.world.cities().empty());  // made, not done yet
    kontor.step->apply(island.world);
    REQUIRE(island.world.cities().size() == 1);
    CHECK(island.world.cities()[0].owner == 1);
    order.items = {{kHouse, 8, 13, 0, 0}};
    editor::PlacedBuildings house = editor::place_buildings(island.world, island.buildings, order, island.names);
    REQUIRE(house.step != nullptr);
    house.step->apply(island.world);
    CHECK(island.world.house(0, 8, 13) != nullptr);
    // Refused where a player could not build: nothing to do, the refusal told.
    order.items = {{kHouse, 25, 25, 0, 0}};
    editor::PlacedBuildings outside = editor::place_buildings(island.world, island.buildings, order, island.names);
    CHECK(outside.step == nullptr);
    CHECK(outside.plan.items[0].refusal == sim::BuildRefusal::Territory);
    // The Kontor stays while its city has other buildings; the house goes.
    CHECK(!editor::remove_building(island.world, island.buildings, 0, {5, 10}).ok());
    CHECK(!editor::remove_building(island.world, island.buildings, 0, {20, 25}).ok());
    auto gone = editor::remove_building(island.world, island.buildings, 0, {9, 14});
    REQUIRE(gone.ok());
    (*gone)->apply(island.world);
    CHECK(island.world.house(0, 8, 13) == nullptr);
    // The Kontor last, its piers with it; a road left in its city does not fall to ruins.
    REQUIRE(island.at(4, 11)->id == 21073);
    order.player = 1;
    order.items = {{kRoad, 9, 16, 0, 0}};
    editor::PlacedBuildings road = editor::place_buildings(island.world, island.buildings, order, island.names);
    REQUIRE(road.step != nullptr);
    road.step->apply(island.world);
    auto kontor_gone = editor::remove_building(island.world, island.buildings, 0, {5, 10});
    REQUIRE(kontor_gone.ok());
    (*kontor_gone)->apply(island.world);
    CHECK(island.world.cities().empty());
    CHECK(island.at(4, 11)->id == kBeach);
    CHECK(island.at(9, 16)->id == kGround);
    CHECK(!editor::remove_building(island.world, island.buildings, 0, {4, 11}).ok());  // beach, not built
    (*kontor_gone)->revert(island.world);
    road.step->revert(island.world);
    (*gone)->revert(island.world);
    CHECK(island.world.house(0, 8, 13) != nullptr);
    house.step->revert(island.world);
    kontor.step->revert(island.world);
    CHECK(island.world.cities().empty());
    CHECK(island.at(5, 10)->id == kGround);
}

TEST_CASE("construction: houses get one inhabitant and leave nothing behind when demolished") {
    Island island;
    island.found();
    REQUIRE(island.build({{kHouse, 8, 13, 0, 0}}).placed == 1);
    const sim::House* h = island.world.house(0, 8, 13);
    REQUIRE(h != nullptr);
    CHECK(h->inhabitants() == 1);
    CHECK(island.world.cities()[0].inhabitants[0] == 1);
    // The Kontor stays while the city has other buildings.
    CHECK(island.demolish({5, 10}, {5, 10}) == 0);
    // A click removes a house.
    CHECK(island.demolish({9, 14}, {9, 14}) == 1);
    CHECK(island.world.house(0, 8, 13) == nullptr);
    CHECK(island.world.cities()[0].inhabitants[0] == 0);
    CHECK(island.at(8, 13)->id == kGround);
    CHECK(island.city_at(8, 13) == 0);
    // Now the Kontor may go, and the city with it.
    CHECK(island.demolish({5, 10}, {5, 10}) == 1);
    CHECK(island.world.cities().empty());
    CHECK(island.city_at(8, 13) == sim::MapCell::kNoCity);
}

TEST_CASE("construction: the demolish preview marks only what the order removes") {
    Island island;
    island.found();
    REQUIRE(island.build({{kHouse, 8, 13, 0, 0}}).placed == 1);
    REQUIRE(island.build({{kRoad, 10, 12, 0, 0}}).placed == 1);
    app::BuildTool tool(island.buildings);
    const auto contains = [](const std::vector<sim::TilePos>& tiles, std::int32_t x, std::int32_t y) {
        return std::ranges::find(tiles, sim::TilePos{x, y}) != tiles.end();
    };
    // Nothing unless demolishing.
    CHECK(tool.demolish_preview(island.world, 0, {0, 9, 14}).empty());
    tool.set_mode(app::BuildTool::Mode::Demolish);
    // Hovering: the whole house a click there would remove; bare ground nothing.
    const auto hover = tool.demolish_preview(island.world, 0, {0, 9, 14});
    CHECK(hover.size() == 4);
    CHECK(contains(hover, 8, 13));
    CHECK(contains(hover, 9, 14));
    CHECK(tool.demolish_preview(island.world, 0, {0, 20, 20}).empty());
    // Dragging: only the road; the rectangle leaves the house standing.
    tool.press({0, 7, 12});
    const auto drag = tool.demolish_preview(island.world, 0, {0, 10, 15});
    REQUIRE(drag.size() == 1);
    CHECK((drag[0] == sim::TilePos{10, 12}));
    const auto order = tool.demolish_order(0, {0, 10, 15});
    REQUIRE(order.has_value());
    CHECK(sim::plan_demolish(island.world, island.buildings, *order).size() == 1);
    // Another player's view marks none of them.
    CHECK(tool.demolish_preview(island.world, 1, {0, 10, 15}).empty());
    // A rectangle with nothing but houses in it takes the houses (Open1602).
    tool.release();
    tool.press({0, 7, 13});
    const auto houses = tool.demolish_preview(island.world, 0, {0, 9, 15});
    CHECK(houses.size() == 4);
    CHECK(contains(houses, 8, 13));
    const auto only_houses = tool.demolish_order(0, {0, 9, 15});
    REQUIRE(only_houses.has_value());
    CHECK((sim::plan_demolish(island.world, island.buildings, *only_houses) == std::vector<sim::TilePos>{{8, 13}}));
}

TEST_CASE("construction: a market claims land; demolished, the land and what stands on it is lost") {
    Island island;
    island.found();
    REQUIRE(island.build({{kMarket, 11, 11, 0, 0}}).placed == 1);
    CHECK(island.city_at(16, 12) == 0);  // the market's circle (radius 5) goes further
    REQUIRE(island.build({{kRoad, 16, 12, 0, 0}}).placed == 1);
    CHECK(island.demolish({11, 11}, {11, 11}) == 1);
    CHECK(island.city_at(16, 12) == sim::MapCell::kNoCity);
    CHECK(island.at(16, 12)->id == kGround);  // no ruin record: the ground comes back
    CHECK(island.city_at(12, 11) == 0);       // the Kontor still covers this
}

TEST_CASE("construction: a market needs one full edge in the city, like the original") {
    Island island;
    island.found();
    // The Kontor's circle ends between x 14 and 15 on rows 11 and 12: the left edge is inside.
    REQUIRE(island.city_at(14, 11) == 0);
    REQUIRE(island.city_at(14, 12) == 0);
    REQUIRE(island.city_at(15, 11) == sim::MapCell::kNoCity);
    REQUIRE(island.city_at(15, 12) == sim::MapCell::kNoCity);
    // A house there is refused (S-008), the market stands and claims its circle.
    CHECK(island.build({{kHouse, 14, 11, 0, 0}}).items[0].refusal == sim::BuildRefusal::Territory);
    REQUIRE(island.build({{kMarket, 14, 11, 0, 0}}).placed == 1);
    CHECK(island.city_at(15, 11) == 0);
    CHECK(island.city_at(15, 12) == 0);
    // Demolished, it leaves only what the Kontor covers.
    CHECK(island.demolish({14, 11}, {14, 11}) == 1);
    CHECK(island.city_at(15, 11) == sim::MapCell::kNoCity);
    CHECK(island.city_at(14, 11) == 0);
    // Only a corner inside is not enough.
    REQUIRE(island.city_at(12, 16) == 0);
    REQUIRE(island.city_at(13, 16) == sim::MapCell::kNoCity);
    REQUIRE(island.city_at(12, 17) == sim::MapCell::kNoCity);
    CHECK(island.build({{kMarket, 12, 16, 0, 0}}).items[0].refusal == sim::BuildRefusal::Territory);
}

TEST_CASE("construction: with S-008 brought back every building needs only one edge in the city") {
    Island island;
    sim::OriginalBugs bugs;
    bugs.building_radius = true;
    island.world.set_original_bugs(bugs);
    island.found();
    REQUIRE(island.city_at(15, 11) == sim::MapCell::kNoCity);
    REQUIRE(island.build({{kHouse, 14, 11, 0, 0}}).placed == 1);
    CHECK(island.city_at(15, 11) == 0);  // the free land under it joins the city
    CHECK(island.city_at(15, 12) == 0);
    // A corner alone is still not enough.
    CHECK(island.build({{kHouse, 12, 16, 0, 0}}).items[0].refusal == sim::BuildRefusal::Territory);
}

TEST_CASE("construction: before a market goes, what would lose its land is counted (S-035)") {
    Island island;
    island.found();
    REQUIRE(island.build({{kMarket, 11, 11, 0, 0}}).placed == 1);
    REQUIRE(island.build({{kRoad, 16, 12, 0, 0}}).placed == 1);
    const sim::DemolishOrder market{0, 0, {11, 11}, {11, 11}};
    // Roads alone fall without asking: nothing worth a warning.
    CHECK(!sim::demolish_loss(island.world, island.buildings, market).any());
    REQUIRE(island.build({{kHouse, 15, 13, 0, 0}}).placed == 1);
    CHECK(island.city_at(16, 14) == 0);
    const sim::DemolishLoss loss = sim::demolish_loss(island.world, island.buildings, market);
    CHECK(loss.houses == 1);
    CHECK(loss.buildings == 0);
    CHECK(loss.any());
    // Demolishing the house itself takes no land from anything.
    CHECK(!sim::demolish_loss(island.world, island.buildings, sim::DemolishOrder{0, 0, {15, 13}, {15, 13}}).any());
    // Inside the Kontor's circle, nothing is lost with the market.
    island.demolish({15, 13}, {15, 13});
    REQUIRE(island.build({{kHouse, 8, 13, 0, 0}}).placed == 1);
    CHECK(!sim::demolish_loss(island.world, island.buildings, market).any());
}

TEST_CASE("construction: fields grow where the island is fertile, forest becomes meadow") {
    Island fertile;  // fertile for ware 46, ground Randwachs 100: every tile carries
    fertile.found();
    const sim::BuildPlan plan = fertile.build({{kField, 9, 12, 0, 0}});
    REQUIRE(plan.placed == 1);
    bool entry = false;
    for (const auto& g : fertile.world.growth().entries) {
        entry |= g.x == 9 && g.y == 12 && g.stage == 0;
    }
    CHECK(entry);
    // Demolished, the field gives back its ground.
    CHECK(fertile.demolish({9, 12}, {9, 12}) == 1);
    CHECK(fertile.at(9, 12)->id == kGround);

    // S-003: on an island where the crop does not grow, it cannot be planted.
    Island barren(0, 1);
    barren.found();
    std::size_t refused = 0;
    for (std::int32_t x = 7; x <= 12; ++x) {
        const sim::BuildPlan p = barren.build({{kField, x, 13, 0, 0}});
        refused += p.items[0].refusal == sim::BuildRefusal::Infertile ? 1U : 0U;
    }
    CHECK(refused == 4);  // pattern 0: 2 of 32 tiles carry (here x = 8 and 12)

    // With S-003 brought back they are planted withered and paid for, as the original does.
    Island original(0, 1);
    sim::OriginalBugs bugs;
    bugs.infertile_fields = true;
    original.world.set_original_bugs(bugs);
    original.found();
    const std::int32_t before = original.world.players()[0].money;
    const sim::BuildPlan withered = original.build({{kField, 7, 13, 0, 0}});
    REQUIRE(withered.placed == 1);
    CHECK(original.at(7, 13)->id != kField);
    CHECK(original.world.players()[0].money < before);
}

TEST_CASE("construction: with barren patches fertile every tile has the island's fertility") {
    // Poor ground, Randwachs 70 of 100: as in the original, it takes a share of the island's
    // fertility, so a crop the island grows fully still leaves tiles barren.
    const std::uint8_t tobacco = sim::ware::kTobaccoField;
    const auto barren = [&](bool even, std::uint32_t fertility, std::uint8_t climate, std::uint8_t drought) {
        Island island(fertility, climate);
        island.buildings.by_id(kGround)->edge_growth = 70 * 128 / 100;
        island.world.set_fertile_fields(even);
        std::size_t n = 0;
        for (std::int32_t y = 10; y < 18; ++y) {
            for (std::int32_t x = 6; x < 18; ++x) {
                n += sim::tile_fertile_at(island.world, island.buildings, 0, tobacco, x, y, drought) ? 0U : 1U;
            }
        }
        return n;
    };
    const std::uint32_t full = 1U << (tobacco - sim::ware::kCornField);
    CHECK(barren(false, full, 0, 0) > 0);
    CHECK(barren(true, full, 0, 0) == 0);
    // Tobacco in the north without its bit grows half: about half the tiles stay barren with the
    // setting too.
    const std::size_t half = barren(true, 0x0U, 0, 0);
    CHECK(half > 96 / 4);
    CHECK(half < 96 * 3 / 4);
    // An island that does not grow the crop still does not (but for the original's two tiles in 32).
    CHECK(barren(true, 0x0U, 1, 0) >= 96 * 28 / 32);
    // Droughts still take their share.
    CHECK(barren(true, full, 0, 0x60) > 0);
}

TEST_CASE("construction: the rule for fertile fields is kept in the savegame") {
    const assets::BuildingTable buildings = make_buildings();
    for (const bool all : {false, true}) {
        sim::World world(60, 40);
        sim::Rules rules;
        rules.fertile_fields = all;
        rules.climate_crops = all;
        rules.big_ore_veins = all;
        rules.reveal_resources = all;
        world.set_saved_rules(rules);
        auto bytes = sim::write_game(world, buildings);
        REQUIRE(bytes.ok());
        auto loaded = sim::read_game(*bytes, buildings);
        REQUIRE(loaded.ok());
        REQUIRE(loaded->saved_rules().has_value());
        CHECK(loaded->saved_rules()->fertile_fields == all);
        CHECK(loaded->saved_rules()->climate_crops == all);
        CHECK(loaded->saved_rules()->big_ore_veins == all);
        CHECK(loaded->saved_rules()->reveal_resources == all);
    }
    CHECK(!sim::Rules::original().climate_crops);
    CHECK(!sim::Rules::original().big_ore_veins);
    CHECK(!sim::Rules::original().fertile_fields);
    CHECK(!sim::Rules::open1602().fertile_fields);
}

TEST_CASE("construction: forest is planted on every tile, fertile or not") {
    // The fertility check of S-003 is for crops that wither. Trees have no withered record; the
    // original plants them everywhere and the forester harvests them everywhere.
    for (const std::uint32_t fertility : {0x0U, 0x2U, 1U << (53 - 45)}) {
        Island island(fertility);
        island.found();
        for (std::int32_t x = 7; x <= 12; ++x) {
            const sim::BuildPlan p = island.build({{kSapling, x, 13, 0, 0}});
            CHECK(p.items[0].refusal == sim::BuildRefusal::None);
            CHECK(island.at(x, 13)->id == kSapling);
        }
    }
    // During a drought too, the tree is planted as a growing tree, not as anything withered.
    Island dry(0x0U);
    dry.found();
    dry.world.island(0).drought.strength = 0x40;
    REQUIRE(dry.build({{kSapling, 9, 14, 0, 0}}).placed == 1);
    CHECK(dry.at(9, 14)->id == kSapling);
}

TEST_CASE("construction: building and demolishing are commands of the simulation") {
    Island island;
    assets::FigureTable figures;
    sim::Simulation simulation(island.world, island.buildings, figures);
    simulation.set_city_names(island.names);
    sim::BuildOrder order;
    order.player = 0;
    order.island = 0;
    order.items = {{kKontor, 5, 10, 0, 0}};
    order.ship = island.world.ships()[0].id;
    const std::uint64_t seq = simulation.submit(order);
    CHECK(island.world.cities().empty());  // applied at the next tick
    simulation.tick();
    REQUIRE(island.world.cities().size() == 1);
    auto results = simulation.take_command_results();
    REQUIRE(results.size() == 1);
    CHECK(results[0].sequence == seq);
    REQUIRE(results[0].build.has_value());
    CHECK(results[0].build->placed == 1);
    bool founded = false;
    for (const auto& e : simulation.take_construction_events()) {
        founded |= e.kind == sim::ConstructionEvent::Kind::CityFounded;
    }
    CHECK(founded);
    // While paused, commands still apply.
    simulation.set_speed(0);
    simulation.submit(sim::DemolishOrder{0, 0, {5, 10}, {5, 10}});
    simulation.advance(100);
    CHECK(island.world.cities().empty());
}

TEST_CASE("construction: a new city shows its island to the other players as the game setting says") {
    const auto founded_seen_by_other = [](sim::Reveal reveal) {
        Island island;
        assets::FigureTable figures;
        sim::Rules rules;
        rules.reveal = reveal;
        sim::Simulation simulation(island.world, island.buildings, figures, rules);
        simulation.set_city_names(island.names);
        sim::BuildOrder order;
        order.player = 0;
        order.island = 0;
        order.items = {{kKontor, 5, 10, 0, 0}};
        order.ship = island.world.ships()[0].id;
        simulation.submit(order);
        simulation.tick();
        REQUIRE(island.world.cities().size() == 1);
        CHECK(island.world.islands()[0].discovered_by(0));  // the founder always
        return island.world.islands()[0].discovered_by(1);
    };
    // As the original and with all settlements shown: at once.
    CHECK(founded_seen_by_other(sim::Reveal::Original));
    CHECK(founded_seen_by_other(sim::Reveal::All));
    // Nothing: the others find the island by ship.
    CHECK(!founded_seen_by_other(sim::Reveal::None));
}

TEST_CASE("construction: the game setting decides what a demolition gives back") {
    const auto house_back = [](sim::Refund mode, std::uint8_t percent) {
        Island island;
        island.found();
        sim::Rules rules;
        rules.demolish_refund = mode;
        rules.refund_percent = percent;
        REQUIRE(island.build({{kHouse, 8, 13, 0, 0}}).placed == 1);  // 10 gold, 1 t wood
        const std::int32_t money = island.world.players()[0].money;
        const std::uint32_t wood = island.world.cities()[0].stock(island.buildings.materials().wood);
        sim::DemolishOrder order{0, 0, {8, 13}, {8, 13}};
        const sim::DemolishResult r =
            sim::execute_demolish(island.world, island.buildings, order, nullptr, rules.refund_share());
        CHECK(r.removed == 1);
        CHECK(island.world.players()[0].money == money + r.refund.money);
        CHECK(island.world.cities()[0].stock(island.buildings.materials().wood) == wood + r.refund.wood);
        return r.refund;
    };
    // Original: nothing.
    const sim::BuildBill none = house_back(sim::Refund::None, 50);
    CHECK(none.money == 0);
    CHECK(none.wood == 0);
    // Full: everything.
    const sim::BuildBill full = house_back(sim::Refund::Full, 50);
    CHECK(full.money == 10);
    CHECK(full.wood == 32);
    // Partial: the share, rounded down (1/32 t for goods).
    const sim::BuildBill half = house_back(sim::Refund::Partial, 50);
    CHECK(half.money == 5);
    CHECK(half.wood == 16);
    const sim::BuildBill quarter = house_back(sim::Refund::Partial, 25);
    CHECK(quarter.money == 2);
    CHECK(quarter.wood == 8);
    CHECK(sim::Rules::original().refund_share() == 0);
}

TEST_CASE("construction: refunded goods only fill the free room of the warehouse") {
    Island island;
    island.found();
    REQUIRE(island.build({{kHouse, 8, 13, 0, 0}}).placed == 1);
    sim::City* city = island.world.city(0, 0);
    city->capacity = 30 * 32;
    city->goods[island.buildings.materials().wood] = 30 * 32;  // full
    sim::DemolishOrder order{0, 0, {8, 13}, {8, 13}};
    const sim::DemolishResult r = sim::execute_demolish(island.world, island.buildings, order, nullptr, 100);
    CHECK(r.refund.wood == 0);
    CHECK(r.refund.money == 10);
    CHECK(city->stock(island.buildings.materials().wood) == 30 * 32);
}

TEST_CASE("construction: a castle makes its city proud, once per player") {
    Island island;
    island.found();
    island.world.player(0)->infra_mask = 0xFFFFFFFFU;
    REQUIRE(island.build({{kCastle, 10, 12, 0, 0}}).placed == 1);
    CHECK(island.world.city(0, 0)->satisfaction_bonus == 800);
    CHECK(island.world.players()[0].castles_built == 1);
    const sim::BuildPlan again = island.build({{kCastle, 10, 15, 0, 0}});
    CHECK(again.placed == 0);
    CHECK(again.items[0].refusal == sim::BuildRefusal::Limit);
    CHECK(island.world.city(0, 0)->satisfaction_bonus == 800);
}

TEST_CASE("construction: the last allowed triumphal arch locks its level again") {
    Island island;
    island.found();
    sim::Player& p = *island.world.player(0);
    p.infra_mask = 0xFFFFFFFFU;
    p.set_unlocked(13, false);
    p.arches_allowed = 2;
    p.arches_built = 1;                                        // one allowance used up without an arch
    CHECK(island.build({{kArch, 10, 12, 0, 0}}).placed == 0);  // the level is missing
    p.set_unlocked(13, true);
    REQUIRE(island.build({{kArch, 10, 12, 0, 0}}).placed == 1);
    CHECK(p.arches_built == 2);
    CHECK(!p.unlocked(13));
    CHECK(island.build({{kArch, 10, 15, 0, 0}}).placed == 0);
}

TEST_CASE("construction: the castle limit holds anywhere in an order, not only for its first item") {
    Island island;
    island.found();
    island.world.player(0)->infra_mask = 0xFFFFFFFFU;
    const sim::BuildPlan plan = island.build({{kRoad, 9, 11, 0, 0}, {kCastle, 10, 12, 0, 0}, {kCastle, 10, 15, 0, 0}});
    CHECK(plan.items[0].refusal == sim::BuildRefusal::None);
    CHECK(plan.items[1].refusal == sim::BuildRefusal::None);
    CHECK(plan.items[2].refusal == sim::BuildRefusal::Limit);
    CHECK(island.world.players()[0].castles_built == 1);
}

TEST_CASE("construction: walls and towers on the beach, piers in front of a new Kontor") {
    Island island;
    island.found();
    // The Kontor at (5, 10), turned to the beach on its left, lays piers along x 4.
    for (std::int32_t y = 10; y < 13; ++y) {
        const sim::MapCell& c = island.world.islands()[0].map.top(4, static_cast<std::uint32_t>(y));
        REQUIRE(island.at(4, y) != nullptr);
        CHECK(island.at(4, y)->kind == 30);
        CHECK(c.rotation == 2);
        CHECK(c.city == island.city_at(5, 10));
    }
    // A wall on the beach becomes the beach wall, turned like the beach; it costs the land wall.
    const std::int32_t money = island.world.players()[0].money;
    REQUIRE(island.build({{kWall, 4, 15, 0, 0}}).placed == 1);
    REQUIRE(island.at(4, 15) != nullptr);
    CHECK(island.at(4, 15)->id == 22541);
    CHECK(island.world.islands()[0].map.top(4, 15).rotation == 1);
    CHECK(island.world.players()[0].money == money - 18);
    // A second one below joins it: straight north-south, the sea still west.
    REQUIRE(island.build({{kWall, 4, 16, 0, 0}}).placed == 1);
    CHECK(island.at(4, 16)->id == 22541);
    CHECK(island.world.islands()[0].map.top(4, 16).rotation == 1);
    CHECK(island.world.islands()[0].map.top(4, 15).rotation == 1);
    // A land wall next to it on the ground stays a land wall; the beach wall turns into a curve.
    REQUIRE(island.build({{kWall, 5, 15, 0, 0}}).placed == 1);
    CHECK(island.at(5, 15)->kind == 4);
    CHECK(island.at(4, 15)->id == 22542);
    // A tower on the beach is the beach tower; a gate does not stand on the beach.
    REQUIRE(island.build({{kTower, 4, 18, 0, 0}}).placed == 1);
    CHECK(island.at(4, 18)->kind == 7);
    const sim::BuildPlan gate = island.build({{kGate, 4, 13, 0, 0}});
    CHECK(gate.placed == 0);
    CHECK(gate.items[0].refusal == sim::BuildRefusal::Terrain);
    // Demolished, the beach comes back.
    CHECK(island.demolish({4, 18}, {4, 18}) == 1);
    CHECK(island.at(4, 18)->id == kBeach);
}

TEST_CASE("construction: the water mill turns its wheel to the river") {
    // The river west of the mill: rotation 0; east of it: 2 (the shipyard's sides the other way round).
    for (const auto& [x, rotation] : {std::pair<std::int32_t, std::uint8_t>{9, 0}, {8, 2}}) {
        Island island(0x2U, 0, 9);
        island.found();
        const sim::BuildPlan plan = island.build({{kWaterMill, x, 9, 1, 0}});
        REQUIRE(plan.placed == 1);
        CHECK(plan.items[0].rotation == rotation);
        CHECK(island.world.islands()[0].map.top(static_cast<std::uint32_t>(x), 9).rotation == rotation);
    }
    // Away from the river it does not stand.
    Island island(0x2U, 0, 9);
    island.found();
    const sim::BuildPlan plan = island.build({{kWaterMill, 6, 14, 0, 0}});
    CHECK(plan.placed == 0);
    CHECK(plan.items[0].refusal == sim::BuildRefusal::NoRiver);
}

TEST_CASE("construction: a stone wall replaces a palisade, never the other way round") {
    Island island;
    island.found();
    // A palisade of three pieces, the first on the beach.
    REQUIRE(island.build({{22521, 4, 15, 0, 0}, {22521, 5, 15, 0, 0}, {22521, 6, 15, 0, 0}}).placed == 3);
    CHECK(island.at(4, 15)->kreuz_base == 33);
    CHECK(island.at(5, 15)->kreuz_base == 30);
    // The stone wall is built over it, piece by piece, and joins as the palisade did.
    REQUIRE(island.build({{kWall, 4, 15, 0, 0}, {kWall, 5, 15, 0, 0}, {kWall, 6, 15, 0, 0}}).placed == 3);
    CHECK(island.at(4, 15)->kreuz_base == 20);
    CHECK(island.at(5, 15)->kreuz_base == 17);
    CHECK(island.at(6, 15)->kreuz_base == 17);
    // Not back: a palisade does not replace a stone wall, on land or on the beach.
    for (const std::int32_t x : {4, 6}) {
        const sim::BuildPlan back = island.build({{22521, x, 15, 0, 0}});
        CHECK(back.placed == 0);
        CHECK(back.items[0].refusal == sim::BuildRefusal::Terrain);
    }
    CHECK(island.at(6, 15)->kreuz_base == 17);
}

TEST_CASE("construction: what the build menu offers") {
    sim::Player p;
    assets::BuildingType house;
    CHECK(sim::building_available(p, house));  // level 0: always
    assets::BuildingType school;
    school.infra = 3;
    CHECK(!sim::building_available(p, school));
    p.infra_mask = 1U << 2U;
    CHECK(sim::building_available(p, school));
    // Castle and cathedral once, monument and arch by allowance; the level is needed as well.
    assets::BuildingType castle;
    castle.infra = 11;
    p.infra_mask |= 1U << 10U;
    CHECK(sim::building_available(p, castle));
    p.castles_built = 1;
    CHECK(!sim::building_available(p, castle));
    assets::BuildingType arch;
    arch.infra = 13;
    p.infra_mask |= 1U << 12U;
    CHECK(!sim::building_available(p, arch));
    p.arches_allowed = 1;
    CHECK(sim::building_available(p, arch));
    p.arches_built = 1;
    CHECK(!sim::building_available(p, arch));
    assets::BuildingType monument;
    monument.infra = 14;
    p.monuments_allowed = 2;
    CHECK(!sim::building_available(p, monument));  // level 14 not unlocked
    p.infra_mask |= 1U << 13U;
    CHECK(sim::building_available(p, monument));
}

TEST_CASE("construction: buildings of mods may use sprites from 8192 on (review point 30)") {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 8\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20702\n  Kind: 1\n  Gfx: 8200\n  Size: 2, 2\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    const assets::BuildingType* mod = table->by_id(20702);
    REQUIRE(mod != nullptr);
    sim::IslandMap map(4, 4);
    REQUIRE(map.place(*mod, 1, 1, 0, 0).ok());
    // The masked sprite of the original's grid (8200 & 0x1FFF = 8) would be the ground.
    CHECK(map.top(1, 1).sprite >= 8200);
    const sim::SubTile tile = map.sub_tile(*table, 2, 2);
    CHECK(tile.building == mod);
    CHECK(tile.origin_x == 1);
    CHECK(tile.origin_y == 1);

    // Sprites must stay below the empty tile's 0xFFFF.
    const auto too_far = assets::cod::parse(
        "Objekt: HAUS\n  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 65534\n  Size: 1, 2\nEndObj\n", types);
    CHECK(!assets::BuildingTable::from_cod(too_far).ok());
}

TEST_CASE(
    "buildings: a modded haeuser.cod is refused where a number names something, saturated where it counts (HI-10, "
    "HI-17)") {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto load = [&](const std::string& lines) {
        const auto doc = assets::cod::parse(
            "Objekt: HAUS\n  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 8\n" + lines + "EndObj\n", types);
        return assets::BuildingTable::from_cod(doc);
    };
    const auto prodtyp = [&](const std::string& line) {
        return load("  Objekt: HAUS_PRODTYP\n    Kind: 9\n    " + line + "\n  EndObj\n");
    };
    REQUIRE(load("").ok());
    // Names of wares, figures, groups, ore mountains and ruins: beyond their range they would name
    // another one after a cast.
    CHECK(prodtyp("Ware: 255").ok());
    CHECK(!prodtyp("Ware: 258").ok());
    CHECK(!prodtyp("Rohstoff: 300").ok());
    CHECK(!prodtyp("Workstoff: 256").ok());
    CHECK(!prodtyp("Figurnr: 70000").ok());
    CHECK(!prodtyp("BGruppe: 256").ok());
    CHECK(!prodtyp("Erzbergnr: 256").ok());
    CHECK(!load("  Ruinenr: 300\n").ok());
    // A negative cost would pay for building.
    CHECK(!load("  Objekt: HAUS_BAUKOST\n    Money: -5\n  EndObj\n").ok());
    // Amounts saturate instead of wrapping.
    auto big = prodtyp("Maxlager: 200000000");
    REQUIRE(big.ok());
    CHECK(big->by_id(20101)->max_stock == 0xFFFF'FFFFU);
    auto counts = load("  RandAnz: -1\n  RandAdd: 70000\n");
    REQUIRE(counts.ok());
    CHECK(counts->by_id(20101)->rand_count == 0);
    CHECK(counts->by_id(20101)->rand_add == 0xFFFF);
    // Clamped before the product: no signed overflow (UBSan reports one), the largest values.
    auto fast = load("  Wegspeed: 100000000\n  Randwachs: 20000000\n");
    REQUIRE(fast.ok());
    CHECK(fast->by_id(20101)->walk_speed[0] == 126);
    CHECK(fast->by_id(20101)->edge_growth == 255);
    auto slow = load("  Wegspeed: -100000000\n  Randwachs: -20000000\n");
    REQUIRE(slow.ok());
    CHECK(slow->by_id(20101)->walk_speed[0] == 1);
    CHECK(slow->by_id(20101)->edge_growth == 0);
}

TEST_CASE("construction: \"plant all farm fields\" at a forester mixes the trees' variants like the build tool") {
    Island island;
    island.found();
    REQUIRE(island.build({{kForester, 9, 12, 0, 0}}).placed == 1);
    std::size_t first = 0;
    std::size_t second = 0;
    for (const std::uint32_t seed : {1U, 2U}) {
        const auto order = sim::plant_fields_order(island.world, island.buildings, 0, 0, {9, 12}, kSapling, seed);
        REQUIRE(order);
        REQUIRE(order->items.size() > 8);
        for (const sim::BuildItem& item : order->items) {
            first += item.building == kSapling ? 1U : 0U;
            second += item.building == kSapling2 ? 1U : 0U;
        }
        // The same seed gives the same trees, so the order is the same on every computer.
        const auto again = sim::plant_fields_order(island.world, island.buildings, 0, 0, {9, 12}, kSapling, seed);
        REQUIRE(again);
        for (std::size_t i = 0; i < order->items.size(); ++i) {
            CHECK(again->items[i].building == order->items[i].building);
        }
    }
    CHECK(first > 0);
    CHECK(second > 0);
    CHECK(first + second > 16);
}

TEST_CASE("construction: a soldier on the footprint stops a building, one beside it does not (TB-7)") {
    Island island;
    island.found();
    const auto unit = [&](std::int32_t tx, std::int32_t ty) {
        sim::Soldier s;
        s.place_cell(2 * (10 + tx), 2 * (10 + ty));  // the island lies at world tile (10, 10)
        s.owner = 1;
        island.world.add_soldier(s);
    };
    unit(9, 14);   // on the house at (8, 13), 2 x 2
    unit(16, 13);  // two tiles right of the house at (12, 13)
    const sim::BuildPlan plan = island.build({{kHouse, 8, 13, 0, 0}, {kHouse, 12, 13, 0, 0}});
    CHECK(plan.items[0].refusal == sim::BuildRefusal::UnitInTheWay);
    CHECK(plan.items[1].refusal == sim::BuildRefusal::None);
    CHECK(plan.placed == 1);
}

TEST_CASE("construction: no watchtower within its range and two tiles of an enemy soldier") {
    Island island;
    island.found();
    const auto soldier = [&](std::int32_t tx, std::int32_t ty) {
        sim::Soldier s;
        s.place_cell(2 * (10 + tx), 2 * (10 + ty));  // the island lies at world tile (10, 10)
        s.owner = 1;
        s.island = 0;
        island.world.add_soldier(s);
    };
    soldier(8, 20);  // 10 tiles below the tower at (8, 10): its range of 8 and 2 more
    sim::Relation& war = island.world.diplomacy().relation(0, 1);
    war.peace = sim::Treaty::None;
    island.world.diplomacy().relation(1, 0).peace = sim::Treaty::None;
    REQUIRE(island.world.diplomacy().hostile(0, 1));
    CHECK(island.build({{kTower, 8, 10, 0, 0}}).items[0].refusal == sim::BuildRefusal::EnemyNear);
    // At peace, or as in the original: it may be built.
    sim::OriginalBugs bugs;
    bugs.towers_near_enemies = true;
    island.world.set_original_bugs(bugs);
    CHECK(island.build({{kTower, 8, 10, 0, 0}}).placed == 1);
    island.world.set_original_bugs(sim::OriginalBugs{});
    // Beyond the range and the two tiles it may be built.
    CHECK(island.build({{kTower, 8, 7, 0, 0}}).items[0].refusal == sim::BuildRefusal::None);
}

TEST_CASE("construction: the key \"plant all farm fields\" plants the farm's open ground, the nearest first") {
    Island island(1U << (46 - 45));  // fertile for the crop
    island.found();
    REQUIRE(island.build({{kPlantation, 9, 12, 0, 0}}).placed == 1);
    REQUIRE(island.build({{kField, 11, 14, 0, 0}}).placed == 1);  // a field it has already
    REQUIRE(island.build({{kRoad, 12, 11, 0, 0}}).placed == 1);
    island.world.island(0).map.set_sprite(8, 11, static_cast<std::uint16_t>(island.buildings.by_id(kTree)->gfx));
    const auto order = sim::plant_fields_order(island.world, island.buildings, 0, 0, {9, 12}, kField);
    REQUIRE(order);
    REQUIRE(!order->items.empty());
    CHECK(order->player == 0);
    bool over_tree = false;
    for (const sim::BuildItem& item : order->items) {
        CHECK(item.building == kField);
        // Not on the plantation, the field, the road or the Kontor (x 5..6, y 10..12).
        CHECK(!(item.x >= 9 && item.x <= 10 && item.y >= 12 && item.y <= 13));
        CHECK(!(item.x == 11 && item.y == 14));
        CHECK(!(item.x == 12 && item.y == 11));
        CHECK(!(item.x >= 5 && item.x <= 6 && item.y >= 10 && item.y <= 12));
        over_tree |= item.x == 8 && item.y == 11;
    }
    // The forest is cleared for the crop.
    CHECK(over_tree);
    // The nearest first: a tile next to the plantation.
    const sim::BuildItem& first = order->items.front();
    CHECK(first.x >= 8);
    CHECK(first.x <= 11);
    CHECK(first.y >= 11);
    CHECK(first.y <= 14);
    // As much as the money pays for: 12 gold, two fields of 5.
    island.world.player(0)->money = 12;
    sim::BuildOrder o = *order;
    CHECK(sim::execute_build(island.world, island.buildings, o, island.names, nullptr).placed == 2);
    CHECK(island.at(first.x, first.y)->id == kField);
    // Nothing for another building, another player's farm or a field that is not its own.
    CHECK(!sim::plant_fields_order(island.world, island.buildings, 0, 0, {5, 10}, kField));
    CHECK(!sim::plant_fields_order(island.world, island.buildings, 1, 0, {9, 12}, kField));
    CHECK(!sim::plant_fields_order(island.world, island.buildings, 0, 0, {9, 12}, 20211));
    // With money for all of it the tree gives way to a field and the road stays.
    island.world.player(0)->money = 100000;
    sim::BuildOrder all = *order;
    CHECK(sim::execute_build(island.world, island.buildings, all, island.names, nullptr).placed > 2);
    CHECK(island.at(8, 11)->id == kField);
    CHECK(island.at(12, 11)->kind == assets::kind::kRoad);
}

TEST_CASE("construction: \"plant all farm fields\" at a forester leaves the standing forest") {
    Island island;
    island.found();
    REQUIRE(island.build({{kForester, 9, 12, 0, 0}}).placed == 1);
    island.world.island(0).map.set_sprite(8, 11, static_cast<std::uint16_t>(island.buildings.by_id(kTree)->gfx));
    const auto order = sim::plant_fields_order(island.world, island.buildings, 0, 0, {9, 12}, kSapling);
    REQUIRE(order);
    REQUIRE(!order->items.empty());
    for (const sim::BuildItem& item : order->items) {
        CHECK(!(item.x == 8 && item.y == 11));
    }
}

TEST_CASE("construction: scripts build, demolish and destroy as the game would, and steer ships") {
    Island island;
    island.found();
    assets::FigureTable figures;
    sim::Simulation simulation(island.world, island.buildings, figures);
    simulation.set_city_names(island.names);
    std::vector<script::PackageScript> sources;
    sources.push_back(script::PackageScript{
        "builder",
        "function on_start()\n"
        "  print(game.build(0, 0, 'anno1602:20401', 7, 10), game.build(0, 0, 20401, 7, 10))\n"
        "  print(game.build(1, 0, 20401, 8, 10), game.demolish(0, 0, 7, 10), game.demolish(0, 0, 7, 10))\n"
        "  print(game.build(0, 0, 20401, 8, 12), game.destroy(0, 8, 12), game.destroy(0, 9, 12))\n"
        "  local ship = game.ships(0)[1]\n"
        "  print(game.move_ship(ship.id, 60, 61), game.move_ship(999, 60, 61))\n"
        "  game.build(0, 0, 'nothing', 1, 1)\n"
        "end\n",
        {}});
    script::GameScripts scripts(std::move(sources), island.world);
    simulation.set_scripts(&scripts);
    const std::int32_t money = island.world.players()[0].money;
    simulation.tick();
    simulation.set_scripts(nullptr);
    const auto notes = scripts.take_notes();
    REQUIRE(notes.size() == 5);
    CHECK(notes[0].text.starts_with("true\t"));
    CHECK(notes[0].text != "true\ttrue");       // the tile is taken now
    CHECK(!notes[1].text.starts_with("true"));  // not in another player's city
    CHECK(notes[1].text.ends_with("\t1\t0"));
    CHECK(notes[2].text == "true\ttrue\tfalse");
    CHECK(notes[3].text == "true\tfalse");
    CHECK(notes[4].error);
    CHECK(notes[4].text.find("game.build: no such building") != std::string::npos);
    CHECK(island.world.players()[0].money == money - 10);  // two roads, paid as the player builds
    const sim::Ship& ship = island.world.ships()[0];
    CHECK(ship.order.kind == sim::Target::Kind::Position);
    CHECK(ship.order.x == 60);
    CHECK(ship.order.y == 61);
}

namespace {

sim::BlueprintOrder blueprint_order(sim::BlueprintOrder::Kind kind, std::vector<sim::BuildItem> items = {}) {
    sim::BlueprintOrder order;
    order.player = 0;
    order.kind = kind;
    order.island = 0;
    order.items = std::move(items);
    return order;
}

}  // namespace

TEST_CASE("blueprints: placed for free, only in an own city, never a Kontor; a newer one replaces an older") {
    Island island;
    island.found();
    const std::int32_t money = island.world.players()[0].money;
    auto place = blueprint_order(sim::BlueprintOrder::Kind::Place,
                                 {{kHouse, 8, 13, 0, 0}, {kRoad, 9, 15, 0, 0}, {kHouse, 25, 25, 0, 0}});
    CHECK(sim::execute_blueprints(island.world, island.buildings, place, island.names, &island.events) == 2);
    CHECK(island.world.players()[0].money == money);
    CHECK(island.at(8, 13)->id == kGround);  // nothing is built
    REQUIRE(island.world.blueprints().size() == 2);
    CHECK(sim::blueprint_city(island.world, island.world.blueprints()[0]) == 0);
    CHECK(sim::blueprint_at(island.world, island.buildings, 0, 0, {8, 13}) == std::size_t{0});
    CHECK(!sim::blueprint_at(island.world, island.buildings, 1, 0, {8, 13}));  // only its owner's
    // Outside the city and Kontors are refused.
    sim::BuildOrder check;
    check.items = {{kHouse, 25, 25, 0, 0}, {kKontor, 5, 20, 0, 0}};
    const sim::BuildPlan plan = sim::plan_blueprints(island.world, island.buildings, check);
    CHECK(plan.items[0].refusal == sim::BuildRefusal::Territory);
    CHECK(plan.items[1].refusal != sim::BuildRefusal::None);
    // Costs count as for building.
    const sim::BuildBill bill = sim::blueprint_costs(island.world, island.buildings, 0, 0, 0);
    CHECK(bill.money == 15);
    // A road over the house's blueprint replaces it.
    place.items = {{kRoad, 8, 13, 0, 0}};
    CHECK(sim::execute_blueprints(island.world, island.buildings, place, island.names, &island.events) == 1);
    REQUIRE(island.world.blueprints().size() == 2);
    CHECK(island.world.blueprints()[1].building == kRoad);
    CHECK(island.world.blueprints()[1].x == 8);
    // Remove takes a rectangle.
    auto remove = blueprint_order(sim::BlueprintOrder::Kind::Remove);
    remove.from = {9, 16};
    remove.to = {9, 15};
    CHECK(sim::execute_blueprints(island.world, island.buildings, remove, island.names, &island.events) == 1);
    CHECK(island.world.blueprints().size() == 1);
}

TEST_CASE("blueprints: built in their order as the means reach; what is built over one takes its place") {
    Island island;
    island.found();
    auto place = blueprint_order(sim::BlueprintOrder::Kind::Place,
                                 {{kRoad, 8, 14, 0, 0}, {kRoad, 9, 14, 0, 0}, {kRoad, 10, 14, 0, 0}});
    REQUIRE(sim::execute_blueprints(island.world, island.buildings, place, island.names, &island.events) == 3);
    island.world.player(0)->money = 12;  // two roads
    auto build = blueprint_order(sim::BlueprintOrder::Kind::Build);
    build.city = 0;
    CHECK(sim::execute_blueprints(island.world, island.buildings, build, island.names, &island.events) == 2);
    CHECK(island.at(8, 14)->id == kRoad);
    CHECK(island.at(9, 14)->id == kRoad);
    REQUIRE(island.world.blueprints().size() == 1);
    CHECK(island.world.blueprints()[0].x == 10);
    // Building by hand over a blueprint drops it as well.
    island.world.player(0)->money = 1000;
    const std::size_t first = island.events.size();
    REQUIRE(island.build({{kRoad, 10, 14, 0, 0}}).placed == 1);
    sim::drop_covered_blueprints(island.world, island.buildings, std::span(island.events).subspan(first));
    CHECK(island.world.blueprints().empty());
    // One the means do not reach waits, and later ones are still built; roads first.
    place.items = {{kMarket, 11, 11, 0, 0}, {kHouse, 8, 11, 0, 0}, {kRoad, 12, 14, 0, 0}};
    REQUIRE(sim::execute_blueprints(island.world, island.buildings, place, island.names, &island.events) == 3);
    island.world.player(0)->money = 16;  // the road (5) and the house (10), not the market (50)
    CHECK(sim::execute_blueprints(island.world, island.buildings, build, island.names, &island.events) == 2);
    CHECK(island.at(12, 14)->id != kGround);
    CHECK(island.at(8, 11)->id == kHouse);
    CHECK(island.world.players()[0].money == 1);
    REQUIRE(island.world.blueprints().size() == 1);
    CHECK(island.world.blueprints()[0].building == kMarket);
}

TEST_CASE("blueprints: a copied piece builds what the means reach and plans the rest as blueprints") {
    Island island;
    island.found();
    assets::FigureTable figures;
    sim::Simulation simulation(island.world, island.buildings, figures);
    simulation.set_city_names(island.names);
    island.world.player(0)->money = 12;  // two roads, not the house between them
    sim::BuildOrder order;
    order.player = 0;
    order.island = 0;
    order.orient_to_road = false;
    order.items = {{kRoad, 8, 15, 0, 0}, {kHouse, 8, 13, 0, 0}, {kRoad, 9, 15, 0, 0}};
    // A plain order stops at the house: the second road stays unbuilt.
    const sim::BuildPlan plain = sim::plan_build(island.world, island.buildings, order);
    CHECK(plain.placed == 1);
    CHECK(plain.items[2].refusal == sim::BuildRefusal::Money);
    // A copied piece goes on after it, and the house becomes a blueprint.
    order.skip_unpaid = true;
    order.unpaid_as_blueprints = true;
    const sim::BuildPlan copied = sim::plan_build(island.world, island.buildings, order);
    CHECK(copied.placed == 2);
    CHECK(copied.items[1].refusal == sim::BuildRefusal::Money);
    CHECK(copied.items[2].ok());
    simulation.submit(order);
    simulation.tick();
    CHECK(island.at(8, 15)->id == kRoad);
    CHECK(island.at(9, 15)->id != kGround);
    CHECK(island.at(8, 13)->id == kGround);
    CHECK(island.world.players()[0].money == 2);
    REQUIRE(island.world.blueprints().size() == 1);
    CHECK(island.world.blueprints()[0].building == kHouse);
    CHECK(island.world.blueprints()[0].x == 8);
    CHECK(island.world.blueprints()[0].y == 13);
    // Only what lacks the means is planned: a piece on the sea is left out.
    island.world.player(0)->money = 0;
    order.items = {{kRoad, 1, 15, 0, 0}};
    simulation.submit(order);
    simulation.tick();
    CHECK(island.world.blueprints().size() == 1);
}

TEST_CASE("blueprints: a city builds them by itself, demolishing clears them, the game keeps them") {
    Island island;
    island.found();
    assets::FigureTable figures;
    sim::Simulation simulation(island.world, island.buildings, figures);
    simulation.set_city_names(island.names);
    simulation.submit(
        blueprint_order(sim::BlueprintOrder::Kind::Place, {{kHouse, 8, 13, 0, 0}, {kRoad, 12, 15, 0, 0}}));
    auto on = blueprint_order(sim::BlueprintOrder::Kind::Auto);
    on.city = 0;
    on.on = true;
    simulation.submit(on);
    simulation.tick();
    REQUIRE(island.world.blueprints().size() == 2);
    CHECK(sim::blueprint_auto(island.world, 0, 0, 0));
    CHECK(island.at(8, 13)->id == kGround);
    // Kept in the savegame, and part of the game's fingerprint.
    const std::uint64_t hash = simulation.state_hash();
    {
        auto bytes = sim::write_game(island.world, island.buildings);
        REQUIRE(bytes.ok());
        auto loaded = sim::read_game(*bytes, island.buildings);
        REQUIRE(loaded.ok());
        CHECK(loaded->blueprints() == island.world.blueprints());
        CHECK(loaded->blueprint_auto() == island.world.blueprint_auto());
    }
    // The demolish tool clears the road's blueprint.
    simulation.submit(sim::DemolishOrder{0, 0, {12, 15}, {12, 15}});
    simulation.tick();
    REQUIRE(island.world.blueprints().size() == 1);
    CHECK(simulation.state_hash() != hash);
    // Within 5 s of game time the city builds the house.
    for (std::uint32_t t = 0; t < sim::kBlueprintAutoMs / sim::Simulation::kTickMs; ++t) {
        simulation.tick();
    }
    CHECK(island.at(8, 13)->id == kHouse);
    CHECK(island.world.blueprints().empty());
    // Not for another player's city.
    auto foreign = on;
    foreign.player = 1;
    simulation.submit(foreign);
    simulation.tick();
    CHECK(!sim::blueprint_auto(island.world, 1, 0, 0));
}

namespace {

sim::MoveOrder move_order(sim::TilePos from, std::int32_t x, std::int32_t y, std::uint8_t rotation = 0) {
    sim::MoveOrder order;
    order.player = 0;
    order.island = 0;
    order.from = from;
    order.x = x;
    order.y = y;
    order.rotation = rotation;
    order.orient_to_road = false;
    return order;
}

}  // namespace

TEST_CASE("moving: a house takes its inhabitants along, also onto its own old place") {
    Island island;
    island.found();
    REQUIRE(island.build({{kHouse, 8, 13, 0, 0}}).placed == 1);
    sim::House* house = island.world.house(0, 8, 13);
    REQUIRE(house != nullptr);
    house->inhabitants64 = 5 * 64;
    island.world.city(0, 0)->inhabitants[0] = 5;
    const std::int32_t money = island.world.players()[0].money;
    // Free: one tile to the east, over half of where it stands.
    CHECK(sim::movable_at(island.world, island.buildings, 0, 0, {9, 14}).ok());
    const sim::MoveResult moved =
        sim::execute_move(island.world, island.buildings, move_order({9, 14}, 9, 13), false, 0, &island.events);
    CHECK(moved.moved);
    CHECK(island.world.players()[0].money == money);
    CHECK(island.at(8, 13)->id == kGround);
    CHECK(island.at(9, 13)->id == kHouse);
    CHECK(island.world.house(0, 8, 13) == nullptr);
    REQUIRE(island.world.house(0, 9, 13) != nullptr);
    CHECK(island.world.house(0, 9, 13)->inhabitants() == 5);
    CHECK(island.world.city(0, 0)->inhabitants[0] == 5);
    CHECK(island.world.houses().size() == 1);
}

TEST_CASE("moving: Kontors and markets stay, nothing leaves its city, the world is kept while planning") {
    Island island;
    island.found();
    REQUIRE(island.build({{kMarket, 8, 13, 0, 0}, {kHouse, 8, 16, 0, 0}}).placed == 2);
    CHECK(sim::movable_at(island.world, island.buildings, 0, 0, {5, 10}).refusal == sim::MoveRefusal::Holds);
    CHECK(sim::movable_at(island.world, island.buildings, 0, 0, {8, 13}).refusal == sim::MoveRefusal::Holds);
    CHECK(sim::movable_at(island.world, island.buildings, 0, 0, {12, 12}).refusal == sim::MoveRefusal::NoBuilding);
    CHECK(sim::movable_at(island.world, island.buildings, 1, 0, {8, 16}).refusal == sim::MoveRefusal::Nothing);
    // Out of the city, onto the market: refused, and the map is as it was.
    CHECK(sim::plan_move(island.world, island.buildings, move_order({8, 16}, 26, 26), false).items[0].refusal ==
          sim::BuildRefusal::Territory);
    CHECK(sim::plan_move(island.world, island.buildings, move_order({8, 16}, 8, 14), false).items[0].refusal ==
          sim::BuildRefusal::Terrain);
    CHECK(sim::plan_move(island.world, island.buildings, move_order({5, 10}, 12, 12), false).items[0].refusal ==
          sim::BuildRefusal::Immovable);
    CHECK(island.at(8, 16)->id == kHouse);
    CHECK(island.world.house(0, 8, 16) != nullptr);
    CHECK(
        !sim::execute_move(island.world, island.buildings, move_order({8, 16}, 8, 14), false, 0, &island.events).moved);
    CHECK(island.at(8, 16)->id == kHouse);
}

TEST_CASE("moving: as if built anew, the costs at the new place and the demolish refund for the old") {
    Island island;
    island.found();
    REQUIRE(island.build({{kPlantation, 8, 13, 0, 0}}).placed == 1);
    const auto index = island.world.production_index(0, 8, 13);
    REQUIRE(index);
    island.world.productions()[*index].stock = 3 * 32;
    island.world.productions()[*index].raw = 32;
    const std::int32_t money = island.world.players()[0].money;
    const sim::MoveResult moved =
        sim::execute_move(island.world, island.buildings, move_order({8, 13}, 12, 13), true, 50, &island.events);
    REQUIRE(moved.moved);
    CHECK(moved.paid.money == 20);
    CHECK(moved.refund.money == 10);
    CHECK(island.world.players()[0].money == money - 10);
    // The stock and the raw material went along.
    const sim::Production* p = island.world.production(0, 12, 13);
    REQUIRE(p != nullptr);
    CHECK(p->stock == 3 * 32);
    CHECK(p->raw == 32);
    CHECK(island.world.production(0, 8, 13) == nullptr);
}

TEST_CASE("moving: the game setting, the command and the savegame") {
    Island island;
    island.found();
    REQUIRE(island.build({{kHouse, 8, 13, 0, 0}}).placed == 1);
    CHECK(sim::Rules::open1602().moving == sim::Moving::Free);
    CHECK(sim::Rules::original().moving == sim::Moving::Off);
    assets::FigureTable figures;
    sim::Rules off;
    off.moving = sim::Moving::Off;
    {
        sim::World copy = island.world;
        sim::Simulation simulation(copy, island.buildings, figures, off);
        simulation.submit(move_order({8, 13}, 12, 13));
        simulation.tick();
        CHECK(copy.house(0, 8, 13) != nullptr);  // not with the setting off
    }
    sim::Simulation simulation(island.world, island.buildings, figures);
    simulation.submit(move_order({8, 13}, 12, 13));
    simulation.tick();
    CHECK(island.world.house(0, 12, 13) != nullptr);
    for (const sim::Moving moving : {sim::Moving::Free, sim::Moving::Rebuild, sim::Moving::Off}) {
        sim::World world(60, 40);
        sim::Rules rules;
        rules.moving = moving;
        world.set_saved_rules(rules);
        auto bytes = sim::write_game(world, island.buildings);
        REQUIRE(bytes.ok());
        auto loaded = sim::read_game(*bytes, island.buildings);
        REQUIRE(loaded.ok());
        REQUIRE(loaded->saved_rules().has_value());
        CHECK(loaded->saved_rules()->moving == moving);
    }
}

TEST_CASE("construction: the editor renames a city, fills its stock and gives it to another player") {
    Island island;
    sim::BuildOrder order;
    order.player = 0;
    order.island = 0;
    order.items = {{kKontor, 5, 10, 0, 0}};
    editor::PlacedBuildings kontor = editor::place_buildings(island.world, island.buildings, order, island.names);
    REQUIRE(kontor.step != nullptr);
    kontor.step->apply(island.world);
    REQUIRE(island.world.cities().size() == 1);
    const std::uint8_t city = island.world.cities()[0].index;

    auto renamed = editor::rename_city(island.world, 0, city, "Neustadt");
    REQUIRE(renamed.ok());
    (*renamed)->apply(island.world);
    CHECK(island.world.city(0, city)->name == "Neustadt");
    (*renamed)->revert(island.world);
    CHECK(island.world.city(0, city)->name == "Abemsberg");
    CHECK(!editor::rename_city(island.world, 0, city, "").ok());
    CHECK(!editor::rename_city(island.world, 0, city, std::string(41, 'a')).ok());
    CHECK(!editor::rename_city(island.world, 0, 7, "Nirgends").ok());

    // The stock up to the room the Kontor gives, as the game keeps it.
    const std::uint32_t room = editor::city_room(island.world, island.buildings, 0, city);
    REQUIRE(room > 0);
    const std::uint8_t wood = island.buildings.materials().wood;
    auto filled = editor::set_city_goods(island.world, island.buildings, 0, city, wood, room + 1000);
    REQUIRE(filled.ok());
    (*filled)->apply(island.world);
    CHECK(island.world.city(0, city)->goods[wood] == room);
    auto emptied = editor::set_city_goods(island.world, island.buildings, 0, city, wood, 0);
    REQUIRE(emptied.ok());
    (*emptied)->apply(island.world);
    CHECK(island.world.city(0, city)->goods[wood] == 0);

    // The city and its land to the computer; not to the same owner again.
    auto taken = editor::change_city_owner(island.world, 0, city, 1);
    REQUIRE(taken.ok());
    (*taken)->apply(island.world);
    CHECK(island.world.city(0, city)->owner == 1);
    CHECK(island.world.islands()[0].map.top(5, 10).owner == 1);
    CHECK(!editor::change_city_owner(island.world, 0, city, 1).ok());
    CHECK(!editor::change_city_owner(island.world, 0, city, 9).ok());
    (*taken)->revert(island.world);
    CHECK(island.world.city(0, city)->owner == 0);
    // To the natives and to the pirates as well, not to the free trader.
    {
        sim::World world = island.world;
        std::vector<sim::Player> players = world.players();
        players.resize(5);
        sim::make_neutral(players[2], sim::faction::kNatives);
        sim::make_neutral(players[3], sim::faction::kPirates);
        sim::make_neutral(players[4], sim::faction::kTrader);
        world.set_players(players);
        auto natives = editor::change_city_owner(world, 0, city, 2);
        REQUIRE(natives.ok());
        (*natives)->apply(world);
        CHECK(world.city(0, city)->owner == 2);
        CHECK(world.islands()[0].map.top(5, 10).owner == 2);
        auto pirates = editor::change_city_owner(world, 0, city, 3);
        REQUIRE(pirates.ok());
        (*pirates)->apply(world);
        CHECK(world.city(0, city)->owner == 3);
        CHECK(!editor::change_city_owner(world, 0, city, 4).ok());
    }
    CHECK(island.world.islands()[0].map.top(5, 10).owner == 0);
}

TEST_CASE("construction: the editor gives a city land without a building, and takes it back") {
    Island island;
    island.found();
    const std::vector<std::pair<std::uint32_t, std::uint32_t>> far{{25, 25}, {26, 25}, {2, 2}, {25, 25}};
    CHECK(island.city_at(25, 25) == sim::MapCell::kNoCity);
    CHECK(!editor::claim_land(island.world, island.buildings, 0, 5, far, false).ok());  // no such city
    auto given = editor::claim_land(island.world, island.buildings, 0, 0, far, false);
    REQUIRE(given.ok());
    (*given)->apply(island.world);
    CHECK(island.city_at(25, 25) == 0);
    CHECK(island.city_at(26, 25) == 0);
    CHECK(island.city_at(2, 2) == sim::MapCell::kNoCity);  // never the sea
    CHECK(island.world.islands()[0].map.top(25, 25).owner == 0);
    CHECK(island.world.islands()[0].claimed_city(25, 25) == 0);
    // Given already: nothing changes.
    CHECK(!editor::claim_land(island.world, island.buildings, 0, 0, far, false).ok());
    // The city builds there, and its land stays when a market's circle goes.
    REQUIRE(island.build({{kRoad, 25, 25, 0, 0}}).placed == 1);
    REQUIRE(island.build({{kMarket, 11, 11, 0, 0}}).placed == 1);
    CHECK(island.demolish({11, 11}, {11, 11}) == 1);
    CHECK(island.city_at(26, 25) == 0);
    CHECK(island.at(25, 25)->id == kRoad);
    // The savegame keeps it.
    {
        auto bytes = sim::write_game(island.world, island.buildings);
        REQUIRE(bytes.ok());
        auto loaded = sim::read_game(*bytes, island.buildings);
        REQUIRE(loaded.ok());
        CHECK(loaded->islands()[0].claimed_city(26, 25) == 0);
        CHECK(loaded->islands()[0].claimed_city(20, 20) == sim::MapCell::kNoCity);
    }
    // Taken back: open land goes, the road stays the city's, as does land the Kontor covers.
    auto kontor_land = editor::claim_land(island.world, island.buildings, 0, 0, {{8, 10}}, false);
    REQUIRE(kontor_land.ok());
    (*kontor_land)->apply(island.world);
    auto taken = editor::claim_land(island.world, island.buildings, 0, 0, {{25, 25}, {26, 25}, {8, 10}}, true);
    REQUIRE(taken.ok());
    (*taken)->apply(island.world);
    CHECK(island.city_at(26, 25) == sim::MapCell::kNoCity);
    CHECK(island.world.islands()[0].map.top(26, 25).owner == sim::kNoPlayer);
    CHECK(island.city_at(25, 25) == 0);
    CHECK(island.city_at(8, 10) == 0);
    CHECK(island.world.islands()[0].claimed.empty());  // no claim left
    (*taken)->revert(island.world);
    CHECK(island.city_at(26, 25) == 0);
    CHECK(island.world.islands()[0].claimed_city(26, 25) == 0);
}

TEST_CASE("construction: the editor gives a ship a trade route that runs from the start") {
    Island island;
    island.found();
    const assets::FigureTable figures;
    const std::uint32_t id = island.world.ships()[0].id;
    using Op = sim::RouteEdit::Op;
    const auto edit = [&](sim::RouteEdit e) {
        auto op = editor::edit_route(island.world, island.buildings, figures, id, e);
        if (op.ok()) {
            (*op)->apply(island.world);
        }
        return op.ok();
    };
    const sim::Target city = sim::Target::city_of(0, 0);
    // Nothing to start yet; a stop only at a city.
    CHECK(!edit(sim::RouteEdit{sim::kNoPlayer, id, Op::Start}));
    CHECK(!edit(sim::RouteEdit{sim::kNoPlayer, id, Op::SetStop, 0, 0, sim::Target::position(30, 30)}));
    CHECK(!edit(sim::RouteEdit{sim::kNoPlayer, id, Op::SetStop, 0, 0, sim::Target::city_of(0, 4)}));
    CHECK(!editor::edit_route(island.world, island.buildings, figures, 999, sim::RouteEdit{}).ok());
    REQUIRE(edit(sim::RouteEdit{sim::kNoPlayer, id, Op::SetStop, 1, 0, city}));
    sim::RouteEdit wood{sim::kNoPlayer, id, Op::SetEntry, 1, 0};
    wood.value = sim::RouteEntry{island.buildings.materials().wood, true, 5 * 32};
    REQUIRE(edit(wood));
    REQUIRE(edit(sim::RouteEdit{sim::kNoPlayer, id, Op::Start}));
    const sim::Ship* ship = island.world.ship(id);
    CHECK(ship->route.active);
    CHECK(ship->route.current == 1);  // the first stop that is set
    CHECK(ship->target == city);
    CHECK(ship->route.stops[1].entries[0].unload);
    // The scenario keeps it, running.
    {
        auto bytes = sim::write_game(island.world, island.buildings);
        REQUIRE(bytes.ok());
        auto loaded = sim::read_game(*bytes, island.buildings);
        REQUIRE(loaded.ok());
        const sim::Ship& kept = loaded->ships()[0];
        CHECK(kept.route.active);
        CHECK(kept.target == city);
        CHECK(kept.route.stops[1].entries[0].amount == 5 * 32);
    }
    // Without stops it stops; switched off, the ship waits where it is.
    REQUIRE(edit(sim::RouteEdit{sim::kNoPlayer, id, Op::ClearStop, 1}));
    CHECK(!island.world.ship(id)->route.active);
    CHECK(island.world.ship(id)->target.empty());
    REQUIRE(edit(sim::RouteEdit{sim::kNoPlayer, id, Op::SetStop, 0, 0, city}));
    REQUIRE(edit(sim::RouteEdit{sim::kNoPlayer, id, Op::Start}));
    REQUIRE(edit(sim::RouteEdit{sim::kNoPlayer, id, Op::Stop}));
    CHECK(!island.world.ship(id)->route.active);
    CHECK(island.world.ship(id)->target.empty());
}

TEST_CASE("construction: the editor sets what a Kontor sells and buys, and a village trades its goods") {
    Island island;
    sim::BuildOrder order;
    order.player = 0;
    order.island = 0;
    order.items = {{kKontor, 5, 10, 0, 0}};
    editor::PlacedBuildings kontor = editor::place_buildings(island.world, island.buildings, order, island.names);
    REQUIRE(kontor.step != nullptr);
    kontor.step->apply(island.world);
    const std::uint8_t city = island.world.cities()[0].index;
    const std::uint32_t room = editor::city_room(island.world, island.buildings, 0, city);
    REQUIRE(room > 0);
    const std::uint8_t wood = island.buildings.materials().wood;
    const std::uint8_t tools = island.buildings.materials().tools;
    const std::uint16_t base = island.world.goods().price(wood);
    REQUIRE(base > 0);
    const sim::TradePriceRange range = sim::trade_price_range(island.world, wood);
    CHECK(range.max == base);
    CHECK(range.min == base * 300 / 1024);

    // Selling switched on: half the base price, nothing kept; then a price and an amount of its own,
    // the price within its range and the amount within the room.
    sim::TradeSetting s;
    s.ware = wood;
    s.mode = sim::TradeSetting::Mode::Sell;
    auto sell = editor::set_city_trade(island.world, island.buildings, 0, city, s);
    REQUIRE(sell.ok());
    (*sell)->apply(island.world);
    const sim::City::Trade* t = island.world.city(0, city)->trade_of(wood);
    REQUIRE(t != nullptr);
    CHECK(t->sell);
    CHECK(!t->buy);
    CHECK(t->sell_price == base * 64 / 128);
    CHECK(t->keep == 0);
    s.price = 9999;
    s.amount = room + 5000;
    auto priced = editor::set_city_trade(island.world, island.buildings, 0, city, s);
    REQUIRE(priced.ok());
    (*priced)->apply(island.world);
    t = island.world.city(0, city)->trade_of(wood);
    CHECK(t->sell_price == range.max);
    CHECK(t->keep == room);
    s.price = 0;
    auto cheap = editor::set_city_trade(island.world, island.buildings, 0, city, s);
    REQUIRE(cheap.ok());
    (*cheap)->apply(island.world);
    CHECK(island.world.city(0, city)->trade_of(wood)->sell_price == range.min);
    CHECK(!editor::set_city_trade(island.world, island.buildings, 0, city, s).ok());  // nothing changes

    // Buying instead: up to the room at three quarters of the base price; undo goes back.
    sim::TradeSetting b;
    b.ware = tools;
    b.mode = sim::TradeSetting::Mode::Buy;
    auto buy = editor::set_city_trade(island.world, island.buildings, 0, city, b);
    REQUIRE(buy.ok());
    (*buy)->apply(island.world);
    t = island.world.city(0, city)->trade_of(tools);
    CHECK(t->buy);
    CHECK(t->limit == room);
    CHECK(t->buy_price == island.world.goods().price(tools) * 96 / 128);
    (*buy)->revert(island.world);
    CHECK(!island.world.city(0, city)->trade_of(tools)->buy);

    // The scenario keeps it.
    auto bytes = sim::write_game(island.world, island.buildings);
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, island.buildings);
    REQUIRE(loaded.ok());
    REQUIRE(loaded->city(0, city) != nullptr);
    const sim::City::Trade* kept = loaded->city(0, city)->trade_of(wood);
    REQUIRE(kept != nullptr);
    CHECK(*kept == *island.world.city(0, city)->trade_of(wood));

    // Wrong goods, cities without a Kontor; no more wares than the Kontor's slots.
    sim::TradeSetting wrong = s;
    wrong.ware = 0;
    CHECK(!editor::set_city_trade(island.world, island.buildings, 0, city, wrong).ok());
    CHECK(!editor::set_city_trade(island.world, island.buildings, 0, 7, s).ok());
    const sim::TradeSlots slots = sim::trade_slots(island.world, island.buildings, *island.world.city(0, city));
    CHECK(slots.sells == 1);
    if (slots.limited()) {
        std::size_t sold = slots.sells;
        for (const std::uint8_t w : island.world.goods().trade_wares()) {
            if (sold >= slots.max_sells) {
                sim::TradeSetting more;
                more.ware = w;
                more.mode = sim::TradeSetting::Mode::Sell;
                CHECK(!editor::set_city_trade(island.world, island.buildings, 0, city, more).ok());
                break;
            }
            if (w == wood) {
                continue;
            }
            sim::TradeSetting more;
            more.ware = w;
            more.mode = sim::TradeSetting::Mode::Sell;
            auto op = editor::set_city_trade(island.world, island.buildings, 0, city, more);
            REQUIRE(op.ok());
            (*op)->apply(island.world);
            ++sold;
        }
    }

    // Set back: a player's city trades nothing.
    auto reset = editor::reset_city_trade(island.world, 0, city);
    REQUIRE(reset.ok());
    (*reset)->apply(island.world);
    for (const sim::City::Trade& each : island.world.city(0, city)->trade) {
        CHECK(!each.sell);
        CHECK(!each.buy);
    }

    // A native village founded in the editor trades as a new game's villages do: tobacco goods
    // in the north, alcohol and cloth bought up to 10 t, all for goodwill.
    std::vector<sim::Player> players = island.world.players();
    sim::Player natives;
    sim::make_neutral(natives, sim::faction::kNatives);
    players.push_back(natives);
    island.world.set_players(players);
    const auto village_owner = static_cast<sim::PlayerId>(players.size() - 1);
    sim::BuildOrder hut;
    hut.player = village_owner;
    hut.island = 0;
    hut.items = {{kKontor, 5, 22, 0, 0}};
    editor::PlacedBuildings founded = editor::place_buildings(island.world, island.buildings, hut, island.names);
    REQUIRE(founded.step != nullptr);
    founded.step->apply(island.world);
    REQUIRE(island.world.cities().size() == 2);
    const std::uint8_t vi = island.world.cities()[1].index;
    const sim::City& v = *island.world.city(0, vi);
    CHECK(v.owner == village_owner);
    REQUIRE(v.trade_of(assets::ware::kTobaccoGoods) != nullptr);
    CHECK(v.trade_of(assets::ware::kTobaccoGoods)->sell);
    CHECK(v.trade_of(assets::ware::kTobaccoGoods)->sell_price == 0);
    CHECK(!v.trade_of(assets::ware::kSpices)->sell);
    CHECK(v.trade_of(assets::ware::kAlcohol)->buy);
    CHECK(v.trade_of(assets::ware::kCloth)->limit == 10 * 32);
    CHECK(v.trade_of(assets::ware::kCloth)->buy_price == 0);
    // Set back after a change; in the south: spices.
    island.world.island(0).header.climate = 1;
    auto offer = editor::reset_city_trade(island.world, 0, vi);
    REQUIRE(offer.ok());
    (*offer)->apply(island.world);
    sim::City& south = *island.world.city(0, vi);
    CHECK(south.trade_of(assets::ware::kSpices)->sell);
    CHECK(!south.trade_of(assets::ware::kTobaccoGoods)->sell);

    // A village given to a player trades nothing (it would sell for nothing); the player's city
    // given to the natives gets a village's offer.
    auto taken = editor::change_city_owner(island.world, 0, vi, 1);
    REQUIRE(taken.ok());
    (*taken)->apply(island.world);
    for (const sim::City::Trade& each : island.world.city(0, vi)->trade) {
        CHECK(!each.sell);
        CHECK(!each.buy);
    }
    auto native = editor::change_city_owner(island.world, 0, city, village_owner);
    (*taken)->revert(island.world);
    REQUIRE(native.ok());
    (*native)->apply(island.world);
    CHECK(island.world.city(0, city)->trade_of(assets::ware::kSpices)->sell);
    CHECK(island.world.city(0, city)->trade_of(assets::ware::kAlcohol)->buy);
}

TEST_CASE("construction: the editor puts ships on the water and units on land, and takes them away") {
    Island island;
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 1\n  Kind: 1\n  Gfx: 0\n  Blocknr: 1\n  Speed: 260\n  Maxenergy: 20\n"
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Speed: 500\n  Maxenergy: 50\n"
        "EndObj\n",
        types);
    auto figures = assets::FigureTable::from_cod(doc);
    REQUIRE(figures.ok());
    const sim::World before = island.world;
    using editor::UnitRefusal;

    // A ship on the open sea and on the sea inside the island's rectangle; not on land, not next
    // to the ship that is there, not for nobody.
    CHECK(editor::ship_fits(island.world, island.buildings, 3, 3) == UnitRefusal::None);
    CHECK(editor::ship_fits(island.world, island.buildings, 11, 15) == UnitRefusal::None);
    CHECK(editor::ship_fits(island.world, island.buildings, 20, 20) == UnitRefusal::NoWater);
    CHECK(editor::ship_fits(island.world, island.buildings, 13, 21) == UnitRefusal::Taken);
    CHECK(editor::ship_fits(island.world, island.buildings, -1, 3) == UnitRefusal::Outside);
    CHECK(!editor::place_ship(island.world, island.buildings, *figures, 0, 21, 20, 20, 0).ok());
    CHECK(!editor::place_ship(island.world, island.buildings, *figures, 5, 21, 3, 3, 0).ok());
    CHECK(!editor::place_ship(island.world, island.buildings, *figures, 0, 1, 3, 3, 0).ok());  // a unit
    auto ship = editor::place_ship(island.world, island.buildings, *figures, 1, 21, 3, 3, 6);
    REQUIRE(ship.ok());
    (*ship)->apply(island.world);
    REQUIRE(island.world.ships().size() == 2);
    const sim::Ship& placed = island.world.ships().back();
    CHECK(placed.owner == 1);
    CHECK(placed.x == 3);
    CHECK(placed.y == 3);
    CHECK(placed.direction == 6);
    CHECK(placed.health == figures->get(21)->ship.max_health);
    CHECK(placed.health > 0);
    CHECK(placed.cargo.size() == 4);
    CHECK(placed.name.empty());  // the game names it

    // A unit on the island's ground; not on the sea, not on a tree, not on another.
    CHECK(editor::unit_fits(island.world, island.buildings, 20, 20) == UnitRefusal::None);
    CHECK(editor::unit_fits(island.world, island.buildings, 3, 3) == UnitRefusal::NoGround);
    CHECK(editor::unit_fits(island.world, island.buildings, 31, 16) == UnitRefusal::NoGround);
    CHECK(!editor::place_unit(island.world, island.buildings, *figures, 0, 21, 20, 20, 0).ok());  // a ship
    auto unit = editor::place_unit(island.world, island.buildings, *figures, 0, 1, 20, 20, 2);
    REQUIRE(unit.ok());
    (*unit)->apply(island.world);
    REQUIRE(island.world.soldiers().size() == 1);
    const sim::Soldier soldier = island.world.soldiers().back();
    CHECK(soldier.owner == 0);
    CHECK(soldier.island == 0);
    CHECK(soldier.health == figures->get(1)->combat.max_health);
    CHECK(soldier.health > 0);
    CHECK(soldier.direction == 2);
    CHECK(soldier.pos_x / sim::kPosUnit == 20);
    CHECK(editor::unit_fits(island.world, island.buildings, 20, 20) == UnitRefusal::Taken);

    // Its holds loaded: a trade ware up to a hold's room, emptied with 0 t.
    {
        const std::uint32_t id = island.world.ships().back().id;
        REQUIRE(!island.world.goods().trade_wares().empty());
        const std::uint8_t ware = island.world.goods().trade_wares().front();
        CHECK(!editor::set_ship_cargo(island.world, id, 9, ware, 32).ok());
        CHECK(!editor::set_ship_cargo(island.world, id, 0, ware, sim::kHoldCapacity + 1).ok());
        CHECK(!editor::set_ship_cargo(island.world, id + 100, 0, ware, 32).ok());
        auto loaded = editor::set_ship_cargo(island.world, id, 1, ware, 30 * 32);
        REQUIRE(loaded.ok());
        (*loaded)->apply(island.world);
        CHECK(island.world.ship(id)->cargo[1].ware == ware);
        CHECK(island.world.ship(id)->cargo[1].amount == 30U * 32U);
        auto emptied = editor::set_ship_cargo(island.world, id, 1, ware, 0);
        REQUIRE(emptied.ok());
        (*emptied)->apply(island.world);
        CHECK(island.world.ship(id)->cargo[1].ware == 0);
        (*emptied)->revert(island.world);
        (*loaded)->revert(island.world);
        CHECK(island.world.ship(id)->cargo[1].ware == 0);
    }

    // Taken away, with the orders that name them.
    const std::uint32_t ship_id = island.world.ships().back().id;
    island.world.ships().front().target = sim::Target{sim::Target::Kind::Ship, 0, 0, 0, 0, ship_id, 0};
    auto gone = editor::remove_ship(island.world, ship_id);
    REQUIRE(gone.ok());
    (*gone)->apply(island.world);
    CHECK(island.world.ship(ship_id) == nullptr);
    CHECK(island.world.ships().front().target.kind == sim::Target::Kind::None);
    CHECK(!editor::remove_ship(island.world, ship_id).ok());
    auto away = editor::remove_unit(island.world, soldier.id);
    REQUIRE(away.ok());
    (*away)->apply(island.world);
    CHECK(island.world.soldiers().empty());
    CHECK(!editor::remove_unit(island.world, soldier.id).ok());

    // Undone, all of it.
    (*away)->revert(island.world);
    (*gone)->revert(island.world);
    (*unit)->revert(island.world);
    (*ship)->revert(island.world);
    CHECK(island.world.ships().size() == before.ships().size());
    CHECK(island.world.soldiers().empty());
}

TEST_CASE("construction: the editor gives the pirates a ship of theirs") {
    Island island;
    std::vector<sim::Player> players = island.world.players();
    players.resize(3);
    sim::make_neutral(players[2], sim::faction::kPirates);
    island.world.set_players(players);
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 31\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 2\n  Speed: 500\n  Maxenergy: 60\n"
        "EndObj\n",
        types);
    auto figures = assets::FigureTable::from_cod(doc);
    REQUIRE(figures.ok());
    auto ship = editor::place_ship(island.world, island.buildings, *figures, 2, assets::figure::kPirate, 3, 3, 0);
    REQUIRE(ship.ok());
    (*ship)->apply(island.world);
    CHECK(island.world.ships().back().owner == 2);
    CHECK(island.world.ships().back().figure == assets::figure::kPirate);
    // Not for the free trader.
    players.resize(4);
    sim::make_neutral(players[3], sim::faction::kTrader);
    island.world.set_players(players);
    CHECK(!editor::place_ship(island.world, island.buildings, *figures, 3, assets::figure::kPirate, 6, 6, 0).ok());
}

TEST_CASE("construction: the editor gives the natives their spearmen, and them alone") {
    Island island;
    std::vector<sim::Player> players = island.world.players();
    players.resize(3);
    sim::make_neutral(players[2], sim::faction::kNatives);
    island.world.set_players(players);
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 33\n  Kind: 1\n  Gfx: 0\n  Blocknr: 1\n  Speed: 260\n  Maxenergy: 15\n"
        "EndObj\n",
        types);
    auto figures = assets::FigureTable::from_cod(doc);
    REQUIRE(figures.ok());
    CHECK(!editor::place_unit(island.world, island.buildings, *figures, 0, assets::figure::kSpearman, 20, 20, 0).ok());
    auto unit = editor::place_unit(island.world, island.buildings, *figures, 2, assets::figure::kSpearman, 20, 20, 0);
    REQUIRE(unit.ok());
    (*unit)->apply(island.world);
    REQUIRE(island.world.soldiers().size() == 1);
    CHECK(island.world.soldiers().back().owner == 2);
    CHECK(island.world.soldiers().back().figure == assets::figure::kSpearman);
    CHECK(island.world.soldiers().back().health == figures->get(assets::figure::kSpearman)->combat.max_health);
}

TEST_CASE("construction: the editor gives the free trader his ship, and him alone") {
    Island island;
    std::vector<sim::Player> players = island.world.players();
    players.resize(3);
    sim::make_neutral(players[2], sim::faction::kTrader);
    island.world.set_players(players);
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 29\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 2\n  Speed: 500\n  Maxenergy: 60\n"
        "EndObj\n",
        types);
    auto figures = assets::FigureTable::from_cod(doc);
    REQUIRE(figures.ok());
    auto ship = editor::place_ship(island.world, island.buildings, *figures, 2, assets::figure::kTrader, 3, 3, 0);
    REQUIRE(ship.ok());
    (*ship)->apply(island.world);
    const sim::Ship& placed = island.world.ships().back();
    CHECK(placed.owner == 2);
    CHECK(placed.trader);
    CHECK(placed.trader_goods.size() == island.world.goods().end());
    // Not his ship for a player, and no other ship for him.
    CHECK(!editor::place_ship(island.world, island.buildings, *figures, 0, assets::figure::kTrader, 6, 6, 0).ok());
}

TEST_CASE("construction: the editor names the pirates' nest and gives them one city on an island") {
    Island island;
    std::vector<sim::Player> players = island.world.players();
    players.resize(3);
    sim::make_neutral(players[2], sim::faction::kPirates);
    island.world.set_players(players);
    island.world.set_city_name_index(1);
    sim::BuildOrder order;
    order.player = 2;
    order.island = 0;
    order.items = {{kKontor, 5, 10, 0, 0}};
    editor::PlacedBuildings nest =
        editor::place_buildings(island.world, island.buildings, order, island.names, "Piratennest");
    REQUIRE(nest.step != nullptr);
    nest.step->apply(island.world);
    REQUIRE(island.world.cities().size() == 1);
    CHECK(island.world.cities()[0].owner == 2);
    CHECK(island.world.cities()[0].name == "Piratennest");
    CHECK(island.world.city_name_index() == 1);  // the settlers' list keeps its turn
    // A second city of theirs on the island is refused, as a player's.
    order.items = {{kKontor, 5, 20, 0, 0}};
    editor::PlacedBuildings second =
        editor::place_buildings(island.world, island.buildings, order, island.names, "Piratennest");
    CHECK(second.step == nullptr);
    REQUIRE(!second.plan.items.empty());
    CHECK(second.plan.items[0].refusal == sim::BuildRefusal::CityLimit);
    // Another nest elsewhere takes the next number.
    CHECK(editor::numbered_name(island.world, "Piratennest") == "Piratennest 2");
    CHECK(editor::numbered_name(island.world, "Eingeborenendorf") == "Eingeborenendorf");
}
