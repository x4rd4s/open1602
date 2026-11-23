// SPDX-License-Identifier: GPL-3.0-or-later
// A computer's attack from the sea (BT-1): at war, it picks an enemy city with a
// market or Kontor, sends an armed ship home for the soldiers beside its Kontor, plans a landing on
// a beach near the target's market and puts the troops ashore there.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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
/// `tower` a tower's figure (KANONTURM, 38) of that Hitpoint.
assets::FigureTable make_figures(bool guns = false, const char* tower = nullptr) {
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

    Coast() {
        std::vector<sim::Player> players(2);
        players[kHuman].role = sim::PlayerRole::Human;
        players[kComputer].role = sim::PlayerRole::Computer;
        players[kComputer].money = 20000;
        world.set_players(players);
        add_island(10, 10, 29);
        add_island(60, 10, 0);
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
    // the ship they would sink; with none, or towers that would sink it, the troops land under them.
    const std::vector<std::pair<std::int32_t, std::int32_t>> on_beach = {{2, 5}, {2, 22}};
    const auto play = [](bool warship, const char* tower_hit, std::uint32_t& towers_at_landing, bool& warship_whole,
                         const std::vector<std::pair<std::int32_t, std::int32_t>>& spots) {
        Coast a;
        a.figures = make_figures(true, tower_hit);
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
        for (std::int32_t k = 0; k < 6; ++k) {
            a.add(kComputer, 2 * (10 + 23) + k, 2 * (10 + 18), 0);
        }
        std::vector<std::pair<int, int>> at{{41, 15}};
        if (warship) {
            at.emplace_back(42, 20);
        }
        for (const auto& [x, y] : at) {
            sim::Ship ship = sim::make_ship("Ship", static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                                            kShipFigure, kComputer, 2);
            ship.cargo.resize(4);
            ship.cannons = 4;
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
        const sim::Ship* w = a.world.ship(2);
        warship_whole = w != nullptr && (w->health == 0 || w->health == 50 * 32);  // Maxenergy 50, 1/32
        return a.soldiers_on(kComputer, 1) > 0;
    };
    std::uint32_t towers = 0;
    bool whole = false;
    CHECK(play(true, "1.0", towers, whole, on_beach));
    CHECK(towers < 2);  // shelled first
    CHECK(play(false, "1.0", towers, whole, on_beach));
    CHECK(towers == 2);  // no warship: landed under the towers
    // Towers whose fire would drive the warship off before they fall: it stays out of their reach,
    // the troop ship goes in under them as before (and here is sunk).
    play(true, "40.0", towers, whole, on_beach);
    CHECK(towers == 2);
    CHECK(whole);
    // A tower inland over the way to the market, beyond the ships' range from any water: no ship
    // sails up to it, the troops land as before.
    CHECK(play(true, "1.0", towers, whole, {{10, 10}}));
    CHECK(towers == 1);
    CHECK(whole);
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
