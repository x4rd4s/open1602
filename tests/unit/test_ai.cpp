// SPDX-License-Identifier: GPL-3.0-or-later
// The computer players on a small synthetic world, without the original data: they
// found a city with their ship through the same orders a player gives, run the same on every
// computer, keep their state through a savegame and ignore the players that are not theirs.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "../../engine/sim/src/ai/ai_archive.hpp"
#include "../../engine/sim/src/ai/ai_engine.hpp"
#include "../../engine/sim/src/ai/ai_view.hpp"
#include "../test_framework.hpp"

#include "annokit/core/byte_reader.hpp"
#include "annokit/core/byte_writer.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/assets/cod.hpp"
#include "o1602/assets/figure_table.hpp"
#include "o1602/assets/game_data.hpp"
#include "o1602/assets/goods.hpp"
#include "o1602/assets/own_buildings.hpp"
#include "o1602/assets/resident_classes.hpp"
#include "o1602/sim/ai.hpp"
#include "o1602/sim/balance.hpp"
#include "o1602/sim/computer_orders.hpp"
#include "o1602/sim/computer_slots.hpp"
#include "o1602/sim/construction.hpp"
#include "o1602/sim/consumption.hpp"
#include "o1602/sim/goods_statistics.hpp"
#include "o1602/sim/houses.hpp"
#include "o1602/sim/new_game.hpp"
#include "o1602/sim/pathfinding.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/simulation.hpp"

using namespace o1602;

namespace {

constexpr std::uint32_t kGround = 20101;
constexpr std::uint32_t kSea = 21201;
constexpr std::uint32_t kBeach = 20501;
constexpr std::uint32_t kTree = 20202;
constexpr std::uint32_t kKontor = 22101;  // the Id the computer players build as their first Kontor
constexpr std::uint16_t kShipFigure = 21;

constexpr sim::PlayerId kHuman = 0;
constexpr sim::PlayerId kComputer = 1;
constexpr sim::PlayerId kTrader = 2;
constexpr sim::PlayerId kNatives = 3;

/// `extra` are further HAUS records, numbered on from the Kontor; `kontor` further lines of the
/// Kontor's HAUS_PRODTYP.
assets::BuildingTable make_buildings(const std::string& extra = {}, const std::string& kontor = "    Maxlager: 30\n") {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        std::string("Objekt: HAUS\n") +
            "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"                   // ground
            "  @Nummer: 1\n  Id: 21201\n  Kind: 19\n  Gfx: 1\n  Size: 1, 1\n"                   // sea
            "  @Nummer: 2\n  Id: 20501\n  Kind: 23\n  Gfx: 2\n  Size: 1, 1\n  Destroyflg: 1\n"  // beach
            "  @Nummer: 3\n  Id: 20202\n  Kind: 10\n  Gfx: 3\n  Size: 1, 1\n"                   // a tree
            "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n"
            // Roads: stone and dirt, each a family of straight, curve, T and cross.
            "  @Nummer: 4\n  Id: 20401\n  Kind: 1\n  Gfx: 4\n  Size: 1, 1\n  KreuzBase: 4\n"
            "  Objekt: HAUS_BAUKOST\n    Money: 5\n  EndObj\n"
            "  @Nummer: 5\n  Id: 20402\n  Kind: 1\n  Gfx: 5\n  Size: 1, 1\n  KreuzBase: 4\n"
            "  @Nummer: 6\n  Id: 20403\n  Kind: 1\n  Gfx: 6\n  Size: 1, 1\n  KreuzBase: 4\n"
            "  @Nummer: 7\n  Id: 20404\n  Kind: 1\n  Gfx: 7\n  Size: 1, 1\n  KreuzBase: 4\n"
            "  @Nummer: 8\n  Id: 20411\n  Kind: 1\n  Gfx: 8\n  Size: 1, 1\n  KreuzBase: 8\n"
            "  Objekt: HAUS_BAUKOST\n    Money: 2\n  EndObj\n"
            "  @Nummer: 9\n  Id: 20412\n  Kind: 1\n  Gfx: 9\n  Size: 1, 1\n  KreuzBase: 8\n"
            "  @Nummer: 10\n  Id: 20413\n  Kind: 1\n  Gfx: 10\n  Size: 1, 1\n  KreuzBase: 8\n"
            "  @Nummer: 11\n  Id: 20414\n  Kind: 1\n  Gfx: 11\n  Size: 1, 1\n  KreuzBase: 8\n"
            // Kontor I at the beach.
            "  @Nummer: 12\n  Id: 22101\n  Kind: 35\n  Gfx: 20\n  Size: 2, 3\n  Strandflg: 1\n  PlaceFlg: 1\n"
            "  KreuzBase: 12\n  Ausbauflg: 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n" +
            kontor +
            "  EndObj\n"
            "  Objekt: HAUS_BAUKOST\n    Werkzeug: 3\n    Holz: 4\n    Money: 100\n  EndObj\n" +
            extra + "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

assets::FigureTable make_figures() {
    const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
    const auto doc = assets::cod::parse(
        "Objekt: FIGUR\n"
        "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
        "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Preis: 1000\n  Rohstoff: 23\n  Rohmenge: 25\n"
        "EndObj\n",
        types);
    auto table = assets::FigureTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

/// A world of 90 x 90 tiles of sea with one island of 50 x 50 tiles at (30, 20): open ground, a
/// wood (columns 32..43), a beach facing east (column 46) and sea (columns 47..49). Every crop grows
/// there fully. Player 0 is human, 1 a computer with a ship east of the island, 2 the trader, 3 the
/// natives. With `lake`, a lake inside the island (columns 26..30, rows 18..32) with a beach facing
/// it (column 25) and a second wood (columns 2..12): the lake has more green around it than the
/// coast, but ships cannot reach it from the sea.
struct Game {
    assets::BuildingTable buildings = make_buildings();
    assets::FigureTable figures = make_figures();
    sim::World world{90, 90};

    /// With `lake` a lake of 5 x 15 tiles from column `lake_x` on, a beach left of it.
    explicit Game(bool computer_ship = true, std::int32_t computer_money = 10000, bool lake = false,
                  std::int32_t lake_x = 26) {
        add_islet(2, 2);
        sim::IslandMap map(50, 50);
        for (std::int32_t y = 0; y < 50; ++y) {
            for (std::int32_t x = 0; x < 50; ++x) {
                std::uint32_t id = kGround;
                const bool lake_row = lake && y >= 18 && y <= 32;
                if (lake_row && x >= lake_x && x < lake_x + 5) {
                    id = kSea;
                } else if (lake_row && x == lake_x - 1) {
                    id = kBeach;
                } else if (x > 46) {
                    id = kSea;
                } else if (x == 46) {
                    id = kBeach;
                } else if ((x >= 32 && x <= 43) || (lake && x >= 2 && x <= 12)) {
                    id = kTree;
                }
                REQUIRE(map.place(*buildings.by_id(id), x, y, id == kBeach ? 3 : 0, 0).ok());
            }
        }
        map.capture_ground();
        sim::WorldIsland island{assets::IslandHeader{}, std::move(map), 30, 20};
        island.header.number = static_cast<std::uint8_t>(world.islands().size());
        island.header.width = 50;
        island.header.height = 50;
        island.header.fertility = 0xFFFFFFFFU;
        REQUIRE(world.add_island(std::move(island)).ok());

        std::vector<sim::Player> players(4);
        players[kHuman].role = sim::PlayerRole::Human;
        players[kHuman].money = 10000;
        players[kComputer].role = sim::PlayerRole::Computer;
        players[kComputer].money = computer_money;
        sim::make_neutral(players[kTrader], sim::faction::kTrader);
        sim::make_neutral(players[kNatives], sim::faction::kNatives);
        world.set_players(players);
        if (computer_ship) {
            add_ship(kComputer, 86, 45);
        }
    }

    /// A rock of 5 x 5 tiles of ground, too small for a colony.
    void add_islet(std::uint32_t x, std::uint32_t y) {
        sim::IslandMap map(5, 5);
        for (std::int32_t ty = 0; ty < 5; ++ty) {
            for (std::int32_t tx = 0; tx < 5; ++tx) {
                REQUIRE(map.place(*buildings.by_id(kGround), tx, ty, 0, 0).ok());
            }
        }
        map.capture_ground();
        sim::WorldIsland islet{assets::IslandHeader{}, std::move(map), x, y};
        islet.header.number = static_cast<std::uint8_t>(world.islands().size());
        islet.header.width = 5;
        islet.header.height = 5;
        REQUIRE(world.add_island(std::move(islet)).ok());
    }

    /// An island of `w` x `h` tiles at (x, y): ground with a forest, a beach at its east and sea
    /// beyond it, as the large one; its index.
    std::uint16_t add_coast_island(std::uint32_t x, std::uint32_t y, std::int32_t w, std::int32_t h) {
        sim::IslandMap map(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
        for (std::int32_t ty = 0; ty < h; ++ty) {
            for (std::int32_t tx = 0; tx < w; ++tx) {
                const std::uint32_t id = tx > w - 4                     ? kSea
                                         : tx == w - 4                  ? kBeach
                                         : tx >= w - 24 && tx <= w - 10 ? kTree
                                                                        : kGround;
                REQUIRE(map.place(*buildings.by_id(id), tx, ty, id == kBeach ? 3 : 0, 0).ok());
            }
        }
        map.capture_ground();
        sim::WorldIsland island{assets::IslandHeader{}, std::move(map), x, y};
        const auto index = static_cast<std::uint16_t>(world.islands().size());
        island.header.number = static_cast<std::uint8_t>(index);
        island.header.width = static_cast<std::uint8_t>(w);
        island.header.height = static_cast<std::uint8_t>(h);
        island.header.fertility = 0xFFFFFFFFU;
        REQUIRE(world.add_island(std::move(island)).ok());
        return index;
    }

    sim::Ship& add_ship(sim::PlayerId owner, std::uint32_t x, std::uint32_t y) {
        sim::Ship s = sim::make_ship("Ship", x, y, kShipFigure, owner, 2);
        s.cargo.resize(4);
        world.add_ship(s);
        return world.ships().back();
    }

    [[nodiscard]] std::size_t cities_of(sim::PlayerId owner) const {
        return static_cast<std::size_t>(std::count_if(world.cities().begin(), world.cities().end(),
                                                      [&](const sim::City& c) { return c.owner == owner; }));
    }
};

/// Ticks until `done` holds or `ms` of game time have passed; whether `done` held.
template <class F>
bool run_until(sim::Simulation& s, std::uint32_t ms, F&& done) {
    for (std::uint32_t t = 0; t < ms; t += sim::Simulation::kTickMs) {
        s.tick();
        if (done()) {
            return true;
        }
    }
    return false;
}

void run(sim::Simulation& s, std::uint32_t ms) {
    for (std::uint32_t t = 0; t < ms; t += sim::Simulation::kTickMs) {
        s.tick();
    }
}

bool same(const std::vector<sim::ComputerPlayers::View>& a, const std::vector<sim::ComputerPlayers::View>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto& x = a[i];
        const auto& y = b[i];
        if (x.player != y.player || x.cities != y.cities || x.max_stage != y.max_stage || x.score != y.score ||
            x.settling != y.settling || x.settle_failed != y.settle_failed || x.stack != y.stack ||
            x.plans.size() != y.plans.size()) {
            return false;
        }
        for (std::size_t k = 0; k < x.plans.size(); ++k) {
            const auto& p = x.plans[k];
            const auto& q = y.plans[k];
            if (p.island != q.island || p.city != q.city || p.stage != q.stage || p.flags != q.flags ||
                p.slots != q.slots) {
                return false;
            }
        }
    }
    return true;
}

/// A pirate slot behind the natives (slot 4).
void add_pirates(Game& g) {
    std::vector<sim::Player> players = g.world.players();
    players.push_back(sim::Player{});
    sim::make_neutral(players.back(), sim::faction::kPirates);
    g.world.set_players(players);
}

/// Runs until the computer has founded its city, then `ms` more.
void found_city(Game& g, sim::Simulation& s, std::uint32_t ms = 1'000) {
    REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    run(s, ms);
}

bool remembers(const sim::World& world, sim::PlayerId who, sim::DiplomacyEventType type, sim::PlayerId partner) {
    const auto& events = world.diplomacy().events(who);
    return std::any_of(events.begin(), events.end(),
                       [&](const sim::DiplomacyEvent& e) { return e.type == type && e.partner == partner; });
}

std::vector<std::uint8_t> saved(const sim::ComputerPlayers& ai) {
    annokit::core::ByteWriter out;
    ai.save(out);
    return out.take();
}

}  // namespace

TEST_CASE("ai: a computer with a ship founds a city through a build order") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    const std::uint32_t ship = g.world.ships().front().id;
    bool directed = false;
    const bool founded = run_until(s, 600'000, [&] {
        directed = directed || s.computers().directs(ship);
        return g.cities_of(kComputer) != 0;
    });
    REQUIRE(founded);
    CHECK(directed);  // the ship sailed under the computer's command
    CHECK(!s.computers().directs(ship));
    const sim::City* city = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            city = &c;
        }
    }
    REQUIRE(city != nullptr);
    CHECK(city->island == 1);
    REQUIRE(city->warehouse_x.has_value());
    REQUIRE(city->warehouse_y.has_value());
    // A Kontor at the coast, its warehouse.
    const sim::MapCell& at = g.world.islands()[1].map.top(*city->warehouse_x, *city->warehouse_y);
    const assets::BuildingType* kontor = g.buildings.by_sprite(at.sprite);
    REQUIRE(kontor != nullptr);
    CHECK(kontor->id == kKontor);
    CHECK(at.owner == kComputer);
    // Built like a player's Kontor: the game reports the new city.
    bool reported = false;
    for (const auto& e : s.take_construction_events()) {
        reported = reported || (e.kind == sim::ConstructionEvent::Kind::CityFounded && e.island == 1);
    }
    CHECK(reported);
    const auto view = s.computers().view();
    REQUIRE(view.size() == 1);
    CHECK(view[0].player == kComputer);
    CHECK(view[0].cities == 1);
    CHECK(!view[0].settling);
}

TEST_CASE("ai: a computer founds its first city on the island the scenario chose, the search passes it over") {
    // A small island of some 800 tiles of land: the search wants 1800 and takes the large one;
    // chosen by the scenario, the computer founds on it. The choice is spent then.
    for (const bool improved : {false, true}) {
        for (const bool chosen : {false, true}) {
            Game g;
            const std::uint16_t small = g.add_coast_island(40, 72, 50, 17);
            if (chosen) {
                g.world.player(kComputer)->first_island = sim::FirstIsland::Fixed;
                g.world.player(kComputer)->first_island_number = g.world.islands()[small].header.number;
            }
            sim::Rules rules;
            rules.bugs.classic_ai = !improved;
            rules.reveal_resources = true;
            sim::Simulation s(g.world, g.buildings, g.figures, rules);
            REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
            for (const sim::City& c : g.world.cities()) {
                if (c.owner == kComputer) {
                    CHECK(c.island == (chosen ? small : 1));
                }
            }
            CHECK(g.world.players()[kComputer].first_island == sim::FirstIsland::Original);
        }
    }
}

TEST_CASE("ai: a first island chosen that cannot take a city, or drawn at random, still ends in a city") {
    // The islet (25 tiles) is too small even for the least the search ends with: the computer
    // searches as always. At random it draws among the islands free for a first city.
    for (const sim::FirstIsland choice :
         {sim::FirstIsland::Fixed, sim::FirstIsland::AtRandom, sim::FirstIsland::LargeAtRandom}) {
        Game g;
        g.world.player(kComputer)->first_island = choice;
        g.world.player(kComputer)->first_island_number = g.world.islands()[0].header.number;
        sim::Simulation s(g.world, g.buildings, g.figures);
        REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
        for (const sim::City& c : g.world.cities()) {
            if (c.owner == kComputer) {
                CHECK(c.island == 1);
            }
        }
        CHECK(g.world.players()[kComputer].first_island == sim::FirstIsland::Original);
    }
}

TEST_CASE("ai: an improved computer sails out to explore before it founds, the original's knows every island") {
    // Nothing is discovered at the start. The improved computer's ship sails into the rectangle of
    // an island, discovers it and explores its crops (the first city wants one); the original's
    // computer picks its island from the whole map.
    for (const bool original : {true, false}) {
        Game g;
        sim::Rules rules;
        rules.bugs.classic_ai = original;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        for (const sim::WorldIsland& isl : g.world.islands()) {
            REQUIRE(!isl.discovered_by(kComputer));
        }
        REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
        const sim::City* city = nullptr;
        for (const sim::City& c : g.world.cities()) {
            if (c.owner == kComputer) {
                city = &c;
            }
        }
        REQUIRE(city != nullptr);
        if (!original) {
            CHECK(g.world.islands()[city->island].discovered_by(kComputer));
            CHECK(g.world.islands()[city->island].explored_by(kComputer));
        }
    }
}

TEST_CASE("ai: with every island's resources known an improved computer founds as the original's, no trip first") {
    // The setting explores every island for every player from the start: the improved computer
    // knows what the original's knows and picks the same island without a trip first.
    std::uint64_t when[2] = {0, 0};
    std::uint16_t where[2] = {0, 0};
    for (const bool original : {true, false}) {
        Game g;
        sim::Rules rules;
        rules.bugs.classic_ai = original;
        rules.reveal_resources = true;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
        for (const sim::City& c : g.world.cities()) {
            if (c.owner == kComputer) {
                where[original ? 0 : 1] = c.island;
            }
        }
        when[original ? 0 : 1] = g.world.time_ms();
    }
    CHECK(where[0] == where[1]);
    // The improved computer's own gold rules may cost it a moment, an exploring trip would cost a minute.
    CHECK(when[1] < when[0] + 5'000);
}

TEST_CASE("ai: a ship whose goods do not fit into the full home warehouse does not lie there for good") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s, 30'000);
    sim::City* home = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            home = g.world.city(c.island, c.index);
        }
    }
    REQUIRE(home != nullptr);
    sim::Ship& ship = g.world.ships().front();
    REQUIRE(ship.owner == kComputer);
    // At the Kontor: on the first sea tile east of it (the island lies at 30, 20; sea from x 47).
    const sim::WorldIsland& isl = g.world.islands()[home->island];
    ship.place(isl.x + 48, isl.y + *home->warehouse_y);
    REQUIRE(sim::building_in_reach(g.world, g.buildings, home->island, *home->warehouse_x, *home->warehouse_y, ship.x,
                                   ship.y));
    // Nothing else to do but this: a hold of a ware the warehouse is full of (S-097: it stays on board).
    const std::uint8_t ware = g.buildings.materials().tools;
    home->ensure_ware(ware);
    home->goods[ware] = home->capacity;
    ship.route = sim::TradeRoute{};
    ship.target = sim::Target{};
    for (sim::CargoSlot& slot : ship.cargo) {
        slot = sim::CargoSlot{};
    }
    ship.cargo[0] = sim::CargoSlot{ware, 10 * 32, 0};
    const std::uint32_t id = ship.id;
    // Every 10 s the computer looks at its ships; before, it tried to unload on every look.
    const bool sails = run_until(s, 60'000, [&] {
        const sim::Ship* now = g.world.ship(id);
        return now != nullptr && (now->route.active || !now->target.empty());
    });
    CHECK(sails);
    CHECK(g.world.ship(id)->load(ware) == 10 * 32);  // nothing thrown away
}

TEST_CASE("ai: a computer's ship under the white flag gets its next job, which strikes the flag") {
    // A ship a pirate plundered keeps its white flag; the original's pickers do not look at the flag
    // (the original tests the wrong bit), so the next job sails it on.
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s, 30'000);
    sim::City* home = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            home = g.world.city(c.island, c.index);
        }
    }
    REQUIRE(home != nullptr);
    sim::Ship& ship = g.world.ships().front();
    REQUIRE(ship.owner == kComputer);
    const sim::WorldIsland& isl = g.world.islands()[home->island];
    ship.place(isl.x + 48, isl.y + *home->warehouse_y);
    const std::uint8_t ware = g.buildings.materials().tools;
    home->ensure_ware(ware);
    home->goods[ware] = home->capacity;
    ship.route = sim::TradeRoute{};
    ship.target = sim::Target{};
    for (sim::CargoSlot& slot : ship.cargo) {
        slot = sim::CargoSlot{};
    }
    ship.cargo[0] = sim::CargoSlot{ware, 10 * 32, 0};
    ship.held = true;
    const std::uint32_t id = ship.id;
    const bool sails = run_until(s, 60'000, [&] {
        const sim::Ship* now = g.world.ship(id);
        return now != nullptr && (now->route.active || !now->target.empty());
    });
    CHECK(sails);
    CHECK(!g.world.ship(id)->held);
}

TEST_CASE("ai: the original's computer players (Rules::bugs.classic_ai) found their city too") {
    Game g;
    sim::Rules rules;
    rules.bugs.classic_ai = true;
    sim::Simulation s(g.world, g.buildings, g.figures, rules);
    CHECK(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
}

TEST_CASE("ai: computer opponents switched off found nothing, by the rule or by the game's settings") {
    {
        Game g;
        sim::Rules rules;
        rules.computer_players = sim::Computers::Off;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        CHECK(!run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    }
    {
        // Bit 1 of the settings word (TIMERS +0xA8): the original skips its AI tick.
        Game g;
        sim::World::GameSettings settings = g.world.game_settings();
        settings.flags |= sim::World::GameSettings::kNoComputers;
        g.world.set_game_settings(settings);
        sim::Simulation s(g.world, g.buildings, g.figures);
        CHECK(!run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    }
    {
        // Switched on, they play whatever the game says.
        Game g;
        sim::World::GameSettings settings = g.world.game_settings();
        settings.flags |= sim::World::GameSettings::kNoComputers;
        g.world.set_game_settings(settings);
        sim::Rules rules;
        rules.computer_players = sim::Computers::On;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        CHECK(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    }
}

TEST_CASE("ai: a computer founds its city at the open sea, not at a lake its ship cannot reach") {
    Game g(true, 10000, true);
    sim::Simulation s(g.world, g.buildings, g.figures);
    REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    const sim::City* city = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            city = &c;
        }
    }
    REQUIRE(city != nullptr);
    REQUIRE(city->warehouse_x.has_value());
    CHECK(*city->warehouse_x >= 40);  // at the beach of column 46, not at the lake (column 25)
}

TEST_CASE("ai: a person who takes a seat over finds its ships standing") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    const std::uint32_t id = g.world.ships().front().id;
    // The computer sends its ship off to found a city; a person takes the seat over on the way.
    REQUIRE(run_until(s, 600'000, [&] { return !g.world.ship(id)->target.empty(); }));
    s.submit(sim::PlayerControl{kComputer, false});
    run(s, 1'000);
    const sim::Ship* ship = g.world.ship(id);
    REQUIRE(ship != nullptr);
    CHECK(ship->target.empty());
    const std::int32_t x = ship->x;
    const std::int32_t y = ship->y;
    run(s, 60'000);
    CHECK(g.world.ship(id)->x == x);
    CHECK(g.world.ship(id)->y == y);
    CHECK(g.cities_of(kComputer) == 0);
}

TEST_CASE("ai: the new city gets roads from the computer's own build orders") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    const std::int32_t after_founding = g.world.players()[kComputer].money;
    run(s, 60'000);
    std::size_t roads = 0;
    const sim::IslandMap& map = g.world.islands()[1].map;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const sim::MapCell& c = map.top(x, y);
            const assets::BuildingType* b = c.empty() ? nullptr : g.buildings.by_sprite(c.sprite);
            roads += b != nullptr && b->kind == 1 && c.owner == kComputer ? 1U : 0U;
        }
    }
    CHECK(roads > 0);
    // Paid like any player's buildings, and never into debt.
    CHECK(g.world.players()[kComputer].money < after_founding);
    CHECK(g.world.players()[kComputer].money > 0);
}

TEST_CASE("ai: two runs of the same game stay the same, tick by tick") {
    Game a;
    Game b;
    sim::Simulation sa(a.world, a.buildings, a.figures);
    sim::Simulation sb(b.world, b.buildings, b.figures);
    // Through the voyage, the founding and the first roads.
    for (int step = 0; step < 12; ++step) {
        run(sa, 5'000);
        run(sb, 5'000);
        const auto ha = sa.state_hash();
        CHECK(ha == sb.state_hash());
        CHECK(same(sa.computers().view(), sb.computers().view()));
        CHECK(saved(sa.computers()) == saved(sb.computers()));
    }
    CHECK(a.cities_of(kComputer) == 1);
}

// Lockstep needs the same game on every machine (ADR 0004). This hash is checked on every build of
// CI (GCC, Clang, MSVC, Apple Clang), so a difference between compilers or standard libraries shows
// here. A change that alters the game on purpose updates the number, and says why in its PR.
TEST_CASE("ai: a computer's first minutes give the same state on every platform") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 90'000);
    const std::uint64_t hash = s.state_hash();
    // Names left empty without Text.cod (MD-10); SIMU in parts (DS-3); tiles by blocks (TB-26); a ship
    // whose goods do not fit at home is free again; the computers' state version 4 (a search's target);
    // the revised bread chain by default; the computers' state version 5 (the slots counted).
    // The wider hunter's hut by default. The military's state version 4 (the unit that goes first).
    // Up to 16 cities on an island by default (only the rules' bytes); improved computers without
    // the original's gifts, setting their own taxes, seeing only what their ships saw; the
    // computers' state version 9 (fleet target hold, left islands). Improved computers' cities
    // keep the figures of the statistics windows (their overview).
    constexpr std::uint64_t kExpected = 0xe5bb67b33e28d788ULL;
    if (hash != kExpected) {
        std::printf("  state hash after 90 s: 0x%016llx\n", static_cast<unsigned long long>(hash));
    }
    CHECK(hash == kExpected);
    CHECK(g.cities_of(kComputer) == 1);
}

TEST_CASE("ai: the fingerprint with the blocks' hashes kept is the one taken anew while the computer builds (TB-26)") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    for (int k = 0; k < 18; ++k) {
        run(s, 5'000);
        CHECK(s.state_hash() == s.state_hash_anew());
    }
    CHECK(g.cities_of(kComputer) == 1);
    // A tile changed behind the game's back changes the fingerprint at once.
    const std::uint64_t hash = s.state_hash();
    sim::IslandMap& map = g.world.island(0).map;
    map.set_anim_phase(0, map.height() - 1, static_cast<std::uint8_t>(map.top(0, map.height() - 1).anim_phase ^ 1U));
    CHECK(s.state_hash() != hash);
    CHECK(s.state_hash() == s.state_hash_anew());
}

TEST_CASE("ai: a game saved while the computer is under way goes on as if never saved") {
    // Saved before, during and after the voyage of the founding ship.
    bool under_way = false;
    for (const std::uint32_t at : {4'000U, 8'000U, 15'000U, 40'000U}) {
        Game a;
        sim::Simulation sa(a.world, a.buildings, a.figures);
        run(sa, at);
        const std::uint32_t ship = a.world.ships().front().id;
        under_way = under_way || (sa.computers().view().front().settling && sa.computers().directs(ship));
        const auto bytes = sim::write_game(a.world, a.buildings, &sa, sim::SaveOptions{false});
        REQUIRE(bytes.ok());
        auto loaded = sim::read_game(*bytes, a.buildings, nullptr, &a.figures);
        REQUIRE(loaded.ok());
        sim::World world = std::move(loaded).value();
        sim::Simulation sb(world, a.buildings, a.figures);
        CHECK(!sb.state_damaged());
        CHECK(same(sa.computers().view(), sb.computers().view()));
        CHECK(saved(sa.computers()) == saved(sb.computers()));
        CHECK(sa.state_hash() == sb.state_hash());
        CHECK(sa.computers().directs(ship) == sb.computers().directs(ship));
        run(sa, 30'000);
        run(sb, 30'000);
        CHECK(same(sa.computers().view(), sb.computers().view()));
        CHECK(sa.state_hash() == sb.state_hash());
        CHECK(a.cities_of(kComputer) == 1);
    }
    CHECK(under_way);
}

TEST_CASE("ai: a saved state with values out of range is refused, not run") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 8'000);
    const std::vector<std::uint8_t> bytes = saved(s.computers());
    sim::ai::State good;
    {
        annokit::core::ByteReader in(bytes);
        sim::StateIn reader(in);
        sim::ai::Loader loader(reader, s.computers().state_version());
        loader(good);
        REQUIRE(reader.ok());
        REQUIRE(!good.players.empty());
    }
    const auto loads = [&](sim::ai::State state) {
        annokit::core::ByteWriter out;
        sim::ai::Saver saver(out);
        saver(state);
        const std::vector<std::uint8_t> data = out.take();
        annokit::core::ByteReader in(data);
        sim::ComputerPlayers computers(s);
        return computers.load(in, sim::ComputerPlayers::max_state_version());
    };
    CHECK(loads(good));
    sim::ai::State bad = good;
    bad.players[0].search.segment = -2'000'000'000;  // crashed the first step after loading
    CHECK(!loads(bad));
    bad = good;
    bad.players[0].search.slot = 0xFF;
    CHECK(!loads(bad));
    bad = good;
    bad.players.push_back(bad.players[0]);  // one computer acting twice a tick
    CHECK(!loads(bad));
    bad = good;
    bad.players[0].player = 200;
    CHECK(!loads(bad));
    bad = good;
    bad.players[0].stack = {sim::ai::kDevelop, sim::ai::kDevelop};
    CHECK(!loads(bad));
    bad = good;
    bad.players[0].stack = {0xEE};
    CHECK(!loads(bad));
}

TEST_CASE("ai: a state of version 1 loads and goes on as the same state") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 8'000);
    const std::vector<std::uint8_t> now = saved(s.computers());
    sim::ai::State state;
    {
        annokit::core::ByteReader in(now);
        sim::StateIn reader(in);
        sim::ai::Loader loader(reader, s.computers().state_version());
        loader(state);
        REQUIRE(reader.ok());
    }
    // As version 1 wrote it: with an (empty) list of attack plans and a state byte per island.
    annokit::core::ByteWriter old;
    sim::ai::Saver saver(old, 1);
    saver(state);
    const std::vector<std::uint8_t> v1 = old.take();
    CHECK(v1 != now);
    annokit::core::ByteReader in(v1);
    sim::ComputerPlayers computers(s);
    REQUIRE(computers.load(in, 1));
    CHECK(saved(computers) == now);
}

TEST_CASE("ai: a state of version 2 loads without a yard request") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 8'000);
    sim::ai::State state;
    {
        const std::vector<std::uint8_t> now = saved(s.computers());
        annokit::core::ByteReader in(now);
        sim::StateIn reader(in);
        sim::ai::Loader loader(reader, s.computers().state_version());
        loader(state);
        REQUIRE(reader.ok());
    }
    REQUIRE(!state.players.empty());
    for (sim::ai::AiPlayer& p : state.players) {
        p.yard_wanted = true;  // version 2 has no room for it
    }
    annokit::core::ByteWriter old;
    sim::ai::Saver saver(old, 2);
    saver(state);
    const std::vector<std::uint8_t> v2 = old.take();
    annokit::core::ByteReader in(v2);
    sim::ComputerPlayers computers(s);
    REQUIRE(computers.load(in, 2));
    // Saved again as the newest version: one byte more per computer, and no request (version 3),
    // the search's target (version 4) and the number of slots of each computer and city (version 5).
    const std::vector<std::uint8_t> again = saved(computers);
    std::size_t plans = 0;
    for (const sim::ai::AiPlayer& p : state.players) {
        plans += p.plans.size();
    }
    CHECK(again.size() == v2.size() + state.players.size() * (1 + 2 * sizeof(std::int32_t) + sizeof(std::uint32_t)) +
                              plans * sizeof(std::uint32_t));
    annokit::core::ByteReader back(again);
    sim::StateIn reader(back);
    sim::ai::State loaded;
    sim::ai::Loader loader(reader, computers.state_version());
    loader(loaded);
    REQUIRE(reader.ok());
    for (const sim::ai::AiPlayer& p : loaded.players) {
        CHECK(!p.yard_wanted);
    }
}

TEST_CASE("ai: a computer arming for a war still arms after the game is loaded (version 13)") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 8'000);
    sim::ai::State state;
    {
        const std::vector<std::uint8_t> now = saved(s.computers());
        annokit::core::ByteReader in(now);
        sim::StateIn reader(in);
        sim::ai::Loader loader(reader, s.computers().state_version());
        loader(state);
        REQUIRE(reader.ok());
    }
    REQUIRE(!state.players.empty());
    state.players[0].strategy.war_arming = true;
    annokit::core::ByteWriter out;
    sim::ai::Saver saver(out, sim::ai::kStateWithArming);
    saver(state);
    const std::vector<std::uint8_t> bytes = out.take();
    annokit::core::ByteReader in(bytes);
    sim::ComputerPlayers computers(s);
    REQUIRE(computers.load(in, sim::ai::kStateWithArming));
    CHECK(computers.state_version() == sim::ai::kStateWithArming);
    // Saved again, it reads back with the flag.
    const std::vector<std::uint8_t> again = saved(computers);
    CHECK(again == bytes);
    annokit::core::ByteReader back(again);
    sim::StateIn reader(back);
    sim::ai::State loaded;
    sim::ai::Loader loader(reader, computers.state_version());
    loader(loaded);
    REQUIRE(reader.ok());
    CHECK(loaded.players[0].strategy.war_arming);
    // Without a computer arming, the state keeps its earlier version and bytes.
    CHECK(s.computers().state_version() < sim::ai::kStateWithArming);
}

TEST_CASE("ai: a computer's attacks without a landing survive saving (version 18), older states load") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 8'000);
    sim::ai::State state;
    {
        const std::vector<std::uint8_t> now = saved(s.computers());
        annokit::core::ByteReader in(now);
        sim::StateIn reader(in);
        sim::ai::Loader loader(reader, s.computers().state_version());
        loader(state);
        REQUIRE(reader.ok());
    }
    REQUIRE(!state.players.empty());
    // Without tries the state keeps its earlier version, so a version 17 reader reads it.
    CHECK(s.computers().state_version() < sim::ai::kStateWithAttackTries);
    state.players[0].strategy.attack_tries = {{0x0101, 5'000}, {0x0101, 6'000}};
    annokit::core::ByteWriter out;
    sim::ai::Saver saver(out, sim::ai::kStateWithAttackTries);
    saver(state);
    const std::vector<std::uint8_t> bytes = out.take();
    annokit::core::ByteReader in(bytes);
    sim::ComputerPlayers computers(s);
    REQUIRE(computers.load(in, sim::ai::kStateWithAttackTries));
    CHECK(computers.state_version() == sim::ai::kStateWithAttackTries);
    const std::vector<std::uint8_t> again = saved(computers);
    CHECK(again == bytes);
    annokit::core::ByteReader back(again);
    sim::StateIn reader(back);
    sim::ai::State loaded;
    sim::ai::Loader loader(reader, computers.state_version());
    loader(loaded);
    REQUIRE(reader.ok());
    CHECK(loaded.players[0].strategy.attack_tries == state.players[0].strategy.attack_tries);
    // A version 17 state loads with none.
    annokit::core::ByteWriter old;
    sim::ai::Saver old_saver(old, sim::ai::kStateWithWarParty);
    old_saver(state);
    const std::vector<std::uint8_t> v17 = old.take();
    annokit::core::ByteReader old_in(v17);
    sim::ComputerPlayers older(s);
    REQUIRE(older.load(old_in, sim::ai::kStateWithWarParty));
    CHECK(older.state_version() < sim::ai::kStateWithAttackTries);
}

TEST_CASE("ai: land freed on its island lets a computer's city search its sites anew, not the original's") {
    for (const bool classic : {false, true}) {
        Game g;
        sim::Rules rules;
        rules.bugs.classic_ai = classic;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        found_city(g, s);
        // Every search of the city given up.
        sim::ai::State state;
        {
            const std::vector<std::uint8_t> now = saved(s.computers());
            annokit::core::ByteReader in(now);
            sim::StateIn reader(in);
            sim::ai::Loader loader(reader, s.computers().state_version());
            loader(state);
            REQUIRE(reader.ok());
        }
        std::optional<std::uint16_t> island;
        for (sim::ai::AiPlayer& p : state.players) {
            for (sim::ai::CityPlan& r : p.plans) {
                island = r.island;
                r.flags |= sim::ai::city_flag::kMarket;
                for (sim::ai::SlotState& slot : r.slots) {
                    slot.attempts = 2;
                    slot.fail_level = 2;
                }
            }
        }
        REQUIRE(island.has_value());
        annokit::core::ByteWriter out;
        sim::ai::Saver saver(out);
        saver(state);
        const std::vector<std::uint8_t> bytes = out.take();
        annokit::core::ByteReader in(bytes);
        sim::ComputerPlayers computers(s);
        REQUIRE(computers.load(in, sim::ComputerPlayers::max_state_version()));
        const auto given_up = [&](bool expected) {
            for (const auto& v : computers.view()) {
                for (const auto& c : v.plans) {
                    CHECK(((c.flags & sim::ai::city_flag::kMarket) != 0) == expected);
                    for (const auto& slot : c.slots) {
                        CHECK((slot[1] == 2 && slot[2] == 2) == expected);
                    }
                }
            }
        };
        computers.land_freed(static_cast<std::uint16_t>(*island + 1));  // another island
        given_up(true);
        computers.land_freed(*island);
        given_up(classic);  // the original waits for its hourly new start
    }
}

TEST_CASE("ai: at its hourly new start an improved computer keeps its cities' stages, the original's starts over") {
    for (const bool classic : {false, true}) {
        Game g;
        sim::Rules rules;
        rules.bugs.classic_ai = classic;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        found_city(g, s);
        const auto state_of = [](const sim::ComputerPlayers& ai) {
            sim::ai::State state;
            const std::vector<std::uint8_t> now = saved(ai);
            annokit::core::ByteReader in(now);
            sim::StateIn reader(in);
            sim::ai::Loader loader(reader, ai.state_version());
            loader(state);
            REQUIRE(reader.ok());
            return state;
        };
        // The city has reached the main city's stage, and the hour is up.
        sim::ai::State state = state_of(s.computers());
        bool found = false;
        for (sim::ai::AiPlayer& p : state.players) {
            for (sim::ai::CityPlan& r : p.plans) {
                r.stage = sim::ai::stage::kMain;
                p.max_stage = sim::ai::stage::kMain;
                found = true;
            }
            p.init_ms = g.world.time_ms() + 1;
        }
        REQUIRE(found);
        annokit::core::ByteWriter out;
        sim::ai::Saver saver(out);
        saver(state);
        const std::vector<std::uint8_t> bytes = out.take();
        annokit::core::ByteReader in(bytes);
        sim::ComputerPlayers computers(s);
        REQUIRE(computers.load(in, sim::ComputerPlayers::max_state_version()));
        for (int i = 0; i < 30; ++i) {
            computers.tick(500);
        }
        bool started_over = false;
        for (const sim::ai::AiPlayer& p : state_of(computers).players) {
            started_over = started_over || p.init_ms <= g.world.time_ms();
        }
        CHECK(started_over);
        for (const auto& v : computers.view()) {
            for (const auto& c : v.plans) {
                CHECK((c.stage >= sim::ai::stage::kMain) == !classic);
            }
        }
    }
}

TEST_CASE("ai: the names for new cities and ships travel with the saved game") {
    Game a;
    sim::Simulation sa(a.world, a.buildings, a.figures);
    sa.offer_names({"Neustadt", "Altdorf"}, {"Seeadler"});
    sa.offer_names({"Newtown"}, {"Sea eagle"});  // the game has names already
    const std::vector<std::string> german{"Neustadt", "Altdorf"};
    CHECK(sa.city_names() == german);
    run(sa, 40'000);
    const auto bytes = sim::write_game(a.world, a.buildings, &sa, sim::SaveOptions{false});
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, a.buildings, nullptr, &a.figures);
    REQUIRE(loaded.ok());
    sim::World world = std::move(loaded).value();
    sim::Simulation sb(world, a.buildings, a.figures);
    CHECK(!sb.state_damaged());
    // An installation in another language keeps the game's names, and the computers stay alike.
    sb.offer_names({"Newtown"}, {"Sea eagle"});
    CHECK(sb.city_names() == sa.city_names());
    CHECK(sa.state_hash() == sb.state_hash());
    CHECK(a.cities_of(kComputer) == 1);
    CHECK(a.world.cities().front().name == "Neustadt");
}

TEST_CASE("ai: the players' ships without a name get names of their own from the list") {
    // The start ships of the players five on are copies of a level's ship and come without a name.
    Game a;
    a.add_ship(kComputer, 80, 60).name.clear();
    a.add_ship(kHuman, 82, 60).name.clear();
    sim::Ship trader = sim::make_ship("", 84, 60, kShipFigure, kTrader, 2);
    a.world.add_ship(trader);
    sim::Simulation sa(a.world, a.buildings, a.figures);
    sa.offer_names({"Neustadt"}, {"Seeadler", "Gull", "Albatros"});
    const auto& ships = a.world.ships();
    CHECK(ships[0].name == "Ship");  // a named ship keeps its name
    REQUIRE(!ships[1].name.empty());
    REQUIRE(!ships[2].name.empty());
    CHECK(ships[1].name != ships[2].name);
    CHECK(ships[3].name.empty());  // only the players' ships
}

TEST_CASE("ai: the state of the computers goes to bytes and back whole") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 20'000);
    const auto bytes = saved(s.computers());
    sim::ComputerPlayers copy(s);
    annokit::core::ByteReader in(bytes);
    REQUIRE(copy.load(in, s.computers().state_version()));
    CHECK(in.at_end());
    CHECK(same(copy.view(), s.computers().view()));
    CHECK(saved(copy) == bytes);
}

TEST_CASE("ai: damaged state is refused and leaves the computers as they were") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 20'000);
    const auto bytes = saved(s.computers());
    REQUIRE(bytes.size() > 4);

    // A copy that knows a different moment: the state before the founding.
    Game h;
    sim::Simulation t(h.world, h.buildings, h.figures);
    run(t, 2'000);
    sim::ComputerPlayers copy(s);
    {
        const auto earlier = saved(t.computers());
        annokit::core::ByteReader in(earlier);
        REQUIRE(copy.load(in, t.computers().state_version()));
    }
    const auto before = saved(copy);
    REQUIRE(before != bytes);

    // Every truncation of the good state.
    std::size_t accepted = 0;
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        annokit::core::ByteReader in(std::span(bytes.data(), n));
        accepted += copy.load(in, s.computers().state_version()) ? 1U : 0U;
    }
    CHECK(accepted == 0);
    CHECK(saved(copy) == before);

    // An unknown version, and garbage after the right one.
    {
        annokit::core::ByteReader in(bytes);
        CHECK(!copy.load(in, 0x63));
    }
    for (const std::uint8_t fill : {std::uint8_t{0xFF}, std::uint8_t{0x7F}, std::uint8_t{0x01}}) {
        std::vector<std::uint8_t> garbage(4096, fill);
        std::copy_n(bytes.begin(), 4, garbage.begin());
        annokit::core::ByteReader in(garbage);
        CHECK(!copy.load(in, s.computers().state_version()));
    }
    CHECK(saved(copy) == before);
}

TEST_CASE("ai: reset takes the computers from the world, and only them") {
    Game g;
    // A pirate slot too: the pirates are run by the same machinery.
    std::vector<sim::Player> players = g.world.players();
    players.push_back(sim::Player{});
    sim::make_neutral(players.back(), sim::faction::kPirates);
    g.world.set_players(players);
    sim::Simulation s(g.world, g.buildings, g.figures);
    REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
    run(s, 5'000);

    sim::ComputerPlayers fresh(s);
    fresh.reset();
    const auto view = fresh.view();
    REQUIRE(view.size() == 2);
    CHECK(view[0].player == kComputer);
    CHECK(view[1].player == 4);
    for (const auto& v : view) {
        CHECK(v.player != kHuman);
        CHECK(v.player != kTrader);
        CHECK(v.player != kNatives);
    }
    // The computer's city is known again, nothing is half done.
    CHECK(view[0].cities == 1);
    REQUIRE(view[0].plans.size() == 1);
    CHECK(view[0].plans[0].island == 1);
    CHECK(!view[0].settling);
    CHECK(view[0].stack.empty());
    CHECK(view[1].cities == 0);
}

TEST_CASE("ai: humans, the trader and the natives are left alone") {
    Game g(false);
    // The computer's slot is played by a human now; a human with a ship and money too.
    g.world.player(kComputer)->role = sim::PlayerRole::Human;
    sim::Ship& ship = g.add_ship(kHuman, 86, 45);
    const std::uint32_t id = ship.id;
    const std::uint32_t x = ship.x;
    const std::uint32_t y = ship.y;
    const std::vector<sim::Player> before = g.world.players();
    sim::Simulation s(g.world, g.buildings, g.figures);
    CHECK(s.computers().view().empty());
    run(s, 120'000);
    CHECK(s.computers().view().empty());
    CHECK(g.world.cities().empty());
    CHECK(!s.computers().directs(id));
    const sim::Ship* after = g.world.ship(id);
    REQUIRE(after != nullptr);
    CHECK(after->x == x);
    CHECK(after->y == y);
    for (std::size_t p = 0; p < before.size(); ++p) {
        CHECK(g.world.players()[p].money == before[p].money);
    }
}

TEST_CASE("ai: a seat the computer did not hold before is taken on when it becomes the computer's") {
    // A game saved after the host left gives the other people's seats to the computer.
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    run(s, 5'000);
    REQUIRE(s.computers().view().size() == 1);
    g.world.player(kHuman)->role = sim::PlayerRole::Computer;
    run(s, 1'000);
    const auto view = s.computers().view();
    REQUIRE(view.size() == 2);
    CHECK(view[0].player == kComputer);
    CHECK(view[1].player == kHuman);
}

TEST_CASE("ai: a computer without a ship founds nothing and harms nobody") {
    Game g(false);
    sim::Simulation s(g.world, g.buildings, g.figures);
    const std::int32_t money = g.world.players()[kComputer].money;
    run(s, 60'000);
    CHECK(g.world.cities().empty());
    CHECK(g.world.ships().empty());
    CHECK(g.world.players()[kComputer].money == money);
    CHECK(g.world.players()[kHuman].money == 10000);
    const auto view = s.computers().view();
    REQUIRE(view.size() == 1);
    CHECK(view[0].cities == 0);
    CHECK(!view[0].settling);
    CHECK(!s.computers().directs(0));
    CHECK(!s.computers().directs(12345));
}

TEST_CASE("ai: a computer a scenario keeps quiet stays so until a human breaks the peace") {
    Game g;
    g.add_ship(kHuman, 86, 10);  // a human still in the game
    g.world.player(kComputer)->scenario.flags = sim::scenario_flag::kPassive | sim::scenario_flag::kNoTrader;
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s);
    run(s, 120'000);
    CHECK(g.world.players()[kComputer].scenario.has(sim::scenario_flag::kPassive));
    // War with the human ends the switch for good; the others stay.
    auto& d = g.world.diplomacy();
    REQUIRE(d.set_peace(g.world, kHuman, kComputer, sim::Treaty::None, g.world.time_ms()));
    CHECK(run_until(s, 600'000,
                    [&] { return !g.world.players()[kComputer].scenario.has(sim::scenario_flag::kPassive); }));
    CHECK(g.world.players()[kComputer].scenario.flags == sim::scenario_flag::kNoTrader);
}

TEST_CASE("ai: a computer the scenario forbids colonies founds its first city, wants no second") {
    Game g;
    g.world.player(kComputer)->scenario.flags = sim::scenario_flag::kNoColonies;
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s);
    run(s, 600'000);
    CHECK(g.cities_of(kComputer) == 1);
    const auto view = s.computers().view();
    REQUIRE(view.size() == 1);
    CHECK(!view[0].settling);
}

TEST_CASE("ai: the original's computer without money founds its city all the same, an improved one cannot") {
    // The original gives a computer short of money its founding money (task 4); an improved
    // computer gets nothing from nowhere and cannot pay for the Kontor, as a player could not.
    const auto founds = [](bool original) {
        Game g(true, 0);
        sim::Rules rules;
        rules.bugs.classic_ai = original;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        const bool founded = run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; });
        run(s, 30'000);
        CHECK(g.world.players()[kComputer].money >= 0);
        CHECK(g.world.players()[kHuman].money == 10000);
        return founded;
    };
    CHECK(founds(true));
    CHECK(!founds(false));
}

TEST_CASE("ai: a city founded next to a young colony is remembered against the founder") {
    struct Case {
        sim::PlayerId founder = kHuman;
        std::uint16_t island = 0;  ///< where the new city lies
        bool colony = true;        ///< the computer has a colony on the islet
        std::uint32_t people = 0;  ///< its inhabitants (two classes)
        bool war = false;
    };
    // The computer's home city on the big island (island 1), a colony on the islet (island 0); then
    // somebody founds a city.
    const auto remembered = [](const Case& k) {
        Game g(false);
        const auto add_city = [&](std::uint16_t island, std::uint8_t index, sim::PlayerId owner, std::uint32_t people) {
            sim::City c;
            c.island = island;
            c.index = index;
            c.owner = owner;
            c.inhabitants = {people - people / 2, people / 2};
            g.world.add_city(c);
        };
        add_city(1, 0, kComputer, 0);
        if (k.colony) {
            add_city(0, 0, kComputer, k.people);
        }
        if (k.war) {
            g.world.diplomacy().relation(kComputer, kHuman).peace = sim::Treaty::None;
        }
        add_city(k.island, 1, k.founder, 0);
        sim::Simulation s(g.world, g.buildings, g.figures);
        sim::ComputerPlayers ai(s);
        ai.reset();
        ai.city_founded(k.island, 1);
        const auto& events = g.world.diplomacy().events(kComputer);
        return std::any_of(events.begin(), events.end(), [&](const sim::DiplomacyEvent& e) {
            return e.type == sim::DiplomacyEventType::Settled && e.partner == k.founder;
        });
    };

    const auto with = [](auto&& change) {
        Case k;
        change(k);
        return k;
    };
    CHECK(remembered(Case{}));                                         // at peace with the founder
    CHECK(remembered(with([](Case& k) { k.people = 9; })));            // nine are still a young colony
    CHECK(!remembered(with([](Case& k) { k.people = 10; })));          // ten are not
    CHECK(!remembered(with([](Case& k) { k.war = true; })));           // at war: nothing more to hold against him
    CHECK(!remembered(with([](Case& k) { k.island = 1; })));           // on the island of the home city
    CHECK(!remembered(with([](Case& k) { k.founder = kComputer; })));  // its own second city there
    CHECK(!remembered(with([](Case& k) { k.colony = false; })));       // no colony of the computer there
}

TEST_CASE("ai: a computer answers a ship that is about to found a city on its island") {
    using Kind = sim::DiplomacyNotice::Kind;
    struct Case {
        sim::PlayerId asker = kHuman;
        std::uint16_t island = 0;  ///< where the ship wants to found
        std::uint32_t people = 9;  ///< inhabitants of the computer's colony on the islet
        std::uint8_t max_stage = 0x29;
        bool home = true;  ///< the computer's home city is known (on the big island)
        bool war = false;
        std::uint16_t size = 0;  ///< template size of the islet
    };
    // The computer's home city on the big island (island 1), a colony on the islet (island 0).
    const auto answer = [](const Case& k) -> std::optional<sim::DiplomacyNotice> {
        Game g(false);
        const auto add_city = [&](std::uint16_t island, sim::PlayerId owner, std::uint32_t people) {
            sim::City c;
            c.island = island;
            c.owner = owner;
            c.inhabitants = {people - people / 2, people / 2};
            g.world.add_city(c);
        };
        add_city(1, kComputer, 400);
        add_city(0, kComputer, k.people);
        g.world.island(0).header.template_size = k.size;
        if (k.war) {
            g.world.diplomacy().relation(kComputer, kHuman).peace = sim::Treaty::None;
        }
        sim::Simulation s(g.world, g.buildings, g.figures);
        sim::ComputerPlayers ai(s);
        ai.reset();
        sim::ai::State state;
        {
            const std::vector<std::uint8_t> now = saved(ai);
            annokit::core::ByteReader in(now);
            sim::StateIn reader(in);
            sim::ai::Loader loader(reader, ai.state_version());
            loader(state);
            REQUIRE(reader.ok());
        }
        for (sim::ai::AiPlayer& p : state.players) {
            p.max_stage = k.max_stage;
            p.home_valid = k.home;
            p.home_island = 1;
        }
        annokit::core::ByteWriter out;
        sim::ai::Saver saver(out, sim::ComputerPlayers::max_state_version());
        saver(state);
        const std::vector<std::uint8_t> bytes = out.take();
        annokit::core::ByteReader in(bytes);
        REQUIRE(ai.load(in, sim::ComputerPlayers::max_state_version()));
        (void)g.world.diplomacy().take_notices();
        ai.founding_query(k.asker, k.island);
        const auto notices = g.world.diplomacy().take_notices();
        if (notices.empty()) {
            return std::nullopt;
        }
        REQUIRE(notices.size() == 1);
        CHECK(notices[0].to == k.asker);
        CHECK(notices[0].from == kComputer);
        CHECK(notices[0].island == k.island);  // its city there
        return notices[0];
    };
    const auto kind = [&](const Case& k) { return answer(k).value().kind; };
    const auto with = [](auto&& change) {
        Case k;
        change(k);
        return k;
    };
    // A young colony of a computer that has reached the merchants: it would tolerate it.
    CHECK(kind(Case{}) == Kind::FoundingTolerated);
    CHECK(kind(with([](Case& k) { k.people = 10; })) == Kind::FoundingRefused);       // a colony of ten is not young
    CHECK(kind(with([](Case& k) { k.max_stage = 0x28; })) == Kind::FoundingRefused);  // not that far yet
    // On the island of its home city, or with no home city known: a protest under threat of war.
    CHECK(kind(with([](Case& k) { k.island = 1; })) == Kind::FoundingProtest);
    CHECK(kind(with([](Case& k) { k.home = false; })) == Kind::FoundingProtest);
    // No answer at war, on a large island, to itself, or where it has no city.
    CHECK(!answer(with([](Case& k) { k.war = true; })));
    CHECK(!answer(with([](Case& k) { k.size = 4; })));
    CHECK(answer(with([](Case& k) { k.size = 3; })));
    CHECK(!answer(with([](Case& k) { k.asker = kComputer; })));
    CHECK(!answer(with([](Case& k) { k.island = 7; })));
}

TEST_CASE("ai: a computer's colony plan and the ship kept for it survive a save") {
    Game g(false);
    sim::Simulation s(g.world, g.buildings, g.figures);
    sim::ComputerPlayers ai(s);
    ai.reset();
    const std::uint32_t before = ai.state_version();
    CHECK(before < sim::ai::kStateWithColonyPlan);  // no plan: the older version suffices
    sim::ai::State state;
    {
        const std::vector<std::uint8_t> now = saved(ai);
        annokit::core::ByteReader in(now);
        sim::StateIn reader(in);
        sim::ai::Loader loader(reader, before);
        loader(state);
        REQUIRE(reader.ok());
    }
    REQUIRE(!state.players.empty());
    state.players[0].settle.plan_crop = sim::ware::kTobaccoField;
    state.players[0].settle.plan_ms = 123'456;
    state.players[0].settle.plan_ship = 7;
    annokit::core::ByteWriter out;
    sim::ai::Saver saver(out, sim::ComputerPlayers::max_state_version());
    saver(state);
    const std::vector<std::uint8_t> bytes = out.take();
    annokit::core::ByteReader in(bytes);
    REQUIRE(ai.load(in, sim::ComputerPlayers::max_state_version()));
    CHECK(ai.state_version() == sim::ai::kStateWithColonyPlan);
    const std::vector<std::uint8_t> again = saved(ai);
    annokit::core::ByteReader in2(again);
    sim::StateIn reader(in2);
    sim::ai::State copy;
    sim::ai::Loader loader(reader, sim::ai::kStateWithColonyPlan);
    loader(copy);
    REQUIRE(reader.ok());
    REQUIRE(!copy.players.empty());
    CHECK(copy.players[0].settle.plan_crop == sim::ware::kTobaccoField);
    CHECK(copy.players[0].settle.plan_ms == 123'456);
    CHECK(copy.players[0].settle.plan_ship == 7);
    // A game saved before (version 12, a ship being loaded) loads and saves the same bytes again.
    state.players[0].settle.plan_crop = 0;
    state.players[0].settle.plan_ms = 0;
    state.players[0].settle.plan_ship = 0;
    state.players[0].settle.load_ms = 7'000;
    annokit::core::ByteWriter older;
    sim::ai::Saver old_saver(older, sim::ai::kStateWithColonyLoad);
    old_saver(state);
    const std::vector<std::uint8_t> v12 = older.take();
    annokit::core::ByteReader in3(v12);
    REQUIRE(ai.load(in3, sim::ai::kStateWithColonyLoad));
    CHECK(ai.state_version() == sim::ai::kStateWithColonyLoad);
    CHECK(saved(ai) == v12);
}

TEST_CASE("ai: a founding the computers do not know of is ignored") {
    Game g(false);
    sim::Simulation s(g.world, g.buildings, g.figures);
    sim::ComputerPlayers ai(s);
    ai.reset();
    const auto before = saved(ai);
    ai.city_founded(1, 0);   // no such city
    ai.city_founded(99, 7);  // no such island
    CHECK(saved(ai) == before);
    for (const auto& e : g.world.diplomacy().events(kComputer)) {
        CHECK(e.type == sim::DiplomacyEventType::None);
    }
}

TEST_CASE("ai: computers sign trade treaties with each other and leave their peace alone") {
    Game g;
    // Slot 0 is a computer with a city on the islet.
    g.world.player(kHuman)->role = sim::PlayerRole::Computer;
    sim::City c;
    c.island = 0;
    c.owner = kHuman;
    g.world.add_city(c);
    sim::Simulation s(g.world, g.buildings, g.figures);
    const auto& d = g.world.diplomacy();
    REQUIRE(d.relation(kComputer, kHuman).trade == sim::Treaty::None);
    const sim::Treaty with_trader = d.relation(kComputer, kTrader).trade;
    const bool signed_both = run_until(s, 600'000, [&] {
        return d.relation(kComputer, kHuman).trade == sim::Treaty::Signed &&
               d.relation(kHuman, kComputer).trade == sim::Treaty::Signed;
    });
    CHECK(signed_both);
    CHECK(g.cities_of(kComputer) == 1);
    CHECK(d.relation(kComputer, kHuman).peace == sim::Treaty::Signed);
    CHECK(d.relation(kHuman, kComputer).peace == sim::Treaty::Signed);
    CHECK(d.relation(kComputer, kTrader).trade == with_trader);
}

TEST_CASE("ai: a computer's colony plan counts its own gold once") {
    // With its goal gold, a computer that has no gold vein wants a colony for it once its budget
    // carries one. Its colony plan keeps the colony's 2000 gold; holding 3000, the goal's colony is
    // still carried by that kept gold and is not refused as if it cost the gold a second time.
    Game g;
    assets::ResidentClass pioneers;
    pioneers.max_per_house = 2;
    pioneers.tax = 1;
    g.buildings.set_resident_classes({pioneers});
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s);
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            g.world.city(c.island, c.index)->inhabitants = {400};
        }
    }
    g.world.player(kComputer)->money = 3000;
    const std::vector<std::uint8_t> bytes = saved(s.computers());
    annokit::core::ByteReader in(bytes);
    sim::StateIn reader(in);
    sim::ai::State state;
    sim::ai::Loader loader(reader, s.computers().state_version());
    loader(state);
    REQUIRE(reader.ok());
    sim::ai::AiPlayer* p = nullptr;
    for (sim::ai::AiPlayer& k : state.players) {
        if (k.player == kComputer) {
            p = &k;
        }
    }
    REQUIRE(p != nullptr);
    p->strategy.goal = sim::ai::goal::kGold;
    p->settle.active = false;
    p->settle.ship = 0;
    sim::ai::Engine engine(s, state);
    p->settle.plan_crop = 0;
    CHECK(engine.goal_crop(*p) == assets::ware::kGold);
    // The plan holds its gold: the goal's colony is the same one, carried by the same gold.
    p->settle.plan_crop = assets::ware::kGold;
    CHECK(engine.goal_crop(*p) == assets::ware::kGold);
}

TEST_CASE("ai: below the hardest level a rival is near only by cities on islands the computer discovered") {
    // A player knows of another's city only once their ships have discovered its island. Here the
    // rival's city stands on the computer's home island, which the test leaves undiscovered; the
    // hardest level sees every city.
    for (const std::uint8_t difficulty : {std::uint8_t{2}, std::uint8_t{3}}) {
        Game g;
        sim::World::GameSettings settings = g.world.game_settings();
        settings.difficulty = difficulty;
        g.world.set_game_settings(settings);
        sim::City own;
        own.island = 1;
        own.owner = kComputer;
        g.world.add_city(own);
        sim::City theirs;
        theirs.island = 1;
        theirs.owner = kHuman;
        g.world.add_city(theirs);
        sim::Simulation s(g.world, g.buildings, g.figures);
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::ai::AiPlayer p;
        p.player = kComputer;
        p.home_valid = true;
        p.home_island = 1;
        p.home_city = 0;
        const bool hardest = difficulty == 3;
        REQUIRE(!g.world.islands()[1].discovered_by(kComputer));
        bool unseen = false;
        CHECK(engine.rival_near(p, kHuman, &unseen) == hardest);
        CHECK(unseen == !hardest);
        g.world.island(1).discover(kComputer);
        unseen = false;
        CHECK(engine.rival_near(p, kHuman, &unseen));
        CHECK(!unseen);
        // A player without a city there is not near.
        CHECK(!engine.rival_near(p, sim::PlayerId{2}));
    }
}

TEST_CASE("ai: below the hardest level a war is measured by the target's cities the computer knows of") {
    // A player reads the inhabitants of a city on the map, but only of cities on islands they have
    // discovered. The measure counts each known city's inhabitants and fifty for the city; the
    // hardest level counts every city.
    for (const std::uint8_t difficulty : {std::uint8_t{2}, std::uint8_t{3}}) {
        Game g;
        sim::World::GameSettings settings = g.world.game_settings();
        settings.difficulty = difficulty;
        g.world.set_game_settings(settings);
        for (const std::uint16_t island : {std::uint16_t{0}, std::uint16_t{1}}) {
            sim::City c;
            c.island = island;
            c.owner = kHuman;
            c.inhabitants = {100, 20};
            g.world.add_city(c);
        }
        sim::Simulation s(g.world, g.buildings, g.figures);
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::ai::AiPlayer p;
        p.player = kComputer;
        const bool hardest = difficulty == 3;
        REQUIRE(!g.world.islands()[0].discovered_by(kComputer));
        REQUIRE(!g.world.islands()[1].discovered_by(kComputer));
        CHECK(engine.war_measure(p, kHuman) == (hardest ? 2U * 170U : 0U));
        g.world.island(1).discover(kComputer);
        CHECK(engine.war_measure(p, kHuman) == (hardest ? 2U : 1U) * 170U);
        g.world.island(0).discover(kComputer);
        CHECK(engine.war_measure(p, kHuman) == 2U * 170U);
    }
}

TEST_CASE("ai: below the hardest level a computer knows a partner's Kontor as a human would") {
    // A human opens a partner's Kontor window on an island they have discovered and see there its
    // offer: stock less what it keeps and what carriers have reserved, nothing under a ton. The
    // hardest level sees every Kontor and its stock less what it keeps.
    constexpr std::uint8_t kWare = assets::ware::kCloth;
    constexpr std::uint32_t kTon = 32;
    for (const std::uint8_t difficulty : {std::uint8_t{2}, std::uint8_t{3}}) {
        Game g;
        sim::World::GameSettings settings = g.world.game_settings();
        settings.difficulty = difficulty;
        g.world.set_game_settings(settings);
        sim::City c;
        c.island = 1;
        c.owner = kHuman;
        g.world.add_city(c);
        sim::City* city = g.world.city(1, 0);
        REQUIRE(city != nullptr);
        city->ensure_ware(kWare);
        city->trade.resize(std::size_t{kWare} + 1);
        city->trade[kWare].sell = true;
        city->trade[kWare].keep = 10 * kTon;
        city->goods[kWare] = 30 * kTon;
        city->reserved[kWare] = 4 * kTon;
        sim::Simulation s(g.world, g.buildings, g.figures);
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::ai::AiPlayer p;
        p.player = kComputer;
        const bool hardest = difficulty == 3;
        REQUIRE(!g.world.islands()[1].discovered_by(kComputer));
        CHECK(engine.kontor_known(p, *city) == hardest);
        g.world.island(1).discover(kComputer);
        CHECK(engine.kontor_known(p, *city));
        CHECK(engine.kontor_offer(*city, kWare) == (hardest ? 20U : 16U) * kTon);
        // Less than a ton the window does not offer.
        city->reserved[kWare] = 20 * kTon - kTon / 2;
        CHECK(engine.kontor_offer(*city, kWare) == (hardest ? 20U * kTon : 0U));
        // What it does not sell it offers nowhere.
        city->trade[kWare].sell = false;
        CHECK(engine.kontor_offer(*city, kWare) == 0U);
    }
}

TEST_CASE("ai: a crop counts as grown by a computer only where its farm finds a site") {
    // The computer's island grows tobacco fully. Once its tobacco farm found no site there after
    // repeated tries, an improved computer no longer counts the crop as its own and may found a
    // colony for it; the original's computers count the island's crops as before.
    for (const bool original : {true, false}) {
        Game g;
        sim::Rules rules;
        rules.bugs.classic_ai = original;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        found_city(g, s);
        const std::vector<std::uint8_t> bytes = saved(s.computers());
        annokit::core::ByteReader in(bytes);
        sim::StateIn reader(in);
        sim::ai::State state;
        sim::ai::Loader loader(reader, s.computers().state_version());
        loader(state);
        REQUIRE(reader.ok());
        sim::ai::AiPlayer* p = nullptr;
        for (sim::ai::AiPlayer& k : state.players) {
            if (k.player == kComputer) {
                p = &k;
            }
        }
        REQUIRE(p != nullptr);
        REQUIRE(!p->plans.empty());
        sim::ai::Engine engine(s, state);
        CHECK(engine.crop_available(*p, assets::ware::kTobaccoField));
        p->plans.front().slots[sim::ai::kSlotTobacco].fail_level = 2;
        CHECK(engine.crop_available(*p, assets::ware::kTobaccoField) == original);
        // Another crop of the island still counts.
        CHECK(engine.crop_available(*p, assets::ware::kSpiceField));
    }
}

TEST_CASE("ai: a computer at war takes a human's peace offer, unless it is still angry") {
    for (const bool angry : {false, true}) {
        Game g;
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s);
        auto& d = g.world.diplomacy();
        d.relation(kComputer, kHuman).peace = sim::Treaty::None;
        d.relation(kHuman, kComputer).peace = sim::Treaty::None;
        if (angry) {
            d.relation(kComputer, kHuman).damage = 400;  // a mood of -100
        }
        REQUIRE(d.set_peace(g.world, kHuman, kComputer, sim::Treaty::Offered, g.world.time_ms()));
        REQUIRE(d.relation(kComputer, kHuman).peace == sim::Treaty::Received);
        // A changed treaty is answered at once.
        run(s, 1'000);
        CHECK(d.relation(kComputer, kHuman).peace == (angry ? sim::Treaty::Received : sim::Treaty::Signed));
        CHECK(d.relation(kHuman, kComputer).peace == (angry ? sim::Treaty::Offered : sim::Treaty::Signed));
    }
}

TEST_CASE("ai: a computer a human has hurt declares war if it is as strong, else pays the pirates against him") {
    for (const bool human_fleet : {false, true}) {
        Game g;
        add_pirates(g);
        constexpr sim::PlayerId kPirates = 4;
        // The cannoneer's damage weighs the cannons of a fleet.
        const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
        auto figures = assets::FigureTable::from_cod(assets::cod::parse(
            "Objekt: FIGUR\n"
            "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
            "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Preis: 1000\n  Rohstoff: 23\n  Rohmenge: 25\n"
            "  Nummer: 25\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 3\n  Maxkanon: 8\n  Speed: 550\n"
            "  Drehtime: 0.13\n  Maxenergy: 65\n  Hitpoint: 2.0\n  Preis: 1500\n  Rohstoff: 23\n  Rohmenge: 32\n"
            "  Nummer: 13\n  Gfx: 0\n  Hitpoint: 50.0\n  Worktime: 1.0\n"
            "EndObj\n",
            types));
        REQUIRE(figures.ok());
        g.figures = std::move(figures).value();
        if (human_fleet) {
            // A warship (KRIEG1): a merchant ship's cannons are not seen below the hardest level.
            sim::Ship& warship = g.add_ship(kHuman, 5, 80);
            warship.figure = assets::figure::ship_figure(assets::figure::kFirstWarship);
            warship.cannons = 20;
        }
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s);
        auto& d = g.world.diplomacy();
        REQUIRE(d.relation(kComputer, kHuman).peace == sim::Treaty::Signed);
        d.relation(kComputer, kHuman).damage = 400;
        // Rich enough to bribe the pirates, with 9000 to spare.
        g.world.player(kComputer)->money = 30'000;
        const std::int32_t before = g.world.players()[kComputer].money;
        // The computer looks at its relations every 30 s.
        run(s, 31'000);
        if (!human_fleet) {
            CHECK(d.relation(kComputer, kHuman).peace == sim::Treaty::None);
            CHECK(d.relation(kHuman, kComputer).peace == sim::Treaty::None);
            CHECK(!remembers(g.world, kPirates, sim::DiplomacyEventType::BribedBy, kComputer));
        } else {
            CHECK(d.relation(kComputer, kHuman).peace == sim::Treaty::Signed);
            CHECK(remembers(g.world, kPirates, sim::DiplomacyEventType::BribedBy, kComputer));
            CHECK(remembers(g.world, kPirates, sim::DiplomacyEventType::BribedAgainst, kHuman));
            CHECK(g.world.players()[kComputer].money < before);
        }
    }
}

TEST_CASE("ai: a fight at sea shows an improved computer a warship's cannons, not more") {
    for (const bool held : {false, true}) {
        Game g;
        const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
        auto figures = assets::FigureTable::from_cod(assets::cod::parse(
            "Objekt: FIGUR\n"
            "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
            "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Preis: 1000\n  Rohstoff: 23\n  Rohmenge: 25\n"
            "  Nummer: 25\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 3\n  Maxkanon: 8\n  Speed: 550\n"
            "  Drehtime: 0.13\n  Maxenergy: 65\n  Hitpoint: 2.0\n  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n"
            "  Stirbtime: 4.0\n"
            "EndObj\n",
            types));
        REQUIRE(figures.ok());
        g.figures = std::move(figures).value();
        // At war, a computer's warship with 8 cannons and a human's without any, 4 tiles apart.
        const std::uint16_t warship = assets::figure::ship_figure(assets::figure::kFirstWarship);
        sim::Ship& own = g.add_ship(kComputer, 78, 45);
        own.figure = warship;
        own.cannons = 8;
        const std::uint32_t shooter = own.id;
        sim::Ship& other = g.add_ship(kHuman, 82, 45);  // `own` may be moved by now
        other.figure = warship;
        other.cannons = 0;
        other.held = held;  // under the white flag it is kept from firing back
        const std::uint32_t target = other.id;
        sim::Simulation s(g.world, g.buildings, g.figures);
        auto& d = g.world.diplomacy();
        d.relation(kComputer, kHuman).peace = sim::Treaty::None;
        d.relation(kHuman, kComputer).peace = sim::Treaty::None;
        REQUIRE(d.hostile(kComputer, kHuman));
        const auto state = [&] {
            const std::vector<std::uint8_t> bytes = saved(s.computers());
            annokit::core::ByteReader in(bytes);
            sim::StateIn reader(in);
            sim::ai::State out;
            sim::ai::Loader loader(reader, s.computers().state_version());
            loader(out);
            REQUIRE(reader.ok());
            return out;
        };
        const auto sighting = [](const sim::ai::State& st, std::uint32_t ship) -> const sim::ai::Sighting* {
            const auto it = std::ranges::find(st.sightings, ship, &sim::ai::Sighting::ship);
            return it != st.sightings.end() ? &*it : nullptr;
        };
        // Before the first shot nothing is seen, and the state keeps its earlier version.
        CHECK(s.computers().state_version() < sim::ai::kStateWithSightings);
        // A shot of 8 cannons does twice a single cannon's damage (cannon_factor 2): three at least.
        bool fired = false;
        bool unarmed = false;
        for (std::uint32_t t = 0; t < 30'000 && !unarmed; t += 500) {
            run(s, 500);
            const sim::ai::State st = state();
            if (const sim::ai::Sighting* r = sighting(st, shooter)) {
                CHECK(r->armed == 3);
                fired = true;
            }
            if (const sim::ai::Sighting* r = sighting(st, target)) {
                unarmed = r->unarmed;
            }
        }
        CHECK(fired);
        // The human's ship did not fire back within a reload: it has no cannons. Under the white flag
        // that tells nothing.
        CHECK(unarmed == !held);
        // The sightings travel with the saved game.
        REQUIRE(s.computers().state_version() == sim::ai::kStateWithSightings);
        const std::vector<std::uint8_t> bytes = saved(s.computers());
        annokit::core::ByteReader in(bytes);
        sim::ComputerPlayers computers(s);
        REQUIRE(computers.load(in, sim::ai::kStateWithSightings));
        CHECK(saved(computers) == bytes);
    }
}

TEST_CASE("ai: a computer a human has hurt guesses the human's merchant ships armed as its own are") {
    // Merchant ships carry cannons too, and a ship's window shows none: the computer takes another
    // player's merchant ships to carry the share of their most that its own carry.
    for (const bool armed : {false, true}) {
        Game g;
        add_pirates(g);
        constexpr sim::PlayerId kPirates = 4;
        const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
        auto figures = assets::FigureTable::from_cod(assets::cod::parse(
            "Objekt: FIGUR\n"
            "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
            "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Preis: 1000\n  Rohstoff: 23\n  Rohmenge: 25\n"
            "  Nummer: 25\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 3\n  Maxkanon: 8\n  Speed: 550\n"
            "  Drehtime: 0.13\n  Maxenergy: 65\n  Hitpoint: 2.0\n  Preis: 1500\n  Rohstoff: 23\n  Rohmenge: 32\n"
            "  Nummer: 13\n  Gfx: 0\n  Hitpoint: 50.0\n  Worktime: 1.0\n"
            "EndObj\n",
            types));
        REQUIRE(figures.ok());
        g.figures = std::move(figures).value();
        // Four merchant ships of the human's with all their cannons, far off.
        for (std::uint32_t k = 0; k < 4; ++k) {
            g.add_ship(kHuman, 5 + 4 * k, 80).cannons = 6;
        }
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s);
        // The computer's own merchant ship: unarmed, or with all its cannons.
        for (sim::Ship& ship : g.world.ships()) {
            if (ship.owner == kComputer) {
                ship.cannons = armed ? 6 : 0;
            }
        }
        auto& d = g.world.diplomacy();
        REQUIRE(d.relation(kComputer, kHuman).peace == sim::Treaty::Signed);
        d.relation(kComputer, kHuman).damage = 400;
        g.world.player(kComputer)->money = 30'000;
        run(s, 31'000);
        // Unarmed itself it takes the human's merchant ships for unarmed and declares war; armed, it
        // takes them for armed as well, is the weaker and pays the pirates.
        CHECK((d.relation(kComputer, kHuman).peace == sim::Treaty::None) == !armed);
        CHECK(remembers(g.world, kPirates, sim::DiplomacyEventType::BribedBy, kComputer) == armed);
    }
}

TEST_CASE("ai: an improved computer arms a warship at its Kontor with all the cannons it has, the original's one") {
    for (const auto& [classic, tower] : {std::pair{false, false}, std::pair{true, false}, std::pair{false, true}}) {
        Game g;
        if (tower) {
            // A stone tower the city may still build: its 2 t of cannons stay in store.
            g.buildings = make_buildings(
                "  @Nummer: 13\n  Id: 20305\n  Kind: 4\n  Gfx: 30\n  Size: 1, 1\n"
                "  Objekt: HAUS_BAUKOST\n    Kanon: 2\n    Money: 100\n  EndObj\n");
        }
        const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
        auto figures = assets::FigureTable::from_cod(assets::cod::parse(
            "Objekt: FIGUR\n"
            "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
            "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Preis: 1000\n  Rohstoff: 23\n  Rohmenge: 25\n"
            "  Nummer: 25\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 3\n  Maxkanon: 8\n  Speed: 550\n"
            "  Drehtime: 0.13\n  Maxenergy: 65\n  Hitpoint: 2.0\n  Preis: 1500\n  Rohstoff: 23\n  Rohmenge: 32\n"
            "EndObj\n",
            types));
        REQUIRE(figures.ok());
        g.figures = std::move(figures).value();
        sim::Rules rules;
        rules.bugs.classic_ai = classic;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        found_city(g, s, 30'000);
        sim::City* home = nullptr;
        for (const sim::City& c : g.world.cities()) {
            if (c.owner == kComputer) {
                home = g.world.city(c.island, c.index);
            }
        }
        REQUIRE(home != nullptr);
        // An unarmed warship at the Kontor, and 10 t of cannons in store.
        const sim::WorldIsland& isl = g.world.islands()[home->island];
        sim::Ship warship = sim::make_ship("Ship", isl.x + 48, isl.y + *home->warehouse_y,
                                           assets::figure::ship_figure(assets::figure::kFirstWarship), kComputer, 2);
        warship.cargo.resize(3);
        g.world.add_ship(warship);
        const std::uint32_t id = g.world.ships().back().id;
        const sim::Ship& ship = g.world.ships().back();
        REQUIRE(sim::building_in_reach(g.world, g.buildings, home->island, *home->warehouse_x, *home->warehouse_y,
                                       ship.x, ship.y));
        const std::uint8_t cannons = g.buildings.materials().cannons;
        home->ensure_ware(cannons);
        home->goods[cannons] = 10 * 32;
        // The computer looks at its ships every 10 s.
        run(s, 11'000);
        const sim::Ship* armed = g.world.ship(id);
        REQUIRE(armed != nullptr);
        if (tower) {
            CHECK(armed->cannons == 7);  // down to the tower's 2 t and the ton the original keeps
            CHECK(home->stock(cannons) == 3 * 32);
        } else if (!classic) {
            CHECK(armed->cannons == 8);  // the type's most, from 10 t
            CHECK(home->stock(cannons) == 2 * 32);
        } else {
            CHECK(armed->cannons <= 1);
        }
    }
}

TEST_CASE("ai: a rich computer buys peace from the pirates, a poor one does not") {
    for (const std::int32_t money : {9'000, 100'000}) {
        Game g;
        add_pirates(g);
        constexpr sim::PlayerId kPirates = 4;
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s);
        g.world.player(kComputer)->money = money;
        run(s, 7'000);  // the 6-second recruiting phase
        const bool bought = remembers(g.world, kPirates, sim::DiplomacyEventType::PeaceBought, kComputer);
        CHECK(bought == (money > 10'000));
        CHECK((g.world.players()[kComputer].money < money) == bought);
    }
}

TEST_CASE("ai: the computer switches its workshops by their switch first, as the original") {
    // A toolsmith and a weaving mill, both out of raw material for five cycles (Maxnorohst 5).
    Game g;
    g.buildings = make_buildings(
        "  @Nummer: 13\n  Id: 20990\n  Gfx: 100\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Maxnorohst: 5\n    Ware: 22\n    Rohstoff: 10\n"
        "    Rohmenge: 1\n    Prodmenge: 1\n    Interval: 20\n  EndObj\n"
        "  @Nummer: 14\n  Id: 20991\n  Gfx: 101\n  Size: 1, 1\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Maxnorohst: 5\n    Ware: 19\n    Rohstoff: 4\n"
        "    Rohmenge: 1\n    Prodmenge: 1\n    Interval: 20\n  EndObj\n");
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s);
    sim::City* city = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            city = g.world.city(c.island, c.index);
        }
    }
    REQUIRE(city != nullptr);
    sim::IslandMap& map = g.world.island(city->island).map;
    // Two free tiles of the city's ground.
    std::vector<std::pair<std::int32_t, std::int32_t>> free;
    for (std::uint32_t y = 0; y < 50 && free.size() < 2; ++y) {
        for (std::uint32_t x = 0; x < 46 && free.size() < 2; ++x) {
            const sim::MapCell& cell = map.top(x, y);
            if (cell.city == city->index && cell.owner == kComputer &&
                g.buildings.by_sprite(cell.sprite) == g.buildings.by_id(kGround)) {
                free.emplace_back(static_cast<std::int32_t>(x), static_cast<std::int32_t>(y));
            }
        }
    }
    REQUIRE(free.size() == 2);
    const auto put = [&](std::uint32_t id, std::pair<std::int32_t, std::int32_t> at, bool enabled) {
        REQUIRE(map.build(*g.buildings.by_id(id), at.first, at.second, 0, 0, kComputer, city->index).ok());
        sim::Production p;
        p.island = city->island;
        p.x = static_cast<std::uint16_t>(at.first);
        p.y = static_cast<std::uint16_t>(at.second);
        p.enabled = enabled;
        p.idle_cycles = 5;
        g.world.set_production(p);
        return *g.world.production_index(p.island, p.x, p.y);
    };
    const std::uint32_t smith = put(20990, free[0], false);  // switched off while it was starved
    const std::uint32_t mill = put(20991, free[1], true);    // still on
    constexpr std::uint32_t kTon = sim::kStockUnitsPerTon;
    city->goods.resize(std::max<std::size_t>(city->goods.size(), 25));
    city->goods[10] = 5 * kTon;  // iron
    city->goods[22] = 0;         // tools
    city->goods[4] = 10 * kTon;  // wool

    // The off toolsmith comes on: its idle count belongs to the time it was on. The starved mill
    // goes off although it has wool, and on again at a later refresh, which clears its count.
    bool smith_on = false;
    bool mill_off = false;
    bool mill_back = false;
    run_until(s, 60'000, [&] {
        city->goods[10] = 5 * kTon;
        city->goods[22] = 0;
        city->goods[4] = 10 * kTon;
        const auto& prods = g.world.productions();
        smith_on = smith_on || prods[smith].enabled;
        mill_off = mill_off || !prods[mill].enabled;
        mill_back = mill_back || (mill_off && prods[mill].enabled);
        return smith_on && mill_back;
    });
    CHECK(smith_on);
    CHECK(mill_off);
    CHECK(mill_back);

    // At 40 t of tools the toolsmith goes off, starved or not, and stays off.
    bool smith_off = false;
    run_until(s, 60'000, [&] {
        city->goods[22] = 40 * kTon;
        smith_off = !g.world.productions()[smith].enabled;
        return smith_off;
    });
    CHECK(smith_off);
    run(s, 20'000);
    CHECK(!g.world.productions()[smith].enabled);
}

TEST_CASE("ai: a computer's Kontor never asks visiting ships for more than its storage room") {
    // A Kontor of 8 t room that buys four wares: the bricks the computer wants (10 t, plus a
    // quarter) would not fit. Ships may not sell beyond the room, so the limit stays at it.
    Game g;
    g.buildings = make_buildings({}, "    Maxlager: 8\n    Maxware: 4, 4\n");
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s);
    sim::City* city = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            city = g.world.city(c.island, c.index);
        }
    }
    REQUIRE(city != nullptr);
    const std::uint8_t bricks = g.buildings.materials().bricks;
    REQUIRE(g.world.goods().is_trade(bricks));
    constexpr std::uint32_t kTon = sim::kStockUnitsPerTon;
    REQUIRE(run_until(s, 120'000, [&] {
        const sim::City::Trade* t = city->trade_of(bricks);
        return city->capacity != 0 && t != nullptr && t->buy;
    }));
    CHECK(city->capacity == 8 * kTon);
    CHECK(city->trade_of(bricks)->limit == city->capacity);
}

TEST_CASE("ai: a colony moves its Kontor up to the computer's best one when that is of use") {
    // The computer's city on the big island is made a colony (balance: too little land for more);
    // its other city on the islet has a Kontor II. The colony's Kontor follows when a good fills its
    // warehouse or the computer is rich; the original's computers move up only the main city's.
    struct Case {
        std::int32_t money = 10'000;
        bool full = false;     ///< a good fills the colony's warehouse
        bool other_ii = true;  ///< the other city has a Kontor II (else a Kontor I)
        bool classic = false;
    };
    static constexpr std::uint32_t kKontor2 = 22103;
    const auto moved_up = [](const Case& k) {
        Game g;
        g.buildings = make_buildings(
            "  @Nummer: 13\n  Id: 22103\n  Kind: 35\n  Gfx: 30\n  Size: 2, 3\n  Strandflg: 1\n  PlaceFlg: 1\n"
            "  KreuzBase: 12\n  Ausbauflg: 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Radius: 8\n    Maxlager: 50\n  EndObj\n"
            "  Objekt: HAUS_BAUKOST\n    Werkzeug: 2\n    Holz: 6\n    Money: 300\n  EndObj\n");
        sim::Balance balance;
        balance.ai_colony_below_land = 1'000'000;
        g.world.set_balance(balance);
        sim::Rules rules;
        rules.bugs.classic_ai = k.classic;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        found_city(g, s);
        sim::City* colony = nullptr;
        for (const sim::City& c : g.world.cities()) {
            if (c.owner == kComputer) {
                colony = g.world.city(c.island, c.index);
            }
        }
        REQUIRE(colony != nullptr);
        REQUIRE(colony->warehouse_x.has_value());
        const std::uint16_t colony_island = colony->island;
        const std::uint8_t colony_index = colony->index;
        const std::uint32_t kx = *colony->warehouse_x;
        const std::uint32_t ky = *colony->warehouse_y;

        // The other city: a Kontor on the islet (island 0).
        const std::uint32_t other_kontor = k.other_ii ? kKontor2 : kKontor;
        REQUIRE(g.world.island(0).map.place(*g.buildings.by_id(other_kontor), 0, 0, 0, 0, kComputer, 0).ok());
        sim::City other;
        other.island = 0;
        other.index = 0;
        other.owner = kComputer;
        other.warehouse_x = 0;
        other.warehouse_y = 0;
        g.world.add_city(other);

        // The colony has the materials; the computer has `money`.
        colony = g.world.city(colony_island, colony_index);
        const auto& m = g.buildings.materials();
        constexpr std::uint32_t kTon = sim::kStockUnitsPerTon;
        for (const std::uint8_t w : {m.tools, m.wood}) {
            colony->ensure_ware(w);
            colony->goods[w] = 10 * kTon;
        }
        g.world.player(kComputer)->money = k.money;
        const auto level = [&] {
            const sim::City* c = g.world.city(colony_island, colony_index);
            const assets::BuildingType* b = g.buildings.by_sprite(g.world.island(c->island).map.top(kx, ky).sprite);
            return b != nullptr ? b->id : 0U;
        };
        // Held before every tick: the computer's own goods would fill the warehouse too.
        const auto hold = [&] {
            sim::City* c = g.world.city(colony_island, colony_index);
            for (std::uint32_t& stock : c->goods) {
                stock = std::min(stock, c->capacity / 2);  // room enough for every good
            }
            if (k.full) {
                c->goods[m.wood] = c->capacity;  // the woodcutters fill the warehouse
            }
            g.world.player(kComputer)->money = k.money;
        };
        hold();
        return run_until(s, 180'000, [&] {
            hold();
            return level() == kKontor2;
        });
    };
    const auto with = [](auto&& change) {
        Case k;
        change(k);
        return k;
    };
    CHECK(moved_up(Case{}));                                                 // a rich computer
    CHECK(!moved_up(with([](Case& k) { k.money = 5'000; })));                // not so rich, room enough
    CHECK(moved_up(with([](Case& k) { k.money = 5'000, k.full = true; })));  // not so rich, the warehouse full
    CHECK(!moved_up(with([](Case& k) { k.other_ii = false; })));             // no better Kontor to follow
    CHECK(!moved_up(with([](Case& k) { k.classic = true; })));               // the original's computers
}

TEST_CASE("ai: a farm leaves the trees of the city's woodcutter standing (S-122)") {
    // A woodcutter in the wood west of the Kontor and a tobacco plantation next to it, both on the
    // road the computer lays from its Kontor. The original plants the tobacco over the woodcutter's
    // trees; the woodcutter then lacks trees and its fields go round. Pastures do the same with
    // their grass.
    const auto fields_in_reach = [] {
        Game g;
        g.buildings = make_buildings(
            // Woodcutter, tobacco plantation, a tobacco field growing and ripe.
            "  @Nummer: 13\n  Id: 20701\n  Kind: 14\n  Gfx: 30\n  Size: 2, 2\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Ware: 23\n    Rohstoff: 53\n    Rohmenge: 1\n"
            "    Prodmenge: 1\n    Maxlager: 4\n    Interval: 20\n    Radius: 5\n  EndObj\n"
            "  @Nummer: 14\n  Id: 20702\n  Kind: 14\n  Gfx: 40\n  Size: 2, 2\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Ware: 6\n    Rohstoff: 46\n    Rohmenge: 1\n"
            "    Prodmenge: 1\n    Maxlager: 4\n    Interval: 20\n    Radius: 4\n  EndObj\n"
            "  @Nummer: 15\n  Id: 20703\n  Kind: 10\n  Gfx: 50\n  Size: 1, 1\n  AnimAnz: 4\n  AnimAdd: 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 160\n  EndObj\n"
            "  @Nummer: 16\n  Id: 20704\n  Kind: 10\n  Gfx: 54\n  Size: 1, 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 46\n  EndObj\n");
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s, 5'000);  // the Kontor at 44,16, a road west along row 17
        sim::BuildOrder order;
        order.player = kComputer;
        order.island = 1;
        order.items = {{20701, 37, 18, 0, 0}, {20702, 40, 18, 0, 0}};
        const sim::BuildPlan plan = sim::execute_build(g.world, g.buildings, order, {}, nullptr);
        REQUIRE(plan.placed == 2);
        run(s, 30'000);
        const sim::IslandMap& map = g.world.islands()[1].map;
        const std::vector<std::uint8_t>& circle = sim::circle_table(5);
        std::size_t fields = 0;
        for (std::int32_t y = 18 - 5; y <= 19 + 5; ++y) {
            for (std::int32_t x = 37 - 5; x <= 38 + 5; ++x) {
                const std::int32_t dx = std::max({37 - x, 0, x - 38});
                const std::int32_t dy = std::max({18 - y, 0, y - 19});
                const sim::MapCell& c = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
                const assets::BuildingType* b = c.empty() ? nullptr : g.buildings.by_sprite(c.sprite);
                if (dx <= circle[static_cast<std::size_t>(dy)] && b != nullptr && (b->id == 20703 || b->id == 20704)) {
                    ++fields;
                }
            }
        }
        return fields;
    };
    CHECK(fields_in_reach() == 0);  // the original plants 39 of them at once
}

TEST_CASE("ai: a farm without room for its fields moves where moving is allowed (Rules::moving)") {
    // A tobacco plantation by the Kontor's road, the wood in its reach flooded: the sweep finds no
    // room for its fields, the wood north of the road has it. Improved computers move it to a site of their farm task;
    // where moving is off they pull it down as the original's do.
    const auto plantations = [](sim::Moving moving) {
        Game g;
        g.buildings = make_buildings(
            "  @Nummer: 13\n  Id: 20702\n  Kind: 14\n  Gfx: 40\n  Size: 2, 2\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Ware: 6\n    Rohstoff: 46\n    Rohmenge: 1\n"
            "    Prodmenge: 1\n    Maxlager: 4\n    Interval: 20\n    Radius: 4\n  EndObj\n"
            "  @Nummer: 14\n  Id: 20703\n  Kind: 10\n  Gfx: 50\n  Size: 1, 1\n  AnimAnz: 4\n  AnimAdd: 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Interval: 160\n  EndObj\n"
            "  @Nummer: 15\n  Id: 20704\n  Kind: 10\n  Gfx: 54\n  Size: 1, 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 46\n  EndObj\n");
        g.buildings.by_id(kKontor)->ware = sim::kAllWares;  // farms are placed near a market or Kontor
        sim::Rules rules;
        rules.moving = moving;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        found_city(g, s, 5'000);  // the Kontor at 44,16, a road west along row 17
        sim::BuildOrder order;
        order.player = kComputer;
        order.island = 1;
        order.items = {{20702, 41, 18, 0, 0}};
        REQUIRE(sim::execute_build(g.world, g.buildings, order, {}, nullptr).placed == 1);
        sim::IslandMap& map = g.world.island(1).map;
        for (std::uint32_t y = 16; y <= 23; ++y) {
            for (std::uint32_t x = 37; x <= 45; ++x) {
                const assets::BuildingType* b = g.buildings.by_sprite(map.top(x, y).sprite);
                if (b == g.buildings.by_id(kTree)) {
                    map.set_sprite(x, y, static_cast<std::uint16_t>(g.buildings.by_id(kSea)->gfx));
                }
            }
        }
        std::vector<sim::TilePos> found;
        run_until(s, 600'000, [&] {
            found.clear();
            for (std::uint32_t y = 0; y < map.height(); ++y) {
                for (std::uint32_t x = 0; x < map.width(); ++x) {
                    const sim::TilePos at{static_cast<std::int32_t>(x), static_cast<std::int32_t>(y)};
                    const auto b = sim::building_at(g.world, g.buildings, 1, at);
                    if (b && b->type->id == 20702 && b->origin.x == at.x && b->origin.y == at.y) {
                        found.push_back(at);
                    }
                }
            }
            return found.size() != 1 || found.front().x != 41 || found.front().y != 18;
        });
        return found;
    };
    const std::vector<sim::TilePos> moved = plantations(sim::Moving::Free);
    REQUIRE(moved.size() == 1);
    CHECK(moved.front().y < 16);  // in the wood north of the road
    CHECK(plantations(sim::Moving::Off).empty());
}

TEST_CASE("ai: a computer that finds no island for its city gives its ship to the pirates and leaves") {
    // The big island under water: only the rock is left, too small for a colony (root task
    // Ship op 5 with value 5, then the players' check retires the computer).
    Game g;
    add_pirates(g);
    constexpr sim::PlayerId kPirates = 4;
    sim::IslandMap& map = g.world.island(1).map;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            map.set_sprite(x, y, static_cast<std::uint16_t>(g.buildings.by_id(kSea)->gfx));
        }
    }
    sim::Simulation s(g.world, g.buildings, g.figures);
    const std::uint32_t ship = g.world.ships().front().id;
    REQUIRE(run_until(s, 3'600'000, [&] { return g.world.ship(ship)->owner != kComputer; }));
    CHECK(g.world.ship(ship)->owner == kPirates);
    CHECK(g.cities_of(kComputer) == 0);
    run(s, 6'000);  // the players' check every 5 s
    CHECK(!g.world.players()[kComputer].active());
}

TEST_CASE("ai: the pirates' nest offers its goods every 42 s without any food (task 5)") {
    // A nest with a pirate Kontor of 6 wares, loot but no food, and a pirate ship at sea.
    Game g(false);
    g.buildings = make_buildings(
        "  @Nummer: 13\n  Id: 22990\n  Kind: 35\n  Gfx: 120\n  Size: 2, 3\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 8\n    Maxware: 6\n    Maxlager: 30\n  EndObj\n");
    add_pirates(g);
    constexpr sim::PlayerId kPirates = 4;
    g.world.player(kHuman)->infra_mask |= 1U << 24;  // citizens: the pirates may sail
    REQUIRE(g.world.island(1).map.build(*g.buildings.by_id(22990), 44, 10, 0, 0, kPirates, 0).ok());
    sim::City nest;
    nest.island = 1;
    nest.index = 0;
    nest.owner = kPirates;
    nest.warehouse_x = 44;
    nest.warehouse_y = 10;
    nest.capacity = 30 * 32;
    nest.ensure_ware(24);
    nest.trade.resize(25);
    nest.goods[assets::ware::kJewellery] = 5 * 32;
    nest.goods[assets::ware::kTools] = 10 * 32;
    g.world.add_city(nest);
    g.add_ship(kPirates, 86, 45);
    sim::Simulation s(g.world, g.buildings, g.figures);
    const auto offered = [&] {
        const sim::City* c = g.world.city(1, 0);
        return c->trade[assets::ware::kJewellery].sell && c->trade[assets::ware::kTools].sell;
    };
    run(s, 30'000);
    CHECK(!offered());  // not before 42 s
    REQUIRE(run_until(s, 60'000, offered));
    CHECK(g.world.city(1, 0)->trade[assets::ware::kJewellery].sell_price == 358);
    // Sold out, the next offer drops the ware.
    g.world.city(1, 0)->goods[assets::ware::kJewellery] = 0;
    REQUIRE(run_until(s, 60'000, [&] { return !g.world.city(1, 0)->trade[assets::ware::kJewellery].sell; }));
    CHECK(g.world.city(1, 0)->trade[assets::ware::kTools].sell);
}

TEST_CASE("ships: only a computer's ship can be given to the pirates") {
    Game g(false);
    add_pirates(g);
    const sim::Ship& mine = g.add_ship(kHuman, 80, 40);
    const std::uint32_t human_ship = mine.id;
    const std::uint32_t computer_ship = g.add_ship(kComputer, 86, 45).id;
    sim::Simulation s(g.world, g.buildings, g.figures);
    const auto give = [&](sim::PlayerId player, std::uint32_t ship) {
        sim::ShipCommand c;
        c.player = player;
        c.ship = ship;
        c.op = sim::ShipCommand::Op::ToPirates;
        return s.execute(c).accepted;
    };
    CHECK(!give(kHuman, human_ship));
    CHECK(!give(kHuman, computer_ship));  // not his
    CHECK(give(kComputer, computer_ship));
    CHECK(g.world.ship(computer_ship)->owner == 4);
}

TEST_CASE("ai: a computer short of ships buys one a player at peace offers (Rules::computers_buy_ships)") {
    // Open1602 (the History Edition's computers buy ships too): a ship at no more than its yard
    // price, if half the money is left. Not with the rule off (the original's), not from an enemy,
    // not at a price above the yard's.
    struct Case {
        bool rule = true;
        bool war = false;
        std::uint16_t factor = 96;
        bool bought = true;
    };
    for (const Case c :
         {Case{}, Case{false, false, 96, false}, Case{true, true, 96, false}, Case{true, false, 200, false}}) {
        Game g;
        sim::Rules rules;
        rules.computers_buy_ships = c.rule;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        found_city(g, s);
        // The computer's only ship goes (to the human): with its city it wants one again.
        g.world.ships().front().owner = kHuman;
        sim::Ship& offered = g.add_ship(kHuman, 10, 80);
        offered.for_sale = true;
        offered.sale_factor = c.factor;
        const std::uint32_t id = offered.id;
        if (c.war) {
            g.world.diplomacy().relation(kHuman, kComputer).peace = sim::Treaty::None;
            g.world.diplomacy().relation(kComputer, kHuman).peace = sim::Treaty::None;
        }
        const std::int32_t human_money = g.world.player(kHuman)->money;
        const bool bought = run_until(s, 120'000, [&] { return g.world.ship(id)->owner == kComputer; });
        CHECK(bought == c.bought);
        if (bought) {
            CHECK(g.world.player(kHuman)->money == human_money + 1000 * c.factor / 128);
            CHECK(!g.world.ship(id)->for_sale);
        }
    }
    CHECK(!sim::Rules::original().computers_buy_ships);
}

TEST_CASE("ai: how many cities a computer founds, four as in the original or scaled to the world") {
    // Scaled (Open1602's default): the islands of more than the smallest size for each player who
    // plays, at least four, at most 6, 8, 10 or 12 by the difficulty.
    const auto limit = [](std::uint32_t islands, std::uint8_t difficulty, const sim::Rules& rules) {
        Game g(false);  // a big island and a rock: one island counts
        for (std::uint32_t i = 1; i < islands; ++i) {
            // The size counts by the header's 31 x 31 (961 tiles: more than the smallest size);
            // the tiles themselves lie in the open sea.
            sim::IslandMap map(1, 1);
            sim::WorldIsland island{assets::IslandHeader{}, std::move(map), i, 85};
            island.header.number = static_cast<std::uint8_t>(g.world.islands().size());
            island.header.width = 31;
            island.header.height = 31;
            REQUIRE(g.world.add_island(std::move(island)).ok());
        }
        sim::World::GameSettings settings = g.world.game_settings();
        settings.difficulty = difficulty;
        g.world.set_game_settings(settings);
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        return s.computers().city_limit();
    };
    const sim::Rules scaled;
    CHECK(scaled.computer_cities == sim::ComputerCities::Scaled);
    CHECK(limit(1, 3, scaled) == 4);   // few islands: as many as the original's
    CHECK(limit(10, 1, scaled) == 5);  // ten islands for two players
    CHECK(limit(30, 0, scaled) == 6);  // easy: six at most
    CHECK(limit(30, 1, scaled) == 8);
    CHECK(limit(30, 2, scaled) == 10);
    CHECK(limit(30, 3, scaled) == 12);  // hard: twelve
    CHECK(limit(20, 3, scaled) == 10);
    CHECK(sim::Rules::original().computer_cities == sim::ComputerCities::Original);
    CHECK(limit(30, 3, sim::Rules::original()) == 4);
}

namespace {

/// Stone walls (a family of straight, curve, T and cross) and a gate, numbered on from the Kontor.
const std::string kWallRecords =
    "  @Nummer: 13\n  Id: 22501\n  Kind: 4\n  Gfx: 30\n  Size: 1, 1\n  KreuzBase: 13\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 10\n    Ziegel: 1\n  EndObj\n"
    "  @Nummer: 14\n  Id: 22502\n  Kind: 4\n  Gfx: 31\n  Size: 1, 1\n  KreuzBase: 13\n"
    "  @Nummer: 15\n  Id: 22503\n  Kind: 4\n  Gfx: 32\n  Size: 1, 1\n  KreuzBase: 13\n"
    "  @Nummer: 16\n  Id: 22504\n  Kind: 4\n  Gfx: 33\n  Size: 1, 1\n  KreuzBase: 13\n"
    "  @Nummer: 17\n  Id: 20304\n  Kind: 3\n  Gfx: 34\n  Size: 1, 1\n"
    "  Objekt: HAUS_BAUKOST\n    Money: 20\n    Ziegel: 2\n  EndObj\n";

struct WallCount {
    std::size_t walls = 0;
    std::size_t gates = 0;
    std::size_t inner = 0;  ///< walls with all eight neighbours in the city
};

WallCount walls_of(const Game& g, const sim::City& city) {
    WallCount n;
    const sim::IslandMap& map = g.world.islands()[city.island].map;
    for (std::uint32_t y = 0; y < map.height(); ++y) {
        for (std::uint32_t x = 0; x < map.width(); ++x) {
            const sim::MapCell& c = map.top(x, y);
            const assets::BuildingType* b = c.empty() ? nullptr : g.buildings.by_sprite(c.sprite);
            if (b == nullptr || c.city != city.index) {
                continue;
            }
            if (b->kind == 3) {
                ++n.gates;
            }
            if (b->kind != 4) {
                continue;
            }
            ++n.walls;
            bool inside = true;
            for (std::int32_t dy = -1; dy <= 1; ++dy) {
                for (std::int32_t dx = -1; dx <= 1; ++dx) {
                    const auto nx = static_cast<std::int32_t>(x) + dx;
                    const auto ny = static_cast<std::int32_t>(y) + dy;
                    inside = inside && map.contains(nx, ny) &&
                             map.top(static_cast<std::uint32_t>(nx), static_cast<std::uint32_t>(ny)).city == city.index;
                }
            }
            n.inner += inside ? 1U : 0U;
        }
    }
    return n;
}

sim::City* computer_city(Game& g) {
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            return g.world.city(c.island, c.index);
        }
    }
    return nullptr;
}

}  // namespace

TEST_CASE("ai: a computer at war with the human walls in its city once it has the bricks and gold") {
    Game g(true, 50'000);
    g.buildings = make_buildings(kWallRecords);
    sim::Simulation s(g.world, g.buildings, g.figures);
    g.add_ship(kHuman, 5, 80);  // still in the game
    g.world.diplomacy().relation(kComputer, kHuman).peace = sim::Treaty::None;
    g.world.diplomacy().relation(kHuman, kComputer).peace = sim::Treaty::None;
    found_city(g, s);
    sim::City* city = computer_city(g);
    REQUIRE(city != nullptr);
    // No walls while the bricks are short: below 16 t at war.
    const std::uint8_t bricks = g.buildings.materials().bricks;
    city->ensure_ware(bricks);
    city->goods[bricks] = 15 * sim::kStockUnitsPerTon;
    run(s, 60'000);
    CHECK(walls_of(g, *city).walls == 0);
    // With enough of them the border goes up, a wall on every side of the city but none inside.
    g.world.player(kComputer)->money = 50'000;
    REQUIRE(run_until(s, 120'000, [&] {
        city->goods[bricks] = city->capacity;
        return walls_of(g, *city).walls >= 20;
    }));
    CHECK(walls_of(g, *city).inner == 0);
    CHECK(walls_of(g, *city).gates == 0);  // never at war
}

/// Whether the city has a wall at (x, y).
bool wall_at(const Game& g, const sim::City& city, std::uint32_t x, std::uint32_t y) {
    const sim::MapCell& c = g.world.islands()[city.island].map.top(x, y);
    const assets::BuildingType* b = c.empty() ? nullptr : g.buildings.by_sprite(c.sprite);
    return b != nullptr && b->kind == 4 && c.city == city.index;
}

TEST_CASE("ai: a computer at peace walls its coast and the border by free beach, not the inland border") {
    Game g(true, 50'000);
    g.buildings = make_buildings(kWallRecords);
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s);
    sim::City* city = computer_city(g);
    REQUIRE(city != nullptr);
    const std::uint8_t bricks = g.buildings.materials().bricks;
    city->ensure_ware(bricks);
    // Below the citizens nothing, whatever the bricks.
    city->goods[bricks] = 60 * sim::kStockUnitsPerTon;
    run(s, 30'000);
    CHECK(walls_of(g, *city).walls == 0);
    // With them, and more than 40 t of bricks.
    g.world.player(kComputer)->set_unlocked(25, true);
    run_until(s, 120'000, [&] {
        city->goods[bricks] = 60 * sim::kStockUnitsPerTon;
        return false;
    });
    CHECK(walls_of(g, *city).walls > 0);
    CHECK(wall_at(g, *city, 46, 12));  // the beach
    // The border inland (column 36) faces neither another city nor free beach.
    for (std::uint32_t y = 0; y < 50; ++y) {
        CHECK(!wall_at(g, *city, 36, y));
    }
}

TEST_CASE("ai: a computer walls the sea's shore, not a lake's inside its island, the original's both") {
    for (const bool classic : {false, true}) {
        Game g(true, 50'000, true, 37);  // the lake's beach in column 36, in the city
        g.buildings = make_buildings(kWallRecords);
        sim::Rules rules;
        rules.bugs.classic_ai = classic;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        found_city(g, s);
        sim::City* city = computer_city(g);
        REQUIRE(city != nullptr);
        const std::uint8_t bricks = g.buildings.materials().bricks;
        city->ensure_ware(bricks);
        g.world.player(kComputer)->set_unlocked(25, true);
        run_until(s, 240'000, [&] {
            city->goods[bricks] = 60 * sim::kStockUnitsPerTon;
            g.world.player(kComputer)->money = 50'000;
            return false;
        });
        CHECK(wall_at(g, *city, 46, 12));  // the sea's beach
        // The lake's beach inside the city, as the sea's is.
        const sim::IslandMap& map = g.world.islands()[city->island].map;
        std::uint32_t owned = 0;
        for (std::uint32_t y = 18; y <= 32; ++y) {
            owned += map.top(36, y).city == city->index ? 1U : 0U;
        }
        REQUIRE(owned > 0);
        std::uint32_t lake = 0;
        for (std::uint32_t y = 18; y <= 32; ++y) {
            lake += wall_at(g, *city, 36, y) ? 1U : 0U;
        }
        CHECK((lake > 0) == classic);
    }
}

TEST_CASE("ai: a wall the city grew past is demolished") {
    Game g(true, 50'000);
    g.buildings = make_buildings(kWallRecords);
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s);
    sim::City* city = computer_city(g);
    REQUIRE(city != nullptr);
    sim::IslandMap& map = g.world.island(city->island).map;
    // A wall on free ground in the middle of the city, away from the coast.
    std::optional<std::pair<std::int32_t, std::int32_t>> at;
    for (std::int32_t y = 3; y < 47 && !at; ++y) {
        for (std::int32_t x = 3; x < 43 && !at; ++x) {
            bool inside = true;
            for (std::int32_t dy = -3; dy <= 3; ++dy) {
                for (std::int32_t dx = -3; dx <= 3; ++dx) {
                    const sim::MapCell& n =
                        map.top(static_cast<std::uint32_t>(x + dx), static_cast<std::uint32_t>(y + dy));
                    inside = inside && n.city == city->index;
                }
            }
            const sim::MapCell& cell = map.top(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
            const std::int32_t k = g.buildings.by_sprite(cell.sprite)->kind;
            if (inside && (k == 10 || k == 11)) {
                at.emplace(x, y);
            }
        }
    }
    REQUIRE(at.has_value());
    REQUIRE(map.build(*g.buildings.by_id(22501), at->first, at->second, 0, 0, kComputer, city->index).ok());
    REQUIRE(wall_at(g, *city, static_cast<std::uint32_t>(at->first), static_cast<std::uint32_t>(at->second)));
    CHECK(run_until(s, 120'000, [&] {
        return !wall_at(g, *city, static_cast<std::uint32_t>(at->first), static_cast<std::uint32_t>(at->second));
    }));
}

TEST_CASE("ai: a computer builds the producer of a package's good its inhabitants need (MD-4)") {
    // A workshop of a package (2 x 2) makes ware 59 out of nothing; the city's class needs it.
    Game g;
    constexpr std::uint32_t kPepperMill = 20992;
    g.buildings = make_buildings(
        "  @Nummer: 13\n  Id: 20992\n  Kind: 14\n  Gfx: 100\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 59\n"
        "    Prodmenge: 1\n    Interval: 20\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n");
    assets::ResidentClass settlers;
    settlers.max_per_house = 10;
    settlers.needs.push_back({59, 100});
    g.buildings.set_resident_classes({settlers});
    g.buildings.by_id(kKontor)->ware = sim::ware::kAll;  // the Kontor holds every good: workshops go near it
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s, 60'000);  // and its first roads
    sim::City* city = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            city = g.world.city(c.island, c.index);
        }
    }
    REQUIRE(city != nullptr);
    // 100 inhabitants use 7.3 t a minute of it; a workshop makes 3 t: the city wants three.
    city->inhabitants.assign(1, 100);
    const sim::IslandMap& map = g.world.island(city->island).map;
    const auto mills = [&] {
        std::size_t n = 0;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const sim::MapCell& cell = map.top(x, y);
                const assets::BuildingType* b = g.buildings.by_sprite(cell.sprite);
                n += b != nullptr && b->id == kPepperMill && cell.owner == kComputer ? 1U : 0U;
            }
        }
        return n / 4;  // four tiles each
    };
    CHECK(run_until(s, 600'000, [&] {
        city->inhabitants.assign(1, 100);
        return mills() != 0;
    }));
    // The survey counts it in the first slot after the original's.
    run(s, 60'000);
    const auto view = s.computers().view();
    REQUIRE(view[0].plans.size() >= 1);
    REQUIRE(view[0].plans[0].slots.size() == sim::ai::kSlots + 1);
    CHECK(view[0].plans[0].slots[sim::ai::kSlots][0] == mills());
    CHECK(mills() >= 1);
}

TEST_CASE("ai: a computer builds a package's farm that makes its good from nothing (raw = \"\")") {
    // A plantation of a package (2 x 2) without fields makes ware 59; the city's class needs it.
    Game g;
    constexpr std::uint32_t kPepperFarm = 20992;
    g.buildings = make_buildings(
        "  @Nummer: 13\n  Id: 20992\n  Kind: 14\n  Gfx: 100\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Maxlager: 4\n    Ware: 59\n"
        "    Prodmenge: 1\n    Interval: 20\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n");
    assets::ResidentClass settlers;
    settlers.max_per_house = 10;
    settlers.needs.push_back({59, 100});
    g.buildings.set_resident_classes({settlers});
    g.buildings.by_id(kKontor)->ware = sim::ware::kAll;
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s, 60'000);
    sim::City* city = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            city = g.world.city(c.island, c.index);
        }
    }
    REQUIRE(city != nullptr);
    const sim::IslandMap& map = g.world.island(city->island).map;
    const auto farms = [&] {
        std::size_t n = 0;
        for (std::uint32_t y = 0; y < map.height(); ++y) {
            for (std::uint32_t x = 0; x < map.width(); ++x) {
                const sim::MapCell& cell = map.top(x, y);
                const assets::BuildingType* b = g.buildings.by_sprite(cell.sprite);
                n += b != nullptr && b->id == kPepperFarm && cell.owner == kComputer ? 1U : 0U;
            }
        }
        return n / 4;
    };
    CHECK(run_until(s, 600'000, [&] {
        city->inhabitants.assign(1, 100);
        return farms() != 0;
    }));
}

TEST_CASE("ai: the computers' cheats come from the game's balance (balance.toml [computers])") {
    // A poor computer gets money for its founding: the original's 799, or what the balance gives.
    // Only the original's computers (Rules::bugs.classic_ai) get them: improved ones get nothing.
    const auto money_after_founding = [](std::uint32_t founding_money, bool original) {
        Game g(true, 300);
        sim::Balance balance;
        balance.ai_founding_money = founding_money;
        g.world.set_balance(balance);
        sim::Rules rules;
        rules.bugs.classic_ai = original;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        REQUIRE(run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; }));
        return g.world.players()[kComputer].money;
    };
    const std::int32_t original = money_after_founding(799, true);
    CHECK(money_after_founding(5799, true) == original + 5000);
    CHECK(money_after_founding(0, true) == original - 799);
    const std::int32_t improved = money_after_founding(799, false);
    CHECK(money_after_founding(5799, false) == improved);
    CHECK(improved == original - 799);
}

TEST_CASE("ai: a stage's build order from the game's balance (computers.toml) is what the city builds (MD-4)") {
    // The pioneers' order of a package asks for two workshops of a package's good, where the
    // inhabitants alone want one.
    const auto mills_built = [](bool with_order) {
        Game g;
        static constexpr std::uint32_t kPepperMill = 20992;
        g.buildings = make_buildings(
            "  @Nummer: 13\n  Id: 20992\n  Kind: 14\n  Gfx: 100\n  Size: 2, 2\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 59\n"
            "    Prodmenge: 1\n    Interval: 20\n  EndObj\n"
            "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n");
        assets::ResidentClass settlers;
        settlers.max_per_house = 10;
        settlers.needs.push_back({59, 100});
        g.buildings.set_resident_classes({settlers});
        g.buildings.by_id(kKontor)->ware = sim::ware::kAll;
        if (with_order) {
            sim::Balance balance;
            balance.computer_orders.push_back(
                {sim::ComputerList::Pioneers, {sim::ComputerNeed{sim::ComputerNeed::Kind::Package, 59, 2}}});
            REQUIRE(sim::valid(balance));
            g.world.set_balance(balance);
        }
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s, 60'000);
        sim::City* city = nullptr;
        for (const sim::City& c : g.world.cities()) {
            if (c.owner == kComputer) {
                city = g.world.city(c.island, c.index);
            }
        }
        REQUIRE(city != nullptr);
        const sim::IslandMap& map = g.world.island(city->island).map;
        const auto mills = [&] {
            std::size_t n = 0;
            for (std::uint32_t y = 0; y < map.height(); ++y) {
                for (std::uint32_t x = 0; x < map.width(); ++x) {
                    const sim::MapCell& cell = map.top(x, y);
                    const assets::BuildingType* b = g.buildings.by_sprite(cell.sprite);
                    n += b != nullptr && b->id == kPepperMill && cell.owner == kComputer ? 1U : 0U;
                }
            }
            return n / 4;
        };
        // Ten inhabitants use 0.7 t a minute: one workshop is enough for them.
        for (std::uint32_t t = 0; t < 600'000; t += 10'000) {
            city->inhabitants.assign(1, 10);
            run(s, 10'000);
        }
        return mills();
    };
    CHECK(mills_built(false) == 1);
    CHECK(mills_built(true) == 2);
}

TEST_CASE("ai: a package's building replaces a producer of the original's ([producer.<name>], MD-4)") {
    // The founded city's order of a package asks for a bakery; a package names its own workshop as
    // the bakery, without the mill first. Without the change the game has no bakery to build.
    const auto workshops_built = [](bool with_change) {
        Game g;
        static constexpr std::uint32_t kSmokehouse = 20992;
        g.buildings = make_buildings(
            "  @Nummer: 13\n  Id: 20992\n  Kind: 14\n  Gfx: 100\n  Size: 2, 2\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 59\n"
            "    Prodmenge: 1\n    Interval: 20\n  EndObj\n"
            "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n");
        g.buildings.by_id(kKontor)->ware = sim::ware::kAll;
        sim::Balance balance;
        balance.computer_orders.push_back(
            {sim::ComputerList::Founded,
             {sim::ComputerNeed{sim::ComputerNeed::Kind::Market, 0, 1},
              sim::ComputerNeed{sim::ComputerNeed::Kind::Building, sim::ai::kSlotBakery, 1},
              sim::ComputerNeed{sim::ComputerNeed::Kind::Houses, 0, 8}}});
        if (with_change) {
            sim::ComputerProducer bakery;
            bakery.slot = sim::ai::kSlotBakery;
            bakery.building = kSmokehouse;
            bakery.after = sim::kNoProducer;
            balance.computer_producers.push_back(bakery);
        }
        REQUIRE(sim::valid(balance));
        g.world.set_balance(balance);
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s, 120'000);
        std::size_t n = 0;
        for (const sim::City& c : g.world.cities()) {
            if (c.owner != kComputer) {
                continue;
            }
            const sim::IslandMap& map = g.world.island(c.island).map;
            for (std::uint32_t y = 0; y < map.height(); ++y) {
                for (std::uint32_t x = 0; x < map.width(); ++x) {
                    const sim::MapCell& cell = map.top(x, y);
                    const assets::BuildingType* b = g.buildings.by_sprite(cell.sprite);
                    n += b != nullptr && b->id == kSmokehouse && cell.owner == kComputer ? 1U : 0U;
                }
            }
        }
        if (with_change) {
            // The survey counts it as the city's bakery, so the order's one is enough.
            const auto view = s.computers().view();
            REQUIRE(!view.empty());
            REQUIRE(!view[0].plans.empty());
            CHECK(view[0].plans[0].slots[sim::ai::kSlotBakery][0] == n / 4);
        }
        return n / 4;  // four tiles each
    };
    CHECK(workshops_built(false) == 0);
    CHECK(workshops_built(true) == 1);
}

TEST_CASE("ai: a package's slots count its good as food and remove a producer (slots.toml)") {
    // As above, the package's workshop is the bakery. Counted as food, the survey finds it on the
    // city's food counter; with the bakery removed the order asks for nothing.
    struct Built {
        std::size_t workshops = 0;
        std::uint32_t food = 0;
    };
    const auto built = [](std::string_view slots_toml) {
        Game g;
        static constexpr std::uint32_t kSmokehouse = 20992;
        g.buildings = make_buildings(
            "  @Nummer: 13\n  Id: 20992\n  Kind: 14\n  Gfx: 100\n  Size: 2, 2\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 1\n    Maxlager: 4\n    Ware: 59\n"
            "    Prodmenge: 1\n    Interval: 20\n  EndObj\n"
            "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n");
        g.buildings.by_id(kKontor)->ware = sim::ware::kAll;
        sim::Balance balance;
        balance.computer_orders.push_back(
            {sim::ComputerList::Founded,
             {sim::ComputerNeed{sim::ComputerNeed::Kind::Market, 0, 1},
              sim::ComputerNeed{sim::ComputerNeed::Kind::Building, sim::ai::kSlotBakery, 1},
              sim::ComputerNeed{sim::ComputerNeed::Kind::Houses, 0, 8}}});
        sim::ComputerProducer bakery;
        bakery.slot = sim::ai::kSlotBakery;
        bakery.building = kSmokehouse;
        bakery.after = sim::kNoProducer;
        balance.computer_producers.push_back(bakery);
        assets::Goods goods;
        assets::OwnGood fish;
        fish.id = "smoke:fish";
        fish.price = 10;
        fish.icon = assets::ware::kFirstTrade;
        REQUIRE(goods.add(fish).ok());
        REQUIRE(goods.own("smoke:fish")->number == 59);
        std::vector<std::string> warnings;
        sim::apply_slots_file(balance.computer_slots, "smoke", slots_toml, warnings, goods);
        REQUIRE(warnings.empty());
        REQUIRE(sim::valid(balance));
        g.world.set_balance(balance);
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s, 120'000);
        Built out;
        for (const sim::City& c : g.world.cities()) {
            if (c.owner != kComputer) {
                continue;
            }
            const sim::IslandMap& map = g.world.island(c.island).map;
            for (std::uint32_t y = 0; y < map.height(); ++y) {
                for (std::uint32_t x = 0; x < map.width(); ++x) {
                    const sim::MapCell& cell = map.top(x, y);
                    const assets::BuildingType* b = g.buildings.by_sprite(cell.sprite);
                    out.workshops += b != nullptr && b->id == kSmokehouse && cell.owner == kComputer ? 1U : 0U;
                }
            }
        }
        out.workshops /= 4;  // four tiles each
        const auto view = s.computers().view();
        REQUIRE(!view.empty());
        REQUIRE(!view[0].plans.empty());
        out.food = view[0].plans[0].slots[sim::ai::kSlotFood][0];
        return out;
    };
    const Built plain = built("");
    CHECK(plain.workshops == 1);
    CHECK(plain.food == 0);
    const Built food = built("[food]\ncounts = \"smoke:fish\"\n");
    CHECK(food.workshops == 1);
    CHECK(food.food == 1);
    CHECK(built("[bakery]\nremove = true\n").workshops == 0);
}

TEST_CASE("ai: a package's public building of a kind of its own is planned by the computers (slots.toml)") {
    // A court of its own kind: the founded city's order asks for one. The test world has no houses
    // for it to serve, so the computer plans it (its "covered" flag) and finds no site. `ahead`
    // public buildings of a kind of their own come before it, so its flag and service may lie
    // beyond 32 bits, and the computers' state needs version 6 to keep them.
    struct Planned {
        std::uint64_t flags = 0;
        std::uint16_t version = 0;
        bool kept = false;
    };
    const auto planned = [](bool with_slot, std::size_t ahead) {
        Game g;
        static constexpr std::uint32_t kCourt = 20992;
        g.buildings = make_buildings(
            "  @Nummer: 13\n  Id: 20992\n  Kind: 14\n  Gfx: 100\n  Size: 2, 1\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 18\n    Radius: 12\n  EndObj\n"
            "  Objekt: HAUS_BAUKOST\n    Money: 50\n  EndObj\n");
        g.buildings.by_id(kKontor)->ware = sim::ware::kAll;
        g.buildings.by_id(kCourt)->own_service = assets::public_building::own(ahead);
        const std::vector<assets::OwnBuilding> own = {{"rome:court", kCourt}};
        sim::Balance balance;
        std::vector<std::string> warnings;
        if (with_slot && ahead == 0) {
            sim::apply_slots_file(balance.computer_slots, "rome",
                                  "[court]\nkind = \"public\"\nbuilding = \"rome:court\"\n", warnings, {}, own,
                                  &g.buildings);
        } else if (with_slot) {
            balance.computer_slots = sim::original_computer_slots();
            for (std::size_t i = 0; i < ahead; ++i) {
                balance.computer_slots.push_back(
                    sim::own_public_slot(i, "other_" + std::to_string(i), kCourt, assets::public_building::own(i)));
            }
            balance.computer_slots.push_back(
                sim::own_public_slot(ahead, "court", kCourt, assets::public_building::own(ahead)));
        }
        sim::apply_computers_file(balance.computer_orders, "rome", "[founded]\norder = \"market 1, court 1\"\n",
                                  warnings, {}, nullptr, own, &balance.computer_slots);
        REQUIRE(warnings.size() == (with_slot ? 0U : 1U));
        REQUIRE(sim::valid(balance));
        g.world.set_balance(balance);
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s, 60'000);
        const auto view = s.computers().view();
        REQUIRE(!view.empty());
        REQUIRE(!view[0].plans.empty());
        Planned out{view[0].plans[0].flags, s.computers().state_version()};
        const auto bytes = saved(s.computers());
        sim::ComputerPlayers copy(s);
        annokit::core::ByteReader in(bytes);
        out.kept = copy.load(in, out.version) && in.at_end() && same(copy.view(), view);
        return out;
    };
    using sim::ai::city_flag::own_public;
    CHECK((planned(false, 0).flags & own_public(0)) == 0);
    const Planned first = planned(true, 0);
    CHECK((first.flags & own_public(0)) != 0);
    // Within 32 bits the flags are the bytes of version 5; the computer's town seed, drawn with its
    // first town plan, asks for version 8 here all the same.
    CHECK(first.version >= sim::ai::kStateWithTownSeed);
    CHECK(first.kept);
    const Planned far = planned(true, 30);
    CHECK(own_public(30) > 0xFFFF'FFFFU);
    CHECK((far.flags & own_public(30)) != 0);
    CHECK(far.version >= sim::ai::kStateWithWideFlags);
    CHECK(far.kept);
}

TEST_CASE("ai: when a city moves on to the next stage comes from the game's balance ([computer_stages])") {
    // A city founded moves on to the pioneers with two inhabitants, by default; a package may ask
    // for more.
    const auto max_stage = [](std::uint32_t pioneers_at) {
        Game g;
        sim::Balance balance;
        balance.ai_pioneers_inhabitants = pioneers_at;
        g.world.set_balance(balance);
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s);
        sim::City* city = nullptr;
        for (const sim::City& c : g.world.cities()) {
            if (c.owner == kComputer) {
                city = g.world.city(c.island, c.index);
            }
        }
        REQUIRE(city != nullptr);
        for (std::uint32_t t = 0; t < 120'000; t += 10'000) {
            city->inhabitants.assign(1, 10);
            run(s, 10'000);
        }
        const auto view = s.computers().view();
        REQUIRE(!view.empty());
        return view.front().max_stage;
    };
    CHECK(max_stage(2) >= 0x14);  // the pioneers (stage 0x14) or later
    CHECK(max_stage(11) == 0);    // ten inhabitants are not enough: still a city just founded
}

TEST_CASE("ai: improved computers settle on a large island another computer holds, the original's do not") {
    // The big island counts as one of the largest (a template of size class 4); a second computer
    // has a young city there (fewer than 50 inhabitants) and two more on islets, so that the island
    // is open to others. An improved computer counts only the cities it knows of (P1.5b): with the
    // islets undiscovered the other looks like a small player, whose island it leaves alone.
    constexpr sim::PlayerId kOther = 4;
    const auto founded = [&](bool classic, sim::PlayerRole other = sim::PlayerRole::Computer, bool known = true) {
        Game g;
        g.add_islet(2, 80);
        g.world.island(1).header.template_number = 1;
        g.world.island(1).header.template_size = 4;
        std::vector<sim::Player> players = g.world.players();
        players.push_back(sim::Player{});
        players.back().role = other;
        g.world.set_players(players);
        const auto add_city = [&](std::uint16_t island, sim::PlayerId owner, std::uint32_t people) {
            sim::City c;
            c.island = island;
            c.owner = owner;
            c.inhabitants = {people};
            g.world.add_city(c);
        };
        add_city(1, kOther, 20);
        add_city(0, kOther, 20);
        add_city(2, kOther, 20);
        if (known) {
            g.world.island(0).discover(kComputer);
            g.world.island(2).discover(kComputer);
        }
        sim::Rules rules;
        rules.bugs.classic_ai = classic;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        return run_until(s, 600'000, [&] { return g.cities_of(kComputer) != 0; });
    };
    CHECK(founded(false));
    CHECK(!founded(false, sim::PlayerRole::Computer, false));
    CHECK(!founded(true));
    // A human's city there the original's computers settle beside.
    CHECK(founded(true, sim::PlayerRole::Human));
}

TEST_CASE("ai: town styles differ by the number drawn for the game, and the same number gives the same style") {
    using sim::ai::town_style;
    std::set<std::tuple<std::int32_t, std::int32_t, std::int32_t, std::int32_t>> seen;
    for (std::uint32_t seed = 1; seed <= 32; ++seed) {
        const sim::ai::TownStyle a = town_style(seed * 2654435761U, 7, 0);
        const sim::ai::TownStyle b = town_style(seed * 2654435761U, 7, 0);
        CHECK(a.room == b.room);
        CHECK(a.shift == b.shift);
        CHECK(a.block == b.block);
        CHECK(a.field_margin == b.field_margin);
        CHECK(a.room >= 6);
        CHECK(a.room <= 9);
        CHECK(a.block >= 7);
        CHECK(a.block <= 9);
        CHECK(a.shift >= 0);
        CHECK(a.shift <= 4);
        seen.emplace(a.room, a.shift, a.block, a.field_margin);
    }
    CHECK(seen.size() >= 16);
    // Two cities of one computer plan differently too.
    int differ = 0;
    for (std::uint8_t city = 1; city <= 8; ++city) {
        const sim::ai::TownStyle a = town_style(12345, 3, 0);
        const sim::ai::TownStyle b = town_style(12345, 3, city);
        differ += a.room != b.room || a.block != b.block || a.shift != b.shift ? 1 : 0;
    }
    CHECK(differ >= 4);
}

TEST_CASE("ai: a site's distance from the town zone") {
    sim::ai::TownZone z;
    z.valid = true;
    z.cx = 20;
    z.cy = 20;
    z.radius = 10;
    CHECK(sim::ai::zone_excess(z, 19, 19, 20, 20) < 0);   // at the middle
    CHECK(sim::ai::zone_excess(z, 30, 20, 31, 21) == 0);  // at the edge
    CHECK(sim::ai::zone_excess(z, 40, 20, 41, 21) == 10);
    CHECK(sim::ai::zone_excess(z, 20, 0, 21, 1) == 9);
}

TEST_CASE("ai: a computer's growing home city builds a castle, the original's only behind a human") {
    // The settlers' order asks for a castle alone; nobody is armed and the human has no city, so
    // the original's computer, which builds one only while it trails the leading human, never does.
    const auto castles_built = [](bool rivals) {
        Game g(true, 30000);
        g.buildings = make_buildings(
            "  @Nummer: 13\n  Id: 20311\n  Kind: 14\n  Gfx: 100\n  Size: 3, 3\n"
            "  Objekt: HAUS_PRODTYP\n    Kind: 15\n    Maxware: 3\n  EndObj\n"
            "  Objekt: HAUS_BAUKOST\n    Money: 500\n  EndObj\n");
        g.buildings.by_id(kKontor)->ware = sim::ware::kAll;  // the castle goes near a Kontor or market
        sim::Balance balance;
        balance.computer_orders.push_back(
            {sim::ComputerList::Settlers, {sim::ComputerNeed{sim::ComputerNeed::Kind::Castle}}});
        REQUIRE(sim::valid(balance));
        g.world.set_balance(balance);
        // Room for the castle by the Kontor: the wood cleared but for its first columns.
        sim::IslandMap& ground = g.world.island(1).map;
        for (std::int32_t y = 0; y < 50; ++y) {
            for (std::int32_t x = 36; x <= 43; ++x) {
                REQUIRE(ground.place(*g.buildings.by_id(kGround), x, y, 0, 0).ok());
            }
        }
        sim::Rules rules;
        rules.bugs.classic_ai = !rivals;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        found_city(g, s, 60'000);
        sim::City* city = nullptr;
        for (const sim::City& c : g.world.cities()) {
            if (c.owner == kComputer) {
                city = g.world.city(c.island, c.index);
            }
        }
        REQUIRE(city != nullptr);
        const sim::IslandMap& map = g.world.island(city->island).map;
        const auto castles = [&] {
            std::size_t n = 0;
            for (std::uint32_t y = 0; y < map.height(); ++y) {
                for (std::uint32_t x = 0; x < map.width(); ++x) {
                    const sim::MapCell& cell = map.top(x, y);
                    const assets::BuildingType* b = g.buildings.by_sprite(cell.sprite);
                    n += b != nullptr && b->id == 20311 && cell.owner == kComputer ? 1U : 0U;
                }
            }
            return n / 9;
        };
        for (std::uint32_t t = 0; t < 900'000; t += 10'000) {
            // The pioneers' roads first, then past [computer_military] castle_inhabitants.
            city->inhabitants.assign(1, t < 240'000 ? 10 : 400);
            run(s, 10'000);
        }
        return castles();
    };
    CHECK(castles_built(true) == 1);  // a second castle only from 700 inhabitants
    CHECK(castles_built(false) == 0);
}

TEST_CASE("ai: an improved computer raises the taxes its content inhabitants bear, the original's never") {
    // A class that asks for half of a good the city has plenty of stays content far above the
    // normal rate: the improved computer raises it as far as the class stays fully content, with
    // a step to spare; the original's computers keep the normal rate.
    const auto rate = [](bool original) {
        Game g;
        assets::ResidentClass settlers;
        settlers.max_per_house = 10;
        settlers.share = 64;  // 50 %
        settlers.needs.push_back({59, 100});
        g.buildings.set_resident_classes({settlers});
        sim::Rules rules;
        rules.bugs.classic_ai = original;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        found_city(g, s);
        sim::City* city = nullptr;
        for (const sim::City& c : g.world.cities()) {
            if (c.owner == kComputer) {
                city = g.world.city(c.island, c.index);
            }
        }
        REQUIRE(city != nullptr);
        std::uint8_t highest = 0;
        run_until(s, 120'000, [&] {
            city->inhabitants.assign(1, 100);
            city->ensure_ware(59);
            city->goods[59] = 50 * sim::kStockUnitsPerTon;
            if (!city->tax_rate.empty()) {
                highest = std::max(highest, city->tax_rate[0]);
            }
            return highest > 128;
        });
        // Fully content at the raised rate, still supplied.
        run_until(s, 10'000, [&] {
            city->inhabitants.assign(1, 100);
            city->goods[59] = 50 * sim::kStockUnitsPerTon;
            return false;
        });
        CHECK(city->satisfaction[0] == sim::kFullSupply);
        return highest;
    };
    CHECK(rate(true) == 128);
    const std::uint8_t improved = rate(false);
    CHECK(improved > 128);
    CHECK(improved <= sim::CityOrder::kMaxTax);
}

TEST_CASE("ai: an improved computer builds a mine only where the carts of its Kontor or markets reach it") {
    // Only a Kontor's or market's carts fetch a mine's goods: a mine out of their reach fills, idles
    // and is torn down as exhausted. Beside the city's roads a mine is reached, far inland it is not,
    // and a planned road to the roads brings a site in the Kontor's circle within reach.
    Game g;
    g.buildings = make_buildings("  @Nummer: 13\n  Id: 20601\n  Kind: 11\n  Gfx: 13\n  Size: 1, 2\n",
                                 "    Ware: 1\n    Maxlager: 30\n");
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s, 300'000);
    const sim::City* city = computer_city(g);
    REQUIRE(city != nullptr);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    p.island = city->island;
    p.city = city->index;
    const assets::BuildingType& mine = *g.buildings.by_id(20601);
    const sim::IslandMap& map = g.world.islands()[city->island].map;
    const auto road = [&](std::uint32_t x, std::uint32_t y) {
        const sim::MapCell& c = map.top(x, y);
        const assets::BuildingType* b = c.empty() ? nullptr : g.buildings.by_sprite(c.sprite);
        return b != nullptr && b->id >= 20401 && b->id <= 20414;
    };
    // The Kontor's road north and south (column 43), as the city laid it.
    REQUIRE(road(43, 12));
    REQUIRE(road(43, 20));
    REQUIRE(!road(42, 20));
    REQUIRE(!road(41, 20));
    CHECK(engine.mine_reached(p, mine, 42, 12, {}));
    CHECK(!engine.mine_reached(p, mine, 25, 30, {}));
    CHECK(!engine.mine_reached(p, mine, 40, 20, {}));
    const std::vector<sim::BuildItem> planned{sim::BuildItem{20411, 42, 20, 0, 0}, sim::BuildItem{20411, 41, 20, 0, 0}};
    CHECK(engine.mine_reached(p, mine, 40, 20, planned));
    // Feedback 15: of the placements around the slope tile, the one beside the road is reached,
    // the one a tile west of it not; task_mine builds only the first.
    CHECK(engine.mine_reached(p, mine, 42, 12, 0, {}));
    CHECK(!engine.mine_reached(p, mine, 41, 12, 0, {}));
}

TEST_CASE("ai: an improved computer does not wait for carts at a quarry") {
    // The stonemason fetches the stone from the quarry's tiles; no cart comes for it, so a quarry
    // site far from the Kontor's carts is as good as one beside them.
    Game g;
    g.buildings = make_buildings(
        "  @Nummer: 13\n  Id: 22407\n  Kind: 11\n  Gfx: 13\n  Size: 1, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 11\n    Ware: 54\n  EndObj\n");
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s, 300'000);
    const sim::City* city = computer_city(g);
    REQUIRE(city != nullptr);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    p.island = city->island;
    p.city = city->index;
    const assets::BuildingType& quarry = *g.buildings.by_id(22407);
    REQUIRE(quarry.prod_kind == assets::prod::kQuarry);
    CHECK(engine.mine_reached(p, quarry, 25, 30, {}));
}

TEST_CASE("ai: an improved computer places a quarry where a stonemason's circle holds it") {
    // The stonemason fetches the stone from the quarry's tiles inside its circle; a quarry beyond it
    // stays full and idle. Without a mason every site counts.
    Game g;
    g.buildings = make_buildings(
        "  @Nummer: 13\n  Id: 22407\n  Kind: 11\n  Gfx: 13\n  Size: 1, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 11\n    Ware: 54\n  EndObj\n"
        "  @Nummer: 14\n  Id: 20901\n  Kind: 14\n  Gfx: 30\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Ware: 9\n    Rohstoff: 54\n    Maxlager: 4\n    Radius: 4\n"
        "  EndObj\n");
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s, 5'000);  // the Kontor at 44,16, a road west along row 17
    const sim::City* city = computer_city(g);
    REQUIRE(city != nullptr);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    p.island = city->island;
    p.city = city->index;
    const assets::BuildingType& quarry = *g.buildings.by_id(22407);
    CHECK(engine.mason_circles(p).empty());
    CHECK(engine.masons_reach({}, quarry, 25, 30));
    sim::BuildOrder order;
    order.player = kComputer;
    order.island = city->island;
    order.items = {{20901, 37, 18, 0, 0}};
    REQUIRE(sim::execute_build(g.world, g.buildings, order, {}, nullptr).placed == 1);
    const std::vector<sim::Catchment> circles = engine.mason_circles(p);
    REQUIRE(circles.size() == 1);
    // The circle of radius 4 about the mason's 2 x 2 tiles (37..38, 18..19): rows 14..23.
    CHECK(engine.masons_reach(circles, quarry, 38, 23, 0));   // rows 23 and 24
    CHECK(!engine.masons_reach(circles, quarry, 38, 24, 0));  // rows 24 and 25
    CHECK(engine.masons_reach(circles, quarry, 33, 18, 0));   // 4 left of column 37
    CHECK(!engine.masons_reach(circles, quarry, 32, 18, 0));
    CHECK(!engine.masons_reach(circles, quarry, 44, 18, 1));  // 1 x 2 turned: 44..45, beyond 42
    CHECK(engine.masons_reach(circles, quarry, 37, 24));      // slope in row 24: the placement a row up
    CHECK(!engine.masons_reach(circles, quarry, 25, 30));
}

TEST_CASE("ai: an improved computer's guard leaves soldiers on their way aboard a ship") {
    // Attack ships board the soldiers at the home Kontor; the guard sent them back to its post every
    // few seconds, so the ships sailed empty. The original's guard takes them as before.
    for (const bool original : {true, false}) {
        Game g;
        sim::Rules rules;
        rules.bugs.classic_ai = original;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        const sim::Target post = sim::Target::spot(41, 41);
        sim::Soldier soldier;
        soldier.owner = kComputer;
        CHECK(engine.guard_takes(soldier, post));
        soldier.target = post;
        CHECK(!engine.guard_takes(soldier, post));
        soldier.target = sim::Target::building(1, 4, 4);
        CHECK(!engine.guard_takes(soldier, post));
        soldier.target = sim::Target::other_ship(7);
        CHECK(engine.guard_takes(soldier, post) == original);
    }
}

TEST_CASE("ai: an improved computer lands soldiers at its city where they can walk into the city") {
    // At a Kontor walled in by the city's own wall the nearest cell to land on can be a pocket cut
    // off from the city: the computer sails to water off the city's walkable land instead. Without
    // the wall it lands where it is.
    for (const bool walled : {false, true}) {
        Game g;
        g.buildings =
            make_buildings(kWallRecords + "  @Nummer: 18\n  Id: 20701\n  Kind: 14\n  Gfx: 35\n  Size: 2, 2\n");
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s);
        const sim::City* city = computer_city(g);
        REQUIRE(city != nullptr);
        REQUIRE(city->warehouse_x.has_value());
        REQUIRE(city->warehouse_y.has_value());
        sim::IslandMap& map = g.world.island(city->island).map;
        // The Kontor (2 x 3) on the beach's land side, the city's road along its west side.
        const std::int32_t kx = *city->warehouse_x;
        const std::int32_t ky = *city->warehouse_y;
        REQUIRE(map.place(*g.buildings.by_id(20701), kx - 6, ky + 1, 0, 0, kComputer, city->index).ok());
        const std::int32_t below = ky + 3;  // the row below the Kontor
        if (walled) {
            // A pocket of the two tiles below the Kontor, between the wall and the beach.
            for (const auto& [x, y] :
                 {std::pair{kx - 1, below}, std::pair{kx, below + 1}, std::pair{kx + 1, below + 1}}) {
                REQUIRE(map.place(*g.buildings.by_id(22501), x, y, 0, 0, kComputer, city->index).ok());
            }
        }
        sim::Ship& ship = g.world.ships().front();
        const sim::WorldIsland& isl = g.world.islands()[city->island];
        ship.x = static_cast<std::uint16_t>(static_cast<std::int32_t>(isl.x) + kx + 3);
        ship.y = static_cast<std::uint16_t>(static_cast<std::int32_t>(isl.y) + below);
        const auto cell = s.military().landing_cell(ship);
        REQUIRE(cell.has_value());
        REQUIRE(cell->first / 2 - static_cast<std::int32_t>(isl.x) == kx + 1);
        REQUIRE(cell->second / 2 - static_cast<std::int32_t>(isl.y) == below);
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::ai::AiPlayer p;
        p.player = kComputer;
        const auto shore = engine.landing_shore(p, ship, *city);
        CHECK(shore.has_value() == walled);
        if (shore) {
            // From there the soldiers land outside the pocket.
            sim::Ship there = ship;
            there.x = static_cast<std::uint16_t>(shore->x);
            there.y = static_cast<std::uint16_t>(shore->y);
            const auto landed = s.military().landing_cell(there);
            REQUIRE(landed.has_value());
            const std::int32_t ly = landed->second / 2 - static_cast<std::int32_t>(isl.y);
            CHECK((ly < ky || ly > below + 1));
        }
    }
}

TEST_CASE("ai: a landing finds no beach on a coast walled all along") {
    // The beach is where soldiers land against a city; a wall on a beach tile takes it, so a city
    // whose whole coast is walled leaves the attacker no beach, and plan_landing ends the mission.
    for (const bool walled : {false, true}) {
        Game g;
        g.buildings = make_buildings(kWallRecords);
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s);
        const sim::City* city = computer_city(g);
        REQUIRE(city != nullptr);
        sim::IslandMap& map = g.world.island(city->island).map;
        std::uint32_t walls = 0;
        if (walled) {
            for (std::int32_t y = 0; y < 50; ++y) {
                walls += map.place(*g.buildings.by_id(22501), 46, y, 0, 0, kComputer, city->index).ok() ? 1U : 0U;
            }
            REQUIRE(walls > 40);
        }
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::ai::AiPlayer p;
        p.player = kHuman;  // the attacker
        CHECK(engine.landing_beach(p, *city).has_value() == !walled);
    }
}

TEST_CASE("ai: an improved computer moves its ship to where the soldiers at the Kontor can board it") {
    // Soldiers go on board only within the engine's boarding range of the ship. The ship lies off
    // the quay strip below the Kontor; walls close that strip off from the land behind the Kontor,
    // where the soldiers stand, so the Kontor itself separates them. The computer moves the ship to
    // water off the soldiers' walkable shore. Without the walls the soldiers reach the ship where it is.
    for (const bool walled : {false, true}) {
        Game g;
        g.buildings = make_buildings(kWallRecords);
        sim::Simulation s(g.world, g.buildings, g.figures);
        found_city(g, s);
        const sim::City* city = computer_city(g);
        REQUIRE(city != nullptr);
        REQUIRE(city->warehouse_x.has_value());
        REQUIRE(city->warehouse_y.has_value());
        sim::IslandMap& map = g.world.island(city->island).map;
        const std::int32_t kx = *city->warehouse_x;
        const std::int32_t ky = *city->warehouse_y;
        const std::int32_t quay = ky + 3;  // the first row below the Kontor (2 x 3)
        if (walled) {
            // The strip below the Kontor, closed on the west and two rows further down.
            for (const auto& [x, y] : {std::pair{kx - 1, quay}, std::pair{kx - 1, quay + 2}, std::pair{kx, quay + 2},
                                       std::pair{kx + 1, quay + 2}}) {
                REQUIRE(map.place(*g.buildings.by_id(22501), x, y, 0, 0, kComputer, city->index).ok());
            }
        }
        const sim::WorldIsland& isl = g.world.islands()[city->island];
        sim::Soldier soldier;  // above the Kontor
        soldier.place_cell(2 * (static_cast<std::int32_t>(isl.x) + kx),
                           2 * (static_cast<std::int32_t>(isl.y) + ky - 3));
        soldier.owner = kComputer;
        soldier.island = city->island;
        g.world.add_soldier(soldier);
        const std::vector<std::uint32_t> soldiers{g.world.soldiers().back().id};
        sim::Ship& ship = g.world.ships().front();
        ship.x = static_cast<std::uint16_t>(static_cast<std::int32_t>(isl.x) + kx + 3);
        ship.y = static_cast<std::uint16_t>(static_cast<std::int32_t>(isl.y) + quay + 4);
        // A tile of the strip, touching the Kontor, is within boarding range of the ship: within
        // the 7 cells a soldier's way to it ends at, not only the 9 it boards from (trace trX: a ship
        // 8 cells from the soldiers' shore waited 80 tries, nobody walked to it).
        REQUIRE(sim::Military::boards_from(ship, 2 * (static_cast<std::int32_t>(isl.x) + kx + 1),
                                           2 * (static_cast<std::int32_t>(isl.y) + quay + 1)));
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::ai::AiPlayer p;
        p.player = kComputer;
        const auto water = engine.boarding_water(p, ship, *city, soldiers);
        CHECK(water.has_value() == walled);
        if (water) {
            // From there a tile above the strip, where the soldier can walk, is within boarding range.
            sim::Ship there = ship;
            there.x = static_cast<std::uint16_t>(water->x);
            there.y = static_cast<std::uint16_t>(water->y);
            CHECK(static_cast<std::int32_t>(there.y) - static_cast<std::int32_t>(isl.y) < quay);
            bool boards = false;
            for (std::int32_t y = ky - 4; y < quay; ++y) {
                for (std::int32_t x = kx - 1; x < kx + 2; ++x) {
                    boards = boards || sim::Military::boards_from(there, 2 * (static_cast<std::int32_t>(isl.x) + x),
                                                                  2 * (static_cast<std::int32_t>(isl.y) + y));
                }
            }
            CHECK(boards);
        }
    }
}

TEST_CASE("ai: a soldier's way on board ends within 7 cells of the ship, so the computer counts no farther") {
    // Trace trX: a ship lay 8 cells from the nearest cell its soldiers could walk to. They board
    // from up to 9 cells (arrived()), but their way to the ship ends within kFriendlyGoal, 7, and
    // no way ended there: each order failed, 80 tries long. boards_from, which boarding_water
    // asks, holds to the way's end.
    sim::Ship ship = sim::make_ship("Ship", 50, 50, 21, kComputer, 2);
    CHECK(sim::Military::boards_from(ship, 2 * 50 + 1, 2 * 50 + 1 + 7));
    CHECK(!sim::Military::boards_from(ship, 2 * 50 + 1, 2 * 50 + 1 + 8));
    CHECK(!sim::Military::boards_from(ship, 2 * 50 - 8, 2 * 50));
}

TEST_CASE("ai: an improved computer's troop ship sails on to land its soldiers instead of chasing ships") {
    // Raids, attacks and landings fight on the way by an attack move. With soldiers aboard the ship
    // sails on to land them by a plain move, as a player steers a troop ship.
    std::uint8_t soldier = 0;
    for (std::uint32_t w = 1; w < 256 && soldier == 0; ++w) {
        soldier = assets::is_soldier_ware(w) ? static_cast<std::uint8_t>(w) : std::uint8_t{0};
    }
    REQUIRE(soldier != 0);
    for (const bool classic : {false, true}) {
        Game g;
        sim::Rules rules;
        rules.bugs.classic_ai = classic;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::Ship& ship = g.world.ships().front();
        for (sim::CargoSlot& c : ship.cargo) {
            c = sim::CargoSlot{};
        }
        CHECK(engine.attack_move_for(ship, 7) == !classic);
        CHECK(engine.attack_move_for(ship, 5) == !classic);
        CHECK(!engine.attack_move_for(ship, 2));
        REQUIRE(!ship.cargo.empty());
        ship.cargo.front().ware = soldier;
        ship.cargo.front().amount = 1;
        CHECK(!engine.attack_move_for(ship, 7));
        CHECK(!engine.attack_move_for(ship, 3));
    }
}

TEST_CASE("ai: a computer finds no way for its ship to water cut off from the sea") {
    // A lake on the island is water a ship cannot sail to from the sea: the computers ask the
    // fleet before they send a ship to water near the coast.
    Game g(true, 10000, true);
    sim::Simulation s(g.world, g.buildings, g.figures);
    const sim::Ship& ship = g.world.ships().front();
    const sim::WorldIsland& isl = g.world.islands().back();
    const auto lake = sim::Target::position(isl.x + 28, isl.y + 25);
    REQUIRE(s.fleet().sea().sailable(static_cast<std::int32_t>(lake.x), static_cast<std::int32_t>(lake.y)));
    CHECK(!s.fleet().finds_way(ship, lake));
    const auto sea = sim::Target::position(isl.x + 48, isl.y + 25);
    REQUIRE(s.fleet().sea().sailable(static_cast<std::int32_t>(sea.x), static_cast<std::int32_t>(sea.y)));
    CHECK(s.fleet().finds_way(ship, sea));
}

TEST_CASE("ai: an improved computer keeps units in a castle whose way out is cut off from the Kontor") {
    // Released units step out next to the castle and walk to the Kontor. A castle behind the wood
    // with no road through it lets them out where they could not leave: they stay inside.
    Game g;
    g.buildings = make_buildings("  @Nummer: 13\n  Id: 20701\n  Kind: 14\n  Gfx: 35\n  Size: 2, 2\n");
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s);
    const sim::City* city = computer_city(g);
    REQUIRE(city != nullptr);
    REQUIRE(city->warehouse_x.has_value());
    REQUIRE(city->warehouse_y.has_value());
    sim::IslandMap& map = g.world.island(city->island).map;
    const auto kx = static_cast<std::uint16_t>(*city->warehouse_x);
    const auto ky = static_cast<std::uint16_t>(*city->warehouse_y);
    // One by the Kontor, one west of the wood (columns 32..43), both in the city's land.
    const std::array<std::pair<std::uint16_t, std::uint16_t>, 2> castles{
        {{kx, static_cast<std::uint16_t>(ky + 5)}, {20, ky}}};
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    for (std::size_t i = 0; i < castles.size(); ++i) {
        const auto [x, y] = castles[i];
        for (std::uint32_t ty = y; ty < y + 2U; ++ty) {
            for (std::uint32_t tx = x; tx < x + 2U; ++tx) {
                map.set_city(tx, ty, city->index, kComputer);
            }
        }
        REQUIRE(map.place(*g.buildings.by_id(20701), x, y, 0, 0, kComputer, city->index).ok());
        sim::ai::CastleEntry e;
        e.x = x;
        e.y = y;
        CHECK(engine.way_out(p, city->island, e) == (i == 0));
    }
}

TEST_CASE("ai: an improved computer tells its warships from its trade ships by type, not by cannons") {
    // An armed trade ship and an unarmed warship lie idle at sea. The improved computer sends the
    // warship on patrol and keeps the trade ship for the supply; the original's sends the ship with
    // the most cannons, here the trade ship.
    for (const bool original : {true, false}) {
        Game g;
        const assets::CodPredefinedSymbol types[] = {{"FIGUR", 1}};
        auto figures = assets::FigureTable::from_cod(assets::cod::parse(
            "Objekt: FIGUR\n"
            "  Nummer: 21\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 4\n  Maxkanon: 6\n  Speed: 500\n"
            "  Drehtime: 0.13\n  Maxenergy: 50\n  Hitpoint: 2.0\n  Preis: 1000\n  Rohstoff: 23\n  Rohmenge: 25\n"
            "  Nummer: 25\n  Gfx: 0\n  Blocknr: 2\n  Rotate: 1\n  Maxware: 3\n  Maxkanon: 8\n  Speed: 550\n"
            "  Drehtime: 0.13\n  Maxenergy: 65\n  Hitpoint: 2.0\n  Shotradius: 7\n  Shottime: 1.0\n  Worktime: 5.0\n"
            "  Stirbtime: 4.0\n"
            "EndObj\n",
            types));
        REQUIRE(figures.ok());
        g.figures = std::move(figures).value();
        sim::Rules rules;
        rules.bugs.classic_ai = original;
        rules.reveal_resources = true;
        sim::Simulation s(g.world, g.buildings, g.figures, rules);
        found_city(g, s);
        g.world.erase_ships([](const sim::Ship& ship) { return ship.owner == kComputer; });
        sim::Ship& trader = g.add_ship(kComputer, 70, 80);
        trader.cannons = 6;
        const std::uint32_t merchant = trader.id;
        sim::Ship& war = g.add_ship(kComputer, 76, 80);
        war.figure = assets::figure::ship_figure(assets::figure::kFirstWarship);
        war.cannons = 0;
        const std::uint32_t warship = war.id;
        std::uint32_t first = 0;
        REQUIRE(run_until(s, 120'000, [&] {
            for (const std::uint32_t id : {merchant, warship}) {
                const sim::Ship* ship = g.world.ship(id);
                if (first == 0 && ship != nullptr && ship->route.active) {
                    first = id;
                }
            }
            return first != 0;
        }));
        CHECK(first == (original ? merchant : warship));
        if (!original) {
            // The trade ship is not made a guard of the home island while it waits for a supply run.
            run(s, 120'000);
            const sim::Ship* ship = g.world.ship(merchant);
            REQUIRE(ship != nullptr);
            CHECK(!ship->route.active);
        }
    }
}

TEST_CASE("ai: an improved computer keeps pressing a rival until it likes it, the original's presses only at mood 0") {
    // A sale at the rival's Kontor lifts the mood a little above 0; the original's pressure ended
    // with it and came back as the mood fell, so a war plan rarely saw it.
    namespace stance = sim::ai::stance;
    sim::ai::StanceInput in;
    in.near = true;
    in.own = 50;
    in.theirs = 20;
    in.press = 150;
    for (const bool improved : {false, true}) {
        in.latch = improved;
        // Enter: only at mood 0 or below, from any stance.
        in.previous = stance::kNeutral;
        in.mood = 0;
        CHECK(sim::ai::stance_step(in).stance == stance::kPressure);
        CHECK(!sim::ai::stance_step(in).held);
        in.mood = 1;
        CHECK(sim::ai::stance_step(in).stance == stance::kNeutral);
        // Hold: from pressure, while the mood stays up to a partner's.
        in.previous = stance::kPressure;
        for (const std::int32_t mood : {1, 10, 20}) {
            in.mood = mood;
            const sim::ai::StanceStep step = sim::ai::stance_step(in);
            CHECK(step.stance == (improved ? stance::kPressure : stance::kNeutral));
            CHECK(step.held == improved);
        }
        // Leave: a partner's mood, an alliance, a rival no longer near or no longer weaker.
        in.mood = 21;
        CHECK(sim::ai::stance_step(in).stance == stance::kPartner);
        in.mood = 5;
        in.allied = true;
        CHECK(sim::ai::stance_step(in).stance == stance::kPartner);
        in.allied = false;
        in.near = false;
        CHECK(sim::ai::stance_step(in).stance == stance::kNeutral);
        in.near = true;
        in.theirs = 40;
        CHECK(sim::ai::stance_step(in).stance == stance::kNeutral);
        in.theirs = 20;
    }
}

TEST_CASE("ai: an improved computer goes to war below its gold reserve only on an income that carries the war") {
    // war_supplied's gold term: the reserve in hand, or an income that carries the army's upkeep at
    // war, the test war_plan makes before it declares, with gold for the first landing. The original's computers pass
    // it always.
    sim::ai::WarGold g;
    g.money = 3500;
    g.reserve = 5000;
    g.floor = 3500;  // the broke line and the first landing's ship
    g.pays_upkeep = true;
    g.civil_income = 40;
    g.military_cost = 20;
    g.arming_budget = 30;
    g.net_income = 20;
    for (const bool improved : {false, true}) {
        g.improved = improved;
        // Carried: income above 0 and the upkeep within the arming budget.
        CHECK(sim::ai::war_carried(g));
        CHECK(sim::ai::war_gold(g));
        // Not carried: the upkeep beyond the budget, or the cities losing gold.
        g.military_cost = 31;
        CHECK(!sim::ai::war_carried(g));
        CHECK(sim::ai::war_gold(g) == !improved);
        g.military_cost = 20;
        g.civil_income = -1;
        CHECK(!sim::ai::war_carried(g));
        CHECK(sim::ai::war_gold(g) == !improved);
        // The reserve in hand passes whatever the income.
        g.money = 5000;
        CHECK(sim::ai::war_gold(g));
        // Below the floor an income does not do: the first landing would end the war.
        g.civil_income = 40;
        g.money = 3499;
        CHECK(sim::ai::war_carried(g));
        CHECK(sim::ai::war_gold(g) == !improved);
        g.money = 3500;
        // Nor an income that falls: the broke test would end the war under the line.
        g.net_income = -1;
        CHECK(sim::ai::war_carried(g));
        CHECK(sim::ai::war_gold(g) == !improved);
        g.net_income = 20;
        // Without upkeep (Rules) the income test does not apply.
        g.pays_upkeep = false;
        g.military_cost = 99;
        CHECK(sim::ai::war_carried(g));
        g.pays_upkeep = true;
        g.military_cost = 20;
    }
}

TEST_CASE("ai view: own cities, and the others' only on islands the viewer discovered") {
    Game g;
    sim::City own;
    own.island = 1;
    own.owner = kComputer;
    own.inhabitants = {40};
    g.world.add_city(own);
    sim::City other;
    other.island = 0;
    other.owner = kHuman;
    other.inhabitants = {90};
    g.world.add_city(other);
    sim::House h;
    h.island = 0;
    h.city = 0;
    g.world.add_house(h);
    h.x = 2;
    g.world.add_house(h);
    sim::Simulation s(g.world, g.buildings, g.figures);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    const sim::ai::View v = engine.view(p);
    CHECK(v.own_city_count() == 1);
    CHECK(v.own_population() == 40);
    REQUIRE(v.own_cities().size() == 1);
    CHECK(v.own_cities().front()->island == 1);
    // The human's city stands on an island the computer has not discovered.
    CHECK(v.seen_city_count(kHuman) == 0);
    CHECK(v.seen_houses(kHuman) == 0);
    g.world.island(0).discover(kComputer);
    CHECK(v.seen_city_count(kHuman) == 1);
    CHECK(v.seen_houses(kHuman) == 2);
    // Its own cities it always sees.
    CHECK(v.seen_city_count(kComputer) == 1);
    // What a foreign city makes is not the viewer's statistics.
    CHECK(v.own_supply(*g.world.city(0, 0), assets::ware::kCloth) == 0);
}

TEST_CASE("ai view: an island's crops once explored, its outline always") {
    Game g;
    sim::Simulation s(g.world, g.buildings, g.figures);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    const sim::ai::View v = engine.view(p);
    CHECK(!v.knows_island(1));
    CHECK(!v.fertility(1, assets::ware::kSpiceField).has_value());
    const std::uint32_t size = v.island_size(1);
    g.world.island(1).discover(kComputer);
    CHECK(v.island_size(1) == size);
    CHECK(v.knows_island(1));
    CHECK(!v.knows_crops(1));
    CHECK(!v.fertility(1, assets::ware::kSpiceField).has_value());
    g.world.island(1).explore(kComputer);
    REQUIRE(v.fertility(1, assets::ware::kSpiceField).has_value());
    CHECK(*v.fertility(1, assets::ware::kSpiceField) == 128);  // every crop grows there fully
    CHECK(!v.fertility(99, assets::ware::kSpiceField).has_value());
}

TEST_CASE("ai view: what an island grows and its free veins only once its crops are known") {
    Game g;
    assets::IslandHeader::Deposit vein;
    vein.ware = assets::ware::kGold;
    vein.x = 20;
    vein.y = 20;
    vein.amount = 100;
    g.world.island(1).header.deposits.push_back(vein);
    sim::Simulation s(g.world, g.buildings, g.figures);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    const sim::ai::View v = engine.view(p);
    g.world.island(1).discover(kComputer);  // seen, not explored: the crops are still unknown
    CHECK(!v.grows(1, assets::ware::kSpiceField));
    CHECK(!v.grows(1, assets::ware::kGold));
    CHECK(v.full_crops(1) == 0);
    CHECK(!v.free_deposit(1, assets::ware::kGold));
    g.world.island(1).explore(kComputer);
    CHECK(v.grows(1, assets::ware::kSpiceField));
    CHECK(v.grows(1, assets::ware::kGold));  // a big vein
    CHECK(!v.grows(1, assets::ware::kOre));  // no ore vein
    CHECK(v.full_crops(1) == 6);             // every crop grows there fully
    CHECK(v.free_deposit(1, assets::ware::kGold));
    CHECK(!v.free_deposit(1, assets::ware::kOre));
    CHECK(!v.free_deposit(99, assets::ware::kGold));
}

TEST_CASE("ai view: another player's soldiers count only on an island the viewer discovered") {
    // Below the hardest level a computer sees the soldiers ashore on islands it discovered, not those
    // elsewhere (P1.5); the hardest level and the owner itself see all of them.
    for (const std::uint8_t difficulty : {std::uint8_t{2}, std::uint8_t{3}}) {
        Game g;
        sim::World::GameSettings settings = g.world.game_settings();
        settings.difficulty = difficulty;
        g.world.set_game_settings(settings);
        g.world.add_soldier(
            sim::make_soldier(40, 30, static_cast<std::uint16_t>(assets::FigureTable::kUnitFamilies[0]), kHuman, 1));
        g.world.add_soldier(
            sim::make_soldier(41, 30, static_cast<std::uint16_t>(assets::FigureTable::kUnitFamilies[0]), kHuman, 1));
        sim::Simulation s(g.world, g.buildings, g.figures);
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::ai::AiPlayer p;
        p.player = kComputer;
        sim::ai::AiPlayer h;
        h.player = kHuman;
        const sim::ai::View v = engine.view(p);
        const sim::ai::View self = engine.view(h);
        const bool hardest = difficulty == 3;
        REQUIRE(!g.world.islands()[1].discovered_by(kComputer));
        CHECK(v.omniscient_level() == hardest);
        CHECK(self.seen_soldiers_on(kHuman, 1) == 2);
        CHECK(v.seen_soldiers_on(kHuman, 1) == (hardest ? 2U : 0U));
        CHECK(v.seen_army(kHuman) == (hardest ? self.seen_army(kHuman) : 0U));
        CHECK(v.strength(kHuman) <= self.strength(kHuman));
        g.world.island(1).discover(kComputer);
        CHECK(v.seen_soldiers_on(kHuman, 1) == 2);
        CHECK(v.seen_army(kHuman) == self.seen_army(kHuman));
        CHECK(v.seen_soldiers_on(kHuman, 99) == 0);
    }
}

TEST_CASE("ai view: soldiers on an island with a city of the viewer's own count, discovered or not") {
    // A computer's own city island need not carry the discovered flag; a human sees the soldiers
    // there all the same (P1.5b: seen_army by knows_island, as seen_soldiers_on).
    Game g;
    sim::World::GameSettings settings = g.world.game_settings();
    settings.difficulty = 2;
    g.world.set_game_settings(settings);
    sim::City own;
    own.island = 1;
    own.owner = kComputer;
    g.world.add_city(own);
    g.world.add_soldier(
        sim::make_soldier(40, 30, static_cast<std::uint16_t>(assets::FigureTable::kUnitFamilies[0]), kHuman, 1));
    sim::Simulation s(g.world, g.buildings, g.figures);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    sim::ai::AiPlayer h;
    h.player = kHuman;
    REQUIRE(!g.world.islands()[1].discovered_by(kComputer));
    REQUIRE(!engine.view(p).omniscient_level());
    CHECK(engine.view(p).seen_soldiers_on(kHuman, 1) == 1);
    CHECK(engine.view(p).seen_army(kHuman) == engine.view(h).seen_army(kHuman));
}

TEST_CASE("ai view: a partner's Kontor offer only while known, and the hardest level is the one that sees all") {
    constexpr std::uint8_t kWare = assets::ware::kCloth;
    constexpr std::uint32_t kTon = 32;
    for (const std::uint8_t difficulty : {std::uint8_t{2}, std::uint8_t{3}}) {
        Game g;
        sim::World::GameSettings settings = g.world.game_settings();
        settings.difficulty = difficulty;
        g.world.set_game_settings(settings);
        sim::City c;
        c.island = 1;
        c.owner = kHuman;
        g.world.add_city(c);
        sim::City* city = g.world.city(1, 0);
        REQUIRE(city != nullptr);
        city->ensure_ware(kWare);
        city->trade.resize(std::size_t{kWare} + 1);
        city->trade[kWare].sell = true;
        city->trade[kWare].sell_price = 42;
        city->trade[kWare].keep = 10 * kTon;
        city->goods[kWare] = 30 * kTon;
        sim::Simulation s(g.world, g.buildings, g.figures);
        sim::ai::State state;
        sim::ai::Engine engine(s, state);
        sim::ai::AiPlayer p;
        p.player = kComputer;
        const sim::ai::View v = engine.view(p);
        const bool hardest = difficulty == 3;
        CHECK(v.omniscient_level() == hardest);
        CHECK(v.kontor_offer(*city, kWare).has_value() == hardest);
        g.world.island(1).discover(kComputer);
        const auto offer = v.kontor_offer(*city, kWare);
        REQUIRE(offer.has_value());
        CHECK(offer->price == 42);
        CHECK(offer->offered == 20U * kTon);
        // What it does not sell, and an own Kontor, offer nothing here.
        CHECK(!v.kontor_offer(*city, assets::ware::kTools).has_value());
        city->owner = kComputer;
        CHECK(!v.kontor_offer(*city, kWare).has_value());
    }
}

TEST_CASE("ai view: only the hardest level and the original's computers see what a human does not (P1.7)") {
    // One human city, two soldiers and nothing else on an island the computer has not discovered:
    // every level below the hardest sees none of it, the hardest and the original's computers all.
    for (const bool classic : {false, true}) {
        for (std::uint8_t difficulty = 0; difficulty < sim::NewGameOptions::kDifficulties; ++difficulty) {
            Game g;
            sim::World::GameSettings settings = g.world.game_settings();
            settings.difficulty = difficulty;
            g.world.set_game_settings(settings);
            sim::City c;
            c.island = 1;
            c.owner = kHuman;
            c.inhabitants = {40};
            g.world.add_city(c);
            for (const std::uint32_t x : {40U, 41U}) {
                g.world.add_soldier(sim::make_soldier(
                    x, 30, static_cast<std::uint16_t>(assets::FigureTable::kUnitFamilies[0]), kHuman, 1));
            }
            sim::Rules rules;
            rules.bugs.classic_ai = classic;
            sim::Simulation s(g.world, g.buildings, g.figures, rules);
            sim::ai::State state;
            sim::ai::Engine engine(s, state);
            sim::ai::AiPlayer p;
            p.player = kComputer;
            sim::ai::AiPlayer h;
            h.player = kHuman;
            const sim::ai::View v = engine.view(p);
            const sim::ai::View self = engine.view(h);
            const bool all = classic || difficulty + 1 == sim::NewGameOptions::kDifficulties;
            REQUIRE(!g.world.islands()[1].discovered_by(kComputer));
            CHECK(v.omniscient_level() == all);
            CHECK(v.seen_city_count(kHuman) == (all ? 1U : 0U));
            CHECK(v.seen_soldiers_on(kHuman, 1) == (all ? 2U : 0U));
            CHECK(v.seen_army(kHuman) == (all ? self.seen_army(kHuman) : 0U));
            CHECK(v.knows_crops(1) == classic);  // crops: every island for the original's, by exploring else
            // The human itself sees all of its own on every level.
            CHECK(self.seen_city_count(kHuman) == 1);
            CHECK(self.seen_soldiers_on(kHuman, 1) == 2);
        }
    }
}

TEST_CASE("ai: a computer city's land use follows its tiles and its producers' work circles (PB.3)") {
    // A woodcutter's circle holds the trees it fells: work land. A stonemason fetches stone that
    // does not grow: its circle is none. Ruins are no land until cleared; a tile's picture changing
    // (as a field ripens) is taken again as well.
    Game g;
    g.buildings = make_buildings(
        "  @Nummer: 13\n  Id: 22407\n  Kind: 11\n  Gfx: 13\n  Size: 1, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 11\n    Ware: 54\n  EndObj\n"
        "  @Nummer: 14\n  Id: 20901\n  Kind: 14\n  Gfx: 30\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Ware: 9\n    Rohstoff: 54\n    Interval: 20\n    Radius: 4\n"
        "  EndObj\n"
        "  @Nummer: 15\n  Id: 20903\n  Kind: 14\n  Gfx: 34\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 2\n    Ware: 23\n    Rohstoff: 53\n    Interval: 20\n    Radius: 4\n"
        "  EndObj\n"
        "  @Nummer: 16\n  Id: 22601\n  Kind: 12\n  Gfx: 40\n  Size: 1, 1\n");
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s, 5'000);  // the Kontor at 44,16, a road west along row 17
    const sim::City* city = computer_city(g);
    REQUIRE(city != nullptr);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    p.island = city->island;
    p.city = city->index;
    sim::IslandMap& map = g.world.island(city->island).map;
    CHECK(engine.land_use(p).circles.empty());
    sim::BuildOrder order;
    order.player = kComputer;
    order.island = city->island;
    order.items = {{20901, 37, 18, 0, 0}};
    REQUIRE(sim::execute_build(g.world, g.buildings, order, {}, nullptr).placed == 1);
    CHECK(engine.land_use(p).circles.empty());  // the mason's stone grows nowhere
    order.items = {{20903, 40, 18, 0, 0}};
    REQUIRE(sim::execute_build(g.world, g.buildings, order, {}, nullptr).placed == 1);
    {
        const sim::ai::LandUse& u = engine.land_use(p);
        REQUIRE(u.circles.size() == 1);
        CHECK(u.workers == std::vector<std::uint32_t>{20903});
        CHECK(!u.zone.valid);  // no town style drawn: no zone, and none is drawn for it
        CHECK(p.town_seed == 0);
        using sim::ai::LandUse;
        CHECK(u.at(40, 22) == (LandUse::bit(LandUse::Use::Work) | LandUse::bit(LandUse::Use::Free)));
        CHECK(u.at(40, 25) == LandUse::bit(LandUse::Use::Free));
        CHECK(u.at(40, 24) == LandUse::bit(LandUse::Use::Road));
        CHECK(u.at(40, 18) == (LandUse::bit(LandUse::Use::Work) | LandUse::bit(LandUse::Use::Built)));
        CHECK((u.at(40, 17) & LandUse::bit(LandUse::Use::Road)) != 0);
        // The circle of radius 4 about the woodcutter's 2 x 2 tiles (40..41, 18..19): rows 14..23.
        CHECK(sim::ai::work_land_under(u, 40, 22, 41, 23) == 4);
        CHECK(sim::ai::work_land_under(u, 40, 23, 41, 24) == 2);
        CHECK(sim::ai::work_land_under(u, 40, 26, 41, 27) == 0);
    }
    // Ruins on the work land are no land; cleared, the tile is free again (it shows its ground).
    REQUIRE(map.place(*g.buildings.by_id(22601), 40, 22, 0, 0, kComputer, city->index).ok());
    CHECK(sim::ai::work_land_under(engine.land_use(p), 40, 22, 41, 23) == 3);
    map.restore_ground(40, 22);
    CHECK(sim::ai::work_land_under(engine.land_use(p), 40, 22, 41, 23) == 4);
    // A tile's picture changing in place, as a field ripens or a tree grows, counts at once.
    const auto ground = map.top(40, 23).sprite;
    map.set_sprite(40, 23, static_cast<std::uint16_t>(g.buildings.by_id(20401)->gfx));
    CHECK(sim::ai::work_land_under(engine.land_use(p), 40, 22, 41, 23) == 3);
    map.set_sprite(40, 23, ground);
    CHECK(sim::ai::work_land_under(engine.land_use(p), 40, 22, 41, 23) == 4);
    // Another city's ground is no free land of this one; unclaimed ground is.
    const std::uint8_t own = map.top(40, 25).city;
    const sim::PlayerId owner = map.top(40, 25).owner;
    map.set_city(40, 25, static_cast<std::uint8_t>(city->index + 1), kHuman);
    CHECK(engine.land_use(p).at(40, 25) == 0);
    map.set_city(40, 25, sim::MapCell::kNoCity, sim::kNoPlayer);
    CHECK(engine.land_use(p).is(40, 25, sim::ai::LandUse::Use::Free));
    map.set_city(40, 25, own, owner);
    // The zone's overlap with the work land: its free tiles, those in a circle, per producer type.
    sim::ai::LandUse zoned = engine.land_use(p);
    for (std::int32_t y = 22; y <= 25; ++y) {  // two rows of work land, a road, a row without
        for (std::int32_t x = 40; x <= 41; ++x) {
            zoned.bits[static_cast<std::size_t>(y - zoned.y0) * static_cast<std::size_t>(zoned.w) +
                       static_cast<std::size_t>(x - zoned.x0)] |= sim::ai::LandUse::bit(sim::ai::LandUse::Use::Zone);
        }
    }
    const sim::ai::ZoneOverlap o = sim::ai::zone_overlap(zoned);
    CHECK(o.zone_free == 6);
    CHECK(o.zone_work == 4);
    REQUIRE(o.by_type.size() == 1);
    CHECK(o.by_type.front().first == 20903);
    CHECK(o.by_type.front().second == 4);
    // A zone tile: as near the zone's middle as its radius.
    const sim::ai::TownZone z{true, 10, 10, 3};
    CHECK(sim::ai::in_zone(z, 13, 10));
    CHECK(sim::ai::in_zone(z, 12, 12));
    CHECK(!sim::ai::in_zone(z, 13, 13));
    CHECK(!sim::ai::in_zone(sim::ai::TownZone{}, 10, 10));
}

namespace {

// A town of its own (a human player's, so that no computer builds in it): two pioneer houses and a
// settler house by a market, no chapel, a little food (P2.4, P2.7).
assets::BuildingTable hungry_town_table() {
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}, {"BGRUPPE", 5}, {"BGRUPPE_WARE", 6}};
    const auto doc = assets::cod::parse(
        "Nahrung: 1.3\n"
        "Objekt: BGRUPPE\n"
        "  Nummer: 0\n  Prozent: 0\n  Maxwohn: 2\n  Steuer: 1.4\n"
        "  Nummer: 1\n  Prozent: 47\n  Maxwohn: 6\n  Steuer: 1.6\n"
        "  Objekt: BGRUPPE_WARE\n    Ware: 19, 0.6\n  EndObj\n"
        "EndObj\n"
        "Objekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "  @Nummer: 1\n  Id: 20401\n  Kind: 1\n  Gfx: 1\n  Size: 1, 1\n"
        "  @Nummer: 2\n  Id: 20605\n  Kind: 14\n  Gfx: 10\n  Size: 2, 2\n  Ruinenr: 0\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 0\n    Radius: 6\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Holz: 3\n  EndObj\n"
        "  @Nummer: 3\n  Id: 20621\n  Kind: 14\n  Gfx: 14\n  Size: 2, 2\n  Ruinenr: 0\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 13\n    BGruppe: 1\n    Radius: 6\n  EndObj\n"
        "  Objekt: HAUS_BAUKOST\n    Werkzeug: 1\n    Holz: 3\n  EndObj\n"
        "  @Nummer: 4\n  Id: 20809\n  Kind: 14\n  Gfx: 20\n  Size: 4, 3\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 7\n    Radius: 16\n    Kosten: 10, 5\n    Arbeiter: 4\n  EndObj\n"
        "  @Nummer: 5\n  Id: 22601\n  Kind: 12\n  Gfx: 50\n  Size: 1, 1\n"
        // A workshop of ware 19 from raw material 6, not built in the town (P2.7 places it).
        "  @Nummer: 6\n  Id: 20702\n  Kind: 14\n  Gfx: 60\n  Size: 2, 2\n  Objekt: HAUS_PRODTYP\n    Kind: 3\n"
        "    Ware: 19\n    Rohstoff: 6\n    Prodmenge: 1\n    Rohmenge: 2\n    Interval: 40\n    Kosten: 5, 2\n"
        "    Arbeiter: 2\n  EndObj\n"
        // A field of ware 5, which employs nobody, and a plantation of raw material 6 from it, neither
        // built in the town (P2.8 plans the plantation for a colony).
        "  @Nummer: 8\n  Id: 20705\n  Kind: 14\n  Gfx: 90\n  Size: 1, 1\n  Objekt: HAUS_PRODTYP\n    Kind: 3\n"
        "    Ware: 5\n    Prodmenge: 1\n    Interval: 40\n  EndObj\n"
        "  @Nummer: 9\n  Id: 20704\n  Kind: 14\n  Gfx: 80\n  Size: 2, 2\n  Objekt: HAUS_PRODTYP\n    Kind: 3\n"
        "    Ware: 6\n    Rohstoff: 5\n    Prodmenge: 1\n    Rohmenge: 1\n    Interval: 40\n    Kosten: 4, 1\n"
        "    Arbeiter: 3\n  EndObj\n"
        // A chapel, not built in the town but where a test places it (P2.8).
        "  @Nummer: 7\n  Id: 20803\n  Kind: 14\n  Gfx: 70\n  Size: 2, 2\n"
        "  Objekt: HAUS_PRODTYP\n    Kind: 18\n    Radius: 16\n    Kosten: 5, 2\n  EndObj\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    return std::move(table).value();
}

void build_hungry_town(sim::World& world, const assets::BuildingTable& buildings, std::uint32_t workshop = 0) {
    sim::IslandMap map{40, 30};
    for (std::int32_t y = 0; y < 30; ++y) {
        for (std::int32_t x = 0; x < 40; ++x) {
            REQUIRE(map.place(*buildings.by_id(y == 10 ? 20401 : 20101), x, y, 0, 0, 0, 0).ok());
        }
    }
    REQUIRE(map.place(*buildings.by_id(20809), 10, 11, 0, 0, 0, 0).ok());
    REQUIRE(map.place(*buildings.by_id(20605), 14, 13, 0, 0, 0, 0).ok());
    REQUIRE(map.place(*buildings.by_id(20605), 17, 13, 0, 0, 0, 0).ok());
    REQUIRE(map.place(*buildings.by_id(20621), 20, 13, 0, 0, 0, 0).ok());
    if (workshop != 0) {
        REQUIRE(map.place(*buildings.by_id(workshop), 26, 13, 0, 0, 0, 0).ok());
    }
    REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 0, 0}).ok());
    sim::City town;
    town.owner = kHuman;
    town.capacity = 30 * 32;
    town.inhabitants = {0, 0};
    town.satisfaction = {128, 128};
    town.tax_rate = {128, 128};
    world.add_city(town);
    std::vector<sim::Player> players(1);
    players[0].role = sim::PlayerRole::Human;
    players[0].money = 1000;
    world.set_players(players);
    sim::Production market;
    market.x = 10;
    market.y = 11;
    world.set_production(market);
    sim::City& city = *world.city(0, 0);
    city.ensure_ware(24);
    city.goods[buildings.food_ware()] = 32;
    const auto add = [&](std::uint16_t x, std::uint8_t cls, std::uint32_t inhabitants64) {
        sim::House h;
        h.x = x;
        h.y = 13;
        h.city = 0;
        h.resident_class = cls;
        h.inhabitants64 = inhabitants64;
        h.timer_group = static_cast<std::uint8_t>(x % 16);
        world.add_house(h);
        city.inhabitants[cls] += inhabitants64 >> 6;
    };
    add(14, 0, 64);
    add(17, 0, 64 + 32);
    add(20, 1, 6 * 64);
    sim::update_services(world, buildings);
}

}  // namespace

TEST_CASE("ai: a city's projection is the game's own, minute by minute: stock, supply, growth and descents (P2.4)") {
    // A town of its own (a human player's, so that no computer builds in it): two pioneer houses
    // and a settler house by a market, no chapel, a little food. The game runs it for 20 minutes and
    // the projection taken at the start says each minute what the game then shows: the food runs out,
    // the houses grow and shrink, the settlers fall back to pioneers.
    const assets::BuildingTable buildings = hungry_town_table();
    const assets::FigureTable figures = make_figures();
    sim::World world{60, 60};
    build_hungry_town(world, buildings);
    sim::Simulation s(world, buildings, figures);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kHuman;
    const sim::ai::CityProjection f = engine.project_city(p, *world.city(0, 0), 20);
    REQUIRE(f.minutes.size() == 20);
    // The market's running costs as the city's books show them (civil_income's figure).
    CHECK(f.running_cost == sim::running_costs(world, buildings)[0]);
    CHECK(f.running_cost > 0);
    bool hungry = false;
    bool fell = false;
    for (const sim::ai::ProjectedMinute& m : f.minutes) {
        run(s, 60'000);
        const sim::City& c = *world.city(0, 0);
        CHECK(m.stock == c.goods);
        std::vector<std::uint8_t> ratio;
        for (const sim::City::Supply& k : c.supply) {
            ratio.push_back(k.ratio);
        }
        CHECK(m.ratio == ratio);
        CHECK(m.inhabitants == c.inhabitants);
        CHECK(m.satisfaction == c.satisfaction);
        CHECK(m.taxes == sim::tax_income(c, buildings));
        hungry = hungry || c.food_ratio < 128;
        const sim::House* settler = world.house(0, 20, 13);
        fell = fell || (settler != nullptr && settler->resident_class == 0);
    }
    CHECK(hungry);
    CHECK(fell);
}

TEST_CASE(
    "ai: a computer's projected city sets its taxes as the computer would where it tends; a person's keeps them "
    "(P2.8)") {
    const assets::BuildingTable buildings = hungry_town_table();
    const assets::FigureTable figures = make_figures();
    sim::World world{60, 60};
    build_hungry_town(world, buildings);
    sim::Simulation s(world, buildings, figures);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kHuman;
    const sim::City& town = *world.city(0, 0);
    const sim::ai::CityProjection person = engine.project_city(p, town, 1);
    world.player(kHuman)->role = sim::PlayerRole::Computer;
    const sim::ai::CityProjection elsewhere = engine.project_city(p, town, 1);  // a city it does not tend now
    p.has_city = true;
    p.island = town.island;
    p.city = town.index;
    const sim::ai::CityProjection computer = engine.project_city(p, town, 1);
    // At the start the town's classes stay content above the normal rate: the computer taxes them more.
    REQUIRE(!person.minutes.empty());
    REQUIRE(!computer.minutes.empty());
    CHECK(computer.minutes.front().taxes > person.minutes.front().taxes);
    CHECK(elsewhere.minutes.front().taxes == person.minutes.front().taxes);
}

TEST_CASE("ai: a good the next class needs raises a projected house to that class's room (P2.8)") {
    // Houses hold people by their class's Maxwohn: a good counts for room only through the ascent it
    // opens. Full pioneer houses with food, wood and tools at hand: with the settlers' good coming in
    // they rise and hold the settlers' Maxwohn, without it they stay at the pioneers'.
    const assets::BuildingTable buildings = hungry_town_table();
    const assets::FigureTable figures = make_figures();
    sim::World world{60, 60};
    build_hungry_town(world, buildings, 20803);
    sim::Production chapel;
    chapel.x = 26;
    chapel.y = 13;
    world.set_production(chapel);
    sim::update_services(world, buildings);
    sim::City& town = *world.city(0, 0);
    town.goods[buildings.food_ware()] = 30 * 32;
    town.goods[22] = 10 * 32;  // tools
    town.goods[23] = 20 * 32;  // wood
    sim::Simulation s(world, buildings, figures);
    town.capacity = 30 * 32;  // room for what comes (the town has no Kontor)
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kHuman;
    constexpr std::uint32_t kMinutes = 30;
    const sim::ai::Inflow good[] = {sim::ai::Inflow{19, 4 * 32, 1, kMinutes}};
    const sim::ai::CityProjection with = engine.project_city(p, town, kMinutes, good);
    const sim::ai::CityProjection without = engine.project_city(p, town, kMinutes);
    REQUIRE(with.minutes.size() == kMinutes);
    REQUIRE(without.minutes.size() == kMinutes);
    const std::vector<std::uint32_t>& a = with.minutes.back().inhabitants;
    const std::vector<std::uint32_t>& b = without.minutes.back().inhabitants;
    const auto room = [&](std::size_t k) { return buildings.resident_classes()[k].max_per_house; };
    const std::uint32_t houses = 3;
    CHECK(a[0] + a[1] == houses * room(1));
    CHECK(b[0] + b[1] == houses * room(0));  // the settler house falls back too, without its good
}

TEST_CASE("ai: a city's projection adds each ware's inflow of the ledger, minute by minute (P2.4)") {
    // A city with no inhabitants: what its ledger shows coming in a minute (made and shipped in,
    // less what went out but to the inhabitants) comes in each projected minute, a sixth each city
    // tick, up to the warehouse's room.
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Nahrung: 1.3\nObjekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    const assets::BuildingTable buildings = std::move(table).value();
    const assets::FigureTable figures = make_figures();
    sim::World world{30, 30};
    sim::IslandMap map{10, 10};
    for (std::int32_t y = 0; y < 10; ++y) {
        for (std::int32_t x = 0; x < 10; ++x) {
            REQUIRE(map.place(*buildings.by_id(20101), x, y, 0, 0, 0, 0).ok());
        }
    }
    REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 0, 0}).ok());
    sim::City town;
    town.owner = kHuman;
    town.capacity = 100 * 32;
    world.add_city(town);
    std::vector<sim::Player> players(1);
    players[0].role = sim::PlayerRole::Human;
    world.set_players(players);
    sim::City& city = *world.city(0, 0);
    city.ensure_ware(24);
    constexpr std::uint8_t kTools = 22;
    constexpr std::uint8_t kWood = 23;
    city.goods[kWood] = 10 * 32;
    for (int minute = 0; minute < 5; ++minute) {
        city.book(kTools, sim::GoodsFlow::Produced, 40);     // 40 a minute made
        city.book(kWood, sim::GoodsFlow::ShippedIn, 50);     // 50 in,
        city.book(kWood, sim::GoodsFlow::Construction, 20);  // 20 out to a building site
        sim::close_goods_minute(world, buildings, nullptr);
    }
    sim::Simulation s(world, buildings, figures);
    world.city(0, 0)->capacity = 100 * 32;  // the room of a Kontor it does not have
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kHuman;
    const sim::ai::CityProjection f = engine.project_city(p, *world.city(0, 0), 100);
    REQUIRE(f.minutes.size() == 100);
    for (std::uint32_t m = 1; m <= 100; ++m) {
        CHECK(f.minutes[m - 1].stock[kTools] == std::min<std::uint32_t>(40 * m, 100 * 32));
        // Building is a decision of its own, left out: the ships' 50 a minute come in.
        CHECK(f.minutes[m - 1].stock[kWood] == std::min<std::uint32_t>(10 * 32 + 50 * m, 100 * 32));
    }
    CHECK(world.city(0, 0)->goods[kTools] == 0);  // the game's city is left as it was
}

TEST_CASE("ai: a city's projection trades at the ledger's rates as far as stock and room go (P2.6)") {
    // The ledger shows 30 a minute sold at 3 gold and 20 bought at 2 gold. The projection goes on
    // trading at those rates and prices; the Kontor's keep and limit as now do not cut it, the
    // stock and the room do.
    const assets::CodPredefinedSymbol types[] = {{"HAUS", 1}};
    const auto doc = assets::cod::parse(
        "Nahrung: 1.3\nObjekt: HAUS\n"
        "  @Nummer: 0\n  Id: 20101\n  Kind: 11\n  Gfx: 0\n  Size: 1, 1\n"
        "EndObj\n",
        types);
    auto table = assets::BuildingTable::from_cod(doc);
    REQUIRE(table.ok());
    const assets::BuildingTable buildings = std::move(table).value();
    const assets::FigureTable figures = make_figures();
    sim::World world{30, 30};
    sim::IslandMap map{10, 10};
    for (std::int32_t y = 0; y < 10; ++y) {
        for (std::int32_t x = 0; x < 10; ++x) {
            REQUIRE(map.place(*buildings.by_id(20101), x, y, 0, 0, 0, 0).ok());
        }
    }
    REQUIRE(world.add_island(sim::WorldIsland{assets::IslandHeader{}, std::move(map), 0, 0}).ok());
    sim::City town;
    town.owner = kHuman;
    world.add_city(town);
    std::vector<sim::Player> players(1);
    players[0].role = sim::PlayerRole::Human;
    world.set_players(players);
    sim::City& city = *world.city(0, 0);
    city.ensure_ware(24);
    constexpr std::uint8_t kSold = 22;
    constexpr std::uint8_t kBought = 23;
    city.trade.resize(24);
    city.trade[kSold].sell = true;
    city.trade[kSold].keep = 200;
    city.trade[kBought].buy = true;
    city.trade[kBought].limit = 400;
    city.goods[kSold] = 320;
    for (int minute = 0; minute < 5; ++minute) {
        city.book(kSold, sim::GoodsFlow::Sold, 30);
        city.book(kSold, sim::GoodsFlow::SoldValue, 90);
        city.book(kBought, sim::GoodsFlow::Bought, 20);
        city.book(kBought, sim::GoodsFlow::BoughtValue, 40);
        sim::close_goods_minute(world, buildings, nullptr);
    }
    sim::Simulation s(world, buildings, figures);
    world.city(0, 0)->capacity = 100 * 32;
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kHuman;
    const sim::ai::CityProjection f = engine.project_city(p, *world.city(0, 0), 30);
    REQUIRE(f.minutes.size() == 30);
    for (std::uint32_t m = 1; m <= 30; ++m) {
        const sim::ai::ProjectedMinute& pm = f.minutes[m - 1];
        CHECK(pm.stock[kSold] == 320 - std::min<std::uint32_t>(320, 30 * m));
        CHECK(pm.stock[kBought] == 20 * m);
        const std::int64_t sold = m <= 10 ? 30 : m == 11 ? 20 : 0;
        const std::int64_t bought = 20;
        CHECK(pm.trade == 3 * sold - 2 * bought);
    }
}

TEST_CASE("ai: an action's value is the city's gold with it less without it, less its cost and running costs (P2.7)") {
    sim::ai::CityProjection with;
    sim::ai::CityProjection without;
    with.running_cost = 5;
    without.running_cost = 5;
    for (int m = 0; m < 3; ++m) {
        sim::ai::ProjectedMinute a;
        a.taxes = 20;
        a.inhabitants = {3, 1};
        with.minutes.push_back(a);
        sim::ai::ProjectedMinute b;
        b.taxes = 10;
        b.inhabitants = {2, 1};
        without.minutes.push_back(b);
    }
    sim::ai::Commitment c;
    c.cost = 15;
    c.upkeep = 2;
    const sim::ai::ActionValue v = sim::ai::action_value(with, without, c);
    CHECK((v.gold == std::vector<std::int64_t>{-15, -7, 1, 9}));
    CHECK(v.payback == 2);
    CHECK((v.people == std::vector<std::int64_t>{1, 0}));
    // An action that costs nothing and brings nothing never pays back.
    const sim::ai::ActionValue none = sim::ai::action_value(without, without, sim::ai::Commitment{});
    CHECK((none.gold == std::vector<std::int64_t>{0, 0, 0, 0}));
    CHECK(none.payback == 0);
    // A run between own cities: what the source loses is added, and the payback found again.
    sim::ai::ActionValue loss;
    loss.gold = {0, -4, -8, -12};
    const sim::ai::ActionValue both = sim::ai::add_value(v, loss);
    CHECK((both.gold == std::vector<std::int64_t>{-15, -11, -7, -3}));
    CHECK(both.payback == 0);
    CHECK((both.people == v.people));
}

TEST_CASE("ai: a colony brings its raw material home and plans the workshop the home lacks (P2.8)") {
    const assets::BuildingTable buildings = hungry_town_table();
    const sim::ai::Sheet sheet(buildings);
    const sim::ai::Sheet::Producer* shop = sheet.producer(20702);  // ware 19 from raw 6
    REQUIRE(shop != nullptr);
    const sim::ai::Inflow good{19, 30, 11, 30};
    {  // the home makes none of the good: the raw material at the workshop's rate, the workshop planned
        sim::ai::Commitment c;
        c.upkeep = 3;
        const sim::ai::ColonyBundle b = sim::ai::colony_bundle(sheet, good, 0, c);
        CHECK(b.workshop == shop);
        CHECK(b.inflow.ware == 6);
        CHECK(b.inflow.per_minute == 30 * static_cast<std::int64_t>(shop->raw_use / shop->output));
        CHECK(b.inflow.from == 11);
        CHECK(c.upkeep == 3 + std::int64_t{shop->running_cost});
        CHECK(c.cost == std::int64_t{shop->build_cost.money});
    }
    {  // it makes the good already: its own workshops take the raw material, none planned
        sim::ai::Commitment c;
        const sim::ai::ColonyBundle b = sim::ai::colony_bundle(sheet, good, 1, c);
        CHECK(b.workshop == nullptr);
        CHECK(b.inflow.ware == 6);
        CHECK(c.upkeep == 0);
    }
    {  // a plantation's ware comes as it is: the plantation is the colony's, its field ware employs nobody
        sim::ai::Commitment c;
        const sim::ai::ColonyBundle b = sim::ai::colony_bundle(sheet, sim::ai::Inflow{6, 30, 11, 30}, 0, c);
        CHECK(b.workshop == nullptr);
        CHECK(b.inflow.ware == 6);
        CHECK(c.upkeep == 0);
    }
    {  // a good no workshop makes comes as it is
        sim::ai::Commitment c;
        const sim::ai::ColonyBundle b = sim::ai::colony_bundle(sheet, sim::ai::Inflow{22, 30, 11, 30}, 0, c);
        CHECK(b.workshop == nullptr);
        CHECK(b.inflow.ware == 22);
        CHECK(b.inflow.per_minute == 30);
    }
}

TEST_CASE(
    "ai: a planned colony's upkeep is its own buildings' less its workers' taxes, its workshop's from its raw (P2.8)") {
    const assets::BuildingTable buildings = hungry_town_table();
    const sim::ai::Sheet sheet(buildings);
    const sim::ai::Sheet::Producer* field = sheet.producer(20704);  // raw 6 from its field ware 5
    const sim::ai::Sheet::Producer* shop = sheet.producer(20702);   // ware 19 from raw 6
    REQUIRE(field != nullptr);
    REQUIRE(shop != nullptr);
    REQUIRE(field->output != 0);
    sim::ai::ColonyBundle b;
    b.inflow = sim::ai::Inflow{6, static_cast<std::int64_t>(field->output) * 2 + 1, 11, 30};  // two fields and a bit
    b.workshop = shop;
    constexpr std::int64_t kKontor = 15;
    constexpr std::uint32_t kRate = 128;
    const sim::ai::ColonyUpkeep u = sim::ai::colony_upkeep(sheet, b, kKontor, kRate);
    CHECK(u.fields == 3);
    CHECK(u.workers == 3 * std::uint64_t{field->workers});
    CHECK(u.colony == kKontor + 3 * std::int64_t{field->running_cost} - sheet.tax(0, u.workers, kRate));
    CHECK(u.workshop == std::int64_t{shop->running_cost});
    CHECK(u.workshop_from == 11);
    // Weighed with it instead of a booked 100 a minute: the colony's own each minute, the workshop's from minute 11.
    sim::ai::ActionValue v;
    v.gold.assign(21, -1000);
    v.payback = 0;
    const sim::ai::ActionValue d = sim::ai::with_upkeep(v, 100, u);
    CHECK(d.gold[0] == -1000);
    CHECK(d.gold[10] == -1000 + 10 * (100 - u.colony));
    CHECK(d.gold[20] == -1000 + 20 * (100 - u.colony) - 10 * u.workshop);
}

TEST_CASE("ai: past the validated horizon an action goes on at its last period's slope until it pays back (P2.8)") {
    sim::ai::ActionValue v;
    v.gold = {-100, -90, -80, -70, -60, -50, -40};  // six minutes, +10 a minute
    {
        const sim::ai::Extension e = sim::ai::extend_value(v, 1, 3, std::nullopt);
        CHECK(!e.too_early);
        CHECK(e.slope == 10);
        CHECK(e.payback == 6 + 5);  // -40 + 5 * 10 = +10
    }
    // Its goods began too late: the slope would hold their run-up.
    CHECK(sim::ai::extend_value(v, 4, 3, std::nullopt).too_early);
    // The game ends first.
    CHECK(sim::ai::extend_value(v, 1, 3, std::uint32_t{10}).payback == 0);
    CHECK(sim::ai::extend_value(v, 1, 3, std::uint32_t{11}).payback == 11);
    // Falling or flat: no payback.
    sim::ai::ActionValue flat;
    flat.gold = {-100, -100, -100, -100};
    CHECK(sim::ai::extend_value(flat, 1, 2, std::nullopt).payback == 0);
    // Paid back within the horizon: that minute.
    sim::ai::ActionValue early;
    early.gold = {-10, 5, 20};
    early.payback = 1;
    CHECK(sim::ai::extend_value(early, 1, 1, std::nullopt).payback == 1);
}

TEST_CASE("ai: a town's idle workshop turns a plantation's raw material into its good, up to its rate (P2.7)") {
    const assets::BuildingTable buildings = hungry_town_table();
    const assets::FigureTable figures = make_figures();
    sim::World world{60, 60};
    build_hungry_town(world, buildings, 20702);
    sim::Production shop;
    shop.x = 26;
    shop.y = 13;
    shop.elapsed = 120;  // a statistics window run, nothing made in it: all of its rate is spare
    world.set_production(shop);
    sim::Simulation s(world, buildings, figures);
    world.city(0, 0)->capacity = 30 * 32;
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kHuman;
    const sim::ai::Sheet::Producer* r = engine.sheet().producer(20702);
    REQUIRE(r != nullptr);
    const auto raw_rate = static_cast<std::int64_t>(r->raw_use);  // what the workshop can take a minute
    REQUIRE(raw_rate > 0);
    // Twice what the workshop takes, for as long as the rest fits the town's room.
    constexpr std::uint32_t kMinutes = 8;
    constexpr std::uint8_t kRaw = 6;
    constexpr std::uint8_t kGood = 19;
    REQUIRE(raw_rate * kMinutes < 30 * 32);
    const sim::ai::Inflow crop[] = {{kRaw, 2 * raw_rate, 1, kMinutes}};
    const sim::ai::CityProjection without = engine.project_city(p, *world.city(0, 0), kMinutes);
    const sim::ai::CityProjection with = engine.project_city(p, *world.city(0, 0), kMinutes, crop);
    for (std::uint32_t m = 1; m <= kMinutes; ++m) {
        const std::int64_t left = std::int64_t{with.minutes[m - 1].stock[kRaw]} - without.minutes[m - 1].stock[kRaw];
        const std::int64_t used = 2 * raw_rate * m - left;
        // The workshop takes what its rate allows, a tick's rounding aside, and no more.
        CHECK(used <= raw_rate * m);
        CHECK(used + raw_rate >= raw_rate * m);
    }
    CHECK(with.minutes.back().stock[kGood] + 0 >= without.minutes.back().stock[kGood]);
    std::int64_t made = 0;
    for (const sim::ai::ProjectedMinute& pm : with.minutes) {
        made = std::max<std::int64_t>(made, pm.stock[kGood]);
    }
    CHECK(made > 0);
    // The settlers take the good, and pay more taxes for it.
    CHECK(with.minutes.back().taxes > without.minutes.back().taxes);
    {  // a full store: the room cuts off what the action brings, and the town's own raw material stays its own
        sim::City& town = *world.city(0, 0);
        town.ensure_ware(kRaw);
        town.goods[kRaw] = town.capacity;
        const sim::ai::CityProjection full_without = engine.project_city(p, town, kMinutes);
        const sim::ai::CityProjection full_with = engine.project_city(p, town, kMinutes, crop);
        CHECK(full_with.minutes.back().stock[kGood] == full_without.minutes.back().stock[kGood]);
        CHECK(full_with.minutes.back().stock[kRaw] == full_without.minutes.back().stock[kRaw]);
    }
}

TEST_CASE(
    "ai: a colony's raw material with a workshop planned along is valued with its goods, taxes and running cost "
    "(P2.7c)") {
    const assets::BuildingTable buildings = hungry_town_table();
    const assets::FigureTable figures = make_figures();
    sim::World world{60, 60};
    build_hungry_town(world, buildings);  // no workshop for the raw material at home
    sim::Simulation s(world, buildings, figures);
    world.city(0, 0)->capacity = 30 * 32;
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kHuman;
    const sim::ai::Sheet::Producer* shop = engine.sheet().producer(20702);
    REQUIRE(shop != nullptr);
    constexpr std::uint32_t kMinutes = 8;
    constexpr std::uint8_t kRaw = 6;
    constexpr std::uint8_t kGood = 19;
    const auto raw_rate = static_cast<std::int64_t>(shop->raw_use);
    REQUIRE(raw_rate * kMinutes < 30 * 32);
    const sim::ai::Inflow crop[] = {{kRaw, raw_rate, 1, kMinutes}};
    const sim::ai::CityProjection without = engine.project_city(p, *world.city(0, 0), kMinutes);
    const sim::ai::CityProjection alone = engine.project_city(p, *world.city(0, 0), kMinutes, crop);
    const sim::ai::Sheet::Producer* const plan[] = {shop};
    const sim::ai::CityProjection bundle = engine.project_city(p, *world.city(0, 0), kMinutes, crop, plan);
    // Alone, the raw material only fills the store; with the workshop it becomes the good the settlers pay for.
    CHECK(alone.minutes.back().stock[kGood] == without.minutes.back().stock[kGood]);
    CHECK(alone.minutes.back().taxes == without.minutes.back().taxes);
    CHECK(bundle.minutes.back().stock[kRaw] < alone.minutes.back().stock[kRaw]);
    CHECK(bundle.minutes.back().taxes > without.minutes.back().taxes);
    // Before its running cost the bundle's gold is above the raw material's alone; the value takes that cost off a
    // minute.
    sim::ai::Commitment c;
    const sim::ai::ActionValue v_alone = sim::ai::action_value(alone, without, c);
    sim::ai::Commitment with_shop;
    with_shop.upkeep = shop->running_cost;
    const sim::ai::ActionValue v_bundle = sim::ai::action_value(bundle, without, with_shop);
    const sim::ai::ActionValue v_gross = sim::ai::action_value(bundle, without, c);
    CHECK(v_alone.gold.back() <= 0);
    CHECK(v_gross.gold.back() > v_alone.gold.back());
    CHECK(v_bundle.gold.back() == v_gross.gold.back() - std::int64_t{shop->running_cost} * kMinutes);
}

TEST_CASE("ai: the value of a supply run, a colony and a plantation, projected with and without it (P2.7)") {
    const assets::BuildingTable buildings = hungry_town_table();
    const assets::FigureTable figures = make_figures();
    sim::World world{60, 60};
    build_hungry_town(world, buildings);
    sim::Simulation s(world, buildings, figures);
    world.city(0, 0)->capacity = 30 * 32;  // room for what comes (the town has no Kontor)
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kHuman;
    constexpr std::uint32_t kMinutes = 20;
    const std::uint8_t food = buildings.food_ware();
    const sim::ai::CityProjection without = engine.project_city(p, *world.city(0, 0), kMinutes);
    REQUIRE(without.minutes.size() == kMinutes);
    sim::ai::Commitment free;

    {
        // a supply run's food, landed at once, keeps inhabitants the town loses without it.
        const sim::ai::Inflow load[] = {{food, 20 * 32, 1, 1}};
        const sim::ai::CityProjection with = engine.project_city(p, *world.city(0, 0), kMinutes, load);
        const sim::ai::ActionValue v = sim::ai::action_value(with, without, free);
        CHECK(v.people[0] > 0);  // the pioneers stay; the settlers still lack their ware 19
        CHECK(v.gold.back() > 0);
    }
    {
        // the same load taken from the town as an own source: its stock falls, and it is worth no gain to it.
        sim::City& town = *world.city(0, 0);
        town.ensure_ware(food);
        const std::uint32_t had = town.goods[food];
        town.goods[food] = 20 * 32;
        const sim::ai::Inflow taken[] = {{food, -20 * 32, 1, 1}};
        const sim::ai::CityProjection kept = engine.project_city(p, town, kMinutes);
        const sim::ai::CityProjection lost = engine.project_city(p, town, kMinutes, taken);
        const sim::ai::ActionValue v = sim::ai::action_value(lost, kept, free);
        CHECK(lost.minutes.front().stock[food] < kept.minutes.front().stock[food]);
        CHECK(v.gold.back() <= 0);
        CHECK(v.people[0] <= 0);
        town.goods[food] = had;
    }
    {
        // a colony's food from its delivery minute on: nothing changes before, its costs count from now.
        constexpr std::uint32_t kDelivery = 10;
        const sim::ai::Inflow ship[] = {{food, 32, kDelivery, kMinutes}};
        const sim::ai::CityProjection with = engine.project_city(p, *world.city(0, 0), kMinutes, ship);
        sim::ai::Commitment c;
        c.cost = 100;
        c.upkeep = 3;
        const sim::ai::ActionValue v = sim::ai::action_value(with, without, c);
        for (std::uint32_t m = 1; m < kDelivery; ++m) {
            CHECK(with.minutes[m - 1].stock == without.minutes[m - 1].stock);
            CHECK(v.gold[m] == -100 - 3 * std::int64_t{m});
        }
        CHECK(with.minutes[kDelivery - 1].stock[food] > without.minutes[kDelivery - 1].stock[food]);
    }
    {
        // a plantation whose ware nobody works or needs is worth no more than its costs.
        constexpr std::uint8_t kRaw = 22;
        const sim::ai::Inflow crop[] = {{kRaw, 32, 1, kMinutes}};
        const sim::ai::CityProjection with = engine.project_city(p, *world.city(0, 0), kMinutes, crop);
        sim::ai::Commitment c;
        c.cost = 100;
        c.upkeep = 3;
        const sim::ai::ActionValue v = sim::ai::action_value(with, without, c);
        for (std::uint32_t m = 1; m <= kMinutes; ++m) {
            CHECK(with.minutes[m - 1].stock[kRaw] == without.minutes[m - 1].stock[kRaw] + 32 * m);
        }
        CHECK(v.gold.back() == -100 - 3 * std::int64_t{kMinutes});
        CHECK(v.payback == 0);
    }
}
TEST_CASE("ai: the projected gold adds every city's taxes less its running costs (P2.4)") {
    sim::ai::CityProjection a;
    a.running_cost = 5;
    a.minutes.resize(3);
    for (auto& m : a.minutes) {
        m.taxes = 15;
    }
    sim::ai::CityProjection b;
    b.running_cost = 1;
    b.minutes.resize(2);
    b.minutes[0].taxes = 3;
    b.minutes[1].taxes = 2;
    b.minutes[1].trade = 2;
    std::vector<std::int64_t> gold(4, 0);
    sim::ai::add_city_gold(gold, a);
    sim::ai::add_city_gold(gold, b);
    // a: 10, 20, 30; b: 2, 5, and 5 past its last minute.
    CHECK(gold[0] == 0);
    CHECK(gold[1] == 12);
    CHECK(gold[2] == 25);
    CHECK(gold[3] == 35);
}

TEST_CASE("ai: open land for a farm counts a crop's field only where it bears (K3)") {
    using sim::ai::Engine;
    // A plantation claims no market for desert grass its crop would not bear on.
    CHECK(Engine::field_open(true, true, true, true));
    CHECK(!Engine::field_open(true, true, true, false));
    // A herd's meadow is open wherever grass grows.
    CHECK(Engine::field_open(false, true, true, false));
    // Claimed or built-on land never counts.
    CHECK(!Engine::field_open(true, false, true, true));
    CHECK(!Engine::field_open(false, true, false, true));
}

namespace {

/// K3: the corn records (growing, ripe, withered) and a desert wood: trees on dry ground whose
/// Randwachs is a quarter, as the player's desert grass.
const std::string kCornAndDesert =
    "  @Nummer: 13\n  Id: 20651\n  Kind: 10\n  Gfx: 100\n  Size: 1, 1\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 10\n  EndObj\n"
    "  @Nummer: 14\n  Id: 20652\n  Kind: 10\n  Gfx: 101\n  Size: 1, 1\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 45\n  EndObj\n"
    "  @Nummer: 15\n  Id: 20653\n  Kind: 10\n  Gfx: 102\n  Size: 1, 1\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 10\n    Doerrflg: 1\n  EndObj\n"
    "  @Nummer: 16\n  Id: 20203\n  Kind: 10\n  Gfx: 103\n  Size: 1, 1\n  Randwachs: 25\n"
    "  Objekt: HAUS_PRODTYP\n    Kind: 9\n    Ware: 53\n  EndObj\n";

/// field_land of the computer's city for corn and for a meadow, with the wood's rows from
/// `desert_from` on dry ground (50: none).
std::pair<bool, bool> corn_and_meadow_land(std::int32_t desert_from, bool corn_island = true) {
    Game g;
    g.buildings = make_buildings(kCornAndDesert);
    g.buildings.by_id(kKontor)->ware = sim::ware::kAll;
    sim::IslandMap& map = g.world.island(1).map;
    for (std::int32_t y = desert_from; y < 50; ++y) {
        for (std::int32_t x = 32; x <= 43; ++x) {
            REQUIRE(map.place(*g.buildings.by_id(20203), x, y, 0, 0).ok());
        }
    }
    map.capture_ground();
    if (!corn_island) {
        g.world.island(1).header.fertility = 0;
    }
    sim::Simulation s(g.world, g.buildings, g.figures);
    found_city(g, s);
    const sim::City* city = nullptr;
    for (const sim::City& c : g.world.cities()) {
        if (c.owner == kComputer) {
            city = &c;
        }
    }
    REQUIRE(city != nullptr);
    sim::ai::State state;
    sim::ai::Engine engine(s, state);
    sim::ai::AiPlayer p;
    p.player = kComputer;
    p.island = city->island;
    p.city = city->index;
    std::int32_t x = -1;
    std::int32_t y = -1;
    const bool corn = engine.field_land(p, assets::ware::kCornField, x, y);
    return {corn, engine.field_land(p, assets::ware::kWool, x, y)};
}

}  // namespace

TEST_CASE("ai: an improved computer claims no open land for corn on desert ground (K3)") {
    // The island grows corn; its wood stands on ground that bears it fully: open land for corn.
    CHECK((corn_and_meadow_land(50) == std::pair{true, true}));
    // The whole wood on desert ground, a quarter's Randwachs: corn bears too little there for a
    // farm, no land is claimed for it; the herds' meadow still counts.
    CHECK((corn_and_meadow_land(0) == std::pair{false, true}));
    // Desert on the wood's lower half only: the upper half is land for corn.
    CHECK((corn_and_meadow_land(25) == std::pair{true, true}));
    // An island without corn at all.
    CHECK((corn_and_meadow_land(50, false) == std::pair{false, true}));
}
