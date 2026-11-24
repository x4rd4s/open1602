// SPDX-License-Identifier: GPL-3.0-or-later
// A computer's attack from the sea (BT-1): at war, it picks an enemy city with a
// market or Kontor, sends an armed ship home for the soldiers beside its Kontor, plans a landing on
// a beach near the target's market and puts the troops ashore there.

#include <cstdint>
#include <string>
#include <tuple>
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
constexpr std::uint32_t kBeach = 20501;
constexpr std::uint32_t kKontor = 22101;
constexpr std::uint32_t kMarket = 20809;
constexpr std::uint32_t kHouse = 20605;
constexpr std::uint32_t kWall = 22501;
constexpr std::uint32_t kTree = 21304;
constexpr std::uint32_t kTower = 22601;
constexpr std::uint16_t kSword = 1;
constexpr std::uint16_t kShipFigure = 21;

constexpr sim::PlayerId kHuman = 0;
constexpr sim::PlayerId kComputer = 1;

assets::BuildingTable make_buildings() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}};
    const auto doc = assets::cod::parse(
        "Objekt: BGRUPPE\n  Nummer: 0\n  Prozent: 0\n  Maxwohn: 8\n  Steuer: 1.4\nEndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n  Wegspeed: 145, 120, 170, 100\n"
        "  @Nummer: +1\n  Id: 21201\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: +1\n  Id: 20501\n  Kind: 23\n  Gfx: 2\n  Size: 1, 1\n  Wegspeed: 145, 120, 170, 100\n"
        "  @Nummer: +1\n  Id: 22101\n  Kind: 35\n  Gfx: 30\n  Size: 2, 3\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  KreuzBase: 15\n  Ausbauflg: 1\n  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 14\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20809\n  Kind: 14\n  Gfx: 40\n  Size: 4, 3\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Radius: 16\n  EndObj\n"
        "  @Nummer: +1\n  Id: 20605\n  Kind: 14\n  Gfx: 50\n  Size: 2, 2\n  Maxenergy: 20\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n    Radius: 6\n  EndObj\n"
        "  @Nummer: +1\n  Id: 22501\n  Kind: 4\n  Gfx: 60\n  Size: 1, 1\n  Maxenergy: 6\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"
        "  @Nummer: +1\n  Id: 21304\n  Kind: 10\n  Gfx: 70\n  Size: 1, 1\n  Maxenergy: 4\n  NoShotFlg: 1\n"
        "  @Nummer: +1\n  Id: 22601\n  Kind: 6\n  Gfx: 80\n  Size: 1, 1\n  Maxenergy: 55\n  NoShotFlg: 1\n"
        "  Ruinenr: 255\n"  // a stone tower
        "  Objekt: HAUS_BAUKOST\n    Money: 300\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// With `guns` the ship fires (a range and a reload time), as a warship in the game data; with
/// `tower` a tower's figure (KANONTURM, 38) of that Hitpoint; with `slow` a second ship of the
/// same kind (22) that sails at a 25th of its speed.
assets::FigureTable make_figures(bool guns = false, const char* tower = nullptr, bool slow = false) {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        std::string("Objekt: FIGUR\n"
                    "  Nummer: 1\n  Kind: 1\n  Gfx: 0\n  Blocknr: 1\n  Speed: 260\n  Shotradius: 0.75\n"
                    "  Maxstepcnt: 1\n  Rohstoff: 11\n  Rohmenge: 3\n  Worktime: 0.8\n  Shottime: 0.6\n"
                    "  Maxenergy: 20\n  Hitpoint: 1.0\n"
                    "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
                    "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Preis: 1000\n  Rohstoff: 23\n"
                    "  Rohmenge: 25\n") +
            (guns ? "  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n" : "") +
            (slow ? std::string("  Nummer: 22\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n"
                                "  Speed: 20\n  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Preis: 1000\n"
                                "  Rohstoff: 23\n  Rohmenge: 25\n") +
                        (guns ? "  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n" : "")
                  : std::string()) +
            (tower != nullptr ? std::string("  Nummer: 38\n  Gfx: 0\n  Blocknr: 3\n  Maxenergy: 55\n  Hitpoint: ") +
                                    tower + "\n  Worktime: 3.0\n  Shotradius: 7.5\n"
                              : std::string()) +
            "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A world of 100 x 50 tiles of sea with two islands of 30 x 30: the computer's at (10, 10) with
/// its beach to the east (column 29), the human's at (60, 10) with its beach to the west (column 0).
struct Coast {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{100, 50};

    /// `human_beach`: the column of the human's beach, -1 for none.
    explicit Coast(std::int32_t human_beach = 0) {
        std::vector<sim::Player> players(2);
        players[kHuman].role = sim::PlayerRole::Human;
        players[kComputer].role = sim::PlayerRole::Computer;
        players[kComputer].money = 20000;
        world.set_players(players);
        add_island(10, 10, 29);
        add_island(60, 10, human_beach);
    }

    void add_island(std::uint32_t at_x, std::uint32_t at_y, std::int32_t beach) {
        sim::IslandMap map(30, 30);
        for (std::int32_t y = 0; y < 30; ++y) {
            for (std::int32_t x = 0; x < 30; ++x) {
                const std::uint32_t id = x == beach ? kBeach : kGround;
                REQUIRE(map.place(*buildings.by_id(id), x, y, 0, 0).ok());
            }
        }
        map.capture_ground();
        sim::WorldIsland island{assets::IslandHeader{}, std::move(map), at_x, at_y};
        island.header.number = static_cast<std::uint8_t>(world.islands().size());
        island.header.width = 30;
        island.header.height = 30;
        REQUIRE(world.add_island(std::move(island)).ok());
    }

    void build(std::uint32_t id, std::int32_t x, std::int32_t y, sim::PlayerId owner, std::uint16_t island) {
        const std::uint8_t city = static_cast<std::uint8_t>(island);
        REQUIRE(world.island(island).map.place(*buildings.by_id(id), x, y, 0, 0, owner, city).ok());
        if (world.city(island, city) == nullptr) {
            sim::City c;
            c.island = island;
            c.index = city;
            c.owner = owner;
            world.add_city(c);
        }
        const auto ux = static_cast<std::uint16_t>(x);
        const auto uy = static_cast<std::uint16_t>(y);
        if (id == kHouse) {
            sim::House h;
            h.island = island;
            h.x = ux;
            h.y = uy;
            h.city = city;
            h.inhabitants64 = 4 * 64;
            world.add_house(h);
            // Houses without inhabitants are given up, and a city of three buildings is no target.
            auto& n = world.city(island, city)->inhabitants;
            n.resize(1, 0);
            n[0] += 4;
        } else {
            sim::Production p;
            p.island = island;
            p.x = ux;
            p.y = uy;
            world.set_production(p);
        }
        if (id == kKontor) {
            world.city(island, city)->warehouse_x = ux;
            world.city(island, city)->warehouse_y = uy;
        }
    }

    void add(sim::PlayerId owner, std::int32_t cx, std::int32_t cy, std::uint16_t island) {
        sim::Soldier s;
        s.place_cell(cx, cy);
        s.figure = kSword;
        s.owner = owner;
        s.island = island;
        world.add_soldier(s);
    }

    [[nodiscard]] std::size_t soldiers_on(sim::PlayerId owner, std::uint16_t island) const {
        std::size_t n = 0;
        for (const sim::Soldier& s : world.soldiers()) {
            n += s.owner == owner && s.island == island && !s.away() ? 1U : 0U;
        }
        return n;
    }
};

void run(sim::Simulation& s, std::uint32_t ms) {
    for (std::uint32_t t = 0; t < ms; t += sim::Simulation::kTickMs) {
        s.tick();
    }
}

}  // namespace

TEST_CASE("ai landing: at war, a computer ships the soldiers beside its Kontor to a beach of an enemy city") {
    Coast a;
    a.build(kKontor, 28, 14, kComputer, 0);
    a.build(kMarket, 18, 14, kComputer, 0);
    a.build(kKontor, 0, 14, kHuman, 1);
    a.build(kMarket, 6, 6, kHuman, 1);
    for (std::int32_t k = 0; k < 4; ++k) {
        a.build(kHouse, 12 + 3 * k, 20, kHuman, 1);
    }
    for (std::int32_t k = 0; k < 6; ++k) {
        a.add(kComputer, 2 * (10 + 23) + k, 2 * (10 + 18), 0);
    }
    sim::Ship ship = sim::make_ship("Ship", 41, 15, kShipFigure, kComputer, 2);
    ship.cargo.resize(4);
    ship.cannons = 4;
    a.world.add_ship(ship);
    REQUIRE(a.world.diplomacy().set_peace(a.world, kHuman, kComputer, sim::Treaty::None, a.world.time_ms()));
    // The enemy's island is known, as the original shows every new city's island to all players.
    a.world.island(1).discover(kComputer);
    sim::Simulation s(a.world, a.buildings, a.figures);
    run(s, 75'000);
    // Four went aboard (the ship's holds) and are ashore on the enemy's island, beside the beach
    // nearest its market (column 0); two stayed at home.
    CHECK(a.soldiers_on(kComputer, 0) == 2);
    REQUIRE(a.soldiers_on(kComputer, 1) == 4);
    for (const sim::Soldier& soldier : a.world.soldiers()) {
        if (soldier.owner == kComputer && soldier.island == 1) {
            CHECK(soldier.pos_x / sim::kPosUnit - 60 <= 12);
        }
    }
    REQUIRE(a.world.islands()[1].map.top(0, 14).owner == kHuman);
    run(s, 2 * 60'000);
    CHECK(a.world.islands()[1].map.top(0, 14).owner != kHuman);  // the Kontor has fallen
}

TEST_CASE("ai landing: an improved computer attacks alone only with a ship that takes more than three") {
    // Probe p6: a ship alone sails on with more than kFewSoldiers aboard (missions(), case 5). The
    // strongest ship had three holds: it boarded three, called the attack off, and was taken again
    // seconds later, 14 times in a row. An improved computer takes the ship with room for more.
    Coast a;
    a.build(kKontor, 28, 14, kComputer, 0);
    a.build(kMarket, 18, 14, kComputer, 0);
    a.build(kKontor, 0, 14, kHuman, 1);
    a.build(kMarket, 6, 6, kHuman, 1);
    for (std::int32_t k = 0; k < 4; ++k) {
        a.build(kHouse, 12 + 3 * k, 20, kHuman, 1);
    }
    for (std::int32_t k = 0; k < 6; ++k) {
        a.add(kComputer, 2 * (10 + 23) + k, 2 * (10 + 18), 0);
    }
    // The stronger ship has a hold of goods: room for three.
    for (const auto& [y, goods, cannons] : {std::tuple{15, true, 6}, std::tuple{20, false, 4}}) {
        sim::Ship ship = sim::make_ship("Ship", 41, static_cast<std::uint32_t>(y), kShipFigure, kComputer, 2);
        ship.cargo.resize(4);
        if (goods) {
            ship.cargo[3].ware = 1;
            ship.cargo[3].amount = 32;
        }
        ship.cannons = static_cast<std::uint8_t>(cannons);
        a.world.add_ship(ship);
    }
    REQUIRE(a.world.diplomacy().set_peace(a.world, kHuman, kComputer, sim::Treaty::None, a.world.time_ms()));
    a.world.island(1).discover(kComputer);
    sim::Simulation s(a.world, a.buildings, a.figures);
    run(s, 75'000);
    CHECK(a.soldiers_on(kComputer, 1) == 4);  // the party of the ship with room for four
}

TEST_CASE("ai landing: an improved computer lands nobody on an island it has not discovered, the original's does") {
    const auto landed = [](bool original) {
        Coast a;
        a.build(kKontor, 28, 14, kComputer, 0);
        a.build(kMarket, 18, 14, kComputer, 0);
        a.build(kKontor, 0, 14, kHuman, 1);
        a.build(kMarket, 6, 6, kHuman, 1);
        for (std::int32_t k = 0; k < 4; ++k) {
            a.build(kHouse, 12 + 3 * k, 20, kHuman, 1);
        }
        for (std::int32_t k = 0; k < 6; ++k) {
            a.add(kComputer, 2 * (10 + 23) + k, 2 * (10 + 18), 0);
        }
        sim::Ship ship = sim::make_ship("Ship", 41, 15, kShipFigure, kComputer, 2);
        ship.cargo.resize(4);
        ship.cannons = 4;
        a.world.add_ship(ship);
        REQUIRE(a.world.diplomacy().set_peace(a.world, kHuman, kComputer, sim::Treaty::None, a.world.time_ms()));
        sim::Rules rules;
        rules.bugs.classic_ai = original;
        sim::Simulation s(a.world, a.buildings, a.figures, rules);
        run(s, 75'000);
        return a.soldiers_on(kComputer, 1);
    };
    CHECK(landed(true) > 0);
    CHECK(landed(false) == 0);
}

TEST_CASE("ai landing: an improved computer shoots a gap into a coast walled all along and lands through it") {
    Coast a;
    a.figures = make_figures(true);
    a.build(kKontor, 28, 14, kComputer, 0);
    a.build(kMarket, 18, 14, kComputer, 0);
    a.build(kKontor, 0, 14, kHuman, 1);
    a.build(kMarket, 6, 6, kHuman, 1);
    for (std::int32_t k = 0; k < 4; ++k) {
        a.build(kHouse, 12 + 3 * k, 20, kHuman, 1);
    }
    // The human's whole beach (column 0) is walled, but for the Kontor's.
    sim::IslandMap& map = a.world.island(1).map;
    for (std::int32_t y = 0; y < 30; ++y) {
        if (y < 14 || y > 16) {
            REQUIRE(map.place(*a.buildings.by_id(kWall), 0, y, 0, 0, kHuman, 1).ok());
        }
    }
    const auto walls = [&] {
        std::uint32_t n = 0;
        for (std::uint32_t y = 0; y < 30; ++y) {
            const sim::MapCell& c = a.world.islands()[1].map.top(0, y);
            n +=
                !c.empty() && a.buildings.by_sprite(c.sprite) != nullptr && a.buildings.by_sprite(c.sprite)->id == kWall
                    ? 1U
                    : 0U;
        }
        return n;
    };
    REQUIRE(walls() == 27);
    for (std::int32_t k = 0; k < 6; ++k) {
        a.add(kComputer, 2 * (10 + 23) + k, 2 * (10 + 18), 0);
    }
    // The troop ship, and a warship lying idle.
    for (const auto& [x, y] : {std::pair{41, 15}, std::pair{42, 20}}) {
        sim::Ship ship = sim::make_ship("Ship", static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                        kShipFigure, kComputer, 2);
        ship.cargo.resize(4);
        ship.cannons = 4;
        a.world.add_ship(ship);
    }
    REQUIRE(a.world.diplomacy().set_peace(a.world, kHuman, kComputer, sim::Treaty::None, a.world.time_ms()));
    a.world.island(1).discover(kComputer);
    sim::Simulation s(a.world, a.buildings, a.figures);
    run(s, 1'000);
    const std::uint32_t standing = walls();  // those outside the city's land have fallen to ruins
    REQUIRE(standing > 20);
    for (std::uint32_t t = 0; t < 8 * 60 && a.soldiers_on(kComputer, 1) == 0; ++t) {
        run(s, 1'000);
    }
    CHECK(walls() < standing);  // a gap
    CHECK(a.soldiers_on(kComputer, 1) > 0);
}

TEST_CASE("ai landing: an improved computer shoots trees behind the beach open to land") {
    // A ring of forest behind the human's beach, wider than the unload range: the beach is free, but
    // the soldiers find no cell to stand on. A ship fells the trees nearest to the beach.
    Coast a;
    a.figures = make_figures(true);
    a.build(kKontor, 28, 14, kComputer, 0);
    a.build(kMarket, 18, 14, kComputer, 0);
    a.build(kKontor, 0, 14, kHuman, 1);
    a.build(kMarket, 14, 6, kHuman, 1);
    for (std::int32_t k = 0; k < 4; ++k) {
        a.build(kHouse, 14 + 3 * k, 20, kHuman, 1);
    }
    sim::IslandMap& map = a.world.island(1).map;
    const auto is_tree = [&](std::uint32_t x, std::uint32_t y) {
        const sim::MapCell& c = a.world.islands()[1].map.top(x, y);
        return !c.empty() && a.buildings.by_sprite(c.sprite) != nullptr && a.buildings.by_sprite(c.sprite)->id == kTree;
    };
    for (std::int32_t y = 0; y < 30; ++y) {
        for (std::int32_t x = 1; x <= 10; ++x) {
            if (map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).empty() ||
                is_tree(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)) ||
                a.buildings.by_sprite(map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)).sprite)
                        ->id == kGround) {
                REQUIRE(map.place(*a.buildings.by_id(kTree), x, y, 0, 0, kHuman, 1).ok());
            }
        }
    }
    const auto trees = [&] {
        std::uint32_t n = 0;
        for (std::uint32_t y = 0; y < 30; ++y) {
            for (std::uint32_t x = 1; x <= 10; ++x) {
                n += is_tree(x, y) ? 1U : 0U;
            }
        }
        return n;
    };
    const std::uint32_t planted = trees();
    REQUIRE(planted > 250);
    for (std::int32_t k = 0; k < 6; ++k) {
        a.add(kComputer, 2 * (10 + 23) + k, 2 * (10 + 18), 0);
    }
    for (const auto& [x, y] : {std::pair{41, 15}, std::pair{42, 20}}) {
        sim::Ship ship = sim::make_ship("Ship", static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                        kShipFigure, kComputer, 2);
        ship.cargo.resize(4);
        ship.cannons = 4;
        a.world.add_ship(ship);
    }
    REQUIRE(a.world.diplomacy().set_peace(a.world, kHuman, kComputer, sim::Treaty::None, a.world.time_ms()));
    a.world.island(1).discover(kComputer);
    sim::Simulation s(a.world, a.buildings, a.figures);
    for (std::uint32_t t = 0; t < 8 * 60 && a.soldiers_on(kComputer, 1) == 0; ++t) {
        run(s, 1'000);
    }
    CHECK(trees() < planted);  // felled
    CHECK(a.soldiers_on(kComputer, 1) > 0);
}

TEST_CASE("ai landing: an improved computer has a warship shell the tower over the beach before it lands") {
    // Step 3b: the beach and the way inland lie within reach of the human's towers. With a warship
    // lying idle, it shells them while the troops wait aboard, when the towers' value is more than
    // the ship they would sink; with none the troop ship does. Under towers no ship can wreck the
    // computer lands only a party big enough to storm them.
    const std::vector<std::pair<std::int32_t, std::int32_t>> on_beach = {{2, 5}, {2, 22}};
    const auto play = [](bool warship, const char* tower_hit, std::uint32_t& towers_at_landing, bool& warship_whole,
                         const std::vector<std::pair<std::int32_t, std::int32_t>>& spots, std::int32_t party = 6,
                         std::uint32_t* landed = nullptr, bool slow = false, std::uint32_t aboard = 0) {
        Coast a;
        a.figures = make_figures(true, tower_hit, slow);
        a.build(kKontor, 28, 14, kComputer, 0);
        a.build(kMarket, 18, 14, kComputer, 0);
        a.build(kKontor, 0, 14, kHuman, 1);
        a.build(kMarket, 6, 6, kHuman, 1);
        for (std::int32_t k = 0; k < 4; ++k) {
            a.build(kHouse, 12 + 3 * k, 20, kHuman, 1);
        }
        for (const auto& [x, y] : spots) {
            REQUIRE(a.world.island(1).map.place(*a.buildings.by_id(kTower), x, y, 0, 0, kHuman, 1).ok());
            sim::Tower tw;
            tw.island = 1;
            tw.x = static_cast<std::uint16_t>(x);
            tw.y = static_cast<std::uint16_t>(y);
            tw.owner = kHuman;
            tw.figure = 38;  // KANONTURM
            a.world.towers().push_back(tw);
        }
        const auto towers = [&] {
            std::uint32_t n = 0;
            for (const auto& [x, y] : spots) {
                const sim::MapCell& c =
                    a.world.islands()[1].map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                n += !c.empty() && a.buildings.by_sprite(c.sprite) != nullptr &&
                             a.buildings.by_sprite(c.sprite)->id == kTower
                         ? 1U
                         : 0U;
            }
            return n;
        };
        for (std::int32_t k = 0; k < party; ++k) {
            a.add(kComputer, 2 * (10 + 23) + k, 2 * (10 + 18), 0);
        }
        std::vector<std::pair<int, int>> at{{41, 15}};
        if (warship) {
            at.emplace_back(42, 20);
        }
        for (const auto& [x, y] : at) {
            sim::Ship ship = sim::make_ship("Ship", static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                            slow && y == 20 ? 22U : kShipFigure, kComputer, 2);
            ship.cargo.resize(4);
            ship.cannons = 4;
            if (y == 15) {
                for (std::uint32_t k = 0; k < aboard; ++k) {
                    ship.cargo[k].ware = assets::unit_ware(kSword);  // back from a called-off landing
                    ship.cargo[k].amount = sim::kHoldCapacity;
                }
            }
            a.world.add_ship(ship);
        }
        REQUIRE(a.world.diplomacy().set_peace(a.world, kHuman, kComputer, sim::Treaty::None, a.world.time_ms()));
        a.world.island(1).discover(kComputer);
        sim::Simulation s(a.world, a.buildings, a.figures);
        REQUIRE(towers() == spots.size());
        for (std::uint32_t t = 0; t < 12 * 60 && a.soldiers_on(kComputer, 1) == 0 && a.world.ship(1) != nullptr; ++t) {
            run(s, 1'000);
        }
        towers_at_landing = towers();
        const bool on = a.soldiers_on(kComputer, 1) > 0;
        if (landed != nullptr) {
            // The convoy's ships land one after the other.
            *landed = 0;
            for (std::uint32_t t = 0; t < 60; ++t) {
                *landed = std::max(*landed, static_cast<std::uint32_t>(a.soldiers_on(kComputer, 1)));
                run(s, 1'000);
            }
        }
        const sim::Ship* w = a.world.ship(2);
        warship_whole = w != nullptr && (w->health == 0 || w->health == 50 * 32);  // Maxenergy 50, 1/32
        return on;
    };
    std::uint32_t towers = 0;
    bool whole = false;
    CHECK(play(true, "1.0", towers, whole, on_beach));
    CHECK(towers < 2);  // shelled first
    CHECK(play(false, "1.0", towers, whole, on_beach));
    CHECK(towers < 2);  // no warship free: the troop ship shells them itself
    // Towers whose fire would drive the ships off before they fall: no ship goes in under them,
    // and the troops do not land under them either.
    CHECK(!play(true, "40.0", towers, whole, on_beach));
    CHECK(towers == 2);
    CHECK(whole);
    // A tower inland over the way to the market, beyond the ships' range from any water: no ship
    // sails up to it. A party big enough to wreck it on land lands under it and storms it.
    CHECK(play(true, "1.0", towers, whole, {{10, 10}}));
    CHECK(towers == 1);
    CHECK(whole);
    // One too strong for the soldiers a ship takes: no party lands under it.
    CHECK(!play(true, "40.0", towers, whole, {{10, 10}}));
    CHECK(towers == 1);
    CHECK(whole);
    // One that the soldiers of one ship (4 holds) are too few for, but those of two are enough:
    // both ships take soldiers from home and land them together. Six soldiers: the second ship
    // takes two, too few alone, but the convoy's six are enough, and the ships take them on board
    // without calling each other's over.
    std::uint32_t landed = 0;
    CHECK(play(true, "6.0", towers, whole, {{10, 10}}, 6, &landed));
    CHECK(landed == 6);
    CHECK(!play(true, "6.0", towers, whole, {{10, 10}}, 4));
    // The second ship of the convoy sails slowly: the first, off the beach long before it, lands
    // no party too small alone, and the convoy turns home.
    CHECK(!play(true, "6.0", towers, whole, {{10, 10}}, 6, &landed, true));
    CHECK(landed == 0);
    // Four soldiers still aboard the first ship and four at home, too few alone: the convoy counts
    // both (trace trS: soldiers aboard were left out of the party).
    CHECK(play(true, "6.0", towers, whole, {{10, 10}}, 4, &landed, false, 4));
    CHECK(landed == 8);
}

TEST_CASE("ai landing: a convoy takes an empty armed supply ship off its route, after the other ships") {
    // Trace trV: in 32 of 50 skips the landing ships were short of holds, while armed supply ships
    // lay empty between two loads. A player takes such a ship off its route; one with goods aboard
    // keeps them for the city.
    Coast a;
    a.figures = make_figures(true);
    a.build(kKontor, 28, 14, kComputer, 0);
    for (const auto& [x, y] : std::vector<std::pair<int, int>>{{41, 15}, {42, 20}, {43, 25}, {44, 30}}) {
        sim::Ship ship = sim::make_ship("Ship", static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                        kShipFigure, kComputer, 2);
        ship.cargo.resize(4);
        ship.cannons = 4;
        if (y == 25) {
            ship.cargo[0].ware = 1;  // goods for the city
            ship.cargo[0].amount = 32;
        }
        a.world.add_ship(ship);
    }
    sim::Simulation s(a.world, a.buildings, a.figures);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    const auto& ships = a.world.ships();
    for (std::size_t k = 0; k < ships.size(); ++k) {
        sim::ai::ShipNote n;
        n.id = ships[k].id;
        n.supply = k == 0 || k == 2;  // the first: empty on its route; the third: with goods
        n.mission = k == 3 ? 1 : 0;   // the fourth: fetching troops
        p.ships.push_back(n);
    }
    const std::vector<std::uint32_t> want{ships[1].id, ships[0].id};
    CHECK(engine.landing_ship_ids(p) == want);
    // Its run brings tools the city lacks (supply_urgency, as the supply logic weighs it): it stays
    // on its route, so that the city's supply does not stop for the attack.
    sim::Ship* first = a.world.ship(ships[0].id);
    REQUIRE(first != nullptr);
    first->route.stops[0].target = sim::Target::city_of(0, 0);
    first->route.stops[0].entries[0] = sim::RouteEntry{a.buildings.materials().tools, true, 50 * 32};
    REQUIRE(a.world.city(0, 0)->stock(a.buildings.materials().tools) == 0);
    const std::vector<std::uint32_t> armed{ships[1].id};
    CHECK(engine.landing_ship_ids(p) == armed);
}

TEST_CASE("ai landing: a troop ship waits for the soldiers still walking aboard") {
    // Probe p5: a ship moved to water its soldiers can reach (move_to_board) lies beyond the
    // Kontor's reach of 8 tiles; with no soldier left near the Kontor it judged the three aboard
    // too few and called the attack off while the rest walked to it, ten times in a row. An improved
    // computer waits for them, up to its tries; the original decides at once.
    for (const bool classic : {false, true}) {
        Coast a;
        a.figures = make_figures(true);
        a.build(kKontor, 28, 14, kComputer, 0);
        a.build(kKontor, 0, 14, kHuman, 1);
        sim::Ship ship = sim::make_ship("Ship", 41, 15, kShipFigure, kComputer, 2);
        ship.cargo.resize(4);
        ship.cannons = 4;
        for (std::size_t k = 0; k < 3; ++k) {
            ship.cargo[k].ware = assets::unit_ware(kSword);
            ship.cargo[k].amount = sim::kHoldCapacity;
        }
        ship.order = sim::Target::position(41, 15);
        a.world.add_ship(ship);
        const std::uint32_t id = a.world.ships().back().id;
        a.add(kComputer, 2 * (10 + 8), 2 * (10 + 18), 0);  // 20 tiles from the Kontor
        a.world.soldiers().back().target = sim::Target::other_ship(id);
        sim::Rules rules;
        rules.bugs.classic_ai = classic;
        sim::Simulation s(a.world, a.buildings, a.figures, rules);
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::ai::AiPlayer p;
        p.player = kComputer;
        p.home_valid = true;
        p.home_island = 0;
        p.home_city = 0;
        sim::ai::ShipNote n;
        n.id = id;
        n.mission = 5;
        n.counter = 10;
        n.param = (1U << 8) | 1U;  // city 1 on island 1
        p.ships.push_back(n);
        engine.run_missions(p);
        CHECK((p.ships.front().mission == 5) == !classic);
    }
}

TEST_CASE("ai landing: an attack weighs the shelling with the warships free for it") {
    // Gate f24: attack_ok weighed the shelling with every armed ship, those on supply runs too, and
    // shell_towers then had only the free ones: "the warships would turn away first", the landing
    // called off, and the same city attacked again 30 s later, 14 times. The fleet is now the one
    // shell_towers will have: the free warships without the troop ship, or the troop ship alone.
    Coast a;
    a.figures = make_figures(true);
    a.build(kKontor, 28, 14, kComputer, 0);
    for (const auto& [x, y] : std::vector<std::pair<int, int>>{{41, 15}, {42, 20}, {43, 25}}) {
        sim::Ship ship = sim::make_ship("Ship", static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                        kShipFigure, kComputer, 2);
        ship.cargo.resize(4);
        ship.cannons = static_cast<std::uint8_t>(y == 15 ? 6 : 4);  // the first the strongest
        a.world.add_ship(ship);
    }
    sim::Simulation s(a.world, a.buildings, a.figures);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    const auto& ships = a.world.ships();
    for (std::size_t k = 0; k < ships.size(); ++k) {
        sim::ai::ShipNote n;
        n.id = ships[k].id;
        n.supply = k == 2;  // the third on a supply run, with goods aboard
        p.ships.push_back(n);
    }
    a.world.ship(ships[2].id)->cargo[0] = sim::CargoSlot{1, 32, 0, 0};
    const auto ids = [&] {
        std::vector<std::uint32_t> out;
        for (const sim::Ship* o : engine.attack_fleet(p)) {
            out.push_back(o->id);
        }
        return out;
    };
    // The strongest takes the troops; the second shells; the supply ship stays on its run.
    const std::vector<std::uint32_t> second{ships[1].id};
    CHECK(ids() == second);
    // With the second on a mission, the troop ship shells alone.
    p.ships[1].mission = 1;
    const std::vector<std::uint32_t> alone{ships[0].id};
    CHECK(ids() == alone);
}

TEST_CASE("ai landing: an improved computer starts no attack its landing would end at once") {
    // plan_landing ends a landing with 8 of the computer's soldiers on the island; the original's
    // attack_ok starts one up to 10. An improved computer's attack_ok holds to the 8, so that no
    // ship sails only to turn home.
    for (const bool classic : {false, true}) {
        Coast a;
        a.build(kKontor, 28, 14, kComputer, 0);
        a.build(kMarket, 18, 14, kComputer, 0);
        a.build(kKontor, 0, 14, kHuman, 1);
        a.build(kMarket, 6, 6, kHuman, 1);
        for (std::int32_t k = 0; k < 4; ++k) {
            a.build(kHouse, 12 + 3 * k, 20, kHuman, 1);
        }
        for (std::int32_t k = 0; k < 6; ++k) {
            a.add(kComputer, 2 * (10 + 23) + k, 2 * (10 + 18), 0);
        }
        for (std::int32_t k = 0; k < 8; ++k) {
            a.add(kComputer, 2 * (60 + 20) + k, 2 * (10 + 25), 1);  // already on the enemy's island
        }
        sim::Ship ship = sim::make_ship("Ship", 41, 15, kShipFigure, kComputer, 2);
        ship.cargo.resize(4);
        ship.cannons = 4;
        a.world.add_ship(ship);
        REQUIRE(a.world.diplomacy().set_peace(a.world, kHuman, kComputer, sim::Treaty::None, a.world.time_ms()));
        a.world.island(1).discover(kComputer);
        sim::Rules rules;
        rules.bugs.classic_ai = classic;
        sim::Simulation s(a.world, a.buildings, a.figures, rules);
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::ai::AiPlayer p;
        p.player = kComputer;
        p.home_valid = true;
        p.home_island = 0;
        p.home_city = 0;
        sim::ai::ShipNote n;
        n.id = a.world.ships().back().id;
        p.ships.push_back(n);
        CHECK(engine.attack_ok_for(p, *a.world.city(1, 1)) == classic);
    }
}

TEST_CASE("ai landing: an improved computer starts no attack whose shelling would not pay") {
    // attack_ok asked only whether some fleet finishes the shelling; shell_towers then refused one
    // that does not pay (a ship of 1000 sunk for a tower worth 300) and, with no party enough to
    // storm it, called the landing off, and attack_ok sent the ship again. attack_ok now weighs the
    // shelling as shell_towers will, and skips the city.
    Coast a;
    a.figures = make_figures(true, "30.0");
    a.build(kKontor, 28, 14, kComputer, 0);
    a.build(kMarket, 18, 14, kComputer, 0);
    a.build(kKontor, 0, 14, kHuman, 1);
    a.build(kMarket, 6, 6, kHuman, 1);
    for (std::int32_t k = 0; k < 4; ++k) {
        a.build(kHouse, 12 + 3 * k, 20, kHuman, 1);
    }
    for (const auto& [x, y] : std::vector<std::pair<int, int>>{{2, 5}, {2, 22}}) {
        REQUIRE(a.world.island(1).map.place(*a.buildings.by_id(kTower), x, y, 0, 0, kHuman, 1).ok());
        sim::Tower tw;
        tw.island = 1;
        tw.x = static_cast<std::uint16_t>(x);
        tw.y = static_cast<std::uint16_t>(y);
        tw.owner = kHuman;
        tw.figure = 38;  // KANONTURM
        a.world.towers().push_back(tw);
    }
    for (std::int32_t k = 0; k < 6; ++k) {
        a.add(kComputer, 2 * (10 + 23) + k, 2 * (10 + 18), 0);
    }
    for (std::uint32_t k = 0; k < 7; ++k) {  // a troop ship and six warships, which finish losing one
        sim::Ship ship = sim::make_ship("Ship", 41, 15 + 5 * k, kShipFigure, kComputer, 2);
        ship.cargo.resize(4);
        ship.cannons = 4;
        a.world.add_ship(ship);
    }
    REQUIRE(a.world.diplomacy().set_peace(a.world, kHuman, kComputer, sim::Treaty::None, a.world.time_ms()));
    a.world.island(1).discover(kComputer);
    sim::Simulation s(a.world, a.buildings, a.figures);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    p.home_valid = true;
    p.home_island = 0;
    p.home_city = 0;
    for (const sim::Ship& ship : a.world.ships()) {
        sim::ai::ShipNote n;
        n.id = ship.id;
        p.ships.push_back(n);
    }
    CHECK(!engine.attack_ok_for(p, *a.world.city(1, 1)));
}

TEST_CASE("ai landing: a shelling that leaves a tower the party cannot storm starts no attack") {
    // Probe p8: the ships reach the tower on the coast, not the one inland. attack_ok weighed only
    // the shelling and started the attack, the ships wrecked the coast tower, and the landing was
    // called off under the inland one, which the 7 soldiers aboard were too few to storm; four
    // times for one city. The tower left by the shelling now needs a party that storms it, at the
    // start (attack_ok) as at the landing (shell_towers). A weaker tower the six storm.
    const auto play = [](const char* hit, bool aboard, bool& ok, bool& shelled, bool& called_off) {
        Coast a;
        a.figures = make_figures(true, hit);
        a.build(kKontor, 28, 14, kComputer, 0);
        a.build(kMarket, 18, 14, kComputer, 0);
        a.build(kKontor, 0, 14, kHuman, 1);
        a.build(kMarket, 6, 6, kHuman, 1);
        for (std::int32_t k = 0; k < 4; ++k) {
            a.build(kHouse, 12 + 3 * k, 20, kHuman, 1);
        }
        for (const auto& [x, y] : std::vector<std::pair<int, int>>{{2, 5}, {10, 9}}) {  // the coast, inland
            REQUIRE(a.world.island(1).map.place(*a.buildings.by_id(kTower), x, y, 0, 0, kHuman, 1).ok());
            sim::Tower tw;
            tw.island = 1;
            tw.x = static_cast<std::uint16_t>(x);
            tw.y = static_cast<std::uint16_t>(y);
            tw.owner = kHuman;
            tw.figure = 38;  // KANONTURM
            a.world.towers().push_back(tw);
        }
        if (!aboard) {
            for (std::int32_t k = 0; k < 6; ++k) {
                a.add(kComputer, 2 * (10 + 23) + k, 2 * (10 + 18), 0);
            }
        }
        for (std::uint32_t k = 0; k < 8; ++k) {  // a convoy of two with six soldiers, six warships
            sim::Ship ship = sim::make_ship("Ship", 41, 15 + 4 * k, kShipFigure, kComputer, 2);
            ship.cargo.resize(4);
            ship.cannons = 4;
            for (std::size_t h = 0; aboard && k < 2 && h < (k == 0 ? 4U : 2U); ++h) {
                ship.cargo[h].ware = assets::unit_ware(kSword);
                ship.cargo[h].amount = sim::kHoldCapacity;
            }
            if (aboard && k < 2) {
                ship.order = sim::Target::position(ship.x, ship.y);  // arrived at home
            }
            a.world.add_ship(ship);
        }
        REQUIRE(a.world.diplomacy().set_peace(a.world, kHuman, kComputer, sim::Treaty::None, a.world.time_ms()));
        a.world.island(1).discover(kComputer);
        sim::Simulation s(a.world, a.buildings, a.figures);
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::ai::AiPlayer p;
        p.player = kComputer;
        p.home_valid = true;
        p.home_island = 0;
        p.home_city = 0;
        for (const sim::Ship& ship : a.world.ships()) {
            sim::ai::ShipNote n;
            n.id = ship.id;
            p.ships.push_back(n);
        }
        ok = false;
        shelled = false;
        called_off = false;
        if (!aboard) {
            ok = engine.attack_ok_for(p, *a.world.city(1, 1));
            return;
        }
        for (std::size_t k = 0; k < 2; ++k) {
            p.ships[k].mission = 6;
            p.ships[k].counter = 10;
            p.ships[k].param = (1U << 8) | 1U;  // city 1 on island 1
        }
        engine.run_missions(p);
        shelled = std::ranges::any_of(p.ships, [](const sim::ai::ShipNote& o) { return o.mission == 11; });
        called_off = p.ships.front().mission != 6;
    };
    bool ok = false;
    bool shelled = false;
    bool called_off = false;
    play("8.0", false, ok, shelled, called_off);
    CHECK(!ok);
    play("6.0", false, ok, shelled, called_off);
    CHECK(ok);
    play("8.0", true, ok, shelled, called_off);
    CHECK(!shelled);
    CHECK(called_off);
    play("6.0", true, ok, shelled, called_off);
    CHECK(shelled);
    CHECK(!called_off);
}

TEST_CASE("ai landing: a ship that comes late lands its soldiers to join those ashore") {
    // Gate f24: a convoy's slow ship reached the city after the others had stormed it; alone it was
    // too few for the towers and turned home with its 4 soldiers. With soldiers of the computer's
    // already ashore it lands them as reinforcements; with none it calls the landing off as before.
    for (const bool ashore : {true, false}) {
        Coast a;
        a.figures = make_figures(true, "30.0");
        a.build(kKontor, 28, 14, kComputer, 0);
        a.build(kMarket, 18, 14, kComputer, 0);
        a.build(kKontor, 0, 14, kHuman, 1);
        a.build(kMarket, 6, 6, kHuman, 1);
        for (std::int32_t k = 0; k < 4; ++k) {
            a.build(kHouse, 12 + 3 * k, 20, kHuman, 1);
        }
        for (const auto& [x, y] : std::vector<std::pair<int, int>>{{2, 5}, {2, 22}}) {
            REQUIRE(a.world.island(1).map.place(*a.buildings.by_id(kTower), x, y, 0, 0, kHuman, 1).ok());
            sim::Tower tw;
            tw.island = 1;
            tw.x = static_cast<std::uint16_t>(x);
            tw.y = static_cast<std::uint16_t>(y);
            tw.owner = kHuman;
            tw.figure = 38;  // KANONTURM
            a.world.towers().push_back(tw);
        }
        if (ashore) {
            a.add(kComputer, 2 * (60 + 20), 2 * (10 + 25), 1);
        }
        sim::Ship ship = sim::make_ship("Ship", 41, 15, kShipFigure, kComputer, 2);
        ship.cargo.resize(4);
        ship.cannons = 4;
        for (std::size_t h = 0; h < 4; ++h) {
            ship.cargo[h].ware = assets::unit_ware(kSword);
            ship.cargo[h].amount = sim::kHoldCapacity;
        }
        ship.order = sim::Target::position(41, 15);  // arrived at home
        a.world.add_ship(ship);
        REQUIRE(a.world.diplomacy().set_peace(a.world, kHuman, kComputer, sim::Treaty::None, a.world.time_ms()));
        a.world.island(1).discover(kComputer);
        sim::Simulation s(a.world, a.buildings, a.figures);
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::ai::AiPlayer p;
        p.player = kComputer;
        p.home_valid = true;
        p.home_island = 0;
        p.home_city = 0;
        sim::ai::ShipNote n;
        n.id = a.world.ships().back().id;
        n.mission = 6;
        n.counter = 10;
        n.param = (1U << 8) | 1U;  // city 1 on island 1
        p.ships.push_back(n);
        engine.run_missions(p);
        CHECK((p.ships.front().mission == sim::ai::kMissionLanding) == ashore);
    }
}

TEST_CASE("ai landing: an improved computer sends no badly damaged ship to an attack") {
    // A ship at a quarter of its health. The yard calls such a ship in even on a mission, and an
    // attack that took it straight back kept it from both (every 10 s).
    const auto landed = [](bool damaged) {
        Coast a;
        a.build(kKontor, 28, 14, kComputer, 0);
        a.build(kMarket, 18, 14, kComputer, 0);
        a.build(kKontor, 0, 14, kHuman, 1);
        a.build(kMarket, 6, 6, kHuman, 1);
        for (std::int32_t k = 0; k < 4; ++k) {
            a.build(kHouse, 12 + 3 * k, 20, kHuman, 1);
        }
        for (std::int32_t k = 0; k < 6; ++k) {
            a.add(kComputer, 2 * (10 + 23) + k, 2 * (10 + 18), 0);
        }
        sim::Ship ship = sim::make_ship("Ship", 41, 15, kShipFigure, kComputer, 2);
        ship.cargo.resize(4);
        ship.cannons = 4;
        if (damaged) {
            ship.health = 50 * 32 / 4;  // Maxenergy 50, in 1/32
        }
        a.world.add_ship(ship);
        REQUIRE(a.world.diplomacy().set_peace(a.world, kHuman, kComputer, sim::Treaty::None, a.world.time_ms()));
        a.world.island(1).discover(kComputer);
        sim::Simulation s(a.world, a.buildings, a.figures);
        for (std::uint32_t t = 0; t < 3 * 60 && a.soldiers_on(kComputer, 1) == 0; ++t) {
            run(s, 1'000);
        }
        return a.soldiers_on(kComputer, 1);
    };
    CHECK(landed(true) == 0);
    CHECK(landed(false) > 0);
}

TEST_CASE("ai landing: an improved computer lands soldiers on the target's island, not an islet in front") {
    // Feedback 16, K3: the soldiers land on the walkable cell nearest the ship, which lay on an
    // islet in front of the beach; there they stood for good.
    Coast a;
    a.build(kKontor, 0, 14, kHuman, 1);
    a.build(kMarket, 6, 6, kHuman, 1);
    sim::IslandMap map(2, 2);
    for (std::int32_t y = 0; y < 2; ++y) {
        for (std::int32_t x = 0; x < 2; ++x) {
            REQUIRE(map.place(*a.buildings.by_id(kGround), x, y, 0, 0).ok());
        }
    }
    map.capture_ground();
    sim::WorldIsland islet{assets::IslandHeader{}, std::move(map), 55, 20};
    islet.header.number = 2;
    islet.header.width = 2;
    islet.header.height = 2;
    REQUIRE(a.world.add_island(std::move(islet)).ok());
    sim::Ship ship = sim::make_ship("Ship", 57, 21, kShipFigure, kComputer, 2);
    a.world.add_ship(ship);
    sim::Simulation s(a.world, a.buildings, a.figures);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    const sim::Ship& at = a.world.ships().back();
    CHECK(!engine.lands_on_target(p, at, kHuman));
    const auto water = engine.target_landing_water(p, at, kHuman);
    REQUIRE(water.has_value());
    CHECK(water->x > 57);  // toward the target's beach (column 60)
}

TEST_CASE("ai landing: an improved computer arms for the party its war lacks against towers, only in its own war") {
    // Feedback 16, K5: after a city of its war's target fell, the next one stood under towers that
    // no fleet of the attacker wrecked and its six soldiers at home could not storm. It passed the
    // city over, its soldiers waited at the Kontor, and its army never grew for it, until the war
    // ended for no progress. Now it arms for the soldiers lacking, in its own war only.
    const auto play = [](bool own_war, std::uint32_t& lacking, std::uint32_t& more) {
        Coast a;
        a.figures = make_figures(true, "8.0");
        a.world.player(kHuman)->role = sim::PlayerRole::Computer;  // a second computer
        a.build(kKontor, 28, 14, kComputer, 0);
        a.build(kMarket, 18, 14, kComputer, 0);
        a.build(kKontor, 0, 14, kHuman, 1);
        a.build(kMarket, 6, 6, kHuman, 1);
        for (std::int32_t k = 0; k < 4; ++k) {
            a.build(kHouse, 12 + 3 * k, 20, kHuman, 1);
        }
        for (const auto& [x, y] : std::vector<std::pair<int, int>>{{2, 5}, {10, 9}}) {
            REQUIRE(a.world.island(1).map.place(*a.buildings.by_id(kTower), x, y, 0, 0, kHuman, 1).ok());
            sim::Tower tw;
            tw.island = 1;
            tw.x = static_cast<std::uint16_t>(x);
            tw.y = static_cast<std::uint16_t>(y);
            tw.owner = kHuman;
            tw.figure = 38;  // KANONTURM
            a.world.towers().push_back(tw);
        }
        for (std::int32_t k = 0; k < 6; ++k) {
            a.add(kComputer, 2 * (10 + 23) + k, 2 * (10 + 18), 0);
        }
        for (std::uint32_t k = 0; k < 8; ++k) {  // holds for 32 soldiers
            sim::Ship ship = sim::make_ship("Ship", 41, 15 + 4 * k, kShipFigure, kComputer, 2);
            ship.cargo.resize(4);
            ship.cannons = 4;
            a.world.add_ship(ship);
        }
        REQUIRE(a.world.diplomacy().set_peace(a.world, kHuman, kComputer, sim::Treaty::None, a.world.time_ms()));
        a.world.island(1).discover(kComputer);
        sim::Simulation s(a.world, a.buildings, a.figures);
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::ai::AiPlayer p;
        p.player = kComputer;
        p.home_valid = true;
        p.home_island = 0;
        p.home_city = 0;
        p.strategy.war_target = own_war ? kHuman : sim::kNoPlayer;
        for (const sim::Ship& ship : a.world.ships()) {
            sim::ai::ShipNote n;
            n.id = ship.id;
            p.ships.push_back(n);
        }
        const std::uint32_t before = engine.army_target_for(p);
        CHECK(!engine.attack_ok_for(p, *a.world.city(1, 1)));
        lacking = p.strategy.war_party;
        const std::uint32_t after = engine.army_target_for(p);
        more = after > before ? after - before : 0;
        if (own_war) {
            // The city falls to another: it arms for it no more.
            a.world.city(1, 1)->owner = kComputer;
            CHECK(engine.army_target_for(p) == before);
        }
    };
    std::uint32_t lacking = 0;
    std::uint32_t more = 0;
    play(true, lacking, more);
    CHECK(lacking > 0);
    CHECK(more > 0);
    play(false, lacking, more);
    CHECK(lacking == 0);
    CHECK(more == 0);
}

TEST_CASE("ai landing: an improved computer shoots a gap into a wall on a coast without beach and lands through it") {
    // Feedback 16, K4 (b): the target's coast had no beach, and the wall stood on the ground along
    // the water. A breach took only walls on the beach: the ship found nothing to shoot, waited and
    // turned home. Now it shoots the wall on that coast too and lands through the gap.
    Coast a(-1);
    a.build(kMarket, 6, 6, kHuman, 1);
    for (std::int32_t k = 0; k < 4; ++k) {
        a.build(kHouse, 12 + 3 * k, 20, kHuman, 1);
    }
    sim::IslandMap& map = a.world.island(1).map;
    for (std::int32_t y = 0; y < 30; ++y) {  // the west coast walled all along
        REQUIRE(map.place(*a.buildings.by_id(kWall), 0, y, 0, 0, kHuman, 1).ok());
    }
    REQUIRE(a.world.diplomacy().set_peace(a.world, kHuman, kComputer, sim::Treaty::None, a.world.time_ms()));
    sim::Simulation s(a.world, a.buildings, a.figures);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    const sim::City& t = *a.world.city(1, 1);
    CHECK(!engine.landing_beach(p, t).has_value());  // no beach at all
    const auto wall = engine.landing_beach(p, t, true);
    REQUIRE(wall.has_value());
    CHECK(wall->first == 0);  // on the west coast
    // Shot down: the gap is the landing.
    map.restore_ground(0, static_cast<std::uint32_t>(wall->second));
    const auto gap = engine.landing_beach(p, t);
    REQUIRE(gap.has_value());
    CHECK(gap->first == 0);
    CHECK(gap->second == wall->second);
}
