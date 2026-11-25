// SPDX-License-Identifier: GPL-3.0-or-later
// Start places drawn at random (sim::ScenarioInfo::shuffle_starts): the players who start with
// ships only take the places of the map and of its ship start markers in a random order.

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

#include "../test_framework.hpp"

#include "o1602/assets/building_table.hpp"
#include "o1602/sim/save_game.hpp"
#include "o1602/sim/start_places.hpp"
#include "o1602/sim/world.hpp"

using namespace o1602;

namespace {

constexpr std::uint16_t kShipFigure = 21;

/// A world of open sea: two humans or computers with a ship each (the first with a second one two
/// tiles east of it), a third with a city, the trader with his ship.
sim::World sea_world() {
    sim::World world(100, 100);
    std::vector<sim::Player> players(4);
    players[0].role = sim::PlayerRole::Human;
    players[1].role = sim::PlayerRole::Computer;
    players[2].role = sim::PlayerRole::Computer;
    sim::make_neutral(players[3], sim::faction::kTrader);
    world.set_players(players);
    world.add_ship(sim::make_ship("A", 10, 10, kShipFigure, 0));
    world.add_ship(sim::make_ship("A2", 12, 10, kShipFigure, 0));
    world.add_ship(sim::make_ship("B", 60, 60, kShipFigure, 1));
    world.add_ship(sim::make_ship("C", 80, 20, kShipFigure, 2));
    world.add_ship(sim::make_ship("T", 40, 80, kShipFigure, 3));
    sim::City city;
    city.owner = 2;
    city.island = 0;
    world.add_city(city);
    return world;
}

std::pair<std::uint32_t, std::uint32_t> at(const sim::World& world, const char* name) {
    for (const sim::Ship& s : world.ships()) {
        if (s.name == name) {
            return {s.x, s.y};
        }
    }
    return {0, 0};
}

}  // namespace

TEST_CASE("start places: the places of the players who start at sea, and the markers made start places") {
    sim::World world = sea_world();
    CHECK((sim::players_starting_at_sea(world) == std::vector<sim::PlayerId>{0, 1}));
    world.set_markers({
        sim::ScenarioMarker{"ship_start_1", sim::ScenarioMarker::Shape::Point, 30, 50, 1, 1},
        sim::ScenarioMarker{"harbour", sim::ScenarioMarker::Shape::Point, 70, 70, 1, 1},
        sim::ScenarioMarker{"ship_start_area", sim::ScenarioMarker::Shape::Area, 5, 5, 4, 4},
    });
    CHECK(sim::ship_start_marker(world.markers()[0]));
    CHECK(!sim::ship_start_marker(world.markers()[1]));
    CHECK(!sim::ship_start_marker(world.markers()[2]));  // an area is no place
    CHECK((sim::start_places(world) ==
           std::vector<sim::StartPlace>{sim::StartPlace{10, 10}, sim::StartPlace{60, 60}, sim::StartPlace{30, 50}}));
}

TEST_CASE("start places: drawn from the seed, each to one player, the player's ships moving together") {
    std::set<std::pair<std::uint32_t, std::uint32_t>> firsts;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        sim::World world = sea_world();
        world.set_markers({sim::ScenarioMarker{"ship_start_1", sim::ScenarioMarker::Shape::Point, 30, 50, 1, 1}});
        sim::shuffle_start_places(world, assets::BuildingTable{}, seed);
        const auto a = at(world, "A");
        const auto b = at(world, "B");
        const std::set<std::pair<std::uint32_t, std::uint32_t>> places = {{10, 10}, {60, 60}, {30, 50}};
        CHECK(places.count(a) == 1);
        CHECK(places.count(b) == 1);
        CHECK(a != b);
        // The second ship keeps its place beside the first.
        CHECK((at(world, "A2") == std::pair<std::uint32_t, std::uint32_t>{a.first + 2, a.second}));
        // Neither the player with a city nor the trader moves.
        CHECK((at(world, "C") == std::pair<std::uint32_t, std::uint32_t>{80, 20}));
        CHECK((at(world, "T") == std::pair<std::uint32_t, std::uint32_t>{40, 80}));
        firsts.insert(a);

        // The same seed gives the same places (every computer of a multiplayer game).
        sim::World again = sea_world();
        again.set_markers(world.markers());
        sim::shuffle_start_places(again, assets::BuildingTable{}, seed);
        CHECK(at(again, "A") == a);
        CHECK(at(again, "B") == b);
    }
    CHECK(firsts.size() == 3);  // every place comes up for the first player
}

TEST_CASE("start places: a ship whose place is taken goes to the nearest free water") {
    sim::World world = sea_world();
    // The trader lies on the marker: the ship drawn there goes beside it, not onto it.
    world.set_markers({sim::ScenarioMarker{"ship_start_1", sim::ScenarioMarker::Shape::Point, 40, 80, 1, 1}});
    for (std::uint64_t seed = 1; seed <= 20; ++seed) {
        sim::World w = world;
        sim::shuffle_start_places(w, assets::BuildingTable{}, seed);
        for (const char* name : {"A", "A2", "B"}) {
            const auto p = at(w, name);
            const auto off = [](std::uint32_t a, std::uint32_t b) { return a > b ? a - b : b - a; };
            CHECK((off(p.first, 40) > 1 || off(p.second, 80) > 1));
        }
    }
    // One place only: nothing to draw.
    sim::World one = sea_world();
    one.ships().erase(one.ships().begin() + 2);  // player 1 has no ship now
    sim::shuffle_start_places(one, assets::BuildingTable{}, 7);
    CHECK((at(one, "A") == std::pair<std::uint32_t, std::uint32_t>{10, 10}));
}

TEST_CASE("start places: the scenario's switch is saved with it") {
    sim::World world(40, 40);
    world.set_players(std::vector<sim::Player>(2));
    sim::ScenarioInfo info;
    info.shuffle_starts = true;
    world.set_scenario_info(info);
    auto bytes = sim::write_game(world, assets::BuildingTable{});
    REQUIRE(bytes.ok());
    auto loaded = sim::read_game(*bytes, assets::BuildingTable{});
    REQUIRE(loaded.ok());
    REQUIRE(loaded->scenario_info().has_value());
    CHECK(loaded->scenario_info()->shuffle_starts);
}
